# CiosZhong ePSU — 空片烧录与固件升级指南

固件组合（E1 调试阶段，2026-09-08 验证通过）：
- **Boot**: MCUboot v1.0.0  (SWAP_USING_OFFSET，无 scratch)
- **App**:  v0.2.0        (串口 DFU + MCUboot confirm)

Flash 布局（内部 flash0 = 0x08000000 起，2MB 单设备）：
| 区 | 地址 | 内容 |
|---|---|---|
| boot    | 0x08000000 (128KB)  | MCUboot |
| slot0   | 0x08020000 (512KB)  | 主固件（当前运行） |
| slot1   | 0x08100000 (512KB)  | 升级暂存（DFU 写入） |

---

## 一、空片（首次）烧录 —— 需要 ST-Link

前置：
- 板子连接 ST-Link（USB 识别为 0x0483:0x3748）
- 工程已构建好（见文末"构建命令"）

### 方法 A：一键脚本（推荐）

```sh
cd /Users/mac/project/03_siemens/ciosZhong_ePSU
./flash_recover_mcuboot.sh          # 依次烧 boot + app，各自动重试 6 次
```

分步：
```sh
./flash_recover_mcuboot.sh boot     # 只烧 MCUboot @ 0x08000000
./flash_recover_mcuboot.sh app      # 只烧 app @ 0x08020000
```

脚本原理：通过 NRST 硬件复位 + 重试窗口烧录（`reset_config srst_only` +
`reset halt`），烧录时 CPU 被复位停住即可写入，**多数情况下无需 BOOT0**。

### 方法 B：openocd 手动

```sh
# 烧 boot
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" \
  -c "init" -c "reset halt" -c "halt" \
  -c "program build-mcuboot/zephyr/zephyr.bin 0x08000000 verify reset exit"

# 烧 app (slot0)
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" \
  -c "init" -c "reset halt" -c "halt" \
  -c "program build/zephyr/zephyr.signed.bin 0x08020000 verify reset exit"
```

### 如果烧录失败（ST-Link 连不上 / PARTNO 0x0）

多为运行态 + MAX6703A 看门狗干扰或 ST-Link 时序问题：
1. 拔掉 ST-Link USB ≥10s，换 USB 口重插
2. 重跑脚本（重试会碰复位窗口）
3. 仍不行：**BOOT0 拉高 → 断电 ≥10s → 上电（保持 BOOT0 高）** 进 ROM 态 → 重跑

烧完后：**BOOT0 拉低 → 断电重上电**，正常应看到串口输出：

```
===== CiosZhong PSU =====
  App  v0.2.0
  Boot v1.0.0 (MCUboot)
PSU CMD: ready (help for commands)
```

---

## 二、后续固件升级 —— 只需要串口（客户路径）

**不需要 ST-Link、不需要 BOOT0**。板子跑着旧固件时，串口直连（USART1
PB14/PB15，115200）执行：

```sh
python3 tools/psu_dfu.py /dev/cu.usbserial-XXX build/zephyr/zephyr.signed.bin
```

（把 `cu.usbserial-XXX` 换成实际串口，如 `cu.usbserial-120`）

流程（自动）：
1. APP 收到 `dfu` → 擦除 slot1
2. 逐块上传新固件到 **slot1 第二扇区**（0x20000 偏移），逐块 ACK 校验
3. 上传完 `boot_request_upgrade()` → 复位
4. MCUboot swap-using-offset 交换 slot1 ↔ slot0
5. 新固件启动 → `boot_write_img_confirmed()` 固化 → 下次复位不回滚

成功标志：
```
sent 79852/79852
VERIFY slot1+0x20000 = 0x96F3B83D
DFU done 79852/79852, rebooting...
...重启后横幅版本号变化（如 v0.1.0 → v0.2.0）
```

---

## 三、烧录文件清单

| 文件 | 说明 |
|---|---|
| `build-mcuboot/zephyr/zephyr.bin` | Boot v1.0.0（烧 0x08000000） |
| `build/zephyr/zephyr.signed.bin` | App 签名固件（烧 slot0 / 串口 DFU 用） |
| `tools/psu_dfu.py` | 串口升级工具 |
| `flash_recover_mcuboot.sh` | 首次烧录脚本（boot+app） |
| `flash_stlink.sh <xx.hex>` | 烧直烧式 hex（bring-up 用） |

---

## 四、重新构建命令（修改源码后）

```sh
export ZEPHYR_BASE=~/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/project/02_zephyr/zephyr-sdk-1.0.1

# 1) MCUboot（改了 board dts/布局才需重编）
.venv/bin/west build -d build-mcuboot -p always \
  -b cioszhong_psu/stm32h745xx/m7 \
  ~/project/02_zephyr/zephyrproject/bootloader/mcuboot/boot/zephyr \
  -- -DBOARD_ROOT=$PWD/application \
     -DUSER_CACHE_DIR=$PWD/build-mcuboot/.zcache \
     "-DEXTRA_CONF_FILE=$PWD/mcuboot_swap_offset.conf"

# 2) App（每次改版本/代码后）
.venv/bin/west build -d build application \
  -- -DUSER_CACHE_DIR=$PWD/build/.zephyr-cache

# 3) 签名（版本号 = application/Kconfig.project 的 CIOS_ZHONG_FW_VERSION）
.venv/bin/python \
  ~/project/02_zephyr/zephyrproject/bootloader/mcuboot/scripts/imgtool.py \
  sign --key ~/project/02_zephyr/zephyrproject/bootloader/mcuboot/root-rsa-2048.pem \
  --header-size 0x400 --align 8 --version 0.2.0 --slot-size 0x80000 \
  build/zephyr/zephyr.bin build/zephyr/zephyr.signed.bin
```

版本号只改一处：`application/Kconfig.project` → `CIOS_ZHONG_FW_VERSION`
（签名 `--version` 参数需同步成同一版本）。
