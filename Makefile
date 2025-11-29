#
# Makefile for the PipeLLM ACL/OpenSSL Hook Project
#
# 用法:
#   make          - 编译项目 (Release 模式)
#   make debug    - 编译项目 (Debug + AddressSanitizer 模式)
#   make clean    - 清理所有编译生成的文件
#
KERNEL_LIB_DIR = /data/lzy/pipellm_acl
# --- 1. 编译器和标志 ---
CXX = g++

# 通用 C++ 标志
# -std=c++17:    您的项目需要 C++17
# -fPIC:         生成位置无关代码 (创建 .so 必需)
# -Wall -Wextra: 打开所有警告
# -g:            包含调试信息
BASE_CXXFLAGS = -std=c++17 -fPIC -g -Wall -Wextra

# 优化标志 (Release 模式)
RELEASE_FLAGS = -O2

# 调试标志 (Debug 模式)
# -fsanitize=address: 启用内存错误检测 (ASan)
DEBUG_FLAGS = -O0 -fsanitize=address -fno-omit-frame-pointer

# 链接标志
LDFLAGS = -shared -g


# --- 2. 路径和库 ---

# [Ascend/CANN 路径]
CANN_INCLUDE_PATH = /usr/local/Ascend/ascend-toolkit/latest/include
CANN_LIB_PATH = /usr/local/Ascend/ascend-toolkit/latest/lib64

# [!!! 新增: OpenSSL 路径 !!!]
# 根据您之前的日志，指向 conda 环境下的 lib 目录
# 如果您的环境变了，请修改这里
OPENSSL_LIB_PATH = /home/lzy/miniconda3/envs/npu_env/lib

# 头文件包含路径
INCLUDES = -I. -I$(CANN_INCLUDE_PATH)

# 库搜索路径 (-L)
# 同时包含 CANN 和 OpenSSL 的库目录
LDPATH = -L$(KERNEL_LIB_DIR) -L$(CANN_LIB_PATH) -L$(OPENSSL_LIB_PATH) -Wl,-rpath=$(KERNEL_LIB_DIR)


# [!!! 关键修改: 链接库 !!!]
# -lascendcl:  Ascend 运行时库
# -lpthread:   POSIX 线程库
# -ldl:        动态链接库
# -lstdc++:    防止 C++ 标准库链接错误
LIBS = -lascendcl -lpthread -ldl -lstdc++ -lssl -lcrypto -lascendc_kernels_npu


# --- 3. 文件定义 ---

TARGET = pipellm_acl.so
SRCS = pipellm.cpp openssl.cpp worker.cpp
OBJS = $(SRCS:.cpp=.o)
HEADERS = pipellm.h hack.h


# --- 4. 构建规则 ---

# 默认目标: Release 模式
all: CXXFLAGS = $(BASE_CXXFLAGS) $(RELEASE_FLAGS)
all: $(TARGET)

# Debug 目标: 'make debug'
debug: CXXFLAGS = $(BASE_CXXFLAGS) $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=address
debug: $(TARGET)

.PHONY: all clean debug

# 链接规则
$(TARGET): $(OBJS)
	@echo "==> 正在链接 $(TARGET)..."
	# 注意: LDPATH 必须在 OBJS 之后，LIBS 必须在最后
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDPATH) $(LIBS)
	@echo "==> ✅ $(TARGET) 构建完成。"
	@echo "==> 提示: 请确保 $(OPENSSL_LIB_PATH) 在 LD_LIBRARY_PATH 中，或者在启动脚本中设置。"

# 编译规则
%.o: %.cpp $(HEADERS)
	@echo "==> 正在编译 $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@echo "==> 正在清理..."
	rm -f $(TARGET) $(OBJS)
	@echo "==> 清理完成。"