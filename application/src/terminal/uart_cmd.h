/*
 * uart_cmd.h - host command service (USART1, 115200)
 *
 * uartCmdPoll() is called periodically (scheduler 10 ms thread): the UART ISR
 * fills a ring buffer, lines are parsed as ASCII commands.
 */

#ifndef UART_CMD_H
#define UART_CMD_H

/* Process one period: parse and run a line (idempotent, inits UART once). */
void uartCmdPoll(void);

/* DFU upload in progress (other printk threads stay quiet to avoid contention). */
bool uartCmdDfuActive(void);

#endif /* UART_CMD_H */
