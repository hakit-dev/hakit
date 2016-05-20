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
DISTRO = debian

export CROSS_COMPILE := arm-linux-gnueabihf-
#export CROSS_PATH := /usr
export CROSS_ROOT_PATH := /usr/arm-linux-gnueabihf
