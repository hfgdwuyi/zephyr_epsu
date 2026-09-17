# CiosZhong ePSU — 构建命令速查

> **完整说明见 [`SERIAL_DFU_GUIDE.md`](SERIAL_DFU_GUIDE.md)**
> （新设备环境搭建 → boot/app 重建 → 烧录 → 串口升级）。
> 本文只列最常用的命令。

## 推荐：一键脚本

```sh
./tools/build_fw.sh                 # app + 签名（日常开发）
./tools/build_fw.sh --all           # bootloader + app + 签名（新设备/改分区/改密钥）
./tools/build_fw.sh --boot          # 只重建 bootloader
./tools/build_fw.sh --pristine      # 清 build/ 后全量重建
./tools/build_fw.sh --version 0.2.5 # 临时覆盖镜像版本
```

脚本自动完成：定位 `ZEPHYR_BASE`/SDK/mcuboot → 应用 mcuboot 补丁（幂等）→ 构建 →
**用 Kconfig 里的版本签名** → 自检镜像。

产物：

| 产物 | 用途 |
|---|---|
| `build-mcuboot/zephyr/zephyr.bin` | boot，烧 `0x08000000` |
| `build/zephyr/zephyr.signed.bin` | app，烧 `0x08020000` / 串口 DFU 上传 |
| `build/zephyr/zephyr.elf` / `.hex` | 调试 / openocd 直烧 |

## 环境变量（脚本会自动探测，手动构建时需要）

```sh
export ZEPHYR_BASE=~/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
export MCUBOOT=$ZEPHYR_BASE/../bootloader/mcuboot     # 便于下面引用
cd ~/project/03_siemens/ciosZhong_ePSU
```

## 手动构建（脚本的等价命令）

```sh
# 1) bootloader（改板级 dts/分区/补丁/密钥 时）
west build -d build-mcuboot -p always -b cioszhong_psu/stm32h745xx/m7 \
  $MCUBOOT/boot/zephyr -- \
  "-DUSER_CACHE_DIR=$PWD/build-mcuboot/.zcache" \
  "-DEXTRA_CONF_FILE=$PWD/mcuboot_swap_offset.conf" \
  "-DDTC_OVERLAY_FILE=$PWD/application/mcuboot_wdi.overlay"

# 2) app
west build -d build -b cioszhong_psu/stm32h745xx/m7 application -- \
  "-DUSER_CACHE_DIR=$PWD/build/.zephyr-cache"

# 3) 签名（app 构建【不会】自动签名！版本号取自 application/Kconfig.project）
python3 $MCUBOOT/scripts/imgtool.py sign \
  --key $MCUBOOT/root-rsa-2048.pem \
  --header-size 0x400 --align 8 --version 0.2.4 --slot-size 0x80000 \
  build/zephyr/zephyr.bin build/zephyr/zephyr.signed.bin

# 4) 自检
python3 tools/check_signed_image.py build/zephyr/zephyr.signed.bin 0.2.4
```

> 版本号只改一处：`application/Kconfig.project` → `CIOS_ZHONG_FW_VERSION`。
> 手动签名时 `--version` 必须与它一致（脚本会自动读取，不会写错）。

## 烧录 / 升级

```sh
./flash_recover_mcuboot.sh                                   # 首次：ST-Link 烧 boot + app
python3 tools/psu_dfu.py <port> build/zephyr/zephyr.signed.bin   # 之后：串口升级
```
