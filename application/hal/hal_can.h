/*
 * hal_can.h — CAN传输抽象接口 (RTOS无关)
 *
 * CANopen协议栈通过此接口收发CAN帧，不依赖具体CAN控制器或RTOS。
 */

#ifndef HAL_CAN_H
#define HAL_CAN_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CAN帧接收回调 */


typedef void (*hal_can_rx_cb_t)(uint32_t cob_id, const uint8_t *data, uint8_t len);

/* 初始化CAN控制器 */
void hal_can_init(void);

/* 发送CAN帧, 返回0成功 */
int  hal_can_send(uint32_t cob_id, const uint8_t *data, uint8_t len);

/* 注册接收回调 */
void hal_can_rx_register(hal_can_rx_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */
