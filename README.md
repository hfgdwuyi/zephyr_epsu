# zephyr_epsu

Zephyr RTOS application for the CiosZhong PSU (Power Supply Unit) controller.

## Hardware

- **Board**: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7 core)
- **Ethernet**: STM32H7 built-in MAC + LAN8742 PHY
- **Debugger**: STLINK V3 (OpenOCD)

## Building

```bash
cd <zephyrproject>/zephyr
west build -d <build_dir> <app_dir>/src/application -b nucleo_h745zi_q/stm32h745xx/m7
```

## Flashing

```bash
west flash -d <build_dir> --runner openocd
```

## HTTP API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/status/heartbeat` | Health check |
| GET | `/uptime` | System uptime in ms |
| GET | `/diag` | Diagnostic snapshot (faults + events) |
| POST | `/diag/clear` | Clear diagnostic faults `{"mask":...,"clear_latched":...}` |
| POST | `/diag/inject` | (Debug) Inject diagnostic event |
| POST | `/api/v1/control` | Control actions (LED, relay, etc.) |
| POST | `/update/start` | Start OTA update |
| GET | `/update/status` | Update progress |

Default IP: 192.0.2.1 (configurable in prj.conf)
