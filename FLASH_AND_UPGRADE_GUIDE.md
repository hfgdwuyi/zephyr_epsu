# CiosZhong ePSU — 空片烧录与固件升级指南

> **完整版（新设备环境搭建 → boot/app 重建 → 校验 → 升级）见
> [`SERIAL_DFU_GUIDE.md`](SERIAL_DFU_GUIDE.md)**。本文是烧录/升级的操作速查。

固件组合：
- **Boot**: MCUboot v1.0.0（**OVERWRITE_ONLY**，无 scratch）
- **App**:  见 `application/Kconfig.project` → `CIOS_ZHONG_FW_VERSION`

Flash 布局（内部 flash0 = 0x08000000 起，2 MB 单设备）：

| 区 | 地址 | 内容 |
|---|---|---|
| boot  | 0x08000000（128 KB） | MCUboot |
| slot0 | 0x08020000（512 KB） | 主固件（当前运行） |
| slot1 | 0x08100000（512 KB） | 升级暂存（DFU 写入） |

---

## 一、构建产物（先看这里）

```sh
./tools/build_fw.sh --all      # boot + app + 签名 + 自检
```

| 产物 | 用途 |
|---|---|
| `build-mcuboot/zephyr/zephyr.bin` | boot，烧 0x08000000 |
| `build/zephyr/zephyr.signed.bin` | app（**已签名**），烧 0x08020000 / 串口 DFU 上传 |

> ⚠️ `build/zephyr/zephyr.bin` **没有签名**，不能用于升级；必须用 `zephyr.signed.bin`。

---

## 二、空片（首次）烧录 —— 需要 ST-Link

前置：板子接 ST-Link（USB 识别为 `0x0483:0x3748`），已构建好（见上）。

### 方法 A：一键脚本（推荐）

```sh
./flash_recover_mcuboot.sh          # boot + app，各自动重试 6 次
./flash_recover_mcuboot.sh boot     # 只烧 boot @ 0x08000000
./flash_recover_mcuboot.sh app      # 只烧 app  @ 0x08020000
```

原理：通过 NRST 硬件复位 + 重试窗口烧录（`reset_config srst_only` + `reset halt`），
烧录时 CPU 被复位停住即可写入，**多数情况下无需 BOOT0**。

### 方法 B：openocd 手动

```sh
# boot
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" \
  -c "init" -c "reset halt" -c "halt" \
  -c "program build-mcuboot/zephyr/zephyr.bin 0x08000000 verify reset exit"

# app（已签名镜像）
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" \
  -c "init" -c "reset halt" -c "halt" \
  -c "program build/zephyr/zephyr.signed.bin 0x08020000 verify reset exit"
```

### 烧录失败

多为运行态 + MAX6703A 看门狗干扰或 ST-Link 时序问题：

1. 拔 ST-Link USB ≥10 s，换口重插
2. 重跑脚本（重试会碰复位窗口）；必要时 `./flash_force.sh`
3. 仍不行：**BOOT0 拉高 → 断电 ≥10 s → 上电（保持 BOOT0 高）** 进 ROM 态 → 重跑
4. 诊断：`./tools/flash_diag.sh`

烧完后：**BOOT0 拉低 → 断电重上电**，串口应输出：

```
===== CiosZhong PSU =====
  App  v0.2.4
  Boot v1.0.0 (MCUboot)
PSU CMD: ready (help for commands)
```

---

## 三、后续固件升级 —— 只需要串口

**不需要 ST-Link、不需要 BOOT0。** 板子跑着旧固件时，串口直连
（USART1 PB14/PB15，115200 8N1）执行：

```sh
python3 tools/psu_dfu.py /dev/cu.usbserial-XXX build/zephyr/zephyr.signed.bin
```

流程（自动）：
1. app 收到 `dfu` → 擦除 slot1
2. 逐块上传新固件到 **slot1 起始（偏移 0）**，逐块 ACK 校验
3. 上传完 `boot_request_upgrade()` → 复位
4. MCUboot **OVERWRITE_ONLY**：把 slot1 整体覆盖到 slot0
5. 新固件启动 → `boot_write_img_confirmed()` 固化 → 下次复位不回滚

成功标志：
```
  sent 77940/77940
  [DONE] DFU done 77940/77940, rebooting...
  [OK] round 1 succeeded, app is up
```
重启后横幅版本号发生变化（如 v0.2.3 → v0.2.4）。

失败排查见 `SERIAL_DFU_GUIDE.md` §5.4。

---

## 四、文件清单

| 文件 | 说明 |
|---|---|
| `tools/build_fw.sh` | 一键构建 boot + app + 签名 |
| `tools/check_signed_image.py` | 校验签名镜像 |
| `build-mcuboot/zephyr/zephyr.bin` | boot（烧 0x08000000） |
| `build/zephyr/zephyr.signed.bin` | app 签名固件（烧 slot0 / 串口 DFU） |
| `tools/psu_dfu.py` | 串口升级命令行工具 |
| `tools/psu_dfu_gui.py` | 串口升级图形界面 |
| `flash_recover_mcuboot.sh` | 首次烧录脚本（boot + app） |
| `flash_force.sh` | 复位/看门狗干扰下强制烧录 |
| `flash_stlink.sh <xx.hex>` | 直烧式 hex（bring-up 用） |
| `tools/flash_diag.sh` | SWD 连接诊断 |

---

## 五、重新构建（详细步骤）

见 **[`SERIAL_DFU_GUIDE.md`](SERIAL_DFU_GUIDE.md) §2/§3**：

- §2 新设备环境搭建（west 工作区、Zephyr SDK、mcuboot 补丁）
- §3 构建 boot + app（一键脚本 / 手动分步 / 常见坑）
- §7 变更指引（改什么要重建 bootloader、是否要全片重烧）
