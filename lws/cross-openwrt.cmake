#
# CMake Toolchain file for crosscompiling with OpenWRT toolchain.
#
# This can be used when running cmake in the following way:
#  cd build/
#  export 
#  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cross-openwrt.cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr -DLWS_WITHOUT_EXTENSIONS=1 -DLWS_WITH_SSL=0
#  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cross-openwrt.cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr -DLWS_WITHOUT_EXTENSIONS=1 -DLWS_WITH_SSL=0 -DLWS_WITH_ZLIB=0
#

set(CROSS_PATH $ENV{CROSS_PATH})
set(CROSS_ROOT_PATH $ENV{CROSS_ROOT_PATH})
set(CROSS_COMPILE $ENV{CROSS_COMPILE})

# Target operating system name.
set(CMAKE_SYSTEM_NAME Linux)

# Name of C compiler.
set(CMAKE_C_COMPILER "${CROSS_PATH}/bin/$ENV{CROSS_COMPILE}gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PATH}/bin/$ENV{CROSS_COMPILE}g++")

# Where to look for the target environment. (More paths can be added here)
set(CMAKE_FIND_ROOT_PATH "${CROSS_PATH}")
if(CROSS_ROOT_PATH)
list(APPEND CMAKE_FIND_ROOT_PATH "${CROSS_ROOT_PATH}")
endif(CROSS_ROOT_PATH)

# Adjust the default behavior of the FIND_XXX() commands:
# search programs in the host environment only.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search headers and libraries in the target environment only.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
