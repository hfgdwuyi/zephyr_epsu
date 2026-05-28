# zephyr_epsu

Zephyr RTOS application for the CiosZhong PSU (Power Supply Unit) controller.

## Hardware

- **Board**: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7 core)
- **Flash**: 1MB M7 flash @ 0x08000000
- **RAM**: 512KB SRAM3 (M7 main RAM)
- **Debugger**: STLINK V3 (OpenOCD)

## Project Structure

```
ciosZhong_ePSU/
├── boot/                    # MCUboot 共享配置
│   ├── keys/                # 签名密钥（app_epsu + app_test 共用）
│   └── ...
├── app_epsu/                # 主应用工程 (原 src/application/)
│   ├── prj.conf
│   ├── app.overlay
│   └── sysbuild/
├── app_test/                # OTA 测试工程 (原 test_minimal/)
│   ├── prj.conf
│   ├── app.overlay
│   └── sysbuild/
├── lib/                     # 共享库 (bsp, common, http 等)
├── build_minimal/           # app_test 构建产物
└── scripts/
```

- **boot/** — MCUboot 分区表和签名密钥，所有 app 共用
- **app_epsu/** — 主 PSU 控制器固件
- **app_test/** — OTA 双槽升级验证固件(Version 0 ↔ Version 1)
- **lib/** — 板级支持、通用模块、HTTP 服务等共享代码

## Flash Partition Layout

```
&flash0 (2MB @ 0x08000000, dual-bank):
  boot_partition:   128KB  (0x000000 - 0x020000)    MCUboot
  slot0_partition:  512KB  (0x020000 - 0x0A0000)    Primary image slot
  slot1_partition:  512KB  (0x0A0000 - 0x120000)    Secondary image slot
```

All partitions aligned to 128KB flash sectors. SWAP_USING_OFFSET mode.

## Build

### Prerequisites (macOS)

```bash
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1
source $ZEPHYR_BASE/zephyr-env.sh
```

Zephyr SDK 1.0.1 or later required (arm-zephyr-eabi-gcc 14.3.0).

### Build app_test (sysbuild)

```bash
west build \
  -d ~/project/02_zephyr/ciosZhong_ePSU/build_minimal \
  ~/project/02_zephyr/ciosZhong_ePSU/app_test \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Build app_epsu (主应用)

```bash
west build \
  -d ~/project/02_zephyr/ciosZhong_ePSU/build_epsu \
  ~/project/02_zephyr/ciosZhong_ePSU/app_epsu \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Sign the app image

```bash
west sign -d ~/project/02_zephyr/ciosZhong_ePSU/build_minimal -t imgtool -- \
  --key ~/project/02_zephyr/ciosZhong_ePSU/boot/keys/imgtool_key.pem
```

## Flash

### STLINK-V3 (macOS / Linux)

```bash
# Flash MCUboot
west flash -d ~/project/02_zephyr/ciosZhong_ePSU/build_minimal/mcuboot -r openocd

# Flash signed app
west flash -d ~/project/02_zephyr/ciosZhong_ePSU/build_minimal/app_test -r openocd
```

`west flash -r openocd` auto-detects the scripts path.

### STLINK-V3 (Windows / WSL)

```powershell
openocd -s "C:\Program Files\OpenOCD\share\openocd\scripts" ^
  -f board/st_nucleo_h745zi.cfg ^
  -c "init" -c "targets" -c "halt" ^
  -c "program build_minimal\mcuboot\zephyr\zephyr.hex verify" ^
  -c "program build_minimal\app_test\zephyr\zephyr.signed.hex verify" ^
  -c "reset run" -c "shutdown"
```

## Serial Console

115200 baud, 8N1. Expected output:

```
*** Booting Zephyr OS build v4.4.0-... ***
I: Starting bootloader
I: Image index: 0, Swap type: none
I: Bootloader chainload address offset: 0x20000
I: Jumping to the first image slot
*** Booting Zephyr OS build v4.4.0-... ***
===== TestApp Version 0 (OTA) =====
Image confirmed, running...
HTTP server listening on port 80
V0 Alive: 1
...
```

- First "Booting Zephyr OS" → MCUboot
- "Jumping to the first image slot" → MCUboot passes control to slot0
- Second "Booting Zephyr OS" → App kernel init
- "=== MINIMAL: started ===" → App main()

## Known Issue: DTS ranges on STM32H745

STM32H745 `flash0` node has `ranges = <0 0x8000000 0x100000>` which causes
`DT_REG_ADDR` on child partitions to return pre-translated absolute addresses.
`DT_FIXED_PARTITION_ADDR` then adds the flash base address again, resulting in
double-counting. Without `/delete-property/ ranges;` in `mcuboot.overlay`,
MCUboot's `flash_device_base()` computes 0x10000000 instead of 0x08000000
and hangs when trying to jump to the app.

Fix: `app_test/sysbuild/mcuboot.overlay` and
`app_epsu/sysbuild/mcuboot.overlay` both contain
`/delete-property/ ranges;` on `&flash0`.
