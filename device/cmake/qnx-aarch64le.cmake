# CMake toolchain for QNX SDP 8.0 -> Raspberry Pi 5 (aarch64le).
# Prerequisite: `source ~/qnx800/qnxsdp-env.sh` (sets QNX_HOST and QNX_TARGET, puts qcc on PATH).
#
# Third-party libs (libwebsockets 4.5.8, sqlite 3.51.0, and later mediapipe/tflite/OpenCV) come
# from https://oss.qnx.com. Install/extract them into a prefix and pass it with
#   -DBRIM_QNX_DEPS_PREFIX=/path/to/prefix   (or env BRIM_QNX_DEPS_PREFIX)
# TODO(qnx-check): confirm the oss.qnx.com package layout (include/, lib/ vs aarch64le/usr/lib)
# and adjust CMAKE_FIND_ROOT_PATH below if find_library() can't see them.

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_VERSION 8.0.0)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{QNX_TARGET} OR NOT DEFINED ENV{QNX_HOST})
  message(FATAL_ERROR "QNX_TARGET/QNX_HOST not set. Run: source <sdp>/qnxsdp-env.sh")
endif()

set(QNX_TARGET $ENV{QNX_TARGET})
set(QNX_HOST $ENV{QNX_HOST})
set(_qnx_arch gcc_ntoaarch64le)

set(CMAKE_C_COMPILER ${QNX_HOST}/usr/bin/qcc)
set(CMAKE_C_COMPILER_TARGET ${_qnx_arch})
set(CMAKE_CXX_COMPILER ${QNX_HOST}/usr/bin/q++)
set(CMAKE_CXX_COMPILER_TARGET ${_qnx_arch})

set(CMAKE_SYSROOT ${QNX_TARGET})

if(NOT BRIM_QNX_DEPS_PREFIX AND DEFINED ENV{BRIM_QNX_DEPS_PREFIX})
  set(BRIM_QNX_DEPS_PREFIX $ENV{BRIM_QNX_DEPS_PREFIX})
endif()

set(CMAKE_FIND_ROOT_PATH ${QNX_TARGET} ${QNX_TARGET}/aarch64le ${BRIM_QNX_DEPS_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
