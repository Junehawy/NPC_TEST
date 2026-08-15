// 阶段 1 无头示例：一个 NPC 读配置 → 每 tick 收事件 → 仲裁产出意图 → 执行到 MockWorld。
// 运行：仓库根目录执行 ./build/npc_test
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "npc_agent/core/agent_config.h"
#include "npc_agent/core/agent_system.h"
#include "npc_agent/interfaces/intent.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"
#include "npc_agent/testing/toy_capabilities.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::testing;

namespace {

// 意图可读描述（演示输出用）。
std::string describe(const std::optional<Intent>& intent) {
    if (!intent.has_value())
        return "无意图";
    if (!intent->ready)
        return "等待异步结果（ready=false）";
    if (std::holds_alternative<MoveIntent>(intent->payload)) {
        const auto& m = std::get<MoveIntent>(intent->payload);
        return "MoveIntent → (" + std::to_string(m.target.x) + ", " + std::to_string(m.target.y) +
               ")";
    }
    if (std::holds_alternative<SayIntent>(intent->payload)) {
        return "SayIntent → \"" + std::get<SayIntent>(intent->payload).text + "\"";
    }
    if (std::holds_alternative<EmoteIntent>(intent->payload)) {
        return "EmoteIntent → " + std::get<EmoteIntent>(intent->payload).name;
    }
    return "GameEventIntent";
}

} // namespace

int main() {
    // 1. 读 NPC 配置（文件读取属宿主职责，框架只解析 JSON 值）
    std::ifstream ifs("assets/npcs/sample_guard.json");
    if (!ifs) {
        std::cerr << "无法打开 assets/npcs/sample_guard.json（请在仓库根目录运行）\n";
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
        std::cout << "tick " << i << ": " << describe(agent.last_intent()) << '\n';
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
