#!/bin/bash
set -e
# pkill -9 -i -u lzy -f "vllm"
cd /data/lzy/pipeLLM-acl
export PIPE_ENABLE_H2D=1
export PIPE_ENABLE_D2H=1
ASCEND_RT_VISIBLE_DEVICES=4
echo "[1/3] Building libpipellm.so..."
make lib

echo "[2/3] Running inference with hook..."
# export LD_LIBRARY_PATH=/usr/local/Ascend/ascend-toolkit/latest/lib64:/data/lzy/miniconda3/envs/npu_env/Ascend/cann-8.5.0/lib64:$LD_LIBRARY_PATH
# LD_PRELOAD=./libpipellm.so python infer.py 
LD_PRELOAD=./libpipellm.so python infer.py

echo "[3/3] Done!"
