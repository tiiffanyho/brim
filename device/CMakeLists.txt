cmake_minimum_required(VERSION 3.21)
project(brim_device VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BRIM_IS_QNX OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
  set(BRIM_IS_QNX ON)
endif()

# IPC backend: QNX message passing on the Pi, Unix datagram sockets on the laptop.
if(BRIM_IS_QNX)
  set(BRIM_IPC_BACKEND "qnx" CACHE STRING "IPC backend: qnx or uds")
else()
  set(BRIM_IPC_BACKEND "uds" CACHE STRING "IPC backend: qnx or uds")
endif()
set_property(CACHE BRIM_IPC_BACKEND PROPERTY STRINGS qnx uds)
option(BRIM_BUILD_TESTS "Build unit tests" ON)
option(BRIM_BUILD_APP_SERVER "Build app_server (needs libwebsockets + sqlite3)" ON)
# TFLite C API (tflite-runtime 2.21.0 from oss.qnx.com) for the person/object detector. On by default
# for QNX. On the laptop it is off: brim runs --fake with the stub detector there.
option(BRIM_WITH_TFLITE "Link the TFLite C API for real detection" ${BRIM_IS_QNX})

set(BRIM_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(BRIM_SHARED_GEN ${BRIM_REPO_ROOT}/shared/gen)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------- common + ipc
add_library(brim_common STATIC
  src/common/json.cpp
  src/common/log.cpp
  src/common/timing.cpp
  src/common/config.cpp
  src/common/sysinfo.cpp
)
target_include_directories(brim_common PUBLIC src ${BRIM_SHARED_GEN})
target_link_libraries(brim_common PUBLIC Threads::Threads)

if(BRIM_IPC_BACKEND STREQUAL "qnx")
  set(BRIM_IPC_SRC src/ipc/bus_qnx.cpp)
else()
  set(BRIM_IPC_SRC src/ipc/bus_uds.cpp)
endif()
add_library(brim_ipc STATIC src/ipc/event_bus.cpp ${BRIM_IPC_SRC})
target_link_libraries(brim_ipc PUBLIC brim_common)
if(BRIM_IS_QNX)
  target_link_libraries(brim_ipc PUBLIC socket)  # libsocket on QNX
endif()
message(STATUS "Brim IPC backend: ${BRIM_IPC_BACKEND}")

# ---------------------------------------------------------------- pipeline modules
add_library(brim_pipeline STATIC
  src/capture/camera.cpp
  src/detect/detector.cpp
  src/detect/tflite_detector.cpp
  src/detect/faces.cpp
  src/safety/safety_monitor.cpp
  src/nav/nav.cpp
  src/audio/speak_queue.cpp
)
target_link_libraries(brim_pipeline PUBLIC brim_common)
# QNX Camera Framework (libcamapi) for the real webcam. Ships with the QNX 8.0 Pi image
# (/usr/include/camera/camera_api.h, /usr/lib/libcamapi.so).
if(BRIM_IS_QNX)
  find_path(CAMAPI_INCLUDE_DIR camera/camera_api.h PATH_SUFFIXES include usr/include)
  find_library(CAMAPI_LIB NAMES camapi PATH_SUFFIXES lib usr/lib)
  if(CAMAPI_INCLUDE_DIR AND CAMAPI_LIB)
    target_sources(brim_pipeline PRIVATE src/capture/camera_qnx.cpp)
    target_include_directories(brim_pipeline PRIVATE ${CAMAPI_INCLUDE_DIR})
    target_link_libraries(brim_pipeline PUBLIC ${CAMAPI_LIB})
    target_compile_definitions(brim_pipeline PRIVATE BRIM_WITH_CAMAPI=1)
    message(STATUS "Camera: libcamapi ${CAMAPI_LIB}")
  else()
    message(WARNING "libcamapi not found: brim will use the fake camera on QNX")
  endif()
endif()
if(BRIM_WITH_TFLITE)
  # TODO(qnx-check): confirm the header and library names the tflite-runtime-dev package installs:
  #   on the Pi `apk info -L tflite-runtime-dev | grep -E 'c_api.h|\.so|\.a$'`. Extract the same
  #   package into BRIM_QNX_DEPS_PREFIX for cross-compiling.
  find_path(TFLITE_INCLUDE_DIR tensorflow/lite/c/c_api.h PATH_SUFFIXES include usr/include)
  find_library(TFLITE_C_LIB NAMES tensorflowlite_c tensorflow-lite tensorflowlite PATH_SUFFIXES lib usr/lib)
  if(TFLITE_INCLUDE_DIR AND TFLITE_C_LIB)
    target_include_directories(brim_pipeline PRIVATE ${TFLITE_INCLUDE_DIR})
    target_link_libraries(brim_pipeline PUBLIC ${TFLITE_C_LIB})
    target_compile_definitions(brim_pipeline PRIVATE BRIM_WITH_TFLITE=1)
    message(STATUS "TFLite C API: ${TFLITE_INCLUDE_DIR}, ${TFLITE_C_LIB}")
  else()
    # Not fatal, so the rest of the device still builds; brim then reports detection as a spoken fault.
    message(WARNING "BRIM_WITH_TFLITE=ON but tensorflow/lite/c/c_api.h or the TFLite library was not found "
                    "(set BRIM_QNX_DEPS_PREFIX). Building without detection.")
  endif()
endif()

# Face recognition of enrolled people (src/detect/faces.cpp): OpenCV 4.12 from oss.qnx.com,
# FaceDetectorYN + FaceRecognizerSF. Without it brim builds and runs, and nobody is ever named.
option(BRIM_WITH_OPENCV "Link OpenCV for face recognition of enrolled people" ${BRIM_IS_QNX})
if(BRIM_WITH_OPENCV)
  # TODO(qnx-check): confirm the OpenCV 4.12 package from oss.qnx.com ships objdetect + dnn and an
  #   OpenCVConfig.cmake (else point OpenCV_DIR at it under BRIM_QNX_DEPS_PREFIX).
  find_package(OpenCV 4.5.4 QUIET COMPONENTS core imgproc objdetect dnn)
  if(OpenCV_FOUND)
    target_include_directories(brim_pipeline PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(brim_pipeline PUBLIC ${OpenCV_LIBS})
    target_compile_definitions(brim_pipeline PRIVATE BRIM_WITH_OPENCV=1)
    message(STATUS "OpenCV ${OpenCV_VERSION}: face recognition available")
  else()
    message(WARNING "BRIM_WITH_OPENCV=ON but OpenCV (core imgproc objdetect dnn) was not found. "
                    "Building without face recognition: nobody will be named.")
  endif()
endif()

add_library(brim_dashboard STATIC src/dashboard/dashboard.cpp)
target_link_libraries(brim_dashboard PUBLIC brim_common)

# ---------------------------------------------------------------- executables
add_executable(brim src/brim_main.cpp)
target_link_libraries(brim PRIVATE brim_pipeline brim_ipc)

# Laptop test tool: brim's decisions for detector rows on stdin (scripts/laptop/brim_test.py live).
if(NOT BRIM_IS_QNX)
  add_executable(decide_probe tools/decide_probe.cpp)
  target_link_libraries(decide_probe PRIVATE brim_pipeline)
endif()

add_executable(supervisor
  src/supervisor/supervisor_main.cpp
  src/supervisor/child.cpp
  src/supervisor/watchdog.cpp
)
target_link_libraries(supervisor PRIVATE brim_ipc)

# ---------------------------------------------------------------- app_server
if(BRIM_BUILD_APP_SERVER)
  # SQLite 3.51.0 from oss.qnx.com on the Pi; system/Homebrew sqlite on the laptop.
  find_package(SQLite3 REQUIRED)
  if(TARGET SQLite3::SQLite3)
    set(BRIM_SQLITE_TARGET SQLite3::SQLite3)   # CMake >= 4.x
  else()
    set(BRIM_SQLITE_TARGET SQLite::SQLite3)    # older CMake
  endif()
  add_library(brim_db STATIC src/app_server/db.cpp)
  target_link_libraries(brim_db PUBLIC brim_common ${BRIM_SQLITE_TARGET})

  # REST router + relay: no libwebsockets dependency, so they are unit-testable.
  add_library(brim_api STATIC src/app_server/api.cpp src/app_server/pairing.cpp src/app_server/relay.cpp)
  target_link_libraries(brim_api PUBLIC brim_db brim_ipc)

  # libwebsockets 4.5.8 from oss.qnx.com on the Pi; Homebrew/apt on the laptop.
  find_package(libwebsockets CONFIG QUIET)
  if(TARGET websockets_shared)
    set(BRIM_LWS_TARGET websockets_shared)
  elseif(TARGET websockets)
    set(BRIM_LWS_TARGET websockets)
  else()
    find_path(LWS_INCLUDE_DIR libwebsockets.h REQUIRED)
    find_library(LWS_LIBRARY websockets REQUIRED)
    add_library(brim_lws INTERFACE)
    target_include_directories(brim_lws INTERFACE ${LWS_INCLUDE_DIR})
    target_link_libraries(brim_lws INTERFACE ${LWS_LIBRARY})
    set(BRIM_LWS_TARGET brim_lws)
  endif()
  message(STATUS "libwebsockets target: ${BRIM_LWS_TARGET}")

  add_executable(app_server src/app_server/app_server_main.cpp)
  target_link_libraries(app_server PRIVATE brim_api brim_dashboard ${BRIM_LWS_TARGET})
endif()

# ---------------------------------------------------------------- tests
if(BRIM_BUILD_TESTS)
  enable_testing()
  function(brim_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})
    target_compile_definitions(${name} PRIVATE BRIM_REPO_ROOT="${BRIM_REPO_ROOT}")
    add_test(NAME ${name} COMMAND ${name})
  endfunction()
  brim_test(test_schema brim_common)
  brim_test(test_speak_queue brim_pipeline)
  brim_test(test_privacy brim_pipeline)
  brim_test(test_detect brim_pipeline)
  brim_test(test_nav brim_pipeline)
  brim_test(test_safety brim_pipeline)
  brim_test(test_faces brim_pipeline)
  if(BRIM_BUILD_APP_SERVER)
    brim_test(test_db brim_db)
    brim_test(test_api brim_api)
  endif()
endif()
