#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# Target definition: TP-Link MR3020 or WDR4300
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
ARCH = ar71xx
OS = linux
DISTRO = openwrt

export CROSS_COMPILE := mips-openwrt-linux-

# To run 32-bits GCC on 64-bits architecture:
#  sudo dpkg --add-architecture i386
#  sudo apt-get update
#  sudo apt-get install libc6:i386 libstdc++6:i386 zlib1g:i386

export STAGING_DIR := $(realpath $(HAKIT_DIR)/..)/openwrt/OpenWrt-SDK-ar71xx-for-linux-i486-gcc-4.6-linaro_uClibc-0.9.33.2/staging_dir
export CROSS_PATH := $(STAGING_DIR)/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2
export CROSS_ROOT_PATH := $(STAGING_DIR)/target-mips_r2_uClibc-0.9.33.2
#export STAGING_DIR := $(realpath $(HAKIT_DIR)/..)/openwrt/OpenWrt-SDK-ar71xx-generic_gcc-5.3.0_musl-1.1.16.Linux-x86_64/staging_dir
#export CROSS_PATH := $(STAGING_DIR)/toolchain-mips_34kc_gcc-5.3.0_musl-1.1.16
#export CROSS_ROOT_PATH := $(CROSS_PATH)/mips-openwrt-linux
