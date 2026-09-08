# CiosZhong ePSU 烧录操作手册（macOS）

## 0. 环境准备（每次新终端执行）

```bash
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1
cd /Users/mac/project/03_siemens/ciosZhong_ePSU
```

## 1. 检查 ST-Link 与目标连接

```bash
st-info --probe
# 期望看到: chipid: 0x450 / flash: 2097152 / dev-type: STM32H74x_H75x
```

| 现象 | 含义 | 处理 |
|---|---|---|
| `Found 0 stlink` | ST-Link 没插好 | 拔插 USB，等 5 秒 |
| `Failed to enter SWD mode` / chipid 0x000 | ST-Link 半死 或 目标没电/线松 | **拔 USB 10 秒再插回换口**；确认目标板 3.3V |
| chipid 0x450 | ✅ 正常可烧 | 继续 |

## 2. 烧录单区固件（无 MCUboot，当前使用）

### 方式 A：稳定烧录（推荐）—— 需要 BOOT0 拉高 + 断电重启

1. **BOOT0 接 3.3V（拉高）**
2. **断电 2 秒 → 重新上电**（BOOT0 只在复位采样，必须重启才生效）
3. 确认 `st-info --probe` 显示 chipid 0x450
4. 烧录：
```bash
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" \
  -c "init" -c "reset halt" -c "halt" \
  -c "program zephyr.hex verify reset exit"
```
5. 看到 `** Verified OK **` 即成功
6. **BOOT0 恢复下拉** → 断电重启 → 跑固件

### 方式 B：不切 BOOT0 的抢窗口烧录（重试脚本）

芯片跑着固件时也能烧（靠复位窗口 + 重试）：
```bash
./flash_stlink.sh          # 烧 build/zephyr/zephyr.hex
```
> 注：该脚本默认烧 `build/zephyr/zephyr.hex`；要烧指定文件：
> `./flash_stlink.sh 指定文件.hex`

## 3. 常用固件文件

| 文件 | 说明 |
|---|---|
| `zephyr.hex`（项目根目录） | 单区完整固件（0x08000000 起），**当前用这个** |
| `build/zephyr/zephyr.hex` | 源码最新构建产物 |

## 4. 烧录失败快速排查

1. `erase timeout` / `flash register read fail` → 芯片在跑固件被干扰 → 用**方式 A（BOOT0 高）**
2. `cannot read IDR` / `unable to connect` → SWD 线/ST-Link 状态 → **拔插 ST-Link USB 10 秒换口**，确认目标上电
3. ST-Link 反复 `Failed to enter SWD` → 老 V2 状态差，**换野火 DAP 或 ST-Link V3**

## 5. 烧录后观察

串口助手连 **USART1：PB14(TX) / PB15(RX)，115200, 8N1**，GND 共地
期望输出：`===== CiosZhong PSU v0.1.0 =====` + 状态日志
