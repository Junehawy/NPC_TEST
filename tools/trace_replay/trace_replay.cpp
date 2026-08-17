// trace_replay —— 决策日志录制/回放 CLI（tools/trace_replay/，RA 决策表 #22 / R8）。
// 用法：
//   trace_replay record <out.jsonl>   运行验收场景（"听到枪声 → 警戒 → 呼叫支援 → 搜寻"）
//                                     并录制决策日志（50 tick，确定性）
//   trace_replay replay <file.jsonl>  重跑同一场景并与录制逐行比对（R8 严格一致阈值），
//                                     一致退出 0，分歧退出 1 并输出首分歧行
//   trace_replay print  <file.jsonl>  打印日志行
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "npc_agent/testing/acceptance_scenario.h"
#include "npc_agent/tracing/decision_trace.h"

namespace {

constexpr std::size_t kScenarioTicks = 50;

int usage() {
    std::cerr << "用法: trace_replay record|replay|print <file.jsonl>\n";
    return 2;
}

bool read_file(const std::string& path, std::string& text) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "无法读取: " << path << '\n';
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3)
        return usage();
    const std::string mode = argv[1];
    const std::string path = argv[2];

    if (mode == "record") {
        npc_agent::testing::AcceptanceScenario scenario;
        npc_agent::tracing::DecisionTrace trace;
        scenario.system().set_trace(&trace);
        scenario.run(kScenarioTicks);
        std::ofstream out(path);
        if (!out) {
            std::cerr << "无法写入: " << path << '\n';
            return 1;
        }
        out << trace.dump();
        std::cout << "已录制 " << trace.size() << " 行 → " << path << '\n';
        return 0;
    }

    if (mode == "replay") {
        std::string text;
        if (!read_file(path, text))
            return 1;
        npc_agent::tracing::DecisionTrace recorded;
        std::string error;
        if (!npc_agent::tracing::DecisionTrace::load(text, recorded, error)) {
            std::cerr << "录制文件解析失败: " << error << '\n';
            return 1;
        }
        npc_agent::testing::AcceptanceScenario scenario;
        npc_agent::tracing::DecisionTrace live;
        scenario.system().set_trace(&live);
        scenario.run(kScenarioTicks);
        std::size_t diff = 0;
        if (auto err = npc_agent::tracing::DecisionTrace::compare(recorded, live, &diff);
            err.has_value()) {
            std::cerr << "[replay FAIL] " << *err << '\n';
            return 1;
        }
        std::cout << "[replay PASS] " << live.size() << " 行严格一致（R8 阈值）\n";
        return 0;
    }

    if (mode == "print") {
        std::string text;
        if (!read_file(path, text))
            return 1;
        npc_agent::tracing::DecisionTrace trace;
        std::string error;
        if (!npc_agent::tracing::DecisionTrace::load(text, trace, error)) {
            std::cerr << "解析失败: " << error << '\n';
            return 1;
        }
        for (const auto& line : trace.lines())
            std::cout << line.dump() << '\n';
        return 0;
    }

    return usage();
}
