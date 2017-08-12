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
DISTRO = openwrt

export CROSS_COMPILE := mips-openwrt-linux-

export STAGING_DIR := $(realpath $(HAKIT_DIR)/..)/openwrt/OpenWrt-SDK-ar71xx-for-linux-i486-gcc-4.6-linaro_uClibc-0.9.33.2/staging_dir
export CROSS_PATH := $(STAGING_DIR)/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2
export CROSS_ROOT_PATH := $(STAGING_DIR)/target-mips_r2_uClibc-0.9.33.2
