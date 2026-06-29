import sys
import os
import gc
import time

os.environ["ASCEND_RT_VISIBLE_DEVICES"] = "7"

import torch
import torch_npu

# 避免 vllm 重复加载问题
if 'vllm' in sys.modules:
    del sys.modules['vllm']

from vllm import LLM, SamplingParams
from vllm.config import KVTransferConfig
from transformers import AutoTokenizer


# ============================================================
# 模型配置
# ============================================================

MODEL_PATH = "/data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B"

MAX_HISTORY_ROUNDS = 10

# ============================================================
# vLLM Sampling
# ============================================================

sampling_params = SamplingParams(
    temperature=0.7,
    top_p=0.8,
    max_tokens=512,

    # 阻止输出 think 内容
    stop=[
        "<think>",
        "</think>"
    ]
)

# ============================================================
# 初始化 vLLM
# ============================================================

print("====== 初始化 vLLM ======")

kv_transfer_config = KVTransferConfig(
    kv_connector="OffloadingConnector",
    kv_role="kv_both",
    kv_connector_extra_config={
        "num_cpu_blocks": 1000,
        "block_size": 128,
        "spec_name": "NPUOffloadingSpec",
        "spec_module_path": "vllm_ascend.kv_offload.npu",
    },
)

llm = LLM(
    model=MODEL_PATH,
    trust_remote_code=True,
    tensor_parallel_size=1,
    dtype="float16",
    gpu_memory_utilization=0.9,
    enforce_eager=True,
    kv_transfer_config=kv_transfer_config
)

print("====== vLLM 初始化完成 ======\n")

# ============================================================
# Tokenizer
# ============================================================

tokenizer = AutoTokenizer.from_pretrained(
    MODEL_PATH,
    trust_remote_code=True
)

# ============================================================
# 历史对话
# ============================================================

conversation_history = []

# ============================================================
# 自动生成长输入
# ============================================================

LONG_TEXT_BASE = """
浙江大学是一所历史悠久、声誉卓著的高等学府。
学校在人工智能、计算机体系结构、并行计算、
大语言模型推理优化、NPU系统设计等方向具有丰富研究成果。
"""

def generate_prompt_by_tokens(target_tokens):

    pieces = []

    while True:

        pieces.append(LONG_TEXT_BASE)

        text = "\n".join(pieces)

        n_tokens = len(
            tokenizer.encode(text)
        )

        if n_tokens >= target_tokens:

            return text, n_tokens

# ============================================================
# 构造 Chat Messages
# ============================================================

def build_messages():

    messages = []

    # system prompt
    messages.append({
        "role": "system",
        "content":
            "You are a helpful assistant. "
            "Do not output reasoning process "
            "or thinking steps."
    })

    # history
    for role, content in conversation_history:

        messages.append({
            "role": role,
            "content": content
        })

    return messages

# ============================================================
# 打印性能统计
# ============================================================

def print_stats(
    latency,
    input_tokens,
    output_tokens
):

    total_tokens = (
        input_tokens + output_tokens
    )

    throughput = (
        total_tokens / latency
    )

    gen_speed = (
        output_tokens / latency
    )

    print("\n" + "=" * 60)
    print(f"{'性能指标':^60}")
    print("=" * 60)

    print(f"Latency              : {latency:.4f} s")
    print(f"Input Tokens         : {input_tokens}")
    print(f"Output Tokens        : {output_tokens}")
    print(f"Total Tokens         : {total_tokens}")
    print(f"Throughput           : {throughput:.2f} tokens/s")
    print(f"Generation Speed     : {gen_speed:.2f} tokens/s")

    print("=" * 60 + "\n")

# ============================================================
# Chat Loop
# ============================================================

print("====== ChatBot 已启动 ======\n")

print("支持命令:")
print("1. 普通输入")
print("2. /gen 32768      -> 自动生成指定 token 数输入")
print("3. /history        -> 查看历史")
print("4. /clear          -> 清空历史")
print("5. /exit           -> 退出")
print()

while True:

    user_input = input("User > ").strip()

    # ========================================================
    # Exit
    # ========================================================

    if user_input == "/exit":

        break

    # ========================================================
    # Clear History
    # ========================================================

    elif user_input == "/clear":

        conversation_history.clear()

        print("[Info] History Cleared.\n")

        continue

    # ========================================================
    # Show History
    # ========================================================

    elif user_input == "/history":

        print("\n====== History ======")

        for i, (role, content) in enumerate(
            conversation_history
        ):

            print(f"[{i}] {role}")

            preview = content[:300]

            print(preview)

            if len(content) > 300:
                print("...")

            print()

        print("=====================\n")

        continue

    # ========================================================
    # Generate Long Prompt
    # ========================================================

    elif user_input.startswith("/gen"):

        try:

            target_tokens = int(
                user_input.split()[1]
            )

            print(
                f"[Info] Generating "
                f"{target_tokens} tokens..."
            )

            generated_prompt, actual_tokens = \
                generate_prompt_by_tokens(
                    target_tokens
                )

            user_content = generated_prompt

            print(
                f"[Info] Actual Tokens = "
                f"{actual_tokens}\n"
            )

        except:

            print(
                "Usage:\n"
                "/gen 32768"
            )

            continue

    # ========================================================
    # Normal Input
    # ========================================================

    else:

        user_content = user_input

    # ========================================================
    # 保存用户输入
    # ========================================================

    conversation_history.append(
        ("user", user_content)
    )

    # 限制历史长度

    if len(conversation_history) > \
        MAX_HISTORY_ROUNDS * 2:

        conversation_history = \
            conversation_history[
                -MAX_HISTORY_ROUNDS * 2:
            ]

    # ========================================================
    # 构造 Messages
    # ========================================================

    messages = build_messages()

    # ========================================================
    # 使用 Qwen Chat Template
    # ========================================================

    final_prompt = tokenizer.apply_chat_template(
        messages,
        tokenize=False,
        add_generation_prompt=True
    )

    # ========================================================
    # Token统计
    # ========================================================

    input_tokens = len(
        tokenizer.encode(final_prompt)
    )

    print(
        f"\n[Info] Input Tokens = "
        f"{input_tokens}"
    )

    # ========================================================
    # 推理
    # ========================================================

    print("\n[Info] Generating...\n")

    start_time = time.time()

    outputs = llm.generate(
        [final_prompt],
        sampling_params
    )

    end_time = time.time()

    latency = (
        end_time - start_time
    )

    # ========================================================
    # 获取输出
    # ========================================================

    generated_text = \
        outputs[0].outputs[0].text

    output_tokens = len(
        outputs[0].outputs[0].token_ids
    )

    # ========================================================
    # 输出结果
    # ========================================================

    print("Assistant >")

    print(generated_text)

    print()

    # ========================================================
    # 保存历史
    # ========================================================

    conversation_history.append(
        ("assistant", generated_text)
    )

    # ========================================================
    # 打印性能统计
    # ========================================================

    print_stats(
        latency,
        input_tokens,
        output_tokens
    )

# ============================================================
# Cleanup
# ============================================================

print("\n[Info] Shutting down...")

del llm

gc.collect()

print("[Info] LLM Engine shutdown successfully.")