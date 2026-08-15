// 冒烟示例程序：打印框架版本与编译期能力开关（阶段 0 最小可用形态）。
#include <iostream>

#include "npc_agent/config/feature_flags.h"

int main() {
    std::cout << "NPC_TEST running...\n";
    std::cout << "compile flags: llm=" << npc_agent::config::CompileFlags::kLlmCompiled
              << " memory_vector=" << npc_agent::config::CompileFlags::kMemoryVectorCompiled
              << '\n';
    return 0;
}
