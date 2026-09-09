/*
 * uart_cmd.c — 上位机命令服务（USART1 @ PB14(TX)/PB15(RX), 115200, 8N1）
 *
 * 与上位机（PC / 串口助手）通讯：
 *   - UART 中断接收 → 环形缓冲（不丢字节）
 *   - terminal 任务每 500 ms 调用 uartCmdPoll()：取出一行（\r\n 结尾）解析执行
 *   - 响应通过同一 UART 回发（行协议，方便脚本/串口助手调试）
 *   - 注：USART1 同时是 Zephyr console（printk），日志与命令响应同口。
 *
 * 命令集（每行一条）：
 *   help                    — 帮助
 *   info                    — 系统状态（状态机/故障/温度/电压/AC）
 *   dout <idx> <0|1>        — 控制 DOUT 输出（idx 0..doutMax-1）
 *   doutall <hex64>         — 直接写入 64 位 DOUT 位图
 *   dac <mv>                — DAC 恒定输出电压（0..3300 mV，清除方波状态位）
 *   dacwv <0|1>             — pwr_on_off 方波状态位（0=停 1=起，由 bspAoutPoll 驱动）
 *   pwm <ch> <duty>         — 风扇 PWM 占空比（ch 0/1，duty 0..100）
 *   pwmoff <ch>             — 停止 PWM
 *
 * 注意：这些命令直接操作 BSP 输出，与状态机/风扇策略并发时可能被其覆盖，
 * 属调试/产测用途。
 */

/* C standard library */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>   /* snprintk */
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

/* BSP */
#include "bsp_ain.h"    /* AIN_ADC_* 通道枚举 */
#include "bsp_dio.h"
#include "bsp_aout.h"
#include "bsp_pwm.h"

/* Application */
#include "uart_cmd.h"
#include "state_machine.h"
#include "sensor.h"
#include "ac_meter.h"

/* ==================== 常量 ==================== */

#define UART_CMD_DEV   DEVICE_DT_GET(DT_NODELABEL(usart1))   /* PB14(TX)/PB15(RX) */

#define RX_BUF_SIZE    8192   /* 环形缓冲（字节）— DFU 时容纳多行，防 10ms 轮询间隙溢出 */
#define CMD_LINE_MAX   1100   /* 单行命令最大长度（dfu 块 ≤ 512B hex） */
#define TX_CHUNK_MAX   128    /* info 多行输出分块发送 */

/* ==================== 接收环形缓冲 ====================
 * 单生产者（UART ISR）单消费者（terminal 线程）：head 由 ISR 写，tail 由
 * 线程读；满则丢新字节。volatile 索引保证 ISR/线程间可见。 */

static uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

static char     line[CMD_LINE_MAX];
static uint16_t line_len;
static bool     uart_ready;
static bool     inited;

/* ==================== UART 发送 ==================== */

static void uartTxStr(const char *s)
{
	if (!uart_ready) {
		return;
	}
	while (*s != '\0') {
		uart_poll_out(UART_CMD_DEV, (unsigned char)*s++);
	}
}

/* ==================== UART 接收 ISR ==================== */

static void uartRxIsr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	uint8_t c;
	while (uart_fifo_read(dev, &c, 1) == 1) {
		uint16_t next = (uint16_t)((rx_head + 1U) % RX_BUF_SIZE);
		if (next != rx_tail) {
			rx_buf[rx_head] = c;
			rx_head = next;
		}
		/* 缓冲满：丢弃新字节 */
	}
}

/* ==================== 初始化（懒加载，幂等） ==================== */

static void uartCmdInit(void)
{
	if (inited) {
		return;
	}
	inited = true;

	if (!device_is_ready(UART_CMD_DEV)) {
		uart_ready = false;
		return;
	}
	uart_ready = true;

	uart_irq_callback_user_data_set(UART_CMD_DEV, uartRxIsr, NULL);
	uart_irq_rx_enable(UART_CMD_DEV);

	/* 版本横幅走 UART 命令通道（不被 printk 启动刷屏淹没）。 */
	uartTxStr("\r\n===== CiosZhong PSU =====\r\n");
	uartTxStr("  App  v" CONFIG_CIOS_ZHONG_FW_VERSION "\r\n");
	uartTxStr("  Boot v" CONFIG_CIOS_ZHONG_BOOT_VERSION " (MCUboot)\r\n");
	uartTxStr("PSU CMD: ready (help for commands)\r\n");
}

/* ==================== 命令执行 ==================== */

static void cmdHelp(void)
{
	uartTxStr("commands:\r\n"
		  "  help                    - this help\r\n"
		  "  info                    - system status\r\n"
		  "  dout <idx> <0|1>        - set DOUT output (0..doutMax-1)\r\n"
		  "  doutall <hex64>         - write 64-bit DOUT bitmap\r\n"
		  "  dac <mv>                - DAC constant voltage (0..3300 mV)\r\n"
		  "  dacwv <0|1>             - pwr_on_off square wave on/off\r\n"
		  "  pwm <ch> <duty>         - fan PWM duty (ch 0/1, 0..100%)\r\n"
		  "  pwmoff <ch>             - stop PWM\r\n"
		  "  getdout                 - read DOUT bitmap\r\n"
		  "  getdac                  - read DAC mv + wave state\r\n"
		  "  getpwm                  - read PWM duties\r\n"
		  "  getdin                  - read DIN bitmap\r\n");
}

static void cmdInfo(void)
{
	char buf[TX_CHUNK_MAX];

	snprintk(buf, sizeof(buf), "fw=%s boot=%s\r\n",
		 CONFIG_CIOS_ZHONG_FW_VERSION, CONFIG_CIOS_ZHONG_BOOT_VERSION);
	uartTxStr(buf);

	stateMachineState_t st = stateMachineGetState();
	snprintk(buf, sizeof(buf), "state=%d faults=0x%02X err=%s\r\n",
		 (int)st, (unsigned)stateMachineGetFaults(),
		 stateMachineGetErrorStr(stateMachineGetError()));
	uartTxStr(buf);

	snprintk(buf, sizeof(buf), "temp1=%d.%d temp2=%d.%d (C)\r\n",
		 sensorTempGet1() / 10, sensorTempGet1() % 10,
		 sensorTempGet2() / 10, sensorTempGet2() % 10);
	uartTxStr(buf);

	snprintk(buf, sizeof(buf), "pdc0=%u.%03uV 12V=%u.%03uV 5V=%u.%03uV 3V3=%u.%03uV\r\n",
		 sensorGetPhys(AIN_ADC_PDC0) / 1000U, sensorGetPhys(AIN_ADC_PDC0) % 1000U,
		 sensorGetPhys(AIN_ADC_12V) / 1000U, sensorGetPhys(AIN_ADC_12V) % 1000U,
		 sensorGetPhys(AIN_ADC_5V0) / 1000U, sensorGetPhys(AIN_ADC_5V0) % 1000U,
		 sensorGetPhys(AIN_ADC_3V3) / 1000U, sensorGetPhys(AIN_ADC_3V3) % 1000U);
	uartTxStr(buf);

	if (acMeterAcPresent()) {
		snprintk(buf, sizeof(buf), "ac=%u.%03uV %u.%uHz\r\n",
			 acMeterGetVinRmsMv() / 1000U, acMeterGetVinRmsMv() % 1000U,
			 acMeterGetVinFreq() / 10, acMeterGetVinFreq() % 10);
	} else {
		snprintk(buf, sizeof(buf), "ac=n/a\r\n");
	}
	uartTxStr(buf);

	uartTxStr("dout=");
	snprintk(buf, sizeof(buf), "0x%016llX\r\n", (unsigned long long)bspDoutGetBitmap());
	uartTxStr(buf);
}

static void cmdDout(const char *args)
{
	char *tok;
	char *save = NULL;
	long idx, state;

	if (args == NULL) {
		goto err;
	}
	tok = strtok_r((char *)args, " \t", &save);
	if (tok == NULL) { goto err; }
	idx = strtol(tok, NULL, 0);
	tok = strtok_r(NULL, " \t", &save);
	if (tok == NULL) { goto err; }
	state = strtol(tok, NULL, 0);

	if (idx < 0 || idx >= doutMax || (state != 0 && state != 1)) {
		goto err;
	}
	bspDoutSetBitmap(BIT64((uint8_t)idx), state != 0);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: dout <idx> <0|1>\r\n");
}

static void cmdDoutAll(const char *args)
{
	char *end = NULL;
	unsigned long long mask;

	if (args == NULL) {
		goto err;
	}
	mask = strtoull(args, &end, 16);
	if (end == args || (*end != '\0' && *end != '\r' && *end != '\n')) {
		goto err;
	}
	/* 先清全部，再按位图置位（仅低 doutMax 位有效） */
	bspDoutSetBitmap(UINT64_MAX, false);
	bspDoutSetBitmap(mask, true);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: doutall <hex64>\r\n");
}

static void cmdDac(const char *args)
{
	char *end = NULL;
	long mv;

	if (args == NULL) {
		goto err;
	}
	mv = strtol(args, &end, 0);
	if (end == args || mv < 0 || mv > 3300) {
		goto err;
	}
	/* 恒定电压输出：先停方波状态位，再写 DAC */
	bspAoutSetState(AOUT_PWR_ON_OFF, false);
	bspAoutWrite(AOUT_PWR_ON_OFF, (int16_t)mv);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: dac <mv> (0..3300)\r\n");
}

static void cmdDacWave(const char *args)
{
	char *end = NULL;
	long on;

	if (args == NULL) {
		goto err;
	}
	on = strtol(args, &end, 0);
	if (end == args || (on != 0 && on != 1)) {
		goto err;
	}
	bspAoutSetState(AOUT_PWR_ON_OFF, on != 0);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: dacwv <0|1>\r\n");
}

static void cmdPwm(const char *args)
{
	char *tok;
	char *save = NULL;
	long ch, duty;

	if (args == NULL) {
		goto err;
	}
	tok = strtok_r((char *)args, " \t", &save);
	if (tok == NULL) { goto err; }
	ch = strtol(tok, NULL, 0);
	tok = strtok_r(NULL, " \t", &save);
	if (tok == NULL) { goto err; }
	duty = strtol(tok, NULL, 0);

	if (ch < 0 || ch >= 2 || duty < 0 || duty > 100) {
		goto err;
	}
	bspPwmSetDutyCycle((uint8_t)ch, (uint32_t)duty);
	bspPwmStart((uint8_t)ch);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: pwm <ch 0|1> <duty 0..100>\r\n");
}

static void cmdPwmOff(const char *args)
{
	char *end = NULL;
	long ch;

	if (args == NULL) {
		goto err;
	}
	ch = strtol(args, &end, 0);
	if (end == args || ch < 0 || ch >= 2) {
		goto err;
	}
	bspPwmStop((uint8_t)ch);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: pwmoff <ch 0|1>\r\n");
}

/* ---- 状态查询（上位机显示用）---- */

static void cmdGetDout(void)
{
	char buf[48];

	snprintk(buf, sizeof(buf), "dout=0x%016llX\r\n",
		 (unsigned long long)bspDoutGetBitmap());
	uartTxStr(buf);
}

static void cmdGetDac(void)
{
	char buf[48];

	snprintk(buf, sizeof(buf), "dac=%d wv=%d\r\n",
		 (int)bspAoutGetMv(AOUT_PWR_ON_OFF),
		 bspAoutGetState(AOUT_PWR_ON_OFF) ? 1 : 0);
	uartTxStr(buf);
}

static void cmdGetPwm(void)
{
	char buf[48];

	snprintk(buf, sizeof(buf), "pwm0=%u pwm1=%u\r\n",
		 (unsigned)bspPwmGetDutyCycle(FAN_PWM1),
		 (unsigned)bspPwmGetDutyCycle(FAN_PWM2));
	uartTxStr(buf);
}

static void cmdGetDin(void)
{
	char buf[48];

	snprintk(buf, sizeof(buf), "din=0x%08X\r\n", (unsigned)bspDinGetBitmap());
	uartTxStr(buf);
}

/* ==================== 行解析与分发 ==================== */

/* ==================== DFU (串口固件升级 → slot1 + MCUboot swap) ====================
 * 协议（行式，hex，块级 ACK + 偏移校验）：
 *   dfu           进入升级模式（擦除 slot1）
 *   size <hex>    固件总字节数
 *   data <off> <hex..>  数据块：off = 本块在固件中的偏移（hex），
 *                        hex = 块内容（≤512B/行）
 *   host 逐块等 ACK（"ACK <next_off>"），收到 ERR/超时重发同一块；
 *   固件校验 off，写失败不清会话状态，可重试。
 *   写完自动 boot_request_upgrade → 复位 → MCUboot swap-using-offset。
 *
 * MCUboot SWAP_USING_OFFSET 布局：slot1 的第一个扇区保留给 swap 算法做
 * 移动缓冲，升级镜像必须从 slot1 的第二个扇区开始存放（即偏移
 * MCUboot OVERWRITE_ONLY：镜像从 slot1 起始(0x0)写入，MCUboot 读
 * slot1 头判断并整体覆盖到 slot0。DFU_SECONDARY_IMG_OFFSET = 0。
 */
#define DFU_BLOCK_MAX            512   /* bytes per data line */
#define DFU_HEX_CHARS            (DFU_BLOCK_MAX * 2)
/* MCUboot 升级策略为 OVERWRITE_ONLY：MCUboot 从 slot1 起始(0x0)读
 * 镜像头并整体覆盖到 slot0，无 swap 第二扇区偏移要求。故写 0x0 起。 */
#define DFU_SECONDARY_IMG_OFFSET 0x0U

static struct {
	bool     active;
	uint32_t total;
	uint32_t received;
	struct flash_area const *fa;
	uint8_t  block[DFU_BLOCK_MAX];
} dfu;

static uint8_t hexVal(char ch)
{
	if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
	if (ch >= 'a' && ch <= 'f') return (uint8_t)(ch - 'a' + 10);
	if (ch >= 'A' && ch <= 'F') return (uint8_t)(ch - 'A' + 10);
	return 0xFF;
}

/* 解析 hex 字符串到 buf，返回字节数；非法字符返回 -1 */
static int hexParse(const char *s, uint8_t *buf, int max)
{
	int n = 0;
	while (*s != '\0' && n < max) {
		uint8_t hi = hexVal(*s++);
		uint8_t lo = hexVal(*s++);
		if (hi == 0xFF || lo == 0xFF) {
			return -1;
		}
		buf[n++] = (uint8_t)((hi << 4) | lo);
	}
	return n;
}

static void dfuReset(void)
{
	dfu.active = false;
	dfu.total = 0;
	dfu.received = 0;
	dfu.fa = NULL;
}

bool uartCmdDfuActive(void)
{
	return dfu.active;
}

static void cmdDfuEnter(void)
{
	char buf[64];
	int rc;

	dfuReset();
	rc = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &dfu.fa);
	if (rc != 0) {
		uartTxStr("ERR dfu: open slot1 fail\r\n");
		return;
	}
	rc = flash_area_erase(dfu.fa, 0, dfu.fa->fa_size);
	if (rc != 0) {
		uartTxStr("ERR dfu: erase slot1 fail\r\n");
		dfuReset();
		return;
	}
	dfu.active = true;
	snprintk(buf, sizeof(buf), "DFU ok, slot1 erased, send: size <hex>\r\n");
	uartTxStr(buf);
}

static void dfuSetSize(const char *args)
{
	char *end = NULL;
	unsigned long sz;

	if (args == NULL) { goto err; }
	sz = strtoul(args, &end, 16);
	if (end == args || sz == 0 || sz > dfu.fa->fa_size) { goto err; }
	dfu.total = (uint32_t)sz;
	uartTxStr("OK, send: data <hex>\r\n");
	return;
err:
	uartTxStr("ERR usage: size <hexlen>\r\n");
}

static void dfuData(const char *args)
{
	char *save = NULL;
	char *end = NULL;
	char *offtok;
	unsigned long off;
	int n;
	char buf[48];

	if (args == NULL) { goto err; }
	offtok = strtok_r((char *)args, " \t", &save);
	if (offtok == NULL) { goto err; }
	off = strtoul(offtok, &end, 16);
	if (end == offtok || save == NULL) { goto err; }

	/* 块偏移必须与已接收长度一致（允许 host 重发同一块） */
	if ((uint32_t)off != dfu.received) {
		/* 旧块重发或乱序：不破坏状态，要求重发当前块 */
		snprintk(buf, sizeof(buf), "ACK %X\r\n", (unsigned)dfu.received);
		uartTxStr(buf);
		return;
	}

	n = hexParse(save, dfu.block, DFU_BLOCK_MAX);
	if (n <= 0) { goto err; }

	/* 块长必须恰好等于期望长度（末块除外恒为 DFU_BLOCK_MAX）。
	 * UART 行偶发丢字节会得到错长 hex，若照写会把 received 破坏成
	 * 非 512 对齐 → 后续 flash 写永远 offset-not-aligned。
	 * 因此错长块整行拒收（不推进、不清状态），host 重发同块。 */
	uint32_t expected = dfu.total - dfu.received;
	if (expected > DFU_BLOCK_MAX) { expected = DFU_BLOCK_MAX; }
	if ((uint32_t)n != expected) {
		snprintk(buf, sizeof(buf), "ERR len %X need %X\r\n",
			 (unsigned)n, (unsigned)expected);
		uartTxStr(buf);
		return;
	}

	/* MCUboot SWAP_USING_OFFSET 要求镜像放在 slot1 的第二个扇区起
	 * （第一个扇区是 swap 移动缓冲）。host 上传偏移 off 从 0 计，
	 * 写 flash 时统一加 DFU_SECONDARY_IMG_OFFSET。 */
	int rc = flash_area_write(dfu.fa, (off_t)(DFU_SECONDARY_IMG_OFFSET + dfu.received),
				  dfu.block, (uint32_t)n);
	if (rc != 0) {
		/* 写失败：不清状态，host 会重发本块重试 */
		uartTxStr("ERR dfu write\r\n");
		return;
	}
	dfu.received += (uint32_t)n;

	if (dfu.received >= dfu.total) {
		/* 全部写完 → 读回校验 → 请求升级 → 复位 */
		uint32_t magic;
		uint32_t dbg_off = DFU_SECONDARY_IMG_OFFSET;

		if (flash_area_read(dfu.fa, (off_t)dbg_off, &magic, sizeof(magic)) == 0) {
			snprintk(buf, sizeof(buf), "VERIFY slot1+0x%X = 0x%08X\r\n",
				 (unsigned)dbg_off, (unsigned)magic);
			uartTxStr(buf);
		}
		snprintk(buf, sizeof(buf), "DFU done %u/%u, rebooting...\r\n",
			 (unsigned)dfu.received, (unsigned)dfu.total);
		uartTxStr(buf);
		flash_area_close(dfu.fa);
		boot_request_upgrade(0);
		sys_reboot(0);
		return;
	}
	/* 逐块 ACK（带下个期望偏移）：host 收到后才发下一块 */
	snprintk(buf, sizeof(buf), "ACK %X\r\n", (unsigned)dfu.received);
	uartTxStr(buf);
	return;
err:
	uartTxStr("ERR dfu data\r\n");
}

/* 返回 true = 已处理（dfu 专用命令） */
static bool dfuHandle(const char *cmd, const char *args)
{
	if (strcmp(cmd, "dfu") == 0) {
		cmdDfuEnter();
		return true;
	}
	if (!dfu.active) {
		return false;
	}
	if (strcmp(cmd, "size") == 0) {
		dfuSetSize(args);
	} else if (strcmp(cmd, "data") == 0) {
		dfuData(args);
	} else {
		uartTxStr("ERR dfu state\r\n");
	}
	return true;
}

static void uartCmdExecute(char *cmdline)
{
	char *save = NULL;
	char *cmd = strtok_r(cmdline, " \t", &save);
	char *args;

	if (cmd == NULL) {
		return;   /* 空行 */
	}
	args = save;   /* 剩余参数（可能为 NULL） */

	/* DFU 升级命令优先处理（含 dfu/size/data 状态机） */
	if (dfuHandle(cmd, args)) {
		return;
	}

	if (strcmp(cmd, "help") == 0) {
		cmdHelp();
	} else if (strcmp(cmd, "?") == 0) {
		cmdHelp();
	} else if (strcmp(cmd, "info") == 0) {
		cmdInfo();
	} else if (strcmp(cmd, "dout") == 0) {
		cmdDout(args);
	} else if (strcmp(cmd, "doutall") == 0) {
		cmdDoutAll(args);
	} else if (strcmp(cmd, "dac") == 0) {
		cmdDac(args);
	} else if (strcmp(cmd, "dacwv") == 0) {
		cmdDacWave(args);
	} else if (strcmp(cmd, "pwm") == 0) {
		cmdPwm(args);
	} else if (strcmp(cmd, "pwmoff") == 0) {
		cmdPwmOff(args);
	} else if (strcmp(cmd, "getdout") == 0) {
		cmdGetDout();
	} else if (strcmp(cmd, "getdac") == 0) {
		cmdGetDac();
	} else if (strcmp(cmd, "getpwm") == 0) {
		cmdGetPwm();
	} else if (strcmp(cmd, "getdin") == 0) {
		cmdGetDin();
	} else {
		uartTxStr("ERR unknown command (help for list)\r\n");
	}
}

/* 从环形缓冲取一字节；无数据返回 -1。 */
static int uartRxGetByte(void)
{
	if (rx_tail == rx_head) {
		return -1;
	}
	uint8_t c = rx_buf[rx_tail];
	rx_tail = (uint16_t)((rx_tail + 1U) % RX_BUF_SIZE);
	return (int)c;
}

void uartCmdPoll(void)
{
	uartCmdInit();
	if (!uart_ready) {
		return;
	}

	/* 逐字节组行：\n 或 \r 结束（\r\n 视作同一行，\r 丢弃） */
	for (;;) {
		int c = uartRxGetByte();
		if (c < 0) {
			break;
		}
		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			if (line_len > 0) {
				line[line_len] = '\0';
				uartCmdExecute(line);
			}
			line_len = 0;
			continue;
		}
		if (line_len < CMD_LINE_MAX - 1) {
			line[line_len++] = (char)c;
		}
		/* 超长：继续吞字节到行尾（丢弃该行） */
	}
}
