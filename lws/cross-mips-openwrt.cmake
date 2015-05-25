#
# CMake Toolchain file for crosscompiling on ARM.
#
# This can be used when running cmake in the following way:
#  cd build/
#  export 
#  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cross-mips-openwrt.cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr -DLWS_WITHOUT_EXTENSIONS=1 -DLWS_WITH_SSL=0
#  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cross-mips-openwrt.cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr -DLWS_WITHOUT_EXTENSIONS=1 -DLWS_WITH_SSL=0 -DLWS_WITH_ZLIB=0
#

set(CROSS_PATH /home/giroudon/Domo/OpenWrt-SDK-ar71xx-for-linux-i486-gcc-4.6-linaro_uClibc-0.9.33.2/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2)

# Target operating system name.
set(CMAKE_SYSTEM_NAME Linux)

# Name of C compiler.
set(CMAKE_C_COMPILER "${CROSS_PATH}/bin/mips-openwrt-linux-gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PATH}/bin/mips-openwrt-linux-g++")

# Where to look for the target environment. (More paths can be added here)
set(CMAKE_FIND_ROOT_PATH "${CROSS_PATH}")

# Adjust the default behavior of the FIND_XXX() commands:
# search programs in the host environment only.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search headers and libraries in the target environment only.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
