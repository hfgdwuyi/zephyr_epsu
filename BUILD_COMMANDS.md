# CiosZhong ePSU — 构建命令速查（E1 调试, 2026-09-08）

环境变量（每次新终端先执行）：
```sh
export ZEPHYR_BASE=/Users/mac/project/02_zephyr/zephyrproject/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=/Users/mac/project/02_zephyr/zephyr-sdk-1.0.1
cd /Users/mac/project/03_siemens/ciosZhong_ePSU
```

## 构建 MCUboot bootloader（v1.0.0）
```sh
.venv/bin/west build -d build-mcuboot -p always \
  -b cioszhong_psu/stm32h745xx/m7 \
  /Users/mac/project/02_zephyr/zephyrproject/bootloader/mcuboot/boot/zephyr \
  -- -DBOARD_ROOT=$PWD/application \
     -DUSER_CACHE_DIR=$PWD/build-mcuboot/.zcache \
     "-DEXTRA_CONF_FILE=$PWD/mcuboot_swap_offset.conf"
# 注意: EXTRA_CONF_FILE 必须用绝对路径（cmake 在 MCUboot 源码目录解析相对路径）
# 产物: build-mcuboot/zephyr/zephyr.bin
```

## 构建 App 固件（v0.2.0，完整模式）
版本号在 application/Kconfig.project 的 CIOS_ZHONG_FW_VERSION。
```sh
.venv/bin/west build -d build application \
  -- -DUSER_CACHE_DIR=$PWD/build/.zephyr-cache
# 产物: build/zephyr/zephyr.bin
```

## 签名 App（生成串口升级/烧 slot0 用镜像）
```sh
.venv/bin/python \
  /Users/mac/project/02_zephyr/zephyrproject/bootloader/mcuboot/scripts/imgtool.py \
  sign --key /Users/mac/project/02_zephyr/zephyrproject/bootloader/mcuboot/root-rsa-2048.pem \
  --header-size 0x400 --align 8 --version 0.2.0 --slot-size 0x80000 \
  build/zephyr/zephyr.bin build/zephyr/zephyr.signed.bin
# 产物: build/zephyr/zephyr.signed.bin
# 注意: --version 必须与 Kconfig.project 的 CIOS_ZHONG_FW_VERSION 一致
```

## 产物用途
| 产物 | 用途 |
|---|---|
| build-mcuboot/zephyr/zephyr.bin | boot，烧 0x08000000 |
| build/zephyr/zephyr.signed.bin | app，烧 0x08020000 / 串口 DFU 上传 |
