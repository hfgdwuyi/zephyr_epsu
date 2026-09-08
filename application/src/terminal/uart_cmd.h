/*
 * uart_cmd.h — 上位机命令服务（USART1, 115200）
 *
 * 由 terminal 任务周期调用 uartCmdPoll()：UART 中断接收放入环形缓冲，
 * 按行解析 ASCII 命令并执行（DOUT / DAC / PWM 控制，状态查询）。
 */

#ifndef UART_CMD_H
#define UART_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/* 处理一周期：取一行命令解析执行（幂等，首次调用自动初始化 UART）。 */
void uartCmdPoll(void);

/* DFU 上传进行中（供其它 printk 线程在升级期间静默，避免与 ACK 竞争 USART1）。 */
bool uartCmdDfuActive(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_CMD_H */
