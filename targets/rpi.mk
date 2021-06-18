#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# Target definition: Raspberry Pi
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
ARCH = armhf
OS = linux
DISTRO = debian

export CROSS_COMPILE := arm-linux-gnueabihf-
export CROSS_PATH := $(realpath $(HAKIT_DIR)/..)/rpi/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64
export CROSS_ROOT_PATH := $(CROSS_PATH)/arm-linux-gnueabihf
