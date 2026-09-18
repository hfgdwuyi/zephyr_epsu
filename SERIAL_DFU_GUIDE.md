# CiosZhong ePSU — 新设备环境搭建、boot/app 重建与串口升级指南

> **目标**：在**任何一台新设备**上，从零搭建环境 → 重建 MCUboot bootloader + app →
> 烧录 → 用上位机通过**串口**正常升级固件。
> 本文把整条链路（工具链 / 分区布局 / 签名密钥 / MCUboot 配置 / DFU 偏移 / 版本号）
> 全部写死，并提供已验证的脚本 `tools/build_fw.sh`，避免手敲参数出错。

---

## 0. 快速开始（TL;DR）

```sh
# ── 一次性：新设备环境（§2）──
west init -m https://github.com/zephyrproject-rtos/zephyr ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export
# 安装 Zephyr SDK（§2.3）

# ── 获取工程 ──
git clone <本仓库> ciosZhong_ePSU && cd ciosZhong_ePSU

# ── 构建 boot + app + 签名（§3.3）──
./tools/build_fw.sh --all

# ── 首次空片：ST-Link 烧 boot + app（§4）──
./flash_recover_mcuboot.sh

# ── 之后升级：只需串口（§5）──
python3 tools/psu_dfu.py /dev/cu.usbserial-XXXX build/zephyr/zephyr.signed.bin
```

---

## 1. 系统组成与升级链路

```
┌──────────────────────────── 内部 Flash0 (2 MB @ 0x08000000) ────────────────────────────┐
│  boot  128 KB @ 0x08000000   │  slot0 512 KB @ 0x08020000  │  slot1 512 KB @ 0x08100000  │
│  MCUboot v1.0.0              │  当前运行的 app (primary)    │  升级暂存 (secondary)        │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

升级流程（**客户路径，只需串口**）：

```
PC ──USART1(PB14/PB15, 115200 8N1)──> 运行中的 app
   1. "dfu"               → app 擦除 slot1
   2. "size <hex>"        → 声明固件总长度
   3. "data <off> <hex…>" → 逐块写 slot1（512 B/块，逐块 ACK）
   4. 写完 → boot_request_upgrade() → 复位
   5. MCUboot 把 slot1 覆盖到 slot0（OVERWRITE_ONLY）
   6. 新固件启动 → boot_write_img_confirmed() 固化
```

> **不需要 ST-Link、不需要 BOOT0、不需要拨码。**

---

## 2. 新设备环境搭建（一次性）

### 2.1 本工程验证过的版本组合

| 组件 | 版本 | 说明 |
|---|---|---|
| Zephyr | `v4.4.0-rc3-2930-gf48ca153af1`（commit `f48ca153af1`） | `ZEPHYR_BASE` |
| west | 1.5.0 | 工作区管理 |
| MCUboot | `v2.4.0-41-ge36d3c8d`（commit `e36d3c8d`） | 由 Zephyr `west.yml` 锁定 |
| Zephyr SDK | 1.0.1 | 工具链 |
| Python | 3.14.x | 需 `pyserial`（上位机工具） |

> **建议锁定 Zephyr 版本**（新版本可能改变 API/CMake 行为）。若无需完全一致，
> 用 `west init -m <url> --mr main` 也可，但需自行验证。

### 2.2 安装 west 与 Zephyr 工作区

```sh
# 独立虚拟环境（推荐）
python3 -m venv ~/zephyr-venv
source ~/zephyr-venv/bin/activate
pip install west

# 初始化工作区（会 clone zephyr 主仓 + 所有 module，含 bootloader/mcuboot）
cd ~
west init -m https://github.com/zephyrproject-rtos/zephyr \
     --mr f48ca153af1  ~/zephyrproject
cd ~/zephyrproject
west update                 # 拉取 mcuboot 等；耗时较长

# 导出 CMake package（可选，便于外部 find_package）
west zephyr-export
pip install -r ~/zephyrproject/zephyr/scripts/requirements.txt
```

检查：
```sh
ls ~/zephyrproject/bootloader/mcuboot/root-rsa-2048.pem   # 签名密钥（mcuboot 自带）
```

### 2.3 安装 Zephyr SDK

```sh
# 从 https://github.com/zephyrproject-rtos/sdk-ng/releases 下载对应平台包
# macOS: zephyr-sdk-1.0.1_macos-aarch64.tar.xz
cd ~/project/02_zephyr && tar xf <下载的包>
./zephyr-sdk-1.0.1/setup.sh        # 安装到 ~/zephyr-sdk-1.0.1，并把 udev 规则装好
```

`ZEPHYR_SDK_INSTALL_DIR` 指向 SDK 目录，`ZEPHYR_TOOLCHAIN_VARIANT=zephyr`。

### 2.4 获取本工程

```sh
cd ~/project/03_siemens
git clone <本仓库> ciosZhong_ePSU
cd ciosZhong_ePSU

# 图形化升级工具所需（可选）
pip install pyserial
pip install pyserial tkinterdnd2    # 若要 psu_dfu_gui.py
```

### 2.5 应用 MCUboot 本地补丁（**必需**）

`mcuboot_patches/0001-cioszhong-mcuboot-local-fixes.patch` 打在本机 Zephyr 工作区的
mcuboot 检出上。**不打补丁 boot 可能跑不起来**：

| 文件 | 修复 | 不打的后果 |
|---|---|---|
| `boot/zephyr/main.c` | flash 读取前 clean + invalidate cache | ROM bootloader 开了 cache 却未失效 → MCUboot 读到**错误的镜像头**（`img_size` 错）→ 升级后不启动 |
| `boot/zephyr/watchdog.c` | feed 外部 MAX6703A（WDI = PH9） | 覆盖 slot0 期间被 1.6 s 外部看门狗复位 → slot0 半写 |
| `image_validate.c` / `tlv.c` / `swap_offset.c` | 调试日志清理、`app_max_size()` 修正 | 无功能影响（仅日志刷屏） |

`tools/build_fw.sh` 会**自动检测并应用**（幂等）。手动等价：

```sh
MCUBOOT=~/zephyrproject/bootloader/mcuboot
PATCH=$PWD/mcuboot_patches/0001-cioszhong-mcuboot-local-fixes.patch

# 检查是否已应用
git -C $MCUBOOT apply --reverse --check "$PATCH" && echo "已应用" || git -C $MCUBOOT apply "$PATCH"
```

> 补丁应与源码树完全一致：`git -C $MCUBOOT status --porcelain` 应只列出这 5 个文件。

---

## 3. 构建 boot + app

### 3.1 必须保持一致的常量

改任意一项都会导致升级失败或不生效，改动前请读 §8。

| 项目 | 值 | 定义位置 |
|---|---|---|
| 分区：boot | 128 KB @ 0x08000000 | `application/boards/arm/cioszhong_psu/cioszhong_psu_stm32h745xx_m7.dts` |
| 分区：slot0 | 512 KB @ 0x08020000 | 同上 |
| 分区：slot1 | 512 KB @ 0x08100000（bank2） | 同上 |
| 升级模式 | **OVERWRITE_ONLY**（`CONFIG_BOOT_UPGRADE_ONLY=y`） | `mcuboot_swap_offset.conf` |
| 签名算法 | **RSA-2048** | MCUboot `CONFIG_BOOT_SIGNATURE_TYPE_RSA` |
| 签名密钥 | `bootloader/mcuboot/root-rsa-2048.pem` | 同上 |
| 镜像头大小 | `0x400` | `tools/build_fw.sh` |
| 对齐 | `8` | 同上 |
| imgtool `--slot-size` | `0x80000`（512 KB = slot0） | 同上 |
| 镜像版本 | = `application/Kconfig.project` → `CIOS_ZHONG_FW_VERSION` | 同上（脚本自动读取） |
| **DFU 写入偏移** | **`0x0`**（OVERWRITE_ONLY，从 slot1 起始写） | `application/src/terminal/uart_cmd.c` → `DFU_SECONDARY_IMG_OFFSET` |
| DFU 块大小 | 512 B / 行 | 同上 → `DFU_BLOCK_MAX` |
| 板级 target | `cioszhong_psu/stm32h745xx/m7` | `application/boards/arm/cioszhong_psu/`（由 `application/CMakeLists.txt` 自动注册为 `BOARD_ROOT`） |
| 串口 | USART1，PB14(TX)/PB15(RX)，115200 8N1 | `application/boards/.../cioszhong_psu.dtsi` |

**硬约束**：app 必须用**与 bootloader 构建时相同的密钥**签名。

### 3.2 推荐：一键脚本

```sh
./tools/build_fw.sh                 # app + 签名（日常开发循环）
./tools/build_fw.sh --all           # bootloader + app + 签名（新设备/改分区/改密钥）
./tools/build_fw.sh --boot          # 只重建 bootloader
./tools/build_fw.sh --pristine      # 先清 build/，全量重建
./tools/build_fw.sh --version 0.2.5 # 临时覆盖镜像版本（默认取 Kconfig）
```

脚本流程：
1. 自动定位 `ZEPHYR_BASE` / SDK / mcuboot（环境变量无效也会回退常见路径并校验）
2. `--all|--boot`：检测并应用 mcuboot 补丁
3. 构建 bootloader：`EXTRA_CONF_FILE=mcuboot_swap_offset.conf` + `DTC_OVERLAY_FILE=application/mcuboot_wdi.overlay`
4. 构建 app（`-b cioszhong_psu/stm32h745xx/m7`）
5. 从 `application/Kconfig.project` 读取版本，用**同一版本**签名
6. 自检 magic / header / 版本 / 是否放得进 slot0

预期输出：
```
== project      : /path/to/ciosZhong_ePSU
== ZEPHYR_BASE  : /path/to/zephyrproject/zephyr
== mcuboot      : /path/to/zephyrproject/bootloader/mcuboot
== signing key  : /path/to/zephyrproject/bootloader/mcuboot/root-rsa-2048.pem
== image version: 0.2.4
== mcuboot patch: already applied
== building MCUboot ==
== bootloader: build-mcuboot/zephyr/zephyr.bin
== building app ==
-- Firmware version: 0.2.4
== signing ==
OK  build/zephyr/zephyr.signed.bin: version=0.2.4+0 hdr=0x400 payload=0x12b24 total=0x13074 (slot0 0x80000)

== artifacts ==
  boot : build-mcuboot/zephyr/zephyr.bin   -> flash @ 0x08000000 (ST-Link)
  app  : build/zephyr/zephyr.signed.bin    -> flash @ 0x08020000 (ST-Link)
                                          -> tools/psu_dfu.py (serial DFU)
```

### 3.3 手动分步（等价命令）

```sh
export ZEPHYR_BASE=~/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
MCUBOOT=$ZEPHYR_BASE/../bootloader/mcuboot
cd ~/project/03_siemens/ciosZhong_ePSU

# ---- 1) bootloader（仅改板级 dts / 分区 / 补丁 / 密钥时才需重建）----
west build -d build-mcuboot -p always -b cioszhong_psu/stm32h745xx/m7 \
  $MCUBOOT/boot/zephyr -- \
  "-DUSER_CACHE_DIR=$PWD/build-mcuboot/.zcache" \
  "-DEXTRA_CONF_FILE=$PWD/mcuboot_swap_offset.conf" \
  "-DDTC_OVERLAY_FILE=$PWD/application/mcuboot_wdi.overlay"
# 产物 build-mcuboot/zephyr/zephyr.bin（实测约 33.4 KB / 128 KB）

# ---- 2) app ----
west build -d build -b cioszhong_psu/stm32h745xx/m7 application -- \
  "-DUSER_CACHE_DIR=$PWD/build/.zephyr-cache"
# 产物 build/zephyr/zephyr.elf / .bin / .hex

# ---- 3) 签名（app 构建【不会】自动签名，必须显式执行）----
VERSION=$(sed -n '/^config CIOS_ZHONG_FW_VERSION/,/^config /p' application/Kconfig.project \
          | sed -n 's/^[[:space:]]*default[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)
python3 $MCUBOOT/scripts/imgtool.py sign \
  --key $MCUBOOT/root-rsa-2048.pem \
  --header-size 0x400 --align 8 \
  --version "$VERSION" \
  --slot-size 0x80000 \
  build/zephyr/zephyr.bin build/zephyr/zephyr.signed.bin

# ---- 4) 自检 ----
python3 tools/check_signed_image.py build/zephyr/zephyr.signed.bin "$VERSION"
```

### 3.4 ⚠️ 四个最容易踩的坑

1. **app 构建不会自动签名。**
   `application/prj.conf` 里 `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE=""`，`zephyr.signed.bin`
   **不会**由构建产生，必须显式跑 imgtool。若误把 `zephyr.bin` 上传，MCUboot 校验失败 →
   启动旧固件（现象：`rebooting` 后版本没变）。

2. **版本号两处，必须一致。**
   显示/上报用 `application/Kconfig.project` → `CIOS_ZHONG_FW_VERSION`；
   写入镜像头用 imgtool `--version`。`tools/build_fw.sh` 已自动同步。
   （`application/CMakeLists.txt` 现在也从 Kconfig 读取并打印，不会再出现历史硬编码值。）

3. **签名密钥必须与 bootloader 一致。**
   换密钥要**同时**重建 bootloader 并全片重烧。

4. **bootloader 构建必须带 `EXTRA_CONF_FILE` + `DTC_OVERLAY_FILE`。**
   漏掉 conf → 编译出 SWAP 模式（与 app 代码里 `DFU_SECONDARY_IMG_OFFSET=0` 不符，
   升级后不启动）；漏掉 overlay → 外部看门狗引脚节点缺失（`DT_NODE_EXISTS` 为假，
   补丁里的 feed 代码被编译掉）。

---

## 4. 烧录 boot + app（ST-Link）

首次空片 / 全片恢复：

```sh
./flash_recover_mcuboot.sh          # boot + app，各自动重试 6 次
./flash_recover_mcuboot.sh boot     # 只烧 boot @ 0x08000000
./flash_recover_mcuboot.sh app      # 只烧 app  @ 0x08020000
```

手动 openocd：
```sh
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" \
  -c "init" -c "reset halt" -c "halt" \
  -c "program build/zephyr/zephyr.signed.bin 0x08020000 verify reset exit"
```

烧完：**BOOT0 拉低 → 断电重上电**，串口应输出：
```
===== CiosZhong PSU =====
  App  v0.2.4
  Boot v1.0.0 (MCUboot)
PSU CMD: ready (help for commands)
```

### 烧录失败处理

`ST-Link 连不上` / `timed out while waiting for target halted` 多为运行态固件 +
MAX6703A 看门狗干扰，按顺序：

1. 拔 ST-Link USB ≥10 s，换口重插后重跑
2. `./flash_force.sh` —— 轮换 4 种连接/复位策略高频重试
3. 仍不行：**BOOT0 拉高 → 断电 ≥10 s → 上电（保持高）** 进 ROM 态 → 重烧
4. 诊断：`./tools/flash_diag.sh`（逐策略 PASS/FAIL + 结论）

---

## 5. 串口升级（上位机）

### 5.1 命令行

```sh
python3 tools/psu_dfu.py <port> build/zephyr/zephyr.signed.bin [选项]

# 示例
python3 tools/psu_dfu.py /dev/cu.usbserial-120 build/zephyr/zephyr.signed.bin
python3 tools/psu_dfu.py /dev/cu.usbserial-120 build/zephyr/zephyr.signed.bin \
        --count 50 --stop-on-fail --interval 0.5     # 连续 50 次可靠性测试

选项：
  --count N        连续升级 N 次（默认 1）
  --stop-on-fail   任一次失败立即停止
  --interval SEC   每轮之间额外等待秒数（默认 0）
  --quiet          减少打印
```

### 5.2 图形界面（可选）

```sh
./启动升级工具.command            # 双击运行；或把 .signed.bin 拖到脚本图标上
python3 tools/psu_dfu_gui.py      # 需 pyserial + tkinterdnd2
```

### 5.3 升级协议（行式 ASCII / hex）

| 主机发送 | 设备应答 | 说明 |
|---|---|---|
| `dfu` | `DFU ok, slot1 erased, send: size <hex>` | 擦除 slot1；失败 → `ERR dfu: open/erase slot1 fail` |
| `size <hexlen>` | `OK, send: data <hex>` | 总长度；须满足 `0 < size <= slot1 大小` |
| `data <off> <hex…>` | `ACK <next_off>` | 一块 ≤512 B；`off` 必须等于已接收长度 |
| （最后一块） | `… rebooting …` | 触发 `boot_request_upgrade()` 并复位 |
| 非法 / 越界 | `ERR dfu write` / `ERR dfu data` / `ERR dfu state` | 主机重发同一块 |

**偏移语义**：主机侧 `off` 从 **0** 计；固件写入时统一加 `DFU_SECONDARY_IMG_OFFSET`。
OVERWRITE_ONLY 下该值为 **0**，镜像直接写在 slot1 起始。

### 5.4 成功 / 失败判据

成功：
```
firmware: build/zephyr/zephyr.signed.bin (77940 bytes)
port: /dev/cu.usbserial-120   rounds: 1
===== round 1/1 =====
  sent 77940/77940
  [DONE] DFU done 77940/77940, rebooting...
  [OK] round 1 succeeded, app is up
```
重启后横幅 `App v0.2.x` 应变为新版本。

| 失败现象 | 原因 |
|---|---|
| `[FAIL] app did not enter DFU mode` | 串口/波特率不对，或 app 没在跑（停在 MCUboot） |
| `rebooting` 后死机 / 串口再无输出 | **boot 区被擦空**（见 §6.5），需 ST-Link 重烧 |
| `[FAIL] bad size response` | 上传的不是签名镜像（用了 `zephyr.bin`），或长度非法 |
| `[FAIL] block 0x… failed repeatedly` | 串口丢字节（换线/降速/关掉占用串口的程序） |
| `rebooting` 后版本没变 | 镜像密钥不对，或 boot 与 app 分区布局不一致 |
| 重启后回到旧版本 | `CONFIG_BOOT_VALIDATE_SLOT0` 校验失败（签名 / header / 版本不匹配） |

---

## 6. 交付前校验清单

- [ ] `./tools/build_fw.sh --all` 输出 `OK … version=<Kconfig 版本>` 且
      `OK: bootloader and app agree with the expected partition layout`，无 ERROR
- [ ] 升级时串口回 `DFU ok, slot1 @0x100000 size 512K erased`（地址必须是 0x100000）
- [ ] `build/zephyr/zephyr.signed.bin` 存在（**不是** `zephyr.bin`）
- [ ] `git -C <mcuboot> apply --reverse --check mcuboot_patches/0001-*.patch` 成功（补丁已应用）
- [ ] bootloader 与 app 使用**同一** `root-rsa-2048.pem`
- [ ] 分区表未改动（若改动：`--all` 重建 bootloader + ST-Link 全片重烧）
- [ ] 烧录后串口横幅正常（`PSU CMD: ready`）
- [ ] 串口升级后**横幅版本号确实变化**
- [ ] 连续升级 ≥10 次无失败：`--count 10 --stop-on-fail`

自动校验：
```sh
python3 tools/check_signed_image.py build/zephyr/zephyr.signed.bin 0.2.4
python3 tools/check_build_layout.py        # boot/app 分区视图是否一致
```

---

## 6.5 板子变砖恢复（boot 区被擦空）

### 症状

- 串口完全无输出，或升级时报 `app did not enter DFU mode`
- 复位后不启动，openocd 读到 `current mode: Handler HardFault, pc: 0xfffffffe`

### 诊断（只读，不写 flash）

```sh
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" -c "init" \
  -c "targets stm32h7x.cpu0" -c "reset halt" \
  -c "mdw 0x08000000 2"    `# boot 向量：应为 24007780 08001449` \
  -c "mdw 0x08020000 6"    `# slot0 头：magic 应为 96f3b83d` \
  -c "mdw 0x08100000 6" \
  -c "shutdown" 2>&1 | tail -20
```

判据：

| `0x08000000` 读到的值 | 含义 |
|---|---|
| `24007780 08001449` | MCUboot 正常 |
| `ffffffff ffffffff` | **boot 被擦空 → 需要恢复** |
| 其他 | 被别的东西覆盖 |

### 恢复步骤

```sh
openocd -f board/st_nucleo_h745zi.cfg \
  -c "reset_config srst_only" -c "adapter speed 950" -c "init" \
  -c "targets stm32h7x.cpu0" -c "reset halt" \
  -c "stm32h7x mass_erase 0" -c "stm32h7x mass_erase 1" \
  -c "program build-mcuboot/zephyr/zephyr.bin 0x08000000 verify" \
  -c "program build/zephyr/zephyr.signed.bin  0x08020000 verify" \
  -c "reset run" -c "shutdown"
```

或直接用脚本：`./flash_recover_mcuboot.sh`（它会重试并校验）。

### 为什么会发生

串口 DFU **本身只会擦 slot1**（`cmdDfuEnter` → `flash_area_erase(slot1)`，
512 KB @ 0x08100000），代码里没有任何路径会写 `0x08000000`。

但如果某个构建的 flash map 把 `slot1_partition` 解析成了别的位置，
`dfu` 就会去擦那个位置。观测到的被擦区域是 **0x08000000 起正好 128 KB**
—— 恰好等于 `boot_partition` 的大小，也就是“slot1 被解析成了 boot 分区”
这种错误映射的特征。

可能来源（都在**另一台设备的构建配置**里，不在代码里）：

1. MCUboot 构建时没带 `mcuboot_swap_offset.conf` / `mcuboot_wdi.overlay`，
   或用错 `BOARD_ROOT`，导致它自己的 slot0/slot1 视图与 app 不一致；
   升级时 MCUboot 会先擦除它认为的 primary slot，擦错就等于擦掉自己。
2. app 构建时 board dts 与预期不一致，使 `FIXED_PARTITION_ID(slot1_partition)`
   指到了 boot 分区。

### 已加入的防护

| 防护 | 位置 |
|---|---|
| **编译期断言**：slot1 不得与 boot 分区重叠 | `uart_cmd.c` `BUILD_ASSERT(...)` |
| **运行期拒绝**：`dfu` 前若 slot1 落在 flash 起始处，直接拒绝擦除并回 `ERR dfu: slot1 maps to flash start` | `cmdDfuEnter()` |
| **把真实地址打进日志**：`DFU ok, slot1 @0x100000 size 512K erased, send: size <hex>` | `cmdDfuEnter()` |
| **构建布局校验**：对比 boot/app 两个 `zephyr.dts` 的分区 + MCUboot 模式，不一致就让构建失败 | `tools/check_build_layout.py`（已接入 `build_fw.sh`） |

> 所以在另一台设备上重建后，先看 `./tools/build_fw.sh` 是否输出
> `OK: bootloader and app agree with the expected partition layout`，
> 再看升级时串口里 `slot1 @0x...` 的地址对不对。

---

## 7. 变更指引（改什么要重建什么）

| 变更 | 重建 bootloader？ | 全片重烧？ | 备注 |
|---|---|---|---|
| 只改 app 业务代码 | 否 | 否 | `./tools/build_fw.sh` + 串口 DFU |
| 改 `CIOS_ZHONG_FW_VERSION` | 否 | 否 | 脚本自动同步到镜像头 |
| 改分区布局（dts） | **是** | **是** | `--all` + ST-Link |
| 改签名密钥 | **是** | **是** | boot 与 app 必须同一密钥 |
| 改升级模式（swap ↔ overwrite） | **是** | **是** | 同步改 `DFU_SECONDARY_IMG_OFFSET` |
| 更新 mcuboot 补丁 | **是** | 建议是 | 先 `--reverse` 还原旧补丁再应用新补丁 |
| 更新 Zephyr 版本 | 建议是 | 建议是 | 需重新验证分区/CMake/驱动兼容性 |

---

## 8. 已知注意事项 / 待改进

| 项 | 现状 | 影响 / 建议 |
|---|---|---|
| 签名密钥 | 使用 MCUboot 自带 `root-rsa-2048.pem`，构建时打印 `WARNING: Using default MCUboot signing key file, this file is for debug use` | **任何人都能签出被接受的固件**。量产前应换项目自有密钥并同步重建 bootloader |
| 外部看门狗 feed | `build-mcuboot/.config` 中 `CONFIG_BOOT_WATCHDOG_FEED` **未使能**，补丁里的 WDI toggle 被编译掉 | 512 KB 覆盖通常快于 1.6 s 超时（实测可用）；若 slot 变大或 flash 变慢，需打开该选项，否则可能半写 |
| 回滚 | OVERWRITE_ONLY **无自动回滚** | 失败可再 DFU 重刷；需要回滚则改回 swap 方案并重建 bootloader |
| app 自动签名 | 未开启（`MCUBOOT_SIGNATURE_KEY_FILE=""`） | 用脚本规避；如要"构建即签名"，设 `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE` + `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` |
| `.venv` | 工程内自带（Python 3.14） | 仅用于 west/imgtool；上位机工具用系统 python3 + pyserial 亦可 |

---

## 9. 文件清单

| 文件 | 作用 |
|---|---|
| `tools/build_fw.sh` | **一键构建 boot + app + 签名 + 自检**（本文核心） |
| `tools/check_signed_image.py` | 校验签名镜像（magic / header / 版本 / 大小） |
| `tools/check_build_layout.py` | 校验 boot/app 分区视图一致 + MCUboot 升级模式 |
| `tools/psu_dfu.py` | 串口升级命令行工具 |
| `tools/psu_dfu_gui.py` | 串口升级图形界面 |
| `tools/flash_diag.sh` | SWD 连接诊断 |
| `flash_recover_mcuboot.sh` | 首次 / 恢复烧录（boot + app，ST-Link） |
| `flash_force.sh` | 复位循环 / 看门狗干扰下的强制烧录 |
| `flash_stlink.sh` | 烧 .hex（bring-up） |
| `mcuboot_swap_offset.conf` | MCUboot 配置（OVERWRITE_ONLY） |
| `application/mcuboot_wdi.overlay` | 给 MCUboot 定义外部看门狗 WDI 引脚 |
| `mcuboot_patches/0001-cioszhong-mcuboot-local-fixes.patch` | MCUboot 本地补丁（cache 失效 + 看门狗 feed） |
| `application/prj.conf` | app 配置（MCUboot / IMG_MANAGER / STREAM_FLASH） |
| `application/Kconfig.project` | **固件版本号唯一来源** |
| `application/CMakeLists.txt` | 注册 `BOARD_ROOT`、从 Kconfig 读取版本并打印 |
| `application/src/terminal/uart_cmd.c` | DFU 协议实现（`DFU_*` 常量） |
| `application/boards/arm/cioszhong_psu/*.dts*` | 分区布局（boot / slot0 / slot1） |

---

## 10. 实测记录（本机）

```
Zephyr v4.4.0-rc3-2930-gf48ca153af1 / west 1.5.0 / MCUboot v2.4.0-41-ge36d3c8d / SDK 1.0.1

$ ./tools/build_fw.sh --all
== image version: 0.2.4
== mcuboot patch: already applied
== bootloader: build-mcuboot/zephyr/zephyr.bin           (34232 B / 128 KB)
-- Firmware version: 0.2.4
OK  build/zephyr/zephyr.signed.bin: version=0.2.4+0 hdr=0x400 payload=0x12b24 total=0x13074 (slot0 0x80000)

$ python3 tools/psu_dfu.py <port> build/zephyr/zephyr.signed.bin
  sent 77940/77940
  [DONE] DFU done 77940/77940, rebooting...
  [OK] round 1 succeeded, app is up
```
