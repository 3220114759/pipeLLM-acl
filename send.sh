#!/bin/bash
# curl http://0.0.0.0:8000/v1/completions \
#   -H "Content-Type: application/json" \
#   -d '{
#     "model": "qwen3-14b",
#     "prompt": "你是",
#     "max_tokens": 30,
#     "temperature": 0.5
# }'

# vllm bench serve --model qwen3-14b --tokenizer /data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B --host 0.0.0.0 --port 8080 --dataset-name random --random-input 50 --num-prompts 50 --request-rate 5

# vllm bench serve --model qwen3-14b --tokenizer /data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B --host 0.0.0.0 --port 8080 --dataset-name lmarena-ai/VisionArena-Chat --num-prompts 50 --request-rate 5
vllm bench serve --model qwen3-14b --tokenizer /data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B --dataset-name prefix_repetition --prefix-repetition-prefix-len 1024 --prefix-repetition-suffix-len 512 --prefix-repetition-output-len 256 --prefix-repetition-num-prefixes 30 --num-prompts 30 --host 127.0.0.1 --port 8080 --endpoint /v1/chat/completions --backend "openai-chat"