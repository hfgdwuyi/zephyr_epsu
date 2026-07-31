# cios-zhong PSU

Zephyr RTOS application for the CiosZhong PSU (Power Supply Unit) controller.

## Hardware

- **Board**: NUCLEO-H745ZI-Q (STM32H745, Cortex-M7 core)
- **Flash**: 2MB dual-bank @ 0x08000000
- **RAM**: 512KB SRAM3 (M7 main RAM)
- **Debugger**: STLINK V3 (OpenOCD)
- **External Watchdog**: MAX6703A (WDI on PH9, 1.6s timeout)

## Architecture

Single-image firmware — no MCUboot, no Ethernet, no HTTP server.

```
application/
├── src/
│   ├── main.c                    # Zephyr-native scheduler entry point
│   ├── state_machine/
│   │   ├── psu_sm.c              # 11-state PSU controller
│   │   └── psu_sm.h
│   └── ntc/
│       ├── ntc_sensor.c          # NTC R→T lookup + interpolation
│       └── ntc_sensor.h
├── CMakeLists.txt
├── prj.conf
└── app.overlay

lib/bsp/                           # Reusable board support package
├── bsp_ain.c/h                    # 14-channel ADC input
├── bsp_aout.c/h                   # DAC output
├── bsp_board.c/h                  # Board init (LED, AIN, AOUT, PWM)
├── bsp_dio.c/h                    # 32 DOUT + 16 DIN with macro enums
├── bsp_led.c/h                    # NUCLEO on-board LEDs (PB0 green, PE1 yellow)
├── bsp_pwm.c/h                    # Fan PWM (TIM2_CH4 PJ15, TIM8_CH3 PI15)
└── bsp_wtdg.c/h                   # MCU internal window WDT
```

### Layer Separation

| Layer | Directory | Responsibility |
|-------|-----------|----------------|
| Application | `application/src/` | Product-specific logic (state machine, NTC conversion) |
| BSP | `lib/bsp/` | Hardware driver wrappers (no product knowledge) |

## Pin Configuration

### Digital Outputs (32 channels, per pin_config.xlsx)

| Macro | Pin | Signal |
|-------|-----|--------|
| `DOUT_TROLLEY_ENABLE_DRV` | PH1 | Trolley enable driver |
| `DOUT_PWR_ON_OFF` | PA5 | Power on/off |
| `DOUT_K3_1_DRV` | PK1 | K3 relay channel 1 |
| `DOUT_K3_2_DRV` | PK2 | K3 relay channel 2 |
| `DOUT_K4_DRV` | PK3 | K4 relay |
| `DOUT_K5_DRV` | PI4 | K5 relay |
| `DOUT_K6_DRV` | PI5 | K6 relay |
| `DOUT_K8_1_DRV` | PI6 | K8 relay channel 1 |
| `DOUT_K8_2_DRV` | PI7 | K8 relay channel 2 |
| `DOUT_K9_DRV` | PI9 | K9 relay |
| `DOUT_K10_DRV` | PI10 | K10 relay |
| `DOUT_K11_DRV` | PI11 | K11 relay |
| `DOUT_K12_DRV` | PI12 | K12 relay |
| `DOUT_LED_S1_SYS_ON` | PB10 | System 1 config indicator |
| `DOUT_LED_S2_SYS_ON` | PB11 | System 2 config indicator |
| `DOUT_DBG_LED0` | PC8 | Debug LED 0 |
| `DOUT_DBG_LED1` | PC9 | Debug LED 1 |
| `DOUT_DBG_LED2` | PC10 | Debug LED 2 |
| `DOUT_LED_PAC230V_ON` | PC15 | AC 230V power indicator |
| `DOUT_LED_GRID_PWR_IN` | PD2 | Grid power input indicator |
| `DOUT_LED_UPS_IN` | PD3 | UPS/battery indicator |
| `DOUT_LED_SYSTEM_ON` | PD4 | System running indicator |
| `DOUT_LED_S2_SOLO_SYS` | PD5 | S2 solo system indicator |
| `DOUT_LED_TROLLEY_CONNECTED` | PD6 | Trolley connected indicator |
| `DOUT_LED_IS_PC_ON` | PD7 | PC on indicator |
| `DOUT_LED_APPHOST_ON` | PD11 | App host on indicator |
| `DOUT_TROLLEY_CONNECTED_MCU` | PI13 | Trolley status → MCU |
| `DOUT_TROLLEY_CONNECTED_IS_PC` | PI14 | Trolley status → PC |
| `DOUT_DRV_IS_PC_SITE_ON` | PJ11 | PC site on driver |
| `DOUT_DRV_APP_HOST_SITE_ON` | PJ12 | App host site on driver |
| `DOUT_MAINS_CONNECTED_APPHOST` | PJ13 | Mains → app host |
| `DOUT_MAINS_CONNECTED_IS_PC` | PJ14 | Mains → PC |
| `DOUT_WDI` | PH9 | MAX6703A WDI feed |

### Digital Inputs (16 channels, per pin_config.xlsx)

| Macro | Pin | Signal |
|-------|-----|--------|
| `DIN_GRID_MAIN_RELAY_STATUS` | PH5 | Grid main relay status |
| `DIN_ME_BOX_ERROR` | PH6 | ME box error |
| `DIN_TROLLEY_CONNECTED` | PA0 | Trolley connected |
| `DIN_TEMP_ALERT` | PB12 | Temperature alert |
| `DIN_LED_PWR_24_ON` | PC6 | 24V power LED monitor |
| `DIN_LED_CP_24V_ON` | PC7 | 24V CP LED monitor |
| `DIN_SYSTEM_ON_OFF` | PJ0 | System on/off button |
| `DIN_SYSTEM_RESET` | PJ1 | System reset button |
| `DIN_S1_SYSTEM_CONFIG` | PJ2 | System 1 config switch |
| `DIN_S2_SYSTEM_CONFIG` | PJ3 | System 2 config switch |
| `DIN_SOLO_SYSTEM_CONFIG` | PJ4 | Solo config switch |
| `DIN_TROLLEY_CONNECTED_J` | PJ5 | Trolley connected (J) |
| `DIN_IS_PC_ON` | PJ6 | PC on status |
| `DIN_APP_HOST_ON` | PJ7 | App host on status |
| `DIN_SMART_WHS_INDICATE` | PJ8 | Smart warehouse indicate |
| `DIN_DRAWER_INDICATE` | PJ9 | Drawer indicate |
| `DIN_SMART_CTRL_WHS_SEARCH` | PJ10 | Smart control warehouse search |

### Analog Inputs (14 channels)

| Channel | Pin | Signal | ADC |
|---------|-----|--------|-----|
| 0 | PF6 | adc_3v3 | ADC3 INP4 |
| 1 | PF7 | adc_pdc1 | ADC3 INP5 |
| 2 | PF8 | adc_pdc7 | ADC3 INP6 |
| 3 | PF9 | adc_pdc6 | ADC3 INP7 |
| 4 | PF10 | adc_pdc5 | ADC3 INP8 |
| 5 (AIN_TEMP1) | PA3 | adc_temp1 (NTC) | ADC1 INP15 |
| 6 (AIN_TEMP2) | PA4 | adc_temp2 (NTC) | ADC1 INP18 |
| 7 | PA6 | adc_pdc0 | ADC1 INP3 |
| 8 | PC0 | adc_pdc4 | ADC1 INP10 |
| 9 | PB0 | adc_pdc2 | ADC1 INP9 |
| 10 | PB1 | adc_pdc3 | ADC1 INP5 |
| 11 | PC2 | adc_vin | ADC1 INP12 |
| 12 | PC3 | adc_pdc0_alt | ADC1 INP13 |
| 13 | PH4 | adc_5v | ADC3 INP14 |

### Fan PWM

| Channel | Pin | Timer | AF |
|---------|-----|-------|----|
| FAN_PWM1 | PJ15 | TIM2 CH4 | AF1 |
| FAN_PWM2 | PI15 | TIM8 CH3 | AF2 |

### Serial Ports

| USART | Pins | Baud | Usage |
|-------|------|------|-------|
| USART3 | PD8/PD9 | 115200 | Console (printk via ST-LINK VCP) |
| USART1 | PB14/PB15 | 115200 | Application communication |

## State Machine

11-state PSU controller per ePSU timing diagram:

```
INIT ──(trolley OK)──▶ SYS_ON ──(config read)──▶ PILOT_CONTACT
PILOT_CONTACT ──(K3 enable, 200ms, PC6 feedback)──▶ SWITCH_ON
SWITCH_ON ──(K3 close, 500ms, PH5 grid OK)──▶ NORMAL_OP
NORMAL_OP ──┬──(S2 config)──▶ S2_MODE
            ├──(charging req)──▶ CHARGING
            ├──(shutdown btn)──▶ SHUTDOWN ──(4-step relay seq)──▶ OFF
            └──(mains lost / over-temp)──▶ FAULT
FAULT ──(reset btn / 3s timeout)──▶ RESET ──(2s)──▶ INIT
```

### Relay Sequencing (SHUTDOWN example)

| Time | Action |
|------|--------|
| 0ms | K4 off |
| 50ms | K3 off |
| 100ms | PWR_ON / Trolley off |
| 150ms | Fan off, LEDs off |
| 200ms | → OFF state |

### Error Codes

| Code | Trace ID | Trigger |
|------|----------|---------|
| 0x01 | ESLR.35 | Init failure |
| 0x02 | ESTP3.36 | K3 close timeout |
| 0x04 | ESIC1.37 | Switch-on verification fail |
| 0x08 | ESTP1.38 | Mains power loss |
| 0x10 | EAKO3.39 | Reset after error recovery |
| 0x20 | ESIC.40 | Charging control error |

## NTC Temperature Sensor

Vishay NTCALUG02A472FA: R25=4700Ω ±1%, B(25/85)=3984K.

37-point lookup table (-55°C to +125°C) with binary search and linear interpolation.

| Threshold | Temp | Action |
|-----------|------|--------|
| NTC_TEMP_FAN_MID | 30°C | Fan 40% duty |
| NTC_TEMP_FAN_MAX | 50°C | Fan 60% duty |
| NTC_TEMP_WARN | 75°C | Fan 80% duty |
| NTC_TEMP_FAULT | 90°C | Fan 100%, 5s persist → FAULT shutdown |

## Task Scheduling (Zephyr-native)

| Mechanism | Period | Task | Context |
|-----------|--------|------|---------|
| `k_thread` (PRIO=2) | 1ms | `psu_sm_tick()` + `WTDG_Feed()` | Dedicated thread |
| `K_WORK_DELAYABLE` | 10ms | `bspDoutUpdate()` | System workqueue |
| `K_WORK_DELAYABLE` | 50ms | `bspAinPoll()` | System workqueue |
| `K_WORK_DELAYABLE` | 500ms | `bspWdiFeed()` | System workqueue |
| `K_WORK_DELAYABLE` | 3000ms | Status printk | System workqueue |
| `K_TIMER` | 500ms | NUCLEO LED toggle | ISR |
| `k_timer` + `k_work` | 1ms | DIN polling + debounce | ISR → workqueue |

`main()` initializes all modules and returns — no `while(1)` loop.

## Build & Flash

### Prerequisites

```bash
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1
source $ZEPHYR_BASE/zephyr-env.sh
```

### Build

```bash
cd ~/project/02_zephyr/zephyrproject
west build \
  -d ~/project/02_zephyr/ciosZhong_ePSU/build \
  ~/project/02_zephyr/ciosZhong_ePSU/application \
  -b nucleo_h745zi_q/stm32h745xx/m7
```

### Flash

```bash
west flash -d ~/project/02_zephyr/ciosZhong_ePSU/build -r openocd
```

## Expected Serial Output

```
===== CiosZhong Application v0.1.0 =====
PSU_SM: init cfg=0
PSU_SM: -> state 0 (t=0ms)
AIN: init done (poll), inputs=14
WTDG_Init: watchdog0 not in devicetree, skipping
PSU_SM: -> state 1 (t=100ms)
...
PSU_SM: -> state 5 (t=700ms)
PSU [NORMAL] err=OK
PSU [NORMAL] err=OK
```
