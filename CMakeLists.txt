cmake_minimum_required(VERSION 3.16)
project(PipeLLM)

# --- 配置变量 ---
set(SOC_VERSION "Ascend910B3" CACHE STRING "System on Chip")
set(ASCEND_CANN_PACKAGE_PATH "/usr/local/Ascend/ascend-toolkit/latest")
set(RUN_MODE "npu")

# --- 包含华为的编译脚本 ---
# 这行代码会定义 ascendg++ 的路径和 flags
include(cmake/npu_lib.cmake) 

# --- 编译 NPU 算子 (CTR.cpp) ---
# 利用 npu_lib.cmake 中的机制，或者我们手动定义针对 CTR.cpp 的规则
# 这里我们模拟参考工程的做法，显式调用 ascendg++ 编译 CTR.cpp 为 .o
set(KERNEL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/CTR.cpp)
set(KERNEL_OBJ ${CMAKE_CURRENT_BINARY_DIR}/CTR_device.o)

add_custom_command(
    OUTPUT ${KERNEL_OBJ}
    COMMAND ${ASCEND_CANN_PACKAGE_PATH}/bin/ascendg++ 
            -c ${KERNEL_SRC} -o ${KERNEL_OBJ}
            -x c++ -fPIC 
            -soc_version=${SOC_VERSION}
            --cce-enable-sanitizer
            -I${ASCEND_CANN_PACKAGE_PATH}/include
    DEPENDS ${KERNEL_SRC}
    COMMENT "Compiling CTR.cpp with ascendg++ for NPU..."
)

# --- 编译 Host 代码 (生成 pipellm.so) ---
add_library(pipellm SHARED
    pipellm.cpp
    worker.cpp
    openssl.cpp
    ${KERNEL_OBJ}  # 把 NPU 算子编译出来的 .o 链接进去
)

# --- 设置头文件和链接库 ---
target_include_directories(pipellm PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pipellm PRIVATE
    -L${ASCEND_CANN_PACKAGE_PATH}/lib64
    ascendcl
    crypto
    dl
    pthread
)

# --- 打印信息 ---
message(STATUS "Configured for ${SOC_VERSION}")