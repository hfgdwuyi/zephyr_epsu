/*
 * usr_301 - modul for user interfaces
 *
 * Copyright (c) 2011-2017 port GmbH Halle (Saale)
 *------------------------------------------------------------------
 */

/**
* \file usr_301.c
* user interface between the CANopen Library and the user application
* for CANopen services according to CiA-301
*
* \author port GmbH Halle (Saale)
*
* This module contains callback functions to the CANopen services
* specified in the CiA standard CiA-301.
* Additionally there are callback functions for the error handling
* and CANopen Library timers.
* These callback functions are called by the CANopen Library
* when the CANopen service is active and the implemented application-specific
* actions are executed.
*
* The user is responsible for the content of all callback functions.
*/

/* headers of standard C - libraries */
#include <stdio.h>

/* headers of the CANopen Library  */
#include <cal_conf.h>
#include <co_stru.h>
#include <co_acces.h>
#include <co_sdo.h>
#include <co_pdo.h>
#include <co_time.h>
#include <co_emcy.h>
#include <co_flag.h>
#include <co_usr.h>
#include <co_nmt.h>
#include <co_timer.h>
#include <co_drv.h>

#if defined(CONFIG_MASTER) || defined(CONFIG_SLAVE_PLUS)
#include <co_nmt_m.h>
#endif /* defined(CONFIG_MASTER) || defined(CONFIG_SLAVE_PLUS) */

/* #include <objects.h> */

/* constant definitions
---------------------------------------------------------------------------*/

/* local defined data types
---------------------------------------------------------------------------*/

/* list of external used functions, if not in headers
---------------------------------------------------------------------------*/

/* list of global defined functions
---------------------------------------------------------------------------*/

/* list of local defined functions
---------------------------------------------------------------------------*/

/* external variables
---------------------------------------------------------------------------*/

/* global variables
---------------------------------------------------------------------------*/

/* local defined variables
---------------------------------------------------------------------------*/


/*******************************************************************/
/**
* \brief getNodeId - get the node ID of the device
*
* This function has to be filled by the user.
* It has to return the node ID of the device from e.g. a DIP switch
* or nonvolatile memory to the CANopen layer.
*
* \return node-ID
* node-ID in the range of 1..127, 255
*/
UNSIGNED8 getNodeId(
    CO_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
UNSIGNED8 nodeId;

    /* read node-ID from HW-switch or
     * load node-ID from nonvolatile memory */
    nodeId = 0x20; /* e.g. */
    return(nodeId);
}


#ifdef CONFIG_EMCY_CONSUMER
/*******************************************************************/
/**
* \brief emcyInd - indicate the receipt of an EMCY message
*
* This function is called after an EMCY messages was received
* from an other EMCY producer node in the network.
* The user can define an application-specific error handling here.
*
* \return
* nothing
*/
void emcyInd(
    UNSIGNED8   emcyNum,     /**< emergency number */
    EMERGENCY_T *pEmcy       /**< data of the received EMCY message */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    switch (pEmcy->errCode) {
        case 0x4000:
            break;
        default:
            break;
    }
}
#endif /* CONFIG_EMCY_CONSUMER */


#ifdef CONFIG_TIME_CONSUMER
/*******************************************************************/
/**
* \brief timeInd - indicate the receipt of a Time Stamp object
*
* In this function the user can implement the application-specific
* Time Stamp handling.
* The \c TIME_OF_DAY_T structure, referenced by address, contains
* the time in ms after midnight and the number of day since January 1, 1984.
*
* \return
* nothing
*/
void timeInd(
    TIME_OF_DAY_T *address   /**< Time Stamp object */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* CONFIG_TIME_CONSUMER */


#ifdef CONFIG_PDO_CONSUMER
/*******************************************************************/
/**
* \brief pdoInd - indicate the receipt of a PDO
*
* This function is called after a PDO was received.
* All data from this PDO are saved at the object dictionary
* before this function is called.
* Synchronous PDOs are processed after the next SYNC was received.
* It will be saved at the object dictionary
* and after that this function is called.
*
* \return
* nothing
*/
void pdoInd(
    UNSIGNED16 pdoNum        /**< number of PDO 1..512 */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    switch(pdoNum) {
        case 1:
            break;
        default:
            break;
    }
}
#endif /* CONFIG_PDO_CONSUMER */


#if defined(CONFIG_PDO_CONSUMER) && defined(CONFIG_PDO_BAD_LEN_INDICATION)
/*******************************************************************/
/**
* \brief pdoLenInd - indicate the receipt of a PDO with invalid length
*
* This function is called after a PDO was received with invalid length.
* The received PDO data do not match the PDO mapping.
*
* The parameter info means:
* - PDO_LEN_TO_SHORT - to less data received, data will not be processed
* - PDO_LEN_TO_LONG - to much data received, PDO data will be processed
*   unused data are ignored
*
* The call of this indication function can be enabled for PDO consumer
* by the CANopen Design Tool about:
* Line / Object Dictionary / Communication Segment /
* <PDO communication object> / tab Mask / General PDO Settings /
* Enable Wrong PDO Lenght indication
*
* \return
* CANopen return value
*/
RET_T pdoLenInd(
    UNSIGNED16 pdoNum,       /**< number of PDO */
    UNSIGNED8 info           /**< type of the error */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    return (CO_OK);
}
#endif /* CONFIG_PDO_CONSUMER && CONFIG_PDO_BAD_LEN_INDICATION */


#if defined(CONFIG_PDO_CONSUMER) && defined(CONFIG_PDO_EVENTTIMER)
/*******************************************************************/
/**
* \brief pdoTimerInd - indicate the occurrence of a PDO timer event
*
* In this function the user can implement an application-specific
* handling for an occurred PDO timer event.
* For PDO remote requests, initiated by calling the function readPdoReq(),
* the optional timer event time is used to watch the
* occurrence of the requested PDO.
* If the PDO does not arrive in this period,
* this function will be called.
*
* \return
* nothing
*/
void pdoTimerInd(
    UNSIGNED16 pdoNum        /**< number of PDO */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* defined(CONFIG_PDO_CONSUMER) && defined(CONFIG_PDO_EVENTTIMER) */


#if defined(CONFIG_PDO_PRODUCER) && defined(CONFIG_PDO_EVENTTIMER) \
	&& defined(CONFIG_PDO_EVENTTIMER_INDICATION)
/*******************************************************************/
/**
* \brief pdoEventTimerInd - event timer PDO shall be transmitted
*
* This function is called if the time for a timer driven PDO has been elapsed
* and the PDO should be transmitted.
* The user has the possibility to actualize the data
* or start other activities for this event.
*
* The call of this indication function can be enabled for PDO producers
* with a PDO event timer by the CANopen Design Tool about:
* Line / Object Dictionary / Communication Segment /
* <PDO communication object> / tab Mask / General PDO Settings /
* Enable PDO Event Timer Indication
*
* \return
* nothing
*/
void pdoEventTimerInd(
    UNSIGNED16 pdoNum        /**< number of PDO */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* CONFIG_PDO_PRODUCER && CONFIG_PDO_EVENTTIMER && CONFIG_PDO_EVENTTIMER_INDICATION */


#if defined(CONFIG_PDO_PRODUCER) && defined(CONFIG_PDO_RTR_IND)
/*******************************************************************/
/**
* \brief rtrPdoInd - RTR PDO shall be transmitted
*
* This function is called if a RTR was received
* and the PDO should be transmitted.
* The user has the possibility to actualize the data
* or start other activities for this event.
*
* The call of this indication function can be enabled for PDO producers
* by the CANopen Design Tool about:
* Line / Object Dictionary / Communication Segment /
* <PDO communication object> / tab Mask / General PDO Settings /
* Enable RTR-PDO Indication Function
*
* \return
* nothing
*/
void rtrPdoInd(
    UNSIGNED16 pdoNum        /**< number of PDO */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* defined(CONFIG_PDO_PRODUCER) && defined(CONFIG_PDO_RTR_IND) */


#ifdef CONFIG_SDO_SERVER
/********************************************************************/
/**
* \brief sdoWrInd - indicate the receipt of a SDO write request
*
* This function is called if an SDO write request reaches the CANopen
* SDO server. Parameters of the function are the index and sub-index
* of the entry in the local object dictionary where the data
* should be written to.
*
* If numerical data with size up to 4 byte should be written,
* the Library stores the previous value in a temporary buffer.
* The new value is put into the local object dictionary.
*
* If the application does not accept this new value and this function
* return with an error, the old value is restored from the temporary buffer
* and written back to the object dictionary and the SDO write request
* will be answered with a "\b Abort \b Domain \b Transfer" by the Library.
* The SDO Abort Code can be specified by the \b return -value.
*
* \return
* The return value, which has to be specified by the application,
* selects the possible protocol answer of the SDO write request.
* \retval CO_OK
* success
* \retval RET_T
* One of the valid, SDO related, values can be returned.
* This value is transferred  to  \em abortSdoTransf_Req() .
* Possible are:
* \li \c CO_E_NONEXIST_OBJECT
* \li \c CO_E_NONEXIST_SUBINDEX
* \li \c CO_E_NO_READ_PERM
* \li \c CO_E_NO_WRITE_PERM
* \li \c CO_E_MAP
* \li \c CO_E_DATA_LENGTH
* \li \c CO_E_TRANS_TYPE
* \li \c CO_E_VALUE_TO_HIGH
* \li \c CO_E_VALUE_TO_LOW
* \li \c CO_E_WRONG_SIZE
* \li \c CO_E_PARA_INCOMP
* \li \c CO_E_HARDWARE_FAULT
* \li \c CO_E_SRD_NO_RESSOURCE
* \li \c CO_E_SDO_CMD_SPEC_INVALID
* \li \c CO_E_MEM
* \li \c CO_E_SDO_INVALID_BLKSIZE
* \li \c CO_E_SDO_INVALID_BLKCRC
* \li \c CO_E_SDO_TIMEOUT
* \li \c CO_E_INVALID_TRANSMODE
* \li \c CO_E_SDO_OTHER
* \li \c CO_E_DEVICE_STATE
*
* All other return values are defaulting to E_SDO_OTHER.
*/
RET_T sdoWrInd(
    UNSIGNED16 index,        /**< index of object */
    UNSIGNED8  subIndex      /**< sub-index of object */
#ifdef CONFIG_SPLIT_INDICATION
    ,UNSIGNED8 sdoNum        /**< number of the SDO service */
#endif
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
)
{
    return(CO_OK);
}
#endif /* CONFIG_SDO_SERVER */


#ifdef CONFIG_SDO_SERVER
/********************************************************************/
/**
* \brief sdoRdInd - indicate the receipt of a SDO read request
*
* This function is called after the SDO server has received a SDO read
* request and before the Library transmits the requried object value
* from the object dictionary. The user has the possibility to update
* the object value before the object value is sent.
*
* If this functions returns an error, a SDO Abort Transfer is initiated.
*
* \retval CO_OK
* success
* \retval RET_T
* One of the valid, SDO related, values can be returned.
* This value is transferred  to  \em abortSdoTransf_Req() .
* Possible are:
* \li \c CO_E_NONEXIST_OBJECT
* \li \c CO_E_NONEXIST_SUBINDEX
* \li \c CO_E_NO_READ_PERM
* \li \c CO_E_NO_WRITE_PERM
* \li \c CO_E_MAP
* \li \c CO_E_DATA_LENGTH
* \li \c CO_E_TRANS_TYPE
* \li \c CO_E_VALUE_TO_HIGH
* \li \c CO_E_VALUE_TO_LOW
* \li \c CO_E_WRONG_SIZE
* \li \c CO_E_PARA_INCOMP
* \li \c CO_E_HARDWARE_FAULT
* \li \c CO_E_SRD_NO_RESSOURCE
* \li \c CO_E_SDO_CMD_SPEC_INVALID
* \li \c CO_E_MEM
* \li \c CO_E_SDO_INVALID_BLKSIZE
* \li \c CO_E_SDO_INVALID_BLKCRC
* \li \c CO_E_SDO_TIMEOUT
* \li \c CO_E_INVALID_TRANSMODE
* \li \c CO_E_SDO_OTHER
* \li \c CO_E_DEVICE_STATE
*
* All other return values are defaulting to E_SDO_OTHER.
*/
RET_T sdoRdInd(
    UNSIGNED16 index,        /**< index of object */
    UNSIGNED8 subIndex       /**< sub-index of object */
#ifdef CONFIG_SPLIT_INDICATION
    ,UNSIGNED8 sdoNum        /**< number of the SDO service */
#endif
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    return(CO_OK);
}
#endif /* CONFIG_SDO_SERVER */


#if defined(CONFIG_SDO_SERVER) && defined(CONFIG_DOMAIN_INDICATION_SIZE)
/********************************************************************/
/**
* \brief sdoDomainInd - domain size border reached
*
* This function is called for SDO Domain transfers
* after the receipt of CONFIG_DOMAIN_INDICATION_SIZE bytes.
* The application gets the possibilty to save the received data
* for instance in the flash memory.
* After leaving this function is buffer will be overwritten
* with new received data.
*
* The buffer size CONFIG_DOMAIN_INDICATION_SIZE will not be
* divisible by the data length of the SDO messages, i.e. divisible by 7.
* This function is called when CONFIG_DOMAIN_INDICATION_SIZE and more data
* are received.
* The application is responsible to save the oversized bytes temporarily.
* Therefore the function gets as parameter:
* - actSize: number of bytes at the buffer to process by the application,
*   including the number of overSize bytes from the last cycle
* - overSize: number of oversized bytes received with last SDO
* The CANopen Library controls the byte counting.
*
* At the end of SDO transfer the indication function sdoWrInd() is called.
*
* \return
* CANopen return value
*/
# ifdef CO_CONFIG_DOMAIN_INDICATION_DEFERRABLE
RET_T sdoDomainInd(
        UNSIGNED16      index,          /**< index at object dictionary */
        UNSIGNED8       subIndex,       /**< subindex at object dictionary */
        UNSIGNED8       *pData,         /**< pointer the domain buffer */
        UNSIGNED32      actSize,        /**< number of Bytes to flash */
        UNSIGNED8       overSize,       /**< number of Bytes to buffer */
        UNSIGNED8       sdoNr,          /**< number of the SDO service */
        BOOL_T          pausable        /**< if this process can delay the SDO processing */
        CO_COMMA_LINE_PARA_DECL         /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
UNSIGNED8 flashBuffer[CONFIG_DOMAIN_INDICATION_SIZE];
                                      /* buffer to store data for flashing */
static UNSIGNED8 savedBuffer[7];      /* temporary buffer for oversized bytes */
static UNSIGNED8 savedBufferSize = 0; /* byte number in the temporary buffer */
static UNSIGNED8 sdoDomain_sdoNr = 0; /* processed SDO server number */

    /* 1.step: copy overSize bytes from the last cycle to flash buffer */
    memcpy(&flashBuffer[0], &savedBuffer[0], savedBufferSize);

    /* 2.step: copy new received bytes to flash buffer */
    memcpy(&flashBuffer[savedBufferSize], pData, actSize);

    /* 3.step: save new overSize data into the temporary buffer
     *         for the next flash cycle */
    memcpy(&savedBuffer[0], pData + actSize, overSize);
    savedBufferSize = overSize;

    /* 4.step: flash data from flash buffer */

    /* flash data */

    /* return options */

    /* to delay SDO response */
    /* SDO response will be generated when finishSdoDomainInd(..) is called */
    /* sdoDomain_sdoNr = sdoNr;
    return(CO_SDO_IND_BUSY); */

    /* generate SDO abort */
    /* any CO_E_... error code */
    /* return(CO_E_DEVICE_STATE); */ /* application error */

    /* no delay, no abort */
    return(CO_OK);
}
# else /* CO_CONFIG_DOMAIN_INDICATION_DEFERRABLE */
RET_T sdoDomainInd(
    UNSIGNED16 index,    /**< index if current SDO access */
    UNSIGNED8 subIndex,  /**< sub-index of current SDO access */
    UNSIGNED8 *pData,    /**< pointer to domain buffer */
    UNSIGNED32 actSize,  /**< number of bytes to flash */
    UNSIGNED8 overSize   /**< number of bytes to store temporarily */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
UNSIGNED8 flashBuffer[CONFIG_DOMAIN_INDICATION_SIZE];
                                      /* buffer to store data for flashing */
static UNSIGNED8 savedBuffer[7];      /* temporary buffer for oversized bytes */
static UNSIGNED8 savedBufferSize = 0; /* byte number in the temporary buffer */

    /* 1.step: copy overSize bytes from the last cycle to flash buffer */
    memcpy(&flashBuffer[0], &savedBuffer[0], savedBufferSize);

    /* 2.step: copy new received bytes to flash buffer */
    memcpy(&flashBuffer[savedBufferSize], pData, actSize);

    /* 3.step: save new overSize data into the temporary buffer
     *         for the next flash cycle */
    memcpy(&savedBuffer[0], pData + actSize, overSize);
    savedBufferSize = overSize;

    /* 4.step: flash data from flash buffer */

    return(CO_OK);
}
# endif /* CO_CONFIG_DOMAIN_INDICATION_DEFERRABLE */
#endif /* defined(CONFIG_SDO_SERVER) && defined(CONFIG_DOMAIN_INDICATION_SIZE) */


#if defined(CONFIG_SDO_SERVER) && defined(CONFIG_DOMAIN_INDICATION_SIZE)
/********************************************************************/
/**
* \brief coUserSdoDomainUploadInd - update buffer for domain upload
*
* This function is called for SDO domain upload transfers
* after the sending of CONFIG_DOMAIN_INDICATION_SIZE bytes.
* The data has to be provided by the application.
*
* \return
* CANopen return value
*/
RET_T coUserSdoDomainUploadInd(
    UNSIGNED16 index,        /**< index if current SDO access */
    UNSIGNED8 subIndex,      /**< sub-index of current SDO access */
    UNSIGNED8 *pData,        /**< pointer to the domain buffer */
    UNSIGNED32 *pSize        /**< number of loaded bytes to the domain buffer */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    return(CO_OK);
}
# endif /* defined(CONFIG_SDO_SERVER) && defined(CONFIG_DOMAIN_INDICATION_SIZE) */


#if defined(CONFIG_SDO_SERVER) && defined(CONFIG_VALUE_CHECK_FUNCTION)
/********************************************************************/
/**
* \brief testSdoValue - check the value of a SDO before writing to OD
*
* This function makes it possible to check the received value
* before the new value is written into the object dictinary.
* If the return value is not CO_OK the write access will be refused.
* The new value is not written into the object dictinary.
*
* This function is also called for non-numerical objects which usually contain
* more than 8 byte and so both pData and size may not point to valid data.
* The library is then not able to provide backed up data and the new value is
* always written. In this case this function will simply provide another
* point at which to attach some logic, for example some initialization
* for an SDO domain transfer. Care must be taken that this function returns
* CO_OK for non-numerical data.
*
* \return
* CANopen return value
*/
RET_T testSdoValue(
    UNSIGNED16 index,        /**< index of object */
    UNSIGNED8 subIndex,      /**< sub-index of object */
    void *pData,             /**< pointer to new data, little-endian format */
    UNSIGNED32 size          /**< data size */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
#  ifdef CONFIG_BIG_ENDIAN
UNSIGNED8 convBuffer[4];     /* swapping buffer */
UNSIGNED8 i;                 /* loop counter */

    if (size <= 4) {
        for (i = 0; i < size; i++) {
            convBuffer[i] = ((UNSIGNED8 *)pData)[size-1-i];
        }
        pData = &convBuffer;
    }
#  endif

    return CO_OK;
}
#endif /* defined(CONFIG_SDO_SERVER) && defined(CONFIG_VALUE_CHECK_FUNCTION) */


#ifdef CONFIG_SDO_CLIENT
/*******************************************************************/
/**
* \brief sdoWrCon - confirmation function for SDO write access
*
* This function signals that the message sent by \em writeSdoReq()
* was confirmed by the SDO server.
* It handles errors and is useful for program synchronization.
* If the \em errorFlag is not zero, the last SDO transfer
* was terminated by an \b abort \b domain \b transfer.
* The reason for the termination is contained in the \em errorFlag.
* The \em errorFlag can be combinations of the following constants:
*
* \arg E_SDO_NO_ERROR
* \arg E_SDO_SERVICE
* \arg E_SDO_INCONS_PARA
* \arg E_SDO_ILLEG_PARA
* \arg E_SDO_ACCESS
* \arg E_SDO_UNSUPP_ACCESS
* \arg E_SDO_NONEXIST_OBJECT
* \arg E_SDO_INVALID_ADDRESS
* \arg E_SDO_HARDWARE_FAULT
* \arg E_SDO_TYPE_CONFLICT
* \arg E_SDO_INCONS_OBJ_ATTR
* \arg E_SDO_OTHER
* \arg E_SDO_TIMEOUT
*
* \return
* nothing
*/
void sdoWrCon(
    UNSIGNED8 sdoNum,        /**< number of SDO service */
    UNSIGNED32 errorFlag     /**< error information */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    /* SDO timeout occurred */
    if (errorFlag == E_SDO_TIMEOUT) {
        return;
    }

    switch (errorFlag & 0xFF000000)  {
        /* successful confirmation */
        case E_SDO_NO_ERROR:
            break;

        /* service error */
        case E_SDO_SERVICE:
            switch (errorFlag & 0x00FF0000) {
                /* inconsistent parameter */
                case E_SDO_INCONS_PARA:
                    break;
                /* wrong communication parameter or
                 * internal SDO object not exist */
                case E_SDO_ILLEG_PARA:
                    break;
            }
            break;

            /* access error */
            case E_SDO_ACCESS:
                switch (errorFlag & 0x00FF0000) {
                    /* unsupported access */
                    case E_SDO_UNSUPP_ACCESS:
                        switch (errorFlag & 0x00000000FFUL) {
                            /* no write permission */
                            case E_SDO_A_NO_WRITE_PERM:
                                break;
                            default:
                                break;
                        }
                        break;
                    /* index does not exist */
                    case E_SDO_NONEXIST_OBJECT:
                        break;
                    /* mapping fault */
                    case E_PDO_MAPPING:
                        break;
                    /* harware fault */
                    case E_SDO_HARDWARE_FAULT:
                        break;
                    /* size of SDO value is not equal the defined size */
                    case E_SDO_TYPE_CONFLICT:
                        break;
                    /* inconsistent attribut */
                    case E_SDO_INCONS_OBJ_ATTR:
                        switch (errorFlag & 0x00000000FFUL) {
                            /* value higher than maximum */
                            case E_SDO_A_NONEXIST_SUBINDEX:
                                break;
                            /* value higher than maximum */
                            case E_SDO_A_VALUE_TO_HIGH:
                                break;
                            /* value lesser than minimum */
                            case E_SDO_A_VALUE_TO_LOW:
                                break;
                            /* invalid value (testSdoValue) */
                            case E_SDO_A_INVALID_VAL:
                                break;
                            /* object attribute inconsistent */
                            case E_SDO_A_VALUE_RANGE_EXCEED:
                                break;
                            default:
                                break;
                        }
                }
                break;

            case E_SDO_OTHER:
                break;

            default:
                break;
    }
}
#endif /* CONFIG_SDO_CLIENT */


#ifdef CONFIG_SDO_CLIENT
/*******************************************************************/
/**
* \brief sdoRdCon - confirmation function for SDO read access
*
* This functions signals that the message sent by \em readSdoReq()
* was confirmed by the SDO Server.
* It handles errors and is useful for program synchronization.
* If the \em errorFlag is not zero, the last SDO transfer
* was terminated by an \b abort \b domain \b transfer.
* The \em errorFlag contains the reason for the termination.
* In case of success the parameter \em pObj from \em readSdoReq()
* points to the read value.
* The \em errorFlag can be combinations of the following constants:
*
* \arg E_SDO_NO_ERROR
* \arg E_SDO_SERVICE
* \arg E_SDO_INCONS_PARA
* \arg E_SDO_ILLEG_PARA
* \arg E_SDO_ACCESS
* \arg E_SDO_UNSUPP_ACCESS
* \arg E_SDO_NONEXIST_OBJECT
* \arg E_SDO_INVALID_ADDRESS
* \arg E_SDO_HARDWARE_FAULT
* \arg E_SDO_TYPE_CONFLICT
* \arg E_SDO_INCONS_OBJ_ATTR
* \arg E_SDO_OTHER
* \arg E_SDO_TIMEOUT
*
* \return
* nothing
*/
void sdoRdCon(
    UNSIGNED8 sdoNum,        /**< number of SDO service */
    UNSIGNED32 errorFlag     /**< error information */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    if (errorFlag == E_SDO_TIMEOUT) {
        return;
    }

    switch (errorFlag & 0xFF000000)  {
        /* successful confirmation */
        case E_SDO_NO_ERROR:
            break;

        /* service error */
        case E_SDO_SERVICE:
            switch (errorFlag & 0x00FF0000) {
                /* inconsistent parameter */
                case E_SDO_INCONS_PARA:
                    break;
                /* wrong communication parameter or
                 * internal SDO object not exist */
                case E_SDO_ILLEG_PARA:
                    break;
            }
            break;

        /* access error */
        case E_SDO_ACCESS:
            switch (errorFlag & 0x00FF0000) {
                /* unsupported access */
                case E_SDO_UNSUPP_ACCESS:
                    switch (errorFlag & 0x00000000FFUL) {
                        /* no read permission */
                        case E_SDO_A_NO_READ_PERM:
                            break;
                        default:
                            break;
                    }
                    break;
                /* index does not exist */
                case E_SDO_NONEXIST_OBJECT:
                    break;
                /* harware fault */
                case E_SDO_HARDWARE_FAULT:
                    break;
                /* size of SDO value is not equal the defined size */
                case E_SDO_TYPE_CONFLICT:
                    break;
                /* inconsistent attribut */
                case E_SDO_INCONS_OBJ_ATTR:
                    switch (errorFlag & 0x00000000FFUL) {
                        /* sub-index does not exist */
                        case E_SDO_A_NONEXIST_SUBINDEX:
                            break;
                        /* value range exceeded */
                        case E_SDO_A_VALUE_RANGE_EXCEED:
                            break;
                        default:
                            break;
                    }
            }
            break;

        case E_SDO_OTHER:
            break;

        default:
            break;
    }
}
#endif /* CONFIG_SDO_CLIENT */


#if defined(CONFIG_SDO_CLIENT) && defined(CONFIG_DOMAIN_CONFIRMATION)
/********************************************************************/
/**
* \brief sdoDomainCon - domain size border reached
*
* This function is called for SDO Domain transfers
* after CONFIG_DOMAIN_INDICATION_SIZE bytes were received.
* The data has to be process by the application.
* After leaving this function is buffer will be overwritten
* with new received data.
*
* The buffer size CONFIG_DOMAIN_INDICATION_SIZE will not be
* divisible by the data length of the SDO messages, i.e. divisible by 7.
* This function is called when CONFIG_DOMAIN_INDICATION_SIZE and more data
* are received.
* The application is responsible to save the oversized bytes temporarily.
* Therefore the function gets as parameter:
* - actSize: number of bytes at the buffer to process by the application,
*   including the number of overSize bytes from the last cycle
* - overSize: number of oversized bytes received with last SDO
* The CANopen Library controls the byte counting.
*
* \return
* CANopen return value
*/
RET_T sdoDomainCon(
    UNSIGNED32 actSize,  /**< number of bytes to flash */
    UNSIGNED8 overSize   /**< number of bytes to store temporarily */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
UNSIGNED8 flashBuffer[CONFIG_DOMAIN_INDICATION_SIZE];
                                      /* buffer to store data for flashing */
static UNSIGNED8 savedBuffer[7];      /* temporary buffer for oversized bytes */
static UNSIGNED8 savedBufferSize = 0; /* byte number in the temporary buffer */

    /* 1.step: copy overSize bytes from the last cycle to flash buffer */
    memcpy(&flashBuffer[0], &savedBuffer[0], savedBufferSize);

    /* 2.step: copy new received bytes to flash buffer */
    memcpy(&flashBuffer[savedBufferSize], pData, actSize);

    /* 3.step: save new overSize data into the temporary buffer
     *         for the next flash cycle */
    memcpy(&savedBuffer[0], pData + actSize, overSize);
    savedBufferSize = overSize;

    /* 4.step: flash data from flash buffer */

    return(CO_OK);
}
#endif /* CONFIG_SDO_CLIENT && CONFIG_DOMAIN_CONFIRMATION */


#if defined(CONFIG_MASTER) || defined(CONFIG_HEARTBEAT_CONSUMER) || defined(CONFIG_NODE_GUARDING)
/********************************************************************/
/**
* \brief mGuardErrorInd - indicate the occurrence of an error control event
*
* This function defines the application-specific reaction on Node Guarding
* or a Heartbeat events.
*
* Meaning of the parameter:
*
* \li  CO_LOST_GUARDING_MSG
* \par
* Guarding time has elapsed or Node Guarding toggle bit has not altered.
*
* \li  CO_LOST_CONNECTION
* \par
* The lifetime (lifetime factor * guarding time) is elapsed.
*
* \li  CO_NODE_STATE
* \par
* The guarding node has not the expected state.
*
* \li  CO_BOOT_UP
* \par
* Bootup message was received.
*
* \li  CO_HB_STARTED
* \par
* First Heartbeat message was received.
*
* \li  CO_LOST_HEARTBEAT
* \par
* Heartbeat is missing. Heartbeat will be disabled.
*
* \return
* nothing
*/
void mGuardErrorInd(
    UNSIGNED8 nodeId,        /**< node-ID of the producer */
    ERROR_SPEC_T kind        /**< kind of error control event */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    switch(kind) {
        case CO_LOST_GUARDING_MSG:
            break;
        case CO_LOST_CONNECTION:
            break;
        case CO_NODE_STATE:
            break;
        case CO_BOOT_UP:
            break;
        case CO_HB_STARTED:
            break;
        case CO_LOST_HEARTBEAT:
            break;
        default:
            break;
    }
}
#endif /* defined(CONFIG_HEARTBEAT_CONSUMER) || defined(CONFIG_NODE_GUARDING) */


#if defined(CONFIG_NODE_GUARDING)
/********************************************************************/
/**
* \brief sGuardErrorInd - indicate the occurrence of a Node Guarding event
*
* This function defines the reaction on a Node Guarding event
* on the local NMT slave.
* The first missing Guarding message is accepted because the timer resolution.
* An error event is occurred for the second missed Guarding message.
*
* Meaning of the parameter:
*
* \li CO_GUARDING_STARTED
* \par
* Node Guarding was (re)-started.
*
* \li  CO_LOST_GUARDING_MSG
* \par
* Guarding time is elapsed at least the second time.
*
* \li  CO_LOST_CONNECTION
* \par
* The lifetime (lifetime factor * guarding time) is elapsed.
*
* \retval 0
* Node shall keep in the current state.
* \retval 1
* Node shall be forced to the state PRE_OPERATIONAL.
*/
UNSIGNED8 sGuardErrorInd(
    ERROR_SPEC_T kind        /**< kind of Node Guarding event */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
UNSIGNED8 u8Ret;             /* return value */

    switch(kind) {
        case CO_GUARDING_STARTED:
            u8Ret = 0;
        case CO_LOST_GUARDING_MSG:
            u8Ret = 0;
        case CO_LOST_CONNECTION:
            u8Ret = 1;
        default:
            u8Ret = 0;
    }

    return (u8Ret);
}
#endif /* CONFIG_NODE_GUARDING */


#ifdef CONFIG_NON_VOLATILE_MEM
/********************************************************************/
/**
* \brief saveParameterInd - store data into nonvolatile memory
*
* This function indicates a "store parameters to nonvolatile memory"
* command via SDO. This command is a SDO write access to object 0x1010
* with the signature "save".
* In this function the application has to integrate the target-specific
* save functions.
* If this function returns an error an \b SDO \b Abort \b Domain \b Transfer
* is initiated with the error code "hardware fault".
* The parameter segment corresponds to the sub-index of object 0x1010.
*
* \retval CO_TRUE
* success
* \retval CO_FALSE
* error
*/
BOOL_T saveParameterInd(
    UNSIGNED8 segment        /**< sub-index which specifies the memory segment */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    switch(segment) {
        /* all parameters */
        case MEM_SEG_ALL_PARAMETERS:
            break;
        /* communication parameter */
        case MEM_SEG_COM_PARAMETERS:
            break;
        /* application parameter */
        case MEM_SEG_APPL_PARAMETERS:
            break;
        /* segment 4 - 127 manufacturer-specific */
        default:
            return (CO_FALSE);
    }
    return (CO_TRUE);
}
#endif /* CONFIG_NON_VOLATILE_MEM */


#ifdef CONFIG_NON_VOLATILE_MEM
/********************************************************************/
/**
* \brief clearParameterInd - restoring of default parameter
*
* This function indicates a "restore default parameter".
* It is called at a write access to the object 0x1011 with the signature
* "load".
*
* The application has to ensure that the next call of the function
* loadParameterInd() after Reset Communication or Reset Application
* makes all default values available. This can be done by erasing
* the nonvolatile memory at this function.
*
* If this function returns an error an
* \b SDO \b Abort \b Domain \b Transfer
* is initiated with the error code "hardware fault".
*
* \retval CO_TRUE
* success
* \retval CO_FALSE
* error
*/
BOOL_T clearParameterInd(
    UNSIGNED8 segment        /**< sub-index which specifies the memory segment */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    switch(segment) {
        /* all parameters */
        case MEM_SEG_ALL_PARAMETERS:
            break;
        /* communication parameter */
        case MEM_SEG_COM_PARAMETERS:
            break;
        /* application parameter */
        case MEM_SEG_APPL_PARAMETERS:
            break;
        /* segment 4 - 127 manufacturer specific */
        default:
            return (CO_FALSE);
    }
    return (CO_TRUE);
}
#endif /* CONFIG_NON_VOLATILE_MEM */


#ifdef CONFIG_NON_VOLATILE_MEM
/********************************************************************/
/**
* \brief loadParameterInd - load parameters from nonvolatile memory
*
* This function is called to load parameters from the nonvolatile memory
* to the object dictionary. It is called from the Library
* at Boot-up, Reset Communication and Reset Application.
*
* The parameter \em segment describes,
* which part of the object dictionary shall be updated.
*
* The parameter mode specifies, which restore mode should be used:
*
* \li CO_RESTORE_MODE_BOOTUP
* \par
* During boot-up the object dictionary is overwritten
* by data from nonvolatile memory.
*
* \li CO_RESTORE_MODE_RESETCOMM
* \par
* During the next Reset Communication after a write access to object 0x1011
* the object dictionary is overwritten by data from nonvolatile memory.
*
* \li CO_RESTORE_MODE_SDO
* \par
* The signature 'load' was written to object 0x1011.
*
* \retval CO_TRUE
* success
* \retval CO_FALSE
* error
*/
BOOL_T loadParameterInd(
    UNSIGNED8 segment,       /**< sub-index which specifies the memory segment */
    UNSIGNED8 mode           /**< restore mode */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    switch(segment) {
        /* all parameters */
        case MEM_SEG_ALL_PARAMETERS:
            break;
        /* communication parameter */
        case MEM_SEG_COM_PARAMETERS:
            break;
        /* application parameter */
        case MEM_SEG_APPL_PARAMETERS:
            break;
        /* segment 4 - 127 manufacturer specific */
        default:
            return (CO_FALSE);
    }

    return (CO_TRUE);
}
#endif /* CONFIG_NON_VOLATILE_MEM */


#ifdef CONFIG_CAN_ERROR_HANDLING
/********************************************************************/
/**
* \brief canErrorInd - indicate the occurrence of errors on the CAN driver
*
* This function indicates the following errors:
*
* - \c CANFLAG_ACTIVE -
*   CAN Error Active
*
* - \c CANFLAG_BUSOFF -
* CAN-controller error CAN Busoff
*
* - \c CANFLAG_PASSIVE -
* CAN-controller error
*
* - \c CANFLAG_OVERFLOW -
* CAN-controller overrun error
*
* - \c CANFLAG_TXBUFFER_OVERFLOW -
* transmit buffer overflow
*
* - \c CANFLAG_RXBUFFER_OVERFLOW -
* receive buffer overflow
*
*
* All occurred status changes since the last canErrorInd() call are indicated.
* The current state can be read with getCanDriverState().
*
* The handling of CAN driver error can be enabled
* by the CANopen Design Tool about:
* General Settings / Enable CAN communication error handling
*
* \retval
* CO_TRUE
* CAN controller has to stay in the current state
* \retval
* CO_FALSE
* CAN controller has to go to BUS-ON again
*/
BOOL_T canErrorInd(
    UNSIGNED8 errorFlags     /**< CAN error flags */
    CO_COMMA_REDCY_PARA_DECL /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
UNSIGNED8 state;             /* current state of the CAN driver */
BOOL_T ret = CO_TRUE;        /* return value */
# ifdef CONFIG_EMCY_PRODUCER
RET_T retVal;                /* return value from sub-routine */
# endif /* CONFIG_EMCY_PRODUCER */

    /* CAN error passive */
    if ((errorFlags & CANFLAG_PASSIVE) != 0) {

# ifdef CONFIG_EMCY_PRODUCER
	retVal = writeEmcyReq(ERRCODE_CAN_PASSIVE, NULL CO_COMMA_LINE_PARA);
	if (retVal != CO_OK) {
	}
# endif /* CONFIG_EMCY_PRODUCER */
    }

    /* CAN bus-off */
    if ((errorFlags & CANFLAG_BUSOFF) != 0) {
	ret = CO_FALSE; /* auto Bus-On */
    }

    /* CAN controller overflow */
    if ((errorFlags & CANFLAG_OVERFLOW) != 0) {
    }

    /* Library RX buffer overflow */
    if ((errorFlags & CANFLAG_RXBUFFER_OVERFLOW) != 0) {
    }

    /* Library TX buffer overflow */
    if ((errorFlags & CANFLAG_TXBUFFER_OVERFLOW) != 0) {
    }

    state = getCanDriverState(CO_LINE_PARA);

    /* CAN controller inactive */
    if ((state & CANFLAG_INIT) != 0) {
    }

    /* CAN controller active */
    if ((state & CANFLAG_ACTIVE) != 0) {
    }

    /* CAN bus-off */
    if ((state & CANFLAG_BUSOFF) != 0) {
    }

    /* CAN error passive */
    if ((state & CANFLAG_PASSIVE) != 0) {
    }

    return(ret);
}
#endif /* CONFIG_CAN_ERROR_HANDLING */


#ifdef CONFIG_SYNC_PRE_CMD
/*******************************************************************/
/**
* \brief syncPreCommand - actions after SYNC
*
* This function is called immediately after the SYNC was received,
* before other actions, e.g. transmit and receice PDOs, are started.
* The application can update data for PDOs or define own actions.
*
* The call of this indication function can be enabled by the
* CANopen Design Tool about:
* Line / Object Dictionary / Communication Segment / object 1005h /
* tab Mask / General SYNC Settings /
* Enable User Function immediately at SYNC Message
*
* \return
* nothing
*/
void syncPreCommand(
    CO_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* CONFIG_SYNC_PRE_CMD */


#ifdef CONFIG_SYNC_CMD
/*******************************************************************/
/**
* \brief syncCommand - actions after SYNC and updated services
*
* This function is called after the SYNC was received
* and all activities of the Library, e.g. transmit PDOs, are done.
* The application can execute own actions.
*
* The call of this indication function can be enabled by the
* CANopen Design Tool about:
* Line / Object Dictionary / Communication Segment / object 1005h /
* tab Mask / General SYNC Settings /
* Enable User Function after SYNC Message
*
* \return
* nothing
*/
void syncCommand(
    CO_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* CONFIG_SYNC_CMD */


#ifdef CONFIG_USER_TIMER_EVENT
/********************************************************************/
/**
* userTimerEvent - user timer event occurred
*
* This function is called if a user-specific timer has been elapsed.
* The parameter contains the pointer to the actual timer structure.
*
* The call of this indication function can be enabled by the
* CANopen Design Tool about:
* General Settings / Apply user-timer functionality
*
* \return
* nothing
*/
void userTimerEvent(
    TIMER_EVENT_T *pTimer    /**< pointer at user timer */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    /* if (pTimer == &invalidTime)  { */
    /* } */
}
#endif /* CONFIG_USER_TIMER_EVENT */


#ifdef CONFIG_VIRTUAL_OBJECTS
/****************************************************************************/
/**
* \brief getVirtualObjAddr - get address of a virtual object
*
* This function supports the handling of \em virtual objects
* in the manufacturer-specific part of the object dictionary
* and the access from the CANopen network by using SDOs.
* The function delivers the address of a \em virtual object
* referenced by \em index and \em subIndex.
*
* The Library calls this function if no object in the object
* dictionary exists. The application has to provide a pointer
* to varianle and the length of this variable.
* Allowed is only a maximum of up to 4 bytes.
* Does no virtual object with index and subIndex exist
* the correct SDO Abort Code has to be returned.
*
* \return
* CANopen return value
*/
RET_T getVirtualObjAddr(
    UNSIGNED16 index,        /**< index of the object */
    UNSIGNED8 subIndex,      /**< sub-index of the object */
    UNSIGNED8 **ppData,      /**< destination for data address*/
    UNSIGNED32 *pSize        /**< destination for data size */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
RET_T coRet = CO_E_NONEXIST_OBJECT;    /* return value */

    return(coRet);
}
#endif /* CONFIG_VIRTUAL_OBJECTS */


#ifdef CONFIG_VIRTUAL_OBJECTS
/****************************************************************************/
/**
* \brief getVirtualObjAttr - get attributes of a virtual object
*
* This function supports the handling of \em virtual objects
* in the manufacturer-specific part of the object dictionary
* and the access from the CANopen network by using SDOs.
* The function delivers the address of an \em virtual object
* referenced by \em index and \em subIndex.
*
* \retval 0
* object does not exist or attribute is zero
* \retval CO_MAP_PERM
* PDO mapping permission
* \retval CO_READ_PERM
* read access permission
* \retval CO_WRITE_PERM
* write access permission
* \retval CO_NUM_VAL
* object has numerical type
* \retval CO_UP_DN_LD_DOMAIN
* object is a domain type
*/
UNSIGNED16 getVirtualObjAttr(
    UNSIGNED16 index,        /**< index of the object */
    UNSIGNED8 subIndex       /**< sub-index of the object */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
    return(0);
}
#endif /* CONFIG_VIRTUAL_OBJECTS */


#ifdef CONFIG_USER_CAN_MSG
/****************************************************************************/
/**
* \brief usrCanMsgReceived - callback for user defined rx messages
*
* This function supports the handling of \em user-specific cob-IDs
* which where defined by the user.
*
* The Library calls this function when a CAN message
* with a user-defined cob-ID was received.
*
* The compiler define CONFIG_USER_CAN_MSG is set by the CANopen Design Tool
* automatically when the number of user-specific cob-IDs was specified about:
* Line / Additional Settings / Number of user-specific cob-IDs
*
* \return
* nothing
*/
void usrCanMsgReceived(
    CAN_MSG_T *canMsg        /**< pointer at CAN message */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
}
#endif /* CONFIG_USER_CAN_MSG */


#ifdef CO_CONFIG_RESET_OBJ_DIR_IND
/****************************************************************************/
/**
* \brief coResetObjDirInd - reset manufacturer-specific objects
*
* The Library calls this function always when the manufactuere-specific
* objects are reset. The Library does not reset the manufacturer-specific
* objects. This can be useful for time optimizations.
*
* Parameter reason can have the following settings:
*
* \li CO_RESET_OBJ_DIR_IND_APPL
* \par
* Reset manufacturer-specific objects by the application.
*
* The compiler define CO_CONFIG_RESET_OBJ_DIR_IND has to be set by the user
* in the CANopen Design Tool about:
* General Settings / Advanced Configuration in C syntax.
*
* \return
* CANopen return value
*/
RET_T coResetObjDirInd(
    UNSIGNED8 reason         /**< type */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
RET_T coRet = CO_OK;

    switch( reason ) {
        case CO_RESET_OBJ_DIR_IND_APPL:
            break;

        default:
            break;
    }
    return retVal;
}
#endif /*CO_CONFIG_RESET_OBJ_DIR_IND*/


#if defined(CONFIG_MPDO_DEST) || defined(CONFIG_MPDO_SRC)
/****************************************************************************/
/**
* \brief mpdoTimerEventInd - MPDO timer event occurred
*
* This function is called if a MPDO,regardless of source or destination mode,
* timer event is occurred.
*
* Parameter mpdoType can have the following settings:
*
* \li CO_MPDO_DEST_IND
* \par
* MPDO in destination mode
*
* \li CO_MPDO_SRC_IND
* \par
* MPDO in source mode
*
* \return
* CANopen return value
*/
RET_T mpdoTimerEventInd(
    UNSIGNED16 pdoNum,       /**< number of PDO service */
    UNSIGNED8 mpdoType       /**< type of MPDO */
    CO_COMMA_LINE_PARA_DECL  /**< number of CAN line 0..CO_MAX_CAN_LINES-1 */
    )
{
RET_T coRet = CO_OK;

    /* Do application-specific stuff on a mpdo-timer event here,
     * like sending some MPDOs */
    return (coRet);
}
#endif /* CONFIG_MPDO_DEST || CONFIG_MPDO_SRC */

#ifdef CO_CONFIG_DOMAIN_UNKNOWN_SIZE
/********************************************************************/
/**
*  \brief coUserSdoDomainSizeInd ask application for remaining size of
*         transferring domain
*
*  When transferring a domain with the attribute
*  CO_UP_DN_LD_DOMAIN_SIZELESS, the lib will ask for the restsize of
*  the domain before transferring the last segment.
*
*  blocktransfer:
*  return 0-6 lib will transfer 0-6 byte, transfer ends
*  return >=7 lib will transfer data and ask again when at last segment
*  segmented download:
*  return 0-7 lib will transfer 0-7 byte, transfer ends
*  return  >7 lib will transfer data and ask again when at last segment
*
*  (recommended: use segmented download return values regardless of mode)
*
*  The function
*  co_getSdoRestSize(UNSIGNED8, USER_T CO_COMMA_LINE_PARA_DECL);
*  can be used to determine how much data is left from the previous transfer.
*
*
*/
UNSIGNED32 coUserSdoDomainSizeInd(UNSIGNED8 sdoNr CO_COMMA_LINE_PARA_DECL)
{
    /* 0 == no data */
    return (UNSIGNED32) 0;
}
#endif /* CO_CONFIG_DOMAIN_UNKNOWN_SIZE */

#ifdef CO_CONFIG_REPORT_ANY_HB
/********************************************************************/
/**
*  \brief coUserHbReceived ask application to handle received heartbeat
*
*  By default, the CANopen Library only reports "guarding started" or
*  "lost heartbeat" during guarding other nodes with heartbeat consumers.
*  With the optional define CO_CONFIG_REPORT_ANY_HB, the behaviour is
*  changed so that any heartbeat received is also reported to the
*  application via this indication.
*
*  This can result in noticably more cpu load, depending on number and
*  frequency of the used heartbeats.
*
*/
void coUserHbReceived(
    UNSIGNED8 nodeId,
    NODE_STATE_T state
    CO_COMMA_REDCY_PARA_DECL
    )
{
    switch (state)
    {
        case OPERATIONAL:
            switch (nodeId)
            {
                case 0x80:
                    break;
                default:
                    break;
            }
            break;
        case PRE_OPERATIONAL:
            break;
        case STOPPED:
            break;
        default:
            break;
    }
}

#endif /* CO_CONFIG_REPORT_ANY_HB */

#ifdef CO_CONFIG_PDO_INHIBITTIME_INDICATION
/*******************************************************************/
/**
* \brief coUserPdoInhibittimeInd - indicate elapsing of inhibit time
*
* In this function the user can implement an application specific
* handling for elapsed PDO inhibit timers.
*
* kind can be
* 0 for TPDOs
* 1 for RPDOs
*
* RPDOs inhibit timers are undefined in the specification and not
* implemented.
*
*/
RET_T coUserPdoInhibittimeInd(
    UNSIGNED16 pdoNr,
    UNSIGNED8 kind
    CO_COMMA_LINE_PARA_DECL
    )
{
    switch (kind)
    {
        case 0:
            if (pdoNr == 10)
            {
                printf("PDO Nr %u NOT resent\n", pdoNr);
                return CO_E_STATE;
            }

            printf("Inhibit Time elapsed: TPDO %u resent\n", pdoNr);
            return CO_OK;
            break;
        case 1:
            printf("Inhibit Time elapsed: RPDO %u\n", pdoNr);
            break;
        default:
            break;
    }
    return CO_OK;
}
#endif /* CO_CONFIG_PDO_INHIBITTIME_INDICATION */

#ifdef CO_CONFIG_USER_MESSAGE_TEST
/*******************************************************************/
/**
* \brief coUserMessageTestInd - indicate any received message
*
* Any CAN message received by the library will be indicated in this
* function. The application can cause special behaviour or decide that
* the message should be ignored by the library.
*
* Note: SYNC messages are given preference by the stack and handled
* differently. They will not be reported by this function.
*
*\retval CO_OK
* library shall handle the message
*
*\retval other RET_T
* library shall ignore the message
*
*/
RET_T coUserMessageTestInd(
    CAN_MSG_T *canMsg
    CO_COMMA_REDCY_PARA_DECL
)
{
UNSIGNED8 i = 0x0;

    PRINTF("0x%x : ", canMsg-> cobId);
    for (i = 1; i < canMsg->length; i++)
    {
        PRINTF("%x ", canMsg->pData[i-1]);
    }
    PRINTF("\n");

    return CO_OK;
}
#endif /* CO_CONFIG_USER_MESSAGE_TEST */

#if defined(CO_CONFIG_WRONG_MSG_IND_HBC) || defined(CO_CONFIG_WRONG_MSG_IND_NMT)
/*******************************************************************/
/**
* \brief coProtocolErrorInd - indicate a faulty message
*
* Any CAN message received by the library will be indicated in this
* function. The application can cause special behaviour or decide that
* the message should be ignored by the library.
*
* Note: SYNC messages are given preference by the stack and handled
* differently. They will not be reported by this function.
*
*\retval CO_OK
* library shall ignore the message
*
*\retval other RET_T
* library shall handle the message
*
* If the library handles the message further:
* The device will go to STOPPED state in case of NMT error.
* The device will handle the heartbeat, ignoring the faulty length, in case of
* heartbeat error.
*
*/
RET_T coProtocolErrorInd(
    UNSIGNED16 eType,
    CAN_MSG_T *canMsg
    CO_COMMA_REDCY_PARA_DECL
)
{
RET_T retVal = CO_E_LOCAL_CONTROL;

    switch (eType)
    {
#ifdef CO_CONFIG_WRONG_MSG_IND_NMT
        case CO_PROT_ERR_NMT_CMD:
            PRINTF("coProtocolErrorInd: Wrong NMT command\n");
            break;

        case CO_PROT_ERR_NMT_LEN:
            PRINTF("coProtocolErrorInd: Wrong NMT Length\n");
            break;
#endif /* CO_CONFIG_WRONG_MSG_IND_NMT */
#ifdef CO_CONFIG_WRONG_MSG_IND_HBC
        case CO_PROT_ERR_HBC_LEN:
            PRINTF("coProtocolErrorInd: Wrong HBC Length\n");
            break;
#endif /* CO_CONFIG_WRONG_MSG_IND_HBC */
        default:
            retVal = CO_E_LOCAL_CONTROL; /* anything but CO_OK, will cause standard behavior */
            break;
    }

    return retVal;
}
#endif /* defined(CO_CONFIG_WRONG_MSG_IND_HBC) || defined(CO_CONFIG_WRONG_MSG_IND_NMT) */

#ifdef CONFIG_VIRTUAL_OBJECTS_PDO
/****************************************************************************/
/**
* \brief coUserVirtualRpdoInd - receive PDO with virtual object indication
*
* This function indicates a received PDO with a least one virtual object
* mapped in the PDO. The application gets a pointer to the whole PDO data
* and is in charge to copy the data to the virtual object by itself.
* This function gets called after the library copied all the real objects
* to the object dictionary.
*
* \param [in] pdoNr Number of the receive PDO
* \param [in] pBuf pointer to the data of the PDO
*
* \retval not used by the library right now
*
*/
RET_T coUserVirtualRpdoInd(UNSIGNED16 pdoNr, UNSIGNED8* pBuf CO_COMMA_LINE_PARA_DECL)
{
        PRINTF("coUserVirtualRpdoInd pdo-%d\n", pdoNr);

        /* Insanity check */
        if (NULL == pBuf)
        {
                return CO_E_HARDWARE_FAULT;
        }

        switch (pdoNr)
        {
                case 1u:
                        /* copy received data to virtual object */
                        break;
                case 2u:
                        /* copy received data to virtual object */
                        break;
                default:
                        /* should never be here */
                        break;
        }

        return CO_OK;
}
#endif /* CONFIG_VIRTUAL_OBJECTS_PDO */

#ifdef CONFIG_VIRTUAL_OBJECTS_PDO
/****************************************************************************/
/**
* \brief coUserVirtualTpdoInd - transmit PDO with virtual object indication
*
* This function indicates a PDO ready to transmit with a least one virtual object
* mapped in the PDO. The application gets a pointer to the whole PDO data
* and is in charge to fill the buffer with the data of the virtual object.
*
* \param [in] pdoNr Number of the transmit PDO
* \param [out] pBuf pointer to the data of the PDO
*
* \retval CO_OK
* OK
* \retval other
* abort transmit
*
*/
RET_T coUserVirtualTpdoInd(UNSIGNED16 pdoNr, UNSIGNED8* pBuf CO_COMMA_LINE_PARA_DECL)
{
        PRINTF("coUserVirtualTpdoInd pdo-%d\n", pdoNr);

        /* Insanity check */
        if ( NULL == pBuf ) {
                return CO_E_HARDWARE_FAULT;
        }

        switch (pdoNr) {
        case 1u:
                /* fill buffer with data */
                break;
        case 2u:
                /* fill buffer with data */
                break;
        default:
                /* should never be here */
                break;
        }

        return CO_OK;
}
#endif /* CONFIG_VIRTUAL_OBJECTS_PDO */


/*______________________________________________________________________EOF_*/
