/*
 * uart_cmd.c - host command service (USART1 @ PB14 TX / PB15 RX, 115200, 8N1)
 *
 * Line protocol with a host (PC / serial terminal):
 *   - the UART ISR fills a ring buffer
 *   - the cmd thread calls uartCmdPoll() every 10 ms: take one \r\n-terminated
 *     line, parse and execute it
 *   - responses go back over the same UART
 *   - NOTE: USART1 is also the Zephyr console (printk).
 *
 * Commands (one per line):
 *   help                    - help
 *   info                    - system status (state machine/faults/temp/voltage)
 *   dout <idx> <0|1>        - drive a DOUT bit (idx 0..doutMax-1)
 *   doutall <hex64>         - write the whole 64-bit DOUT bitmap
 *   dac <mv>                - constant DAC output (0..3300 mV)
 *   pwr_on_off <0|1>        - indicator mode (1 = breathing, 0 = off)
 *   pwm <ch> <duty>         - fan PWM duty (ch 0/1, duty 0..100)
 *   pwmoff <ch>             - stop PWM
 *
 * These commands write BSP outputs directly and may be overwritten by the
 * state machine: they are for debug / production test.
 */

/* Standard library */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

/* BSP */
#include "bsp_ain.h"
#include "bsp_dio.h"
#include "bsp_aout.h"
#include "bsp_pwm.h"
#include "bsp_wtdg.h"

/* Application */
#include "uart_cmd.h"
#include "state_machine.h"
#include "sensor.h"
#include "ac_meter.h"
#include "tmp75.h"
#include "indicator.h"
#include "max6703a.h"

/* ==================== Constants ==================== */

#define UART_CMD_DEV   DEVICE_DT_GET(DT_NODELABEL(usart1))   /* PB14(TX)/PB15(RX) */

#define RX_BUF_SIZE    8192   /* ring buffer (bytes): holds several DFU lines */
#define CMD_LINE_MAX   1100   /* max line length (dfu block <= 512 B hex) */
#define TX_CHUNK_MAX   128    /* chunk size for multi-line info output */

/* ==================== RX ring buffer ====================
 * Single producer (UART ISR) / single consumer (cmd thread): head written by
 * the ISR, tail read by the thread; a full buffer drops new bytes. */

static uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

static char     line[CMD_LINE_MAX];
static uint16_t line_len;
static bool     uart_ready;
static bool     inited;

/* ==================== UART TX ==================== */

static void uartTxStr(const char *s)
{
	if (!uart_ready) {
		return;
	}
	while (*s != '\0') {
		uart_poll_out(UART_CMD_DEV, (unsigned char)*s++);
	}
}

/* ==================== UART RX ISR ==================== */

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
		/* buffer full: drop the new byte */
	}
}

/* ==================== Init (lazy, idempotent) ==================== */

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

	/* Send the version banner over the command channel. */
	uartTxStr("\r\n===== CiosZhong PSU =====\r\n");
	uartTxStr("  App  v" CONFIG_CIOS_ZHONG_FW_VERSION "\r\n");
	uartTxStr("  Boot v" CONFIG_CIOS_ZHONG_BOOT_VERSION " (MCUboot)\r\n");
	uartTxStr("PSU CMD: ready (help for commands)\r\n");
}

/* ==================== Command execution ==================== */

static void cmdHelp(void)
{
	uartTxStr("commands:\r\n"
		  "  help                    - this help\r\n"
		  "  info                    - system status\r\n"
		  "  dout <idx> <0|1>        - set DOUT output (0..doutMax-1)\r\n"
		  "  doutall <hex64>         - write 64-bit DOUT bitmap\r\n"
		  "  dac <mv>                - DAC constant voltage (0..3300 mV)\r\n"
		  "  dacwv <0|1>             - PA5 status-LED breath on/off\r\n"
		  "  pwm <ch> <duty>         - fan PWM duty (ch 0/1, 0..100%)\r\n"
		  "  pwmoff <ch>             - stop PWM\r\n"
		  "  getdout                 - read DOUT bitmap\r\n"
		  "  getdac                  - read DAC mv + wave state\r\n"
		  "  getpwm                  - read PWM duties\r\n"
		  "  getdin                  - read DIN bitmap\r\n"
		  "  ain [raw]               - read all ADC channels (all by default)\r\n"
		  "  temp                    - read TMP75 temperature (I2C1 0x48)\r\n"
		  "  i2cscan                 - scan I2C1 bus for device addresses\r\n"
		  "  i2cread <a> [reg] [len] - read I2C1 slave (hex addr/reg/len)\r\n");
}

static void cmdInfo(void)
{
	char buf[TX_CHUNK_MAX];

	snprintk(buf, sizeof(buf), "fw=%s boot=%s\r\n",
		 CONFIG_CIOS_ZHONG_FW_VERSION, CONFIG_CIOS_ZHONG_BOOT_VERSION);
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
	/* Clear all, then set the bits of the requested bitmap */
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
	/* Constant output: switch to MANUAL so indicatorUpdate() leaves it alone */
	indicatorSetMode(INDICATOR_MANUAL);
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
	/* pwr_on_off (PA5): 1 = breathing, 0 = off */
	indicatorSetMode(on ? INDICATOR_BREATH : INDICATOR_OFF);
	uartTxStr(on ? "OK breath on\r\n" : "OK breath off\r\n");
	return;
err:
	uartTxStr("ERR usage: dacwv <0|1> (PA5 status LED breath on/off)\r\n");
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

	if (ch < 0 || ch >= (long)bspPwmGetCount() || duty < 0 || duty > 100) {
		goto err;
	}
	bspPwmSetDutyCycle((uint8_t)ch, (uint32_t)duty);
	bspPwmStart((uint8_t)ch);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: pwm <ch> <duty 0..100>\r\n");
}

static void cmdPwmOff(const char *args)
{
	char *end = NULL;
	long ch;

	if (args == NULL) {
		goto err;
	}
	ch = strtol(args, &end, 0);
	if (end == args || ch < 0 || ch >= (long)bspPwmGetCount()) {
		goto err;
	}
	bspPwmStop((uint8_t)ch);
	uartTxStr("OK\r\n");
	return;
err:
	uartTxStr("ERR usage: pwmoff <ch>\r\n");
}

/* ---- Status query (for the host UI) ---- */

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

	snprintk(buf, sizeof(buf), "dac=%d ind=%d\r\n",
		 (int)bspAoutGetMv(AOUT_PWR_ON_OFF),
		 (int)indicatorGetMode());
	uartTxStr(buf);
}

static void cmdGetPwm(void)
{
	char buf[96];
	size_t off = 0;
	const uint8_t count = bspPwmGetCount();

	for (uint8_t i = 0; i < count; i++) {
		if (off >= sizeof(buf) - 16U) {
			break;
		}
		const int w = snprintk(buf + off, sizeof(buf) - off, "%spwm%u=%u",
				       (i == 0U) ? "" : " ", (unsigned)i,
				       (unsigned)bspPwmGetDutyCycle(i));
		if (w < 0) {
			break;
		}
		off += (size_t)w;
	}
	if (off > sizeof(buf) - 2U) {
		off = sizeof(buf) - 2U;
	}
	snprintk(buf + off, sizeof(buf) - off, "\r\n");
	uartTxStr(buf);
}

static void cmdGetDin(void)
{
	char buf[48];

	snprintk(buf, sizeof(buf), "din=0x%08X\r\n", (unsigned)bspDinGetBitmap());
	uartTxStr(buf);
}

/* ---- temp: read the TMP75 (I2C1, 0x48) ---- */
static void cmdTemp(void)
{
	char buf[64];
	int32_t milliDegC = 0;
	int rc;

	if (!tmp75IsReady()) {
		uartTxStr("ERR tmp75 not ready\r\n");
		return;
	}

	rc = tmp75Read(&milliDegC);
	if (rc != 0) {
		snprintk(buf, sizeof(buf), "ERR tmp75 read rc=%d (errs=%u)\r\n",
			 rc, (unsigned)tmp75ErrorCount());
		uartTxStr(buf);
		return;
	}

	/* Split into integer and fraction, handling the sign manually */
	int32_t whole = milliDegC / 1000;
	int32_t frac  = milliDegC % 1000;
	if (frac < 0) {
		frac = -frac;
	}
	snprintk(buf, sizeof(buf), "temp=%d.%03d C (raw_ok, errs=%u)\r\n",
		 (int)whole, (int)frac, (unsigned)tmp75ErrorCount());
	uartTxStr(buf);
}

/* ---- ain: read all ADC channels ----
 * Without arguments prints the physical voltage of every channel; the optional
 * "raw" argument also prints the raw ADC codes. */
static void cmdAin(const char *args)
{
	char buf[96];
	char rawbuf[24];
	const bool with_raw = (args != NULL) && (strstr(args, "raw") != NULL);

	uartTxStr("--- AIN (all channels) ---\r\n");

	for (uint8_t i = 0; i < BSP_AIN_NUMBER; i++) {
		const uint32_t raw = bspAinGetRawValue(i);
		const char *rawtxt;

		if (with_raw) {
			snprintk(rawbuf, sizeof(rawbuf), "  raw=%u", (unsigned)raw);
			rawtxt = rawbuf;
		} else {
			rawtxt = "";
		}

		if (i == AIN_ADC_TEMP1 || i == AIN_ADC_TEMP2) {
			/* temperature channel: cache holds temp x10, not a voltage */
			int16_t t = (int16_t)sensorGetPhys(i);
			int16_t frac = t % 10;
			if (frac < 0) {
				frac = -frac;
			}
			snprintk(buf, sizeof(buf), "AIN[%2u] %-14s %4d.%d C%s\r\n",
				 (unsigned)i, bspAinGetName(i), t / 10, frac, rawtxt);
		} else if (i == AIN_ADC_VIN) {
			/* mains AC channel: RMS is computed by ac_meter */
			if (acMeterAcPresent()) {
				uint32_t rms = acMeterGetVinRmsMv();
				snprintk(buf, sizeof(buf),
					 "AIN[%2u] %-14s %5u.%03u Vrms @ %u.%u Hz%s\r\n",
					 (unsigned)i, bspAinGetName(i),
					 rms / 1000U, rms % 1000U,
					 acMeterGetVinFreq() / 10, acMeterGetVinFreq() % 10,
					 rawtxt);
			} else {
				snprintk(buf, sizeof(buf),
					 "AIN[%2u] %-14s   n/a (no AC)%s\r\n",
					 (unsigned)i, bspAinGetName(i), rawtxt);
			}
		} else {
			const uint32_t mv = sensorGetPhys(i);
			snprintk(buf, sizeof(buf), "AIN[%2u] %-14s %5u.%03u V%s\r\n",
				 (unsigned)i, bspAinGetName(i),
				 mv / 1000U, mv % 1000U, rawtxt);
		}
		uartTxStr(buf);
	}
}

/* ---- i2cscan: probe all slave addresses on I2C1 ---- */
static void cmdI2cScan(void)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay)
	const struct device *bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	char buf[64];
	uint8_t dummy = 0;
	int found = 0;

	if (!device_is_ready(bus)) {
		uartTxStr("ERR i2c1 not ready\r\n");
		return;
	}

	uartTxStr("scan i2c1 (7-bit addr 0x08..0x77):\r\n");

	for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
		/* Zero-length write = START + address + STOP (standard quick command),
		 * so no data byte is ever sent. */
		if (i2c_write(bus, &dummy, 0, (uint8_t)addr) == 0) {
			snprintk(buf, sizeof(buf), "  ACK  0x%02X\r\n", addr);
			uartTxStr(buf);
			found++;
		}
	}

	snprintk(buf, sizeof(buf), "total %d device(s)\r\n", found);
	uartTxStr(buf);
#else
	uartTxStr("ERR i2c1 disabled\r\n");
#endif
}

/* ---- i2cread <addr> [reg] [len]: read I2C1 slave registers ----
 *   i2cread 0x52            -> plain 2-byte read (command-style device)
 *   i2cread 0x52 0x00 4     -> read 4 bytes from register 0x00
 * All arguments are hex; len defaults to 2, max 32. */
static void cmdI2cRead(const char *args)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay)
	const struct device *bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	char *save = NULL, *tok;
	char line[160];
	uint8_t buf[32];
	long addr, reg = -1, len = 2;
	int rc, n = 0;

	if (args == NULL) {
		uartTxStr("ERR usage: i2cread <addr> [reg] [len]\r\n");
		return;
	}

	tok = strtok_r((char *)args, " \t", &save);
	if (tok == NULL) {
		uartTxStr("ERR usage: i2cread <addr> [reg] [len]\r\n");
		return;
	}
	addr = strtol(tok, NULL, 0);

	tok = strtok_r(NULL, " \t", &save);
	if (tok != NULL) {
		reg = strtol(tok, NULL, 0);
		tok = strtok_r(NULL, " \t", &save);
		if (tok != NULL) {
			len = strtol(tok, NULL, 0);
		}
	}
	if (addr < 0x08 || addr > 0x77 || len < 1 || len > (long)sizeof(buf)) {
		uartTxStr("ERR addr 0x08..0x77, len 1..32\r\n");
		return;
	}

	if (!device_is_ready(bus)) {
		uartTxStr("ERR i2c1 not ready\r\n");
		return;
	}

	if (reg >= 0) {
		rc = i2c_burst_read(bus, (uint8_t)addr, (uint8_t)reg,
				    buf, (uint32_t)len);
	} else {
		rc = i2c_read(bus, buf, (uint32_t)len, (uint8_t)addr);
	}

	if (rc != 0) {
		snprintk(line, sizeof(line), "ERR read 0x%02lX rc=%d\r\n", addr, rc);
		uartTxStr(line);
		return;
	}

	n = snprintk(line, sizeof(line), "0x%02lX", addr);
	if (reg >= 0) {
		n += snprintk(line + n, sizeof(line) - n, "[0x%02lX]", reg);
	}
	n += snprintk(line + n, sizeof(line) - n, ":");
	for (long i = 0; i < len; i++) {
		n += snprintk(line + n, sizeof(line) - n, " %02X", buf[i]);
	}
	snprintk(line + n, sizeof(line) - n, "\r\n");
	uartTxStr(line);
#else
	uartTxStr("ERR i2c1 disabled\r\n");
#endif
}

/* ==================== Line parsing and dispatch ==================== */

/* ==================== DFU (serial upgrade -> slot1 + MCUboot) ==========
 * Protocol (line based, hex, block ACK + offset check):
 *   dfu                enter upgrade mode (erase slot1)
 *   size <hex>         total firmware size in bytes
 *   data <off> <hex..> data block: off = offset of this block (hex),
 *                      hex = block payload (<= 512 B per line)
 *   The host waits for "ACK <next_off>" per block and retries on ERR/timeout.
 *   When done: read-back verify -> boot_request_upgrade() -> reset.
 *
 * Note: the layout comment below describes an older SWAP_USING_OFFSET scheme;
 * The build uses MCUboot OVERWRITE_ONLY: the image is written from the start of
 * slot1 (0x0) and MCUboot copies it over slot0. DFU_SECONDARY_IMG_OFFSET = 0.
 */
#define DFU_BLOCK_MAX            512   /* bytes per data line */
#define DFU_HEX_CHARS            (DFU_BLOCK_MAX * 2)
/* OVERWRITE_ONLY: MCUboot reads the image header at the start of slot1 (0x0)
 * and copies it over slot0, so the host writes from offset 0. */
#define DFU_SECONDARY_IMG_OFFSET 0x0U

/* `dfu` erases the whole staging slot before uploading. These are the two values
 * the bootloader and the DFU host tool both assume. A build whose devicetree or
 * flash map disagrees with them must never be able to erase a different region -
 * in the worst case the bootloader at 0x08000000, which bricks the board and
 * makes it unreachable over serial. So the slot is pinned both at build time and
 * at run time, independently of what the flash map resolves to. */
#define DFU_SLOT1_OFFSET  0x100000U   /* slot1 @ 0x08100000 */
#define DFU_SLOT1_SIZE    0x80000U    /* 512 KB */

BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(slot1_partition)) == DFU_SLOT1_OFFSET,
	     "slot1_partition is not at 0x100000 in this devicetree");
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(slot1_partition)) == DFU_SLOT1_SIZE,
	     "slot1_partition is not 512 KB in this devicetree");
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(slot1_partition)) >=
	     DT_REG_SIZE(DT_NODELABEL(boot_partition)),
	     "slot1_partition overlaps the MCUboot boot partition");

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

/* Parse a hex string into buf; return the byte count, -1 on bad input */
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

/* Erase slot1 in chunks, feeding both watchdogs in between.
 *
 * A single flash_area_erase() of the whole 512 KB slot can outlive the feeders
 * (internal WWDG ~250 ms fed every 50 ms, external MAX6703A 1.6 s fed every
 * 500 ms) if those threads cannot get scheduled while the erase runs. An
 * interrupted erase is the dangerous case: the sector ends up blank while the
 * image was never written, which is how a board loses its firmware.
 * One H7 flash sector per chunk keeps each erase far below both timeouts. */
#define DFU_ERASE_CHUNK (128U * 1024U)

static void dfuFeedWatchdogs(void)
{
	bspWtdgFeed();     /* internal WWDG */
	max6703aFeed();    /* external MAX6703A (WDI) */
}

static int dfuEraseSlot1(void)
{
	for (uint32_t off = 0; off < dfu.fa->fa_size; off += DFU_ERASE_CHUNK) {
		const uint32_t len = MIN(DFU_ERASE_CHUNK, dfu.fa->fa_size - off);
		const int rc = flash_area_erase(dfu.fa, (off_t)off, len);

		if (rc != 0) {
			return rc;
		}
		dfuFeedWatchdogs();
	}
	return 0;
}

static void cmdDfuEnter(void)
{
	char buf[96];
	int rc;

	dfuReset();
	rc = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &dfu.fa);
	if (rc != 0) {
		uartTxStr("ERR dfu: open slot1 fail\r\n");
		return;
	}
	/* Hard boundary, deliberately independent of the devicetree: `dfu` runs on the
	 * board, so no build configuration may be able to make it erase the region
	 * that holds the bootloader or the running image. */
	if (dfu.fa->fa_off != (off_t)DFU_SLOT1_OFFSET ||
	    dfu.fa->fa_size != (size_t)DFU_SLOT1_SIZE) {
		snprintk(buf, sizeof(buf),
			 "ERR dfu: slot1 map 0x%lX/%luK != expected 0x%lX/%luK, erase refused\r\n",
			 (unsigned long)dfu.fa->fa_off,
			 (unsigned long)(dfu.fa->fa_size / 1024U),
			 (unsigned long)DFU_SLOT1_OFFSET,
			 (unsigned long)(DFU_SLOT1_SIZE / 1024U));
		uartTxStr(buf);
		flash_area_close(dfu.fa);
		dfuReset();
		return;
	}
	rc = dfuEraseSlot1();
	if (rc != 0) {
		uartTxStr("ERR dfu: erase slot1 fail\r\n");
		flash_area_close(dfu.fa);
		dfuReset();
		return;
	}
	dfu.active = true;
	/* Report the area actually erased so a wrong layout is visible in the host log */
	snprintk(buf, sizeof(buf),
		 "DFU ok, slot1 @0x%lX size %luK erased, send: size <hex>\r\n",
		 (unsigned long)dfu.fa->fa_off,
		 (unsigned long)(dfu.fa->fa_size / 1024U));
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

	/* The block offset must match the received length (allows a retry) */
	if ((uint32_t)off != dfu.received) {
		/* stale/out-of-order block: keep state, ask for the current block */
		snprintk(buf, sizeof(buf), "ACK %X\r\n", (unsigned)dfu.received);
		uartTxStr(buf);
		return;
	}

	n = hexParse(save, dfu.block, DFU_BLOCK_MAX);
	if (n <= 0) { goto err; }

	/* The block length must match exactly (always DFU_BLOCK_MAX except the last).
	 * A short line would break the 512-byte alignment, so a bad length is
	 * rejected as a whole and the host retries. */
	uint32_t expected = dfu.total - dfu.received;
	if (expected > DFU_BLOCK_MAX) { expected = DFU_BLOCK_MAX; }
	if ((uint32_t)n != expected) {
		snprintk(buf, sizeof(buf), "ERR len %X need %X\r\n",
			 (unsigned)n, (unsigned)expected);
		uartTxStr(buf);
		return;
	}

	/* Flash offset = host offset + DFU_SECONDARY_IMG_OFFSET. */
	int rc = flash_area_write(dfu.fa, (off_t)(DFU_SECONDARY_IMG_OFFSET + dfu.received),
				  dfu.block, (uint32_t)n);
	if (rc != 0) {
		/* write failed: keep state, the host will resend this block */
		uartTxStr("ERR dfu write\r\n");
		return;
	}
	dfu.received += (uint32_t)n;
	dfuFeedWatchdogs();   /* a slow flash write must not trip a watchdog timer */

	if (dfu.received >= dfu.total) {
		/* done -> verify -> request upgrade -> reset */
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
	/* Per-block ACK carrying the next expected offset */
	snprintk(buf, sizeof(buf), "ACK %X\r\n", (unsigned)dfu.received);
	uartTxStr(buf);
	return;
err:
	uartTxStr("ERR dfu data\r\n");
}

/* true = handled (dfu-specific command) */
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
		return;   /* empty line */
	}
	args = save;   /* remaining args (may be NULL) */

	/* DFU commands are handled first */
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
	} else if (strcmp(cmd, "temp") == 0) {
		cmdTemp();
	} else if (strcmp(cmd, "ain") == 0) {
		cmdAin(args);
	} else if (strcmp(cmd, "i2cscan") == 0) {
		cmdI2cScan();
	} else if (strcmp(cmd, "i2cread") == 0) {
		cmdI2cRead(args);
	} else {
		uartTxStr("ERR unknown command (help for list)\r\n");
	}
}

/* Take one byte from the ring buffer; -1 if empty. */
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

	/* Assemble a line: \n or \r ends it (\r\n counts as one) */
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
		/* over-long line: swallow bytes until the end and drop it */
	}
}
