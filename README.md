# zephyr_epsu

Zephyr RTOS application for the CiosZhong PSU (Power Supply Unit) controller.

## Hardware

- **Board**: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7 core)
- **Flash**: 2MB dual-bank @ 0x08000000
- **RAM**: 512KB SRAM3 (M7 main RAM)
- **Debugger**: STLINK V3 (OpenOCD)

## Project Structure

```
ciosZhong_ePSU/
├── bootloader/              # MCUboot 签名密钥
│   └── keys/
├── application/             # 主应用工程
│   ├── main/main.c
│   ├── prj.conf
│   ├── app.overlay
│   └── sysbuild/
├── lib/                     # 共享库
│   ├── bsp/                 # 板级支持 (LED, DIO, AIN, AOUT, PWM, WTDG)
│   ├── common/dm/           # 数据模型管理 (DM core, update worker)
│   └── http/                # HTTP REST 框架 (echo, heartbeat, control, update, diag)
├── build_epsu/              # 构建产物
└── scripts/
```

- **bootloader/keys/** — MCUboot ECDSA-P256 签名密钥
- **application/** — 主 PSU 控制器固件
- **lib/** — 板级支持、通用模块、HTTP 服务等共享代码

## Flash Partition Layout

```
&flash0 (2MB @ 0x08000000, dual-bank):
  boot_partition:   128KB  (0x000000 - 0x020000)    MCUboot
  slot0_partition:  512KB  (0x020000 - 0x0A0000)    Primary image slot
  slot1_partition:  512KB  (0x0A0000 - 0x120000)    Secondary image slot
```

All partitions aligned to 128KB flash sectors. SWAP_USING_OFFSET mode.

## Build & Flash

### Prerequisites (macOS)

```bash
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1
source $ZEPHYR_BASE/zephyr-env.sh
```

Zephyr SDK 1.0.1 or later required (arm-zephyr-eabi-gcc 14.3.0).

### Build (sysbuild with MCUboot)

```bash
cd ~/project/02_zephyr/zephyrproject
west build \
  -d ~/project/02_zephyr/ciosZhong_ePSU/build_epsu \
  ~/project/02_zephyr/ciosZhong_ePSU/application \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Flash

```bash
# Flash MCUboot
west flash -d build_epsu/mcuboot -r openocd

# Flash signed app
west flash -d build_epsu/application -r openocd
```

## OTA 固件更新流程 (已验证)

### 步骤 1: 构建 Version 0 并烧录基线

```bash
# 确保 main.c 版本字符串为 "Version 0"
cd ~/project/02_zephyr/zephyrproject

# 构建并烧录
west build -d ~/project/02_zephyr/ciosZhong_ePSU/build_epsu \
  ~/project/02_zephyr/ciosZhong_ePSU/application \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
west flash -d build_epsu/mcuboot -r openocd
west flash -d build_epsu/application -r openocd

# 确认串口输出: "===== EPSU Version 0 (OTA) ====="
```

### 步骤 2: 构建 Version 1 目标镜像

```bash
# 修改 main.c: Version 0 → Version 1
# 重新构建（生成 zephyr.signed.bin）
west build -d ~/project/02_zephyr/ciosZhong_ePSU/build_epsu \
  ~/project/02_zephyr/ciosZhong_ePSU/application \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### 步骤 3: 启动 HTTP 文件服务器

```bash
cd build_epsu/application/zephyr
python3 -m http.server 8080
```

### 步骤 4: 触发 OTA 更新

```bash
curl -X POST http://192.0.2.1/update/start \
  -H "Content-Type: application/json" \
  -d '{"uri":"http://192.0.2.2:8080/zephyr.signed.bin"}'
# Response: {"ok":true}
```

### 步骤 5: 监控更新状态

```bash
curl http://192.0.2.1/update/status
# Response: {"state":<n>,"progress":<pct>,"error":0,"uri":"..."}
```

### 步骤 6: 验证 Version 1 启动

更新完成后 MCUboot 自动 swap 并重启。串口确认:
```
===== EPSU Version 1 (OTA) =====
Image confirmed, running...
EPSU V1 Alive: 1
```

### 后续迭代 (V1→V2→...)

重复步骤 2-6。每次递增版本号，流程完全相同。已验证 V0→V1→V2 连续 OTA。

## OTA 状态机

| State | Value | Description |
|-------|-------|-------------|
| IDLE | 0 | 无待处理更新 |
| REQUESTED | 1 | 已接收 URI，开始下载 |
| DOWNLOADING | 2 | 正在下载固件镜像 |
| VERIFYING | 3 | 校验下载完整性 |
| APPLYING | 4 | 写入 flash |
| REBOOT_PENDING | 5 | 更新完成，即将重启 |
| FAILED | 6 | 更新失败 |

## 预期串口输出

### MCUboot 启动

```
*** Booting Zephyr OS build v4.4.0-... ***
I: Starting bootloader
I: Image index: 0, Swap type: none
I: Bootloader chainload address offset: 0x20000
I: Jumping to the first image slot
```

### App 启动

```
*** Booting Zephyr OS build v4.4.0-... ***
dm_core: ctrl_worker started
update_worker: started
===== EPSU Version 0 (OTA) =====
Image confirmed, running...
Initializing network...
Network ready
HTTP server listening on port 80
EPSU V0 Alive: 1
```

### OTA 更新过程中

```
http_update: parsed uri='http://192.0.2.2:8080/zephyr.signed.bin'
update_worker: downloading from http://192.0.2.2:8080/zephyr.signed.bin
update_worker: download 50%
...
update_worker: download 100%
update_worker: downloaded 173088 bytes
update_worker: image verified v0.0.0 size=171904
update_worker: rebooting...
```

### MCUboot Swap

```
I: Image index: 0, Swap type: test
I: Starting swap using offset algorithm.
I: Bootloader chainload address offset: 0x20000
```

## HTTP REST API

| Method | Path | Description |
|--------|------|-------------|
| POST | `/update/start` | 提交 OTA 更新 URI |
| GET | `/update/status` | 查询更新状态/进度 |
| GET | `/api/v1/status/heartbeat` | 心跳检测 |
| GET | `/api/v1/status/uptime` | 系统运行时间 |
| POST | `/api/v1/control` | LED/继电器/蜂鸣器控制 |
| GET | `/api/v1/diag/status` | 诊断状态查询 |

## 已知问题

### DTS ranges on STM32H745

STM32H745 `flash0` 节点有 `ranges = <0 0x8000000 0x100000>`，导致子分区的
`DT_REG_ADDR` 返回预翻译的绝对地址。`DT_FIXED_PARTITION_ADDR` 会再次添加 flash
基地址，造成双倍计数。不使用 `/delete-property/ ranges;` 时 MCUboot 的
`flash_device_base()` 计算出 0x10000000 而非 0x08000000，导致跳转到 app 时挂起。

修复: `application/app.overlay` 和 `application/sysbuild/mcuboot.overlay`
都在 `&flash0` 上包含 `/delete-property/ ranges;`。
