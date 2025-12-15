# 1. 环境变量设置
# export ASCEND_RT_VISIBLE_DEVICES=4,5,6,7
export LD_PRELOAD=/data/lzy/serial/pipellm_serial.so
export PIPE_ENABLE_H2D=1
export PIPE_ENABLE_D2H=1
export PIPE_ENABLE_CHECK=0
export PIPE_KERNEL_MODE=2
# 阈值设置：避开 8 字节控制信号

# 关键：强制 Fork 模式 (让自动握手的上下文被继承)
export VLLM_USE_RAY=False
export VLLM_WORKER_MULTIPROC_METHOD=fork

# 2. 执行官方指令
python3 -m vllm.entrypoints.openai.api_server \
    --model /data/lzy/models/Qwen1.5-110B/Qwen/Qwen1.5-110B \
    --tensor-parallel-size 8 \
    --port 8000 \
    --host 0.0.0.0 \
    --max-model-len 24000 \
    --gpu-memory-utilization 0.8 \
    --trust-remote-code \
    --enforce-eager \
    --block-size 128 \
    --swap-space 16 \
    --no-enable-prefix-caching