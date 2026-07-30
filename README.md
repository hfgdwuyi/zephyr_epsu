# zephyr_epsu

Zephyr RTOS application for the CiosZhong PSU (Power Supply Unit) controller.

## Hardware

- **Board**: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7 core)
- **Flash**: 2MB dual-bank @ 0x08000000
- **RAM**: 512KB SRAM3 (M7 main RAM)
- **Debugger**: STLINK V3 (OpenOCD)

## Architecture

Dual-image OTA architecture with MCUboot:

```
Flash Layout (2MB @ 0x08000000):

  boot_partition:    128KB   (0x000000 - 0x020000)   MCUboot bootloader
  slot0_partition:   384KB   (0x020000 - 0x080000)   Primary image (currently running)
  slot1_partition:   384KB   (0x080000 - 0x0E0000)   Secondary image (OTA target)
  storage_partition: 128KB   (0x0E0000 - 0x100000)   Persistent storage

SWAP_USING_OFFSET mode: first 128KB sector of slot1 is swap scratch area.
OTA images are written at slot1 offset 0x20000, avoiding the scratch region.
```

Two application images can coexist and swap via OTA:

| Image | Description | HTTP endpoint to trigger swap |
|-------|-------------|-------------------------------|
| **Application** (Main App) | PSU controller firmware | `POST /api/v1/bootloader` → OTA Bootloader → reboot |
| **Bootloader App** | Dedicated bootloader mode | `POST /api/v1/bootloader/exit` → OTA Main App → reboot |

Both apps share the same `lib/` codebase (HTTP server, BSP, DM, update worker).

## Project Structure

```
ciosZhong_ePSU/
├── bootloader/              # Bootloader App 工程
│   ├── src/main.c
│   ├── keys/                # MCUboot ECDSA-P256 签名密钥
│   ├── prj.conf
│   ├── app.overlay
│   ├── sysbuild.conf
│   └── sysbuild/
├── application/             # Main App 工程
│   ├── main/main.c
│   ├── prj.conf
│   ├── app.overlay
│   ├── sysbuild.conf
│   └── sysbuild/
├── lib/                     # 共享库 (两个 App 共用)
│   ├── bsp/                 # 板级支持 (LED, DIO, AIN, AOUT, PWM, WTDG)
│   ├── common/dm/           # 数据模型管理 (DM core, update worker)
│   └── http/                # HTTP REST 框架 (echo, heartbeat, control, update, bootloader, diag)
├── build/              # Main App 构建产物
├── build_bootloader/        # Bootloader App 构建产物
└── scripts/
```

- **bootloader/** — Bootloader App 固件 + MCUboot 签名密钥
- **application/** — 主 PSU 控制器固件
- **lib/** — 两个 App 共享的板级支持、通用模块、HTTP 服务

## Build & Flash

### Prerequisites (macOS)

```bash
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1
source $ZEPHYR_BASE/zephyr-env.sh
```

Zephyr SDK 1.0.1 or later required (arm-zephyr-eabi-gcc 14.3.0).

### Build Main App (sysbuild with MCUboot)

```bash
cd ~/project/02_zephyr/zephyrproject
west build \
  -d ~/project/02_zephyr/ciosZhong_ePSU/build \
  ~/project/02_zephyr/ciosZhong_ePSU/application \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Build Bootloader App

```bash
west build \
  -d ~/project/02_zephyr/ciosZhong_ePSU/build_bootloader \
  ~/project/02_zephyr/ciosZhong_ePSU/bootloader \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Flash

```bash
# Flash MCUboot
west flash -d build/mcuboot -r openocd

# Flash Main App (signed)
west flash -d build/application -r openocd
```

## OTA 固件更新流程 (已验证)

### Main App OTA 更新 (同镜像升级)

```bash
# 1. 启动 HTTP 文件服务器
cd build/application/zephyr
python3 -m http.server 8080 --bind 192.0.2.2

# 2. 触发 OTA 更新
curl -X POST http://192.0.2.1/api/v1/update/start \
  -H "Content-Type: application/json" \
  -d '{"uri":"http://192.0.2.2:8080/zephyr.signed.bin"}'

# 3. 监控更新进度
curl http://192.0.2.1/api/v1/update/status
# Response: {"state":<n>,"progress":<pct>,"error":0,"uri":"..."}
```

### Bootloader App 切换 (跨镜像 swap)

```bash
# Main App → Bootloader App
curl -X POST http://192.0.2.1/api/v1/bootloader \
  -H "Content-Type: application/json" \
  -d '{"uri":"http://192.0.2.2:8080/bootloader.signed.bin"}'
# → 下载 bootloader 固件到 slot1 → 验证 → 重启 → Bootloader 运行

# Bootloader App → Main App
curl -X POST http://192.0.2.1/api/v1/bootloader/exit \
  -H "Content-Type: application/json" \
  -d '{"uri":"http://192.0.2.2:8080/main.signed.bin"}'
# → 下载 main 固件到 slot1 → 验证 → 重启 → Main App 运行
```

每次 swap 都是 OTA 下载目标镜像到 slot1 (offset 0x20000)，然后 MCUboot 执行 permanent swap。
往返 swap (Application→Bootloader→Application) 已验证通过。

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

### Main App 启动

```
*** Booting Zephyr OS build v4.4.0-... ***
dm_core: ctrl_worker started
update_worker: started
===== Application v0.1.0 =====
Image confirmed, running...
Initializing network...
Network ready
HTTP server listening on port 80
Application v0.1.0 Alive: 1
```

### Bootloader App 启动

```
===== Bootloader v0.1.0 =====
Bootloader mode, running...
Initializing network...
Network ready
Bootloader HTTP server listening on port 80
Bootloader v0.1.0 Alive: 1
```

### OTA 更新过程中

```
update_worker: downloading from http://192.0.2.2:8080/zephyr.signed.bin
update_worker: download 50%
...
update_worker: download 100%
update_worker: downloaded 173984 bytes
update_worker: image verified v0.0.0 size=172796
update_worker: rebooting...
```

### MCUboot Swap (permanent)

```
I: Image index: 0, Swap type: perm
I: Starting swap using offset algorithm.
I: Bootloader chainload address offset: 0x20000
```

## HTTP REST API

| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/v1/update/start` | 提交 OTA 更新 URI (同镜像升级) |
| GET | `/api/v1/update/status` | 查询更新状态/进度 |
| POST | `/api/v1/bootloader` | 切换到 Bootloader App (OTA 下载 + swap) |
| POST | `/api/v1/bootloader/exit` | 切换回 Main App (OTA 下载 + swap) |
| GET | `/api/v1/status/heartbeat` | 心跳检测 |
| GET | `/api/v1/status/uptime` | 系统运行时间 |
| GET | `/api/v1/status/version` | 固件版本信息 |
| POST | `/api/v1/control` | LED/继电器/蜂鸣器控制 |
| GET | `/api/v1/diag` | 诊断状态查询 |

## 已知问题

### DTS ranges on STM32H745

STM32H745 `flash0` 节点有 `ranges = <0 0x8000000 0x100000>`，导致子分区的
`DT_REG_ADDR` 返回预翻译的绝对地址。`DT_FIXED_PARTITION_ADDR` 会再次添加 flash
基地址，造成双倍计数。不使用 `/delete-property/ ranges;` 时 MCUboot 的
`flash_device_base()` 计算出 0x10000000 而非 0x08000000，导致跳转到 app 时挂起。

修复: `application/app.overlay` 和 `application/sysbuild/mcuboot.overlay`
都在 `&flash0` 上包含 `/delete-property/ ranges;`。
