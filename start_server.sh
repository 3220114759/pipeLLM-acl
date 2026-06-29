#!/bin/bash
export LD_PRELOAD=./libpipellm.so
export PIPE_ENABLE_H2D=1
export PIPE_ENABLE_D2H=1
export PIPE_ENABLE_CHECK=0
export VLLM_WORKER_MULTIPROC_METHOD=fork
# export MOONCAKE_CONFIG_PATH="/data/lzy/mooncake.json" 

# vllm serve /data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B \
#   --tensor-parallel-size 1 \
#   --port 8000 \
#   --host 0.0.0.0 \
#   --max-model-len 4096 \
#   --gpu-memory-utilization 0.58 \
#   --trust-remote-code \
#   --enforce-eager \
#   --block-size 128 \
#   --no-enable-prefix-caching \
#   --served-model-name qwen3-14b \
#   --kv-transfer-config '{
#     "kv_connector": "AscendStoreConnector",
#     "kv_role": "kv_both",
#     "kv_connector_extra_config": {
#       "backend": "mooncake",
#       "lookup_rpc_port": "0"
#     }
#   }'

ASCEND_RT_VISIBLE_DEVICES=6 vllm serve /data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B --tensor-parallel-size 1 --port 8080 --host 0.0.0.0 --max-model-len 8192 --max-num-seqs 64 --max-num-batched-tokens 16384 --gpu-memory-utilization 0.56 --trust-remote-code --enforce-eager --block-size 256 --served-model-name qwen3-14b --kv-transfer-config '{"kv_connector":"OffloadingConnector","kv_role":"kv_both","kv_connector_extra_config":{"num_cpu_blocks":1000,"block_size":256,"spec_name":"NPUOffloadingSpec","spec_module_path":"vllm_ascend.kv_offload.npu"}}' &> log.txt