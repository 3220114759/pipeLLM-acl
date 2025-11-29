# 1. 环境变量设置
export ASCEND_RT_VISIBLE_DEVICES=5
export LD_PRELOAD=/data/lzy/pipellm_acl/pipellm_acl.so
export PIPELLM_ENABLE_ENCRYPT=1
export PIPELLM_ENABLE_DECRYPT=1
# 阈值设置：避开 8 字节控制信号

# 关键：强制 Fork 模式 (让自动握手的上下文被继承)
export VLLM_USE_RAY=False
export VLLM_WORKER_MULTIPROC_METHOD=fork

# 2. 执行官方指令
python3 -m vllm.entrypoints.openai.api_server \
    --model /data/lzy/models/Qwen3-4B/Qwen/Qwen3-4B \
    --tensor-parallel-size 1 \
    --port 8000 \
    --host 0.0.0.0 \
    --max-model-len 24000 \
    --gpu-memory-utilization 0.7 \
    --trust-remote-code