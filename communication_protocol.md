# CiosZhong ePSU 上位机通信协议

版本：v1.0
固件实现：`application/src/terminal/uart_cmd.c`
上位机实现：`psu_host.py`（启动：`./run_host.sh`）

---

## 1. 物理层

| 项 | 值 |
|---|---|
| 串口 | **USART3** |
| 引脚 | **PB10 = TX**，**PB11 = RX** |
| 电平 | 3.3V TTL（需 USB-TTL 转换器，如 CH340/CP2102/FT232） |
| 接线 | 板 TX(PB10) → 转换器 RX；板 RX(PB11) ← 转换器 TX；**GND 共地** |
| 波特率 | **115200** |
| 数据格式 | 8 数据位，无校验，1 停止位（8N1） |

注意：USART3 同时是 Zephyr console（printk 日志出口），日志与命令响应同口混流；上位机解析时忽略非 `OK`/`ERR`/数据行。

---

## 2. 帧格式（行协议）

- 每条命令为一行 **ASCII 文本**，以 `\r\n`（CR LF）结尾（`\n` 也可，`\r` 被忽略）
- 字段以**空格或 Tab** 分隔
- 请求：`<命令字> [参数1] [参数2]\r\n`
- 响应：单行 `OK` / `ERR ...`，或多行数据（`info`）
- 命令处理延迟：≤500ms（terminal 任务 500ms 轮询一次）

---

## 3. 命令集

### 3.1 控制命令

| 命令 | 格式 | 说明 | 成功响应 |
|---|---|---|---|
| **dout** | `dout <idx> <0\|1>` | 控制单路 DOUT 输出；idx=0..35，1=ON 0=OFF | `OK` |
| **doutall** | `doutall <hex64>` | 直接写入 64 位 DOUT 位图（hex，如 0x0000000000000005；先全清再置位，仅低 36 位有效） | `OK` |
| **dac** | `dac <mv>` | DAC 恒定输出电压，0..3300 mV（自动清除方波状态位） | `OK` |
| **dacwv** | `dacwv <0\|1>` | pwr_on_off 方波状态位：1=开启（由固件 0.25Hz 方波驱动），0=停止 | `OK` |
| **pwm** | `pwm <ch> <duty>` | 风扇 PWM 占空比：ch=0/1，duty=0..100（%）（自动启动该通道） | `OK` |
| **pwmoff** | `pwmoff <ch>` | 停止 PWM：ch=0/1 | `OK` |

参数错误响应：`ERR usage: <命令用法>`（见各命令的 usage 提示）

### 3.2 状态查询命令

| 命令 | 格式 | 响应示例 |
|---|---|---|
| **getdout** | `getdout` | `dout=0x0000000200000000` |
| **getdac** | `getdac` | `dac=1500 wv=0` |
| **getpwm** | `getpwm` | `pwm0=50 pwm1=0` |
| **getdin** | `getdin` | `din=0x00000001`（32 位 DIN 输入位图，bit n = DIN 索引 n 电平） |
| **info** | `info` | 多行，见 3.3 |
| **help** | `help` / `?` | 命令列表（多行） |

### 3.3 `info` 响应格式（每行一个字段）

```
state=5 faults=0x00 err=OK
temp1=25.3 temp2=26.1 (C)
pdc0=12.010V 12V=12.000V 5V=5.010V 3V3=3.300V
ac=229.8V 50.0Hz
dout=0x0000000200000000
```

字段说明：
- `state=<n>`：状态机状态号（0=INIT 1=SYS_ON 2=PILOT 3=SW_ON 4=NORMAL 5=S2 6=CHARGE 7=SHTDWN 8=FAULT 9=RESET 10=OFF）
- `faults=0x..`：故障位图（见 3.4）
- `err=<str>`：错误描述（OK 或 ESLR.35 等）
- `temp1/temp2`：NTC 温度（°C，×10 后除以 10；异常时显示 FAULT 或 n/a）
- `pdc0/12V/5V/3V3`：电压轨（V）
- `ac=<v> <f>Hz`：市电电压/频率（无市电时为 `ac=n/a`）
- `dout=0x..`：64 位 DOUT 位图（bit n = DOUT 索引 n 的状态）

### 3.4 状态机错误码（faults 位图）

| 位 | 值 | 错误 | 含义 |
|---|---|---|---|
| bit0 | 0x01 | ESLR.35 | 初始化/传感器故障（含 NTC 传感器开路/短路） |
| bit1 | 0x02 | ESTP3.36 | K3 闭合超时 |
| bit2 | 0x04 | ESIC1.37 | 合闸验证失败 |
| bit3 | 0x08 | ESTP1.38 | 市电丢失 |
| bit4 | 0x10 | EAKO3.39 | 故障恢复后复位 |
| bit5 | 0x20 | ESIC.40 | 充电控制错误 |

### 3.5 DOUT 索引表（0-35，与 bsp_dio.h / app.overlay 一致）

| idx | 名称 (引脚) | idx | 名称 (引脚) |
|---|---|---|---|
| 0 | trolley_enable (PH1) | 18 | k6_drv (PI5) |
| 1 | wdi (PH9) | 19 | k3_drv (PI6) |
| 2 | pg_13v5 (PA12) | 20 | k2_drv (PI7) |
| 3 | k13_en (PB6) | 21 | k4_drv (PI8) |
| 4 | dbg_led0 (PC8) | 22 | k7_drv (PI9) |
| 5 | dbg_led1 (PC9) | 23 | k10_en (PI10) |
| 6 | dbg_led2 (PC10) | 24 | k11_en (PI11) |
| 7 | led_pwr_24_on (PD0) | 25 | k12_en (PI12) |
| 8 | led_cp_224v_on (PD1) | 26 | k8_1_en (PI13) |
| 9 | led_grid_pwr_in (PD2) | 27 | k8_2_en (PI14) |
| 10 | led_ups_in (PD3) | 28 | k9_en (PI15) |
| 11 | led_system_on (PD4) | 29 | drv_is_pc_site_on (PJ11) |
| 12 | led_s2_solo_sys (PD5) | 30 | drv_app_host_site_on (PJ12) |
| 13 | led_trolley_connected (PD6) | 31 | mains_connected_is_pc (PJ13) |
| 14 | led_is_pc_on (PD7) | 32 | mains_connected_mcu (PJ14) |
| 15 | led_s2_sys_on (PD10) | 33 | led_pj0 (PJ0) |
| 16 | led_app_host_on (PD12) | 34 | led_pj1 (PJ1) |
| 17 | k5_drv (PI4) | 35 | led_pj2 (PJ2) |

### 3.6 DIN 索引表（0-20，与 bsp_dio.h / app.overlay din_config 一致）

| idx | 名称 (引脚) | idx | 名称 (引脚) |
|---|---|---|---|
| 0 | grid_main_relay_status (PH5) | 11 | trl_mu_connected_mcu (PD13) |
| 1 | me_box_error (PH6) | 12 | trl_mu_connected_is_pc (PD14) |
| 2 | fault0 (PA8) | 13 | s2_system_config (PJ3) |
| 3 | fault1 (PA9) | 14 | solo_system_config (PJ4) |
| 4 | fault2 (PA10) | 15 | trolley_connected (PJ5) |
| 5 | fault3 (PA11) | 16 | app_host_on (PJ7) |
| 6 | temp_alert (PB12) | 17 | smart_whs_indicate (PJ8) |
| 7 | led_pwr_24_fb (PC6) | 18 | drawer_indicate (PJ9) |
| 8 | fault4 (PC11) | 19 | smart_ctrl_whs_search (PJ10) |
| 9 | fault5 (PC12) | 20 | is_pc_on (PJ15) |
| 10 | fault6 (PC13) |  |  |

---

---

## 4. 完整交互示例

```
> help
< commands:
<   help                    - this help
<   info                    - system status
<   dout <idx> <0|1>        - set DOUT output (0..doutMax-1)
<   doutall <hex64>         - write 64-bit DOUT bitmap
<   dac <mv>                - DAC constant voltage (0..3300 mV)
<   dacwv <0|1>             - pwr_on_off square wave on/off
<   pwm <ch> <duty>         - fan PWM duty (ch 0/1, 0..100%)
<   pwmoff <ch>             - stop PWM
<   getdout                 - read DOUT bitmap
<   getdac                  - read DAC mv + wave state
<   getpwm                  - read PWM duties

> dout 33 1                 （点亮 PJ0 LED）
< OK

> getdout
< dout=0x0000000200000000   （bit33=1）

> pwm 0 50                  （风扇1 50%）
< OK

> getpwm
< pwm0=50 pwm1=0

> dac 1500
< OK

> getdac
< dac=1500 wv=0

> info
< state=4 faults=0x00 err=OK
< temp1=25.3 temp2=26.1 (C)
< pdc0=12.010V 12V=12.000V 5V=5.010V 3V3=3.300V
< ac=229.8V 50.0Hz
< dout=0x0000000200000000
```

---

## 5. 注意事项

1. **日志混流**：USART3 同口输出 printk 日志（如启动信息、传感器变化），与命令响应交错；解析时只认 `OK`/`ERR`/`dout=`/`dac=`/`pwm0=`/`state=`/`temp1=`/`pdc0=`/`ac=` 行，其余忽略
2. **响应延迟**：命令处理最多 500ms（terminal 任务周期）
3. **与状态机冲突**：控制命令直接操作 BSP 输出，若状态机正在运行（如 PILOT_CONTACT 阶段的 DAC 方波、NORMAL_OP 的风扇策略），状态机可能随后覆盖上位机的设置——本协议用于**调试/产测**
4. **超长行**：单行命令超过 128 字节将被丢弃
5. **DOUT 提交延迟**：命令写入位图后，由固件 1ms 任务刷到 GPIO，实际引脚变化 ≤1ms
6. **断电/重启**：所有控制状态复位（DOUT 全关、DAC=0、PWM 停）

---

## 6. 相关文件

| 文件 | 说明 |
|---|---|
| `application/src/terminal/uart_cmd.c/h` | 固件端协议实现（USART3 中断接收 + 行解析） |
| `psu_host.py` | Python 上位机（GUI） |
| `run_host.sh` | 上位机启动脚本 |
| `application/prj.conf` | `CONFIG_UART_INTERRUPT_DRIVEN=y`（RX 中断必需） |
