#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/can.h>

#define DEF_HW_PART
#include <cal_conf.h>

#include <co_def.h>
#include <co_drv.h>
#include <co_flag.h>

#include "cdriver.h"
#include "can_zephyr.h"
#include "examples.h"

static const struct device *can_dev = NULL;
static u32_t can_baudrate;

static int can_rx_filter_id = -1;

#ifdef CONFIG_TIME_TEST
	struct device *pin_dev = NULL;
#endif

#ifdef CONFIG_DRIVER_FAST_SORT
/*! Start_CAN() called?  */
static BOOL_T fStartCan CO_REDCY_PARA_ARRAY_DEF;
#endif /* CONFIG_DRIVER_FAST_SORT */

#ifdef CONFIG_SYNC_CONSUMER
static COB_IDENT_T coSyncId CO_REDCY_PARA_ARRAY_DEF;
#endif /* CONFIG_SYNC_CONSUMER */

#ifdef CONFIG_CAN_DEBUG_VARS
/*! counter of state changed interrupts */
UNSIGNED32 cal_ch_ints CO_REDCY_PARA_ARRAY_DEF;
/*! counter of transmit interrupts */
UNSIGNED32 cal_tx_ints CO_REDCY_PARA_ARRAY_DEF;
/*! counter of receive interrupts */
UNSIGNED32 cal_rx_ints CO_REDCY_PARA_ARRAY_DEF;
#endif /* CONFIG_CAN_DEBUG_VARS */

K_SEM_DEFINE(can_sem, 0, 1);

static void setNewDriverState(u8_t newState	CO_COMMA_REDCY_PARA_DECL);
static void GetNext_TX_Request(CO_REDCY_PARA_DECL);

/*----------------------------------------------------------------------------*/ 
/*! 
 @brief CAN receive interrupt handler

*/
/*----------------------------------------------------------------------------*/ 
void can_rx_isr_handler(const struct device *dev, struct can_frame *msg, void *arg){
    ARG_UNUSED(dev);
    ARG_UNUSED(arg);
    BUFFER_ENTRY_PTR_T pBuffer; /* Pointer to Message Buffer */
    u8_t bLength;

#ifdef CONFIG_CAN_DEBUG_VARS
    GL_DRV_ARRAY(cal_rx_ints) ++;
#endif /* CONFIG_CAN_DEBUG_VARS */
 
#ifdef CONFIG_SYNC_CONSUMER

    if (msg->id == GL_DRV_ARRAY(coSyncId))
    {		
        /* read Sync Counter */
        GL_ARRAY(co_syncCnt) = 0;
        bLength = msg->dlc;
        if (bLength > 0) {
            GL_ARRAY(co_syncCnt) = msg->data[0];
        }
        /* SYNC message received */
        SET_COLIB_FLAG_ISR(COFLAG_SYNC_RECEIVED);
    } else 

#endif /* CONFIG_SYNC_CONSUMER */
    {
        BUFFER_INIT_PTR(RX, Write);
        CHECK_BUFFER_WRITE( RX , CANFLAG_RXBUFFER_OVERFLOW ){
            /* receive queue is free */

            /* reset error flags */
            GL_DRV_ARRAY(coCanDriverState) &= ~CANFLAG_RXBUFFER_OVERFLOW;
            BUFFER_WRITE(RX, cobId, msg->id);
    
            bLength = msg->dlc;

            if (bLength > 8) {
                bLength = 8;
            }
            
            if ((msg->flags & CAN_FRAME_RTR) != 0U) {
                bLength |= CO_RTR_REQ;
            }

            BUFFER_WRITE(RX, bLength, bLength);


            BUFFER_WRITE(RX, pData[0], msg->data[0]);
            BUFFER_WRITE(RX, pData[1], msg->data[1]);
            BUFFER_WRITE(RX, pData[2], msg->data[2]);
            BUFFER_WRITE(RX, pData[3], msg->data[3]);
            BUFFER_WRITE(RX, pData[4], msg->data[4]);
            BUFFER_WRITE(RX, pData[5], msg->data[5]);
            BUFFER_WRITE(RX, pData[6], msg->data[6]);
            BUFFER_WRITE(RX, pData[7], msg->data[7]);

            BUFFER_ENTRY_INCR(RX, Write, FULL);
        } /* Check Buffer */
    } 

    //TODO: check for overflow?

    // CAN-Task wake up after reading all messages
    // from the CAN Controller
    CO_NEW_RX_MSG(CO_REDCY_PARA);		/* message received */
}

/*----------------------------------------------------------------------------*/ 
/*! 
 @brief CAN transmit interrupt handler

*/
/*----------------------------------------------------------------------------*/ 
void can_tx_isr_handler(const struct device *dev, int error_flags, void *arg){
    ARG_UNUSED(dev);
    ARG_UNUSED(arg);

#ifdef CONFIG_CAN_DEBUG_VARS
    GL_DRV_ARRAY(cal_tx_ints) ++;
#endif /* CONFIG_CAN_DEBUG_VARS */

 #ifdef CONFIG_CAN_DEBUG_VARS
     GL_DRV_ARRAY(cal_tx_ints) ++;
 #endif /* CONFIG_CAN_DEBUG_VARS */

    /* get next transmission request */
    GetNext_TX_Request(CO_REDCY_PARA); 
}

/*----------------------------------------------------------------------------*/ 
/*! 
 @brief CAN state change interrupt handler

*/
/*----------------------------------------------------------------------------*/ 
void can_state_change_isr_handler(const struct device *dev, enum can_state state, struct can_bus_err_cnt err_cnt, void *arg){
    ARG_UNUSED(dev);
    ARG_UNUSED(arg);
    ARG_UNUSED(err_cnt);
#ifdef CONFIG_DRIVER_TEST
    printk("CAN line state changed to ");
#endif
    
#ifdef CONFIG_CAN_DEBUG_VARS
    GL_DRV_ARRAY(cal_ch_ints) ++;
#endif /* CONFIG_CAN_DEBUG_VARS */

    switch (state){
        case CAN_STATE_ERROR_ACTIVE:
            setNewDriverState(CANFLAG_ACTIVE CO_COMMA_REDCY_PARA);
            CO_NEW_RX_MSG(CO_LINE_PARA);
			#ifdef CONFIG_DRIVER_TEST
            printk("active\n");
			#endif
            break;
        case CAN_STATE_ERROR_PASSIVE:
            setNewDriverState(CANFLAG_PASSIVE CO_COMMA_REDCY_PARA);
            CO_NEW_RX_MSG(CO_LINE_PARA);
            #ifdef CONFIG_DRIVER_TEST
            printk("passive\n");
			#endif

            break;
        case CAN_STATE_BUS_OFF:
            setNewDriverState(CANFLAG_BUSOFF CO_COMMA_REDCY_PARA);
            CO_NEW_RX_MSG(CO_LINE_PARA);
			#ifdef CONFIG_DRIVER_TEST
            printk("bus off\n");
			#endif
            break;
        default: 
            //Nothing here
            break;
    }
}

/*----------------------------------------------------------------------------*/ 
/*! 
 @brief Init_CAN - initialize the CAN-Controller

 @param[in] label CAN device label
 @param[in] baudrate

 @retval CO_INIT_CAN_OK success
 @retval CO_E_INIT_HARD_RES_ACTIVE reset is aktive, init failed
 @retval CO_E_INIT_BAUD could'nt adjust baudrate, init failed

*/
/*----------------------------------------------------------------------------*/ 
u8_t Init_CAN(char *label,	u32_t wBaudRate	CO_COMMA_REDCY_PARA_DECL){

#ifdef CONFIG_DRIVER_TEST
#ifdef CONFIG_MULT_LINES
    printk("Init_CAN(wBaudRate=%d, canLine=%s)\n", (int)wBaudRate, canLine);
#else
    printk("Init_CAN(wBaudRate=%d)\n", (int)wBaudRate);
#endif /* CONFIG_MULT_LINES */

#ifdef CONFIG_CAN_FULLCAN_SOFT_RTR
    printk(".. def CONFIG_CAN_FULLCAN_SOFT_RTR\n");
#else 
    printk(".. notdef CONFIG_CAN_FULLCAN_SOFT_RTR\n");
#endif /* CONFIG_CAN_FULLCAN_SOFT_RTR */
#endif /* CONFIG_DRIVER_TEST */


#ifdef CONFIG_TIME_TEST
    //For time test - initialize pins
    pin_dev = device_get_binding(PIN_DEV_LABEL);
    if (pin_dev == NULL) {
		printk("Pin device driver not found: %s.\n", PIN_DEV_LABEL);
		return CO_E_INIT_UNSPEC_ERROR;
	}
    gpio_pin_configure(pin_dev, 4, GPIO_OUTPUT);
    gpio_pin_configure(pin_dev, 5, GPIO_OUTPUT);
#endif

    CO_RESET_BIT(0);
    CO_RESET_BIT(1);
    
    can_dev = device_get_binding(label);

	if (can_dev == NULL) {
		printk("CAN: Device driver not found: %s.\n", label);
		return CO_E_INIT_UNSPEC_ERROR;
	}

    
    can_baudrate = wBaudRate;
    
    can_set_bitrate(can_dev, can_baudrate);
    can_set_mode(can_dev, CAN_MODE_LISTENONLY);


 #  ifdef CONFIG_CAN_DEBUG_VARS
     /* interrupt counters */
     GL_DRV_ARRAY(cal_ch_ints) = 0;
     GL_DRV_ARRAY(cal_tx_ints) = 0;
     GL_DRV_ARRAY(cal_rx_ints) = 0;
 #  endif

     /* reset internal driver state information */
     GL_DRV_ARRAY(coCanDriverState) = CANFLAG_INIT;


 #  ifdef CONFIG_DRIVER_FAST_SORT
     GL_DRV_ARRAY(fStartCan) = CO_FALSE;
 #  endif /*CONFIG_DRIVER_FAST_SORT*/


     /* reset TX/RX buffer */
     clearRxBuffer(CO_REDCY_PARA);
     clearTxBuffer(CO_REDCY_PARA);


     /* initialize COB Array access */
     initCobList( CO_REDCY_PARA );


#ifdef CONFIG_CAN_FULLCAN_SOFT_RTR
    /*--------------------------------------------------------*/
    /* FullCAN mode                                           */
    /* init the Acceptance filter                             */ 
    /*--------------------------------------------------------*/
    //TODO: initialize the acceptance filter
#else /* CONFIG_CAN_FULLCAN_SOFT_RTR */
    /*--------------------------------------------------------*/
    /* BasicCAN mode                                          */
    /*--------------------------------------------------------*/
    /* disable RX Filter/bypass active - receive all Messages */
    struct can_filter filter = {
        .id = 0,
        .mask = 0,
        .flags = 0,
    };
    int filter_id = can_add_rx_filter(can_dev, can_rx_isr_handler, NULL, &filter);
    if (filter_id < 0) {
        printk("CAN: Cannot attach isr handler: %s.\n", label);
        return CO_E_INIT_UNSPEC_ERROR;
    }
    can_rx_filter_id = filter_id;

    /*--------------------------------------------------------*/
#endif /* CONFIG_CAN_FULLCAN_SOFT_RTR */
    can_set_state_change_callback(can_dev, can_state_change_isr_handler, NULL);

     /*--------------------------------------------------------*/
     /* special handling for some services */
     /*--------------------------------------------------------*/
 #ifdef CONFIG_SYNC_CONSUMER
     /* special handling for some services */
     GL_DRV_ARRAY(coSyncId) = CAN_NO_COBID;
 #endif /* CONFIG_SYNC_CONSUMER */


#ifdef CONFIG_CAN_TX_TEST
    /****************** T X - T E S T *******************************/
    /* configure and transmit one test object to measure the bitrate.
     * This could be done if no receiver is connected to the CAN network.
     * We use Msg-Id 100 with one data byte = 0xaa.
     */

    struct can_frame test_frame = {
        .flags = 0,
        .id = 0x100,
        .dlc = 1,
        .data = {0xAA}
    };
    can_set_mode(can_dev, CAN_MODE_NORMAL);
    /* This sending call is blocking until the message is sent. */
    can_send(can_dev, &test_frame, K_MSEC(100), NULL, NULL);    
    printk("CAN driver test: sent 100 0xaa\n");
    /****************** T X - T E S T * END***************************/


#endif /* CONFIG_CAN_TX_TEST */

    return(CO_INIT_CAN_OK);
}

/*----------------------------------------------------------------------------*/ 
/*!
 @brief Start_CAN - starts the CAN-Controller

 @returns nothing

*/
/*----------------------------------------------------------------------------*/ 
void Start_CAN(CO_REDCY_PARA_DECL){
//    DISABLE_CAN_INTERRUPTS(CO_REDCY_PARA);

#ifdef CONFIG_DRIVER_FAST_SORT
    GL_DRV_ARRAY(fStartCan) = CO_TRUE;

    createCobIdIndex(CO_REDCY_PARA);
#endif /* CONFIG_DRIVER_FAST_SORT */

    can_set_mode(can_dev, CAN_MODE_NORMAL);

    setNewDriverState(CANFLAG_ACTIVE CO_COMMA_REDCY_PARA);

    GetNext_TX_Request(CO_REDCY_PARA); /* Transmit buffered messages */

    ENABLE_CAN_INTERRUPTS(CO_REDCY_PARA);
}/* void Start_CAN */

/*----------------------------------------------------------------------------*/ 
/*!
 @brief Stop_CAN - stops the CAN-Controller
 
 @returns nothing

*/
/*----------------------------------------------------------------------------*/ 
void Stop_CAN(CO_REDCY_PARA_DECL){

    can_set_mode(can_dev, CAN_MODE_LISTENONLY);

    DISABLE_CAN_INTERRUPTS(CO_REDCY_PARA);

    /* signal new state */
    setNewDriverState(CANFLAG_INIT CO_COMMA_REDCY_PARA);

#ifdef CONFIG_DRIVER_FAST_SORT
    GL_DRV_ARRAY(fStartCan) = CO_FALSE;
#endif


}

/*----------------------------------------------------------------------------*/
/*!
 @brief Transmit_COB - transmits a COB

  Transmits a CAN message with the attribute of \em pCOB
  and the data of \em pMsg.

 @param[in] pCOB pointer to COB in list
 @param[in] pMsg pointer to data

 @retval CO_OK no error
*/
/*----------------------------------------------------------------------------*/
RET_T Transmit_COB(COB_T *pCOB, u8_t *pMsg CO_COMMA_GLOBVARS_PARA_DECL){
    RET_T retval;

#  if defined(CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
UNSIGNED8	canLine;
#  endif /* CONFIG_MULT_LINES */

    if( pCOB == NULL ){
        return CO_E_NO_INITIATE;
    }

#  if defined(CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
    canLine = pCOB->canLine;
#  endif

#  ifdef CONFIG_DRIVER_TEST
#    if defined(CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
    PRINTF("Line %d ", (int)canLine);
#    endif
    printk("Transmit_COB: Id: 0x%04X\n", (int)(pCOB->cobId));
#  endif /* CONFIG_DRIVER_TEST */

    retval = Insert_TX_Request(pCOB, pMsg CO_COMMA_GLOBVARS_PARA);

    /* get next transmission request from queue
     * in this time the TX IRQ is not allowed */
    DISABLE_CAN_INTERRUPTS(CO_REDCY_PARA);
    GetNext_TX_Request(CO_REDCY_PARA);
    RESTORE_CAN_INTERRUPTS(CO_REDCY_PARA);

    return retval;
    
}

/*----------------------------------------------------------------------------*/
/*!
 @brief setNewDriverState - set a new CAN driver state

 If the state was changed, the library will be informed.

 @param[in] newState state to be set

*/
/*----------------------------------------------------------------------------*/
static void setNewDriverState(u8_t newState	CO_COMMA_REDCY_PARA_DECL){
    u8_t tmpState;

    tmpState = GL_DRV_ARRAY(coCanDriverState);

    if ((newState & CANFLAG_STATE_MASK) != 0){
        /* switch to a different CAN bus state */
        tmpState &= (u8_t)~CANFLAG_STATE_MASK;
    }
    
    tmpState |= newState;

    if(GL_DRV_ARRAY(coCanDriverState) != tmpState)	{
        /* only trigger changes  - CAN Overrun will not saved */
        GL_DRV_ARRAY(coCanDriverState) = tmpState & (u8_t)~CANFLAG_OVERFLOW;
        SET_CAN_FLAG(newState);
        /* signal new state */
        SET_COLIB_FLAG(COFLAG_CAN_EVENT);
    }
}

/*----------------------------------------------------------------------------*/
/*!
 @brief GetNext_TX_Request - transmits message from the transmission queue

  The function is called within the CAN ISR and directly in Transmit_COB();
  A Full CAN controller in Basic CAN mode
  uses object CAN_TRANSMIT_OBJ to Transmit a COB.
  In Full CAN mode the object specified in bChannel
  of the COB structure is used.

 @returns nothing

*/
/*----------------------------------------------------------------------------*/
static void GetNext_TX_Request(CO_REDCY_PARA_DECL){
    BUFFER_ENTRY_PTR_T pBuffer;             /* Pointer to Message Buffer */
    COB_KIND_T eType = CO_COB_DISABLED;     /* COB Type */
    
    struct can_frame tx_frame;

    BUFFER_INIT_PTR(TX, Read);
    CHECK_BUFFER_READ( TX ){

        tx_frame.dlc = BUFFER_READ( TX , bLength);
        eType = BUFFER_READ( TX , eType);

        tx_frame.flags = 0U;
        tx_frame.id = BUFFER_READ( TX , cobId);

        if ((eType & CO_COB_DIR_RTR_MASK) == CO_COB_RX_RTR){
			/* Transmit Remote Frames */
            tx_frame.flags |= CAN_FRAME_RTR;
		}else{
			/* Transmit data frame */

			tx_frame.data[0] = BUFFER_READ(TX, pData[0]);
			tx_frame.data[1] = BUFFER_READ(TX, pData[1]);
			tx_frame.data[2] = BUFFER_READ(TX, pData[2]);
			tx_frame.data[3] = BUFFER_READ(TX, pData[3]);
			tx_frame.data[4] = BUFFER_READ(TX, pData[4]);
			tx_frame.data[5] = BUFFER_READ(TX, pData[5]);
			tx_frame.data[6] = BUFFER_READ(TX, pData[6]);
			tx_frame.data[7] = BUFFER_READ(TX, pData[7]);

		}


		/* Transmit message */
		can_send(can_dev, &tx_frame, K_MSEC(100), can_tx_isr_handler, NULL);
		/* buffer empty */
		BUFFER_ENTRY_INCR( TX , Read , EMPTY);

    }

}

/*----------------------------------------------------------------------------*/
/*!
 @brief Define_COB - creates a COB in the COB-list with attributes

	Creates a COB in the COB-list with attributes given as parameter.
	With Full-CAN controllers also object channels
	in the controllers hardware are occupied.
	The channel is configured according to the type of the COB
	as transmit or receive object.
	The COB ID is assigned later on, with a call to Set_COB_ID().

 @param[in] eType COB type
 @param[in] bLength COB length

 @returns pointer to COB

 @retval  not NULL success
 @retval NULL definition failed, e.g. no more acceptance filters within Full-CAN mode
*
*/
COB_T *Define_COB(COB_KIND_T eType, u8_t bLength CO_COMMA_REDCY_PARA_DECL){

	COB_T 	*pCOB;

#ifdef CONFIG_DRIVER_TEST
    printk("\nDefine_COB(): ");
#endif


	// Create a new COB
    pCOB = initCobEntry(CO_REDCY_PARA);

    if (pCOB == NULL){
    	return(NULL);  // not enough COB entries
    }

    pCOB->cobId    = CAN_NO_COBID; // disable ID
    pCOB->bLength  = bLength;
    pCOB->eType    = (COB_KIND_T)(CO_COB_DISABLED | eType);
    pCOB->bChannel = CAN_NO_CHANNEL; // no channel
#  if defined(CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
    pCOB->canLine  = canLine;
#  endif
/*---------------------------------------------------------------------*/

#  ifdef CONFIG_DRIVER_TEST
    printk("pCOB %p ", pCOB);
    if ((eType & CO_COB_DIR_MASK) == CO_COB_RX){
    	printk("RX ");
	}
	if ((eType & CO_COB_DIR_MASK) == CO_COB_TX){
		printk("TX ");
	}
	if ((eType & CO_COB_RTR) == CO_COB_RTR){
		printk("RTR ");
	}
	if (eType == CO_COB_GUARD_SLAVE){
		printk("NG ");
	}
	if (eType == CO_COB_HB_PROD){
		printk("HB_P ");
	}
	if (eType == CO_COB_HB_CONS){
		printk("HB_C ");
	}
	if (eType == CO_COB_SYNC_CONS)	{
		printk("SYNC ");
	}
	printk("\n");
#  endif /* CONFIG_DRIVER_TEST */

    return(pCOB);
}/* COB_T *Define_COB */



/*----------------------------------------------------------------------------*/
/*!
 @brief Set_COB_ID - assigns an identifier to a specific COB

	Change the COB-ID in software and in the CAN controller channel.
	Also a new COB Type will be set. If needed, a new channel will used.
	If the change is not possible, the old setting will used.

 @param[in] *pCOB	pointer to COB in list
 @param[in] cobId	identifier
 @param[in] cobType	(new) COB-ID Type

 @retval CO_E_NO_INITIATE pCOB is NULL, no memory available
 @retval CO_E_TRANS_TYPE 29 Bit identifier are not supported
 @retval CO_E_CAN_TRANS_ERROR could not restore old settings
 @retval CO_OK no error
*/
/*----------------------------------------------------------------------------*/
RET_T Set_COB_ID(COB_T *pCOB, u32_t cobId, COB_KIND_T cobType CO_COMMA_GLOBVARS_PARA_DECL){

#ifdef CONFIG_MULT_LINES
UNSIGNED8	canLine;
#endif

    if( pCOB == NULL ){
        return CO_E_NO_INITIATE;
    }
    
#ifdef CONFIG_STANDARD_IDENTIFIER
    /* 29 bit IDs not allowed, yet */
    if ((cobId & CAN_29_BIT_ID_FLAG) != 0){
		return(CO_E_TRANS_TYPE);
    }
    if((cobId & CAN_29_BIT_ID_MASK) > 0x7FF){
    	return(CO_E_VALUE_TO_HIGH);
    }
#else
#error "29bit ID's not implemented, yet"
#endif

    pCOB->cobId = (COB_IDENT_T)cobId;
    pCOB->eType = cobType;
    
#  ifdef CONFIG_MULT_LINES
    canLine = pCOB->canLine;
#  endif


#ifdef CONFIG_DRIVER_TEST
#if defined(CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
    printk("L %d: ", (int)canLine);
#endif
    printk("Set_COB_ID(), pCOB %p, Type=0x%x, id=%d/%x\n", pCOB, (int)pCOB->eType, (int)cobId, (int)cobId);
#endif

#ifdef CONFIG_DRIVER_FAST_SORT
	// Update index list only in the active state
    if (GL_DRV_ARRAY(fStartCan) == CO_TRUE) {
		createCobIdIndex(CO_REDCY_PARA);
	}
#endif

#ifdef CONFIG_CAN_FULLCAN_SOFT_RTR
    //  Write new COB-ID to the CAN hardware
    struct can_filter filter = {
        .id = cobId,
        .mask = CAN_STD_ID_MASK,
        .flags = ((pCOB->eType & CO_COB_RTR) != 0U) ? CAN_FRAME_RTR : 0U,
    };

    int filter_id = can_add_rx_filter(can_dev, can_rx_isr_handler, NULL, &filter);
    if (filter_id < 0) {
#ifdef CONFIG_DRIVER_TEST
		printk("Set_COB_ID(): No free filter for COB 0x%04X.\n", cobId);
#endif
		return CO_E_INIT_UNSPEC_ERROR;
	}
#ifdef CONFIG_DRIVER_TEST
	else{
		printk("Set_COB_ID(): Filter #%d attached for COB  0x%04X.\n",  filter_id, cobId);
	}
#endif
#endif //CONFIG_CAN_FULLCAN_SOFT_RTR

// Special handling
#ifdef CONFIG_SYNC_CONSUMER
    if( pCOB->eType == CO_COB_SYNC_CONS ){
		GL_DRV_ARRAY(coSyncId) = (COB_IDENT_T)cobId;
    }
#endif

    return CO_OK;
}

/*----------------------------------------------------------------------------*/
/*!
 @brief Signalize that a message arrives or line state changes

 */
/*----------------------------------------------------------------------------*/
void New_Rx_Msg(CO_LINE_PARA_DECL){
	k_sem_give(&can_sem);
}

/*----------------------------------------------------------------------------*/
/*!
 @brief Wait until new data arrive or line state changes

 */
/*----------------------------------------------------------------------------*/
void Wait_For_New_Msg(CO_LINE_PARA_DECL){
	k_sem_take(&can_sem, K_FOREVER);
}

