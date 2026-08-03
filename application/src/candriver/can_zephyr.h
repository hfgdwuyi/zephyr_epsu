#ifndef CAN_ZEPHYR_H
#define CAN_ZEPHYR_H

#include <stdint.h>

/* Legacy Zephyr integer aliases used by the original CANopen driver port. */
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;


#if defined (CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT)
#   ifdef CONFIG_MULT_LINES
extern unsigned char coLibFlags [CO_MAX_CAN_LINES];/* CONFIG_MULT_LINES */
    #else
extern unsigned char coLibFlags [2]; /* CONFIG_REDUNDANCY_SUPPORT */
    #endif
#   define SET_COLIB_FLAG(FLAG)   DI_FLAG(coLibFlags[canLine] |= (FLAG))
#   define RESET_COLIB_FLAG(FLAG) DI_FLAG(coLibFlags[canLine] &= ~(FLAG))
#   define TEST_COLIB_FLAG(FLAG)  (coLibFlags[canLine] & (FLAG))
#   define SET_COLIB_FLAG_ISR(FLAG)   DI_FLAG(coLibFlags[canLine] |= (FLAG))
#   define RESET_COLIB_FLAG_ISR(FLAG) DI_FLAG(coLibFlags[canLine] &= ~(FLAG))
#  else /* (CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT) */

extern unsigned char coLibFlags;
#   define SET_COLIB_FLAG_ISR(FLAG)   (GL_ARRAY(coLibFlags) |= (FLAG))
#   define RESET_COLIB_FLAG_ISR(FLAG) (GL_ARRAY(coLibFlags) &= ~(FLAG))
#  endif /* (CONFIG_MULT_LINES) || defined(CONFIG_REDUNDANCY_SUPPORT) */

#ifdef CONFIG_MULT_LINES
    #define CAN_LAST_OBJ        (((1024/CONFIG_MULT_LINES/2) * 2) - 1 - 4)
#else
    #define CAN_LAST_OBJ        (1024 - 1 - 4)
#endif // CONFIG_MULT_LINES 

#define CAN_NO_CHANNEL          0xFF

#ifdef CONFIG_STANDARD_IDENTIFIER
    #define CAN_NO_COBID        0xFFFFu
#else
    #define CAN_NO_COBID        0xFFFFFFFFul
#endif // CONFIG_STANDARD_IDENTIFIER 

u8_t Init_CAN(char *label, u32_t wBaudRate	CO_COMMA_REDCY_PARA_DECL);
RET_T Transmit_COB(COB_T *pCOB, UNSIGNED8 *pMsg CO_COMMA_GLOBVARS_PARA_DECL);
void New_Rx_Msg(CO_LINE_PARA_DECL);
void Wait_For_New_Msg(CO_LINE_PARA_DECL);
    
#endif //CAN_ZEPHYR_H
