# zephyr_epsu

Zephyr RTOS firmware for the CiosZhong PSU controller.

## Overview

- Board: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7)
- Debug probe: STLINK V3 (OpenOCD)
- Boot chain: MCUboot + signed application images
- OTA model: dual-image swap (slot0/slot1)

## Flash Layout

```
boot_partition:    128 KB  (0x000000 - 0x020000)  MCUboot
slot0_partition:   384 KB  (0x020000 - 0x080000)  active image
slot1_partition:   384 KB  (0x080000 - 0x0E0000)  OTA target image
storage_partition: 128 KB  (0x0E0000 - 0x100000)  persistent data
```

This project uses MCUboot SWAP_USING_OFFSET. The first 128 KB sector in slot1 acts as swap scratch, so OTA payload is written at slot1 offset `0x20000`.

## Repository Layout

```
zephyr_epsu/
├── application/            # Main application image
├── bootloader/             # Bootloader-mode application image
├── lib/                    # Shared BSP/DM/HTTP modules
├── scripts/                # Test and REST helper scripts
├── tests/                  # Unit/integration test targets
└── build/
    ├── application/        # Sysbuild output for application/
    ├── bootloader/         # Sysbuild output for bootloader/
    └── tests/              # Output for test targets
```

## Build and Flash (WSL)

Run all build and flash commands inside the same WSL distro/environment.

Board target used in this repo:

```bash
nucleo_h745zi_q/stm32h745xx/m7
```

### Build Main Application

```bash
cd /mnt/d/Project/CiosZhong_psu/30_Dev/10_Codes/zephyr_epsu
west build -p always -d build/app application -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Build Bootloader-mode Application

```bash
cd /mnt/d/Project/CiosZhong_psu/30_Dev/10_Codes/zephyr_epsu
west build -p always -d build/bootloader bootloader -b nucleo_h745zi_q/stm32h745xx/m7 --sysbuild
```

### Build Test Targets

```bash
cd /mnt/d/Project/CiosZhong_psu/30_Dev/10_Codes/zephyr_epsu

west build -p always -d build/tests/dm_core tests/dm_core -b nucleo_h745zi_q/stm32h745xx/m7
west build -p always -d build/tests/message_queue tests/message_queue -b nucleo_h745zi_q/stm32h745xx/m7
west build -p always -d build/tests/timing tests/timing -b nucleo_h745zi_q/stm32h745xx/m7
```

### Flash

Main application image:

```bash
west flash -d build/application/application
```

Bootloader-mode application image:

```bash
west flash -d build/bootloader/mcuboot
west flash -d build/bootloader/bootloader
```

Test image example:

```bash
west flash -d build/tests/dm_core
```

## OTA Workflow

Use signed images for OTA (`*.signed.bin`).

Main application self-update:

1. Host `zephyr.signed.bin` on an HTTP server reachable by the target.
2. Call `POST /api/v1/update/start` with the image URI.
3. Poll `GET /api/v1/update/status`.

Cross-image switching:

- Main -> Bootloader-mode app: `POST /api/v1/bootloader`
- Bootloader-mode app -> Main: `POST /api/v1/bootloader/exit`

## REST API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/status/heartbeat` | Health check, returns `{}` |
| GET | `/api/v1/status/uptime` | Uptime in milliseconds |
| GET | `/api/v1/status/version` | Firmware version string |
| POST | `/api/v1/update/start` | Start OTA with JSON body `{ "uri": "..." }` |
| GET | `/api/v1/update/status` | Current OTA state/progress/error |
| POST | `/api/v1/bootloader` | Request switch to bootloader-mode image |
| POST | `/api/v1/bootloader/exit` | Request switch back to main image |
| POST | `/api/v1/control` | Device control (application image only) |
| GET | `/api/v1/diag/status` | Diagnostic status/events |
| POST | `/api/v1/diag/clear` | Clear diagnostic faults |

## Python REST CLI

Primary REST test client: `scripts/rest_api_cli.py`

Quick checks:

```bash
python scripts/rest_api_cli.py --host 192.0.2.1 heartbeat
python scripts/rest_api_cli.py --host 192.0.2.1 uptime
python scripts/rest_api_cli.py --host 192.0.2.1 version
python scripts/rest_api_cli.py --host 192.0.2.1 update-status
python scripts/rest_api_cli.py --host 192.0.2.1 diag-status
```

Control and diagnostics:

```bash
# action=1 -> LED_SET (application image only)
python scripts/rest_api_cli.py --host 192.0.2.1 control --action 1 --index 0 --on 1 --value 0
python scripts/rest_api_cli.py --host 192.0.2.1 diag-clear --mask 1 --clear-latched
```

OTA and image switching:

```bash
python scripts/rest_api_cli.py --host 192.0.2.1 update-start --uri http://192.0.2.2:8080/zephyr.signed.bin
python scripts/rest_api_cli.py --host 192.0.2.1 poll-update --count 10 --interval 2

python scripts/rest_api_cli.py --host 192.0.2.1 bootloader-enter --uri http://192.0.2.2:8080/bootloader.signed.bin
python scripts/rest_api_cli.py --host 192.0.2.1 bootloader-exit --uri http://192.0.2.2:8080/main.signed.bin
```

## Troubleshooting

### OTA download timeout (`last_error = -116`)

This usually means the target cannot connect to the host image server.

- Verify host IP/port in the URI is reachable from the device network.
- Verify the image file is actually served at that URI.
- In restricted environments, use an already-allowed HTTP service or a shared internal file server.

### Heartbeat appears to return "nothing"

`/api/v1/status/heartbeat` intentionally returns an empty JSON object (`{}`), which may appear blank in some shells.

### STM32H745 DTS `ranges` issue

If `ranges` is not deleted from `flash0`, MCUboot may compute a wrong flash base and hang on jump.

Fix is already applied in:

- `application/app.overlay`
- `application/sysbuild/mcuboot.overlay`

Both include `/delete-property/ ranges;` on `&flash0`.
