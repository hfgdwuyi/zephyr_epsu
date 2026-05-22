# zephyr_epsu

Zephyr RTOS application for the CiosZhong PSU (Power Supply Unit) controller.

## Hardware

- **Board**: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7 core)
- **Flash**: 1MB M7 flash @ 0x08000000
- **RAM**: 512KB SRAM3 (M7 main RAM)
- **Debugger**: STLINK V3 (OpenOCD)

## Flash Partition Layout (1MB)

```
&flash0 (1MB @ 0x08000000):
  boot_partition:    128KB  (0x000000 - 0x020000)   MCUboot
  slot0_partition:   384KB  (0x020000 - 0x080000)   Primary image slot
  slot1_partition:   384KB  (0x080000 - 0x0E0000)   Update image slot
  storage_partition: 128KB  (0x0E0000 - 0x100000)   MCUboot scratch
```

All partitions aligned to 128KB flash sectors.

## Build

### Prerequisites

```bash
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1
source $ZEPHYR_BASE/zephyr-env.sh
```

Zephyr SDK 1.0.1 or later required (arm-zephyr-eabi-gcc 14.3.0).

### Build MCUboot + test_minimal app (sysbuild)

```bash
west build -d build_test_minimal test_minimal \
  -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

This builds both MCUboot and the test_minimal app in a single sysbuild.

### Sign the app image

```bash
west sign -d build_test_minimal -t imgtool -- \
  --key test_minimal/boot_keys/imgtool_key.pem
```

MCUboot requires all slot images to be signed. The build system signs automatically; this step re-signs if needed.

## Flash

### STLINK-V3 (macOS / Linux)

```bash
# Flash MCUboot
west flash -d build_test_minimal/mcuboot -r openocd

# Flash signed app
west flash -d build_test_minimal/test_minimal -r openocd
```

`west flash -r openocd` auto-detects the scripts path.

### STLINK-V3 (Windows / WSL)

Windows 安装 [OpenOCD](https://github.com/openocd-org/openocd/releases) 后用完整路径：

```powershell
openocd -s "C:\Program Files\OpenOCD\share\openocd\scripts" ^
  -f board/st_nucleo_h745zi.cfg ^
  -c "init" -c "targets" -c "halt" ^
  -c "program build_test_minimal\mcuboot\zephyr\zephyr.hex verify" ^
  -c "program build_test_minimal\test_minimal\zephyr\zephyr.signed.hex verify" ^
  -c "reset run" -c "shutdown"
```

WSL 需用 [usbipd](https://github.com/dorssel/usbipd-win) 将 ST-Link 绑定到 WSL，然后使用 Linux 路径。

## Serial Console

115200 baud, 8N1. Expected output:

```
*** Booting Zephyr OS build v4.4.0-... ***
I: Starting bootloader
I: Image index: 0, Swap type: none
I: Bootloader chainload address offset: 0x20000
I: Jumping to the first image slot
*** Booting Zephyr OS build v4.4.0-... ***
=== MINIMAL: started ===
=== MINIMAL: skip confirm ===
Alive: 1
Alive: 2
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

Fix: `test_minimal/sysbuild/mcuboot.overlay` and
`src/application/sysbuild/mcuboot.overlay` both contain
`/delete-property/ ranges;` on `&flash0`.
