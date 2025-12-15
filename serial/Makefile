# #
# # Makefile for PipeLLM Serial Verification
# # (Keep original LIBS and Paths)
# #

# # --- 1. 原始路径配置 ---
# KERNEL_LIB_DIR = /data/lzy/serial
# CANN_INCLUDE_PATH = /usr/local/Ascend/ascend-toolkit/latest/include
# CANN_LIB_PATH = /usr/local/Ascend/ascend-toolkit/latest/lib64
# OPENSSL_LIB_PATH = /home/lzy/miniconda3/envs/npu_env/lib

# # --- 2. 编译器和标志 ---
# CXX = g++
# BASE_CXXFLAGS = -std=c++17 -fPIC -g -Wall -Wextra
# RELEASE_FLAGS = -O2
# LDFLAGS = -shared -g

# # --- 3. 头文件和库路径 ---
# INCLUDES = -I. -I$(CANN_INCLUDE_PATH)

# # [保留原样] 库搜索路径
# # 加上 RPATH 以便运行时找到您的算子库和 OpenSSL
# LDPATH = -L$(KERNEL_LIB_DIR) \
#          -L$(CANN_LIB_PATH) \
#          -L$(OPENSSL_LIB_PATH) \
#          -Wl,-rpath=$(KERNEL_LIB_DIR)

# # [保留原样] 链接库列表
# # 您的算子库 (libaes_npu_lib.so) 应该包含在 -lascendc_kernels_npu 或者是隐式依赖
# # 如果您确定原来的 LIBS 能跑通，这里就不变
# LIBS = -lascendcl -lpthread -ldl -lstdc++ -lssl -lcrypto -lascendc_kernels_npu -laes_npu_lib

# # --- 4. 文件定义 (修改部分) ---
# # 目标改为 serial 版
# TARGET = pipellm_serial.so

# # 源文件只保留 pipellm.cpp (移除 worker/openssl)
# SRCS = pipellm.cpp 
# OBJS = $(SRCS:.cpp=.o)
# HEADERS = pipellm.h hack.h

# # --- 5. 构建规则 ---

# all: CXXFLAGS = $(BASE_CXXFLAGS) $(RELEASE_FLAGS)
# all: $(TARGET)

# $(TARGET): $(OBJS)
# 	@echo "==> 正在链接 $(TARGET)..."
# 	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDPATH) $(LIBS)
# 	@echo "==> ✅ $(TARGET) 构建完成。"

# %.o: %.cpp $(HEADERS)
# 	@echo "==> 正在编译 $<..."
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# clean:
# 	rm -f $(TARGET) $(OBJS)

# .PHONY: all clean

# # gcc -O2 -fPIC -shared -o pipellm_serial.so pipellm.c -ldl -lpthread



KERNEL_LIB_DIR = /data/lzy/serial

# 2. 编译器设置
CC = gcc
CFLAGS = -O2 -fPIC -Wall -D_GNU_SOURCE

# 3. 链接器设置
# 不链接任何第三方库 (除了 dl/pthread)
LDFLAGS = -shared 

# 4. 依赖库
LIBS = -ldl -lpthread

# 5. 目标
TARGET = pipellm_serial.so
SRC = pipellm.c

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "==> Compiling $(TARGET)..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC) $(LIBS)
	@echo "==> ✅ Success!"
	@echo "==> Check dependencies (must NOT contain libstdc++):"
	@ldd $(TARGET) | grep libstdc++ || echo "Safe: No libstdc++ found."

clean:
	rm -f $(TARGET)

.PHONY: all clean