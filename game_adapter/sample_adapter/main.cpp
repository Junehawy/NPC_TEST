// 阶段 1 无头示例：一个 NPC 读配置 → 每 tick 收事件 → 仲裁产出意图 → 执行到 MockWorld。
// 运行：任意工作目录执行 npc_test（配置路径自动定位，可用第 1 个命令行参数显式指定）。
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "npc_agent/core/agent_config.h"
#include "npc_agent/core/agent_system.h"
#include "npc_agent/interfaces/intent.h"
#include "npc_agent/testing/intent_desc.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"
#include "npc_agent/testing/toy_capabilities.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::testing;

namespace {

// 示例配置的仓库内相对路径（相对仓库根目录）。
constexpr std::string_view kRelativeAssetPath = "assets/npcs/sample_guard.json";

// 定位示例配置（CS-§9 错误处理路径完整），候选按优先级依次探测：
//   1) 命令行参数显式指定（游戏宿主通常由自身配置目录决定资源路径）；
//   2) 相对当前工作目录（ctest 冒烟、在仓库根目录直接运行）；
//   3) 编译期注入的源码根目录兜底（CMake 注入 NPC_TEST_SOURCE_ROOT，
//      保证从 build/ 等任意目录直接运行也能找到）。
std::optional<std::string> locate_asset_path(int argc, char** argv) {
    std::vector<std::string> candidates;
    if (argc > 1)
        candidates.emplace_back(argv[1]);
    candidates.emplace_back(kRelativeAssetPath);
#ifdef NPC_TEST_SOURCE_ROOT
    candidates.emplace_back(std::string(NPC_TEST_SOURCE_ROOT) + "/" +
                            std::string(kRelativeAssetPath));
#endif
    for (const auto& candidate : candidates) {
        std::ifstream probe(candidate);
        if (probe)
            return candidate;
    }
    return std::nullopt;
}

// 意图可读描述由 testing/intent_desc 提供（示例与 Godot 演示共用，避免重复）。

} // namespace

int main(int argc, char** argv) {
    // 1. 定位并读取 NPC 配置（文件读取属宿主职责，框架只解析 JSON 值）
    const auto asset_path = locate_asset_path(argc, argv);
    if (!asset_path.has_value()) {
        std::cerr << "无法找到示例配置 " << kRelativeAssetPath
                  << "，请用第 1 个命令行参数显式指定其路径。\n";
        return 1;
    }
    std::ifstream ifs(*asset_path);
    if (!ifs) {
        std::cerr << "无法打开配置: " << *asset_path << '\n';
        return 1;
    }
    const nlohmann::json cfg_json = nlohmann::json::parse(ifs);
    AgentConfig cfg;
    if (auto err = parse_agent_config(cfg_json, "sample_guard.json", cfg); err.has_value()) {
        std::cerr << to_string(*err) << '\n';
        return 1;
    }

    // 2. 世界与身体
    MockWorld world;
    world.add_entity("player", Vec3{8, 0, 0});
    MockBody body;
    body.set_position(Vec3{0, 0, 0});

    // 3. 系统与能力装配
    AgentSystem system;
    system.set_current_world(world);
    auto& agent = system.create_agent(std::move(cfg), body);
    agent.set_decision_maker(std::make_unique<ToyPatrolDecision>(Vec3{0, 0, 0}));
    agent.register_capability(std::make_unique<ToyGreetCapability>());
    agent.register_capability(std::make_unique<ToyStartleCapability>());

    // 4. 10 个 tick：第 3 tick 注入枪声，第 6 tick 拉响警报（演示 pending 兜底）
    for (int i = 0; i < 10; ++i) {
        if (i == 3) {
            system.inject_stimulus(Stimulus{"gunshot", Vec3{5, 0, 0}, 1.0f, "player"});
            std::cout << "  >> 注入刺激: 枪声\n";
        }
        if (i == 6) {
            agent.blackboard().set("alarm", true);
            std::cout << "  >> 黑板置位: alarm=true（决策器进入 pending）\n";
        }
        world.advance(0.1);
        system.tick();
        std::cout << "tick " << i << ": " << describe_intent(agent.last_intent()) << '\n';
    }

    // 5. 身体动作日志
    std::cout << "--- 身体动作日志 ---\n";
    for (const auto& a : body.actions()) {
        std::cout << "  " << a.kind << ": " << a.payload.dump() << '\n';
    }

    // 6. 存档演示
    const nlohmann::json saved = system.to_json();
    std::cout << "--- 存档序列化: " << saved.dump().size() << " 字节 ---\n";
    return 0;
}
