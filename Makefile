# Makefile for pipeLLM-ACL (Huawei Ascend NPU)

CC = g++
CXX = g++

# 统一锁死 CANN 8.5.1 基础路径
CANN_BASE = /data/lzy/Ascend/cann-8.5.1

# 头文件路径
ACL_INC = -I$(CANN_BASE)/include

# 动态库路径 (加入我们之前 nm 查出的正确目录 aarch64-linux/lib64)
ACL_LIB_PATH = $(CANN_BASE)/aarch64-linux/lib64
CANN_LIB_PATH = $(CANN_BASE)/lib64

OPENSSL_INC = $(shell pkg-config --cflags openssl 2>/dev/null || echo "-I/usr/include/openssl")
OPENSSL_LIB = $(shell pkg-config --libs openssl 2>/dev/null || echo "-lssl -lcrypto")

# Common flags
CFLAGS = -std=c++17 -fPIC -O2 -Wall
LDFLAGS = -shared -fPIC -lpthread -ldl

# Source files
LIB_SOURCES = pipellm.cpp worker.cpp openssl.cpp
LIB_OBJECTS = $(LIB_SOURCES:.cpp=.o)

# Output library
OUTPUT_LIB = libpipellm.so

# Runtime library paths
LD_PATH = $(ACL_LIB_PATH):$(CANN_LIB_PATH)

.PHONY: all lib clean run

all: lib

lib: $(OUTPUT_LIB)

$(OUTPUT_LIB): $(LIB_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(OPENSSL_LIB) -L$(ACL_LIB_PATH) -lascendcl -lacl_rt

%.o: %.cpp
	$(CXX) $(CFLAGS) $(ACL_INC) $(OPENSSL_INC) -c $< -o $@

# Usage: run inference with LD_PRELOAD
# Example: make run PYTHON=/path/to/python
run:
	@echo "Run inference with hook:"
	@echo "  LD_LIBRARY_PATH=$(LD_PATH):$$LD_LIBRARY_PATH LD_PRELOAD=./$(OUTPUT_LIB) python infer.py"

clean:
	rm -f $(LIB_OBJECTS) $(OUTPUT_LIB)