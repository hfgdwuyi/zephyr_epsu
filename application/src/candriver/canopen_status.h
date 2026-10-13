#ifndef CANOPEN_STATUS_H
#define CANOPEN_STATUS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * CANopen 节点运行状态 —— 应用层诊断接口。
 *
 * 这个头文件刻意不包含任何 CANopen 库头文件，方便串口命令等纯应用代码
 * 查询节点状态而不必拖入整套 CANopen 类型。
 */

/* 最近一次 NMT 状态（由 nmtslave.c 的 newStateInd() 更新）：
 * 0xFF = 尚未进入任何状态；其余见库的 NODE_STATE_T
 * （0 INITIALISATION / 1 STOPPED / 2 OPERATIONAL / 3 PRE-OPERATIONAL）*/
extern volatile uint8_t canopenNmtState;

/* 收到本节点 0x700+nodeId 帧（boot-up + 心跳）的计数。
 * 回环模式下"发送即接收"，因此该计数增长即证明节点在发心跳。*/
uint32_t canopenGetHeartbeatRxCount(void);

/* 协议栈请求发送（Transmit_COB）的次数 —— 用来区分
 * "软件根本没发" 和 "发了但总线上没人 ACK（error-passive/bus-off）"。*/
uint32_t canopenGetTxAttempts(void);

/* 控制器层面的发送结果：
 *   txdone = 发送完成且总线上有 ACK（总线正常）；
 *   txfail = 发送失败（无 ACK / bus-off / 控制器拒绝）；
 *   txrej  = can_send() 直接返回错误（控制器未启动、队列满等，根本没进总线）。*/
uint32_t canopenGetTxDone(void);
uint32_t canopenGetTxFail(void);
uint32_t canopenGetTxReject(void);

/* 挂起/恢复 CANopen 收发与消息分发（串口升级 DFU 期间使用）。
 * 升级时总线上可能没有主站：本节点会反复重发/bus-off，而 1 ms 定时器
 * 还在持续唤醒分发线程，会干扰 DFU 的串口时序。挂起后控制器停止收发，
 * 分发线程也不再处理消息；canopenResume() 可重复调用且是幂等的。*/
/* 运行时切换 CAN 控制器工作模式（不必重烧固件）：
 *   loopback = true  → 内部回环（总线上无其他节点时自检收发）
 *   loopback = false → 正常工作模式
 * 内部会先 can_stop() 再改模式、然后 can_start()。*/
int canopenSetLoopback(bool on);
bool canopenGetLoopback(void);

void canopenSuspend(void);
void canopenResume(void);
bool canopenIsSuspended(void);

#endif /* CANOPEN_STATUS_H */
