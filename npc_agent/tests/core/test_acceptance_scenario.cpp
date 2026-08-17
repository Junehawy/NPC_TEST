// 阶段 2 验收场景测试（TS-§3 scenarios 行，RA 路线图 2.x 验收）：
// "听到枪声 → 警戒 → 呼叫支援 → 搜寻" + trace 回放断言序列一致（R8）。
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "npc_agent/testing/acceptance_scenario.h"
#include "npc_agent/tracing/decision_trace.h"

using namespace npc_agent;
using namespace npc_agent::testing;
using namespace npc_agent::tracing;

namespace {

// 取场景运行到指定 tick（含）后的意图：run(N+1) 执行 tick 0..N。
std::optional<Intent> intent_at(std::size_t ticks) {
    AcceptanceScenario scenario;
    scenario.run(ticks + 1);
    return scenario.agent().last_intent();
}

} // namespace

TEST_CASE("验收场景：枪声 → 警戒 → 呼叫支援 → 搜寻", "[acceptance]") {
    // tick 2（枪声前）：idle → 无意图
    REQUIRE(!intent_at(2).has_value());
    // tick 3（枪声注入当 tick）：警戒表情
    const auto alert = intent_at(3);
    REQUIRE(alert.has_value());
    REQUIRE(std::holds_alternative<EmoteIntent>(alert->payload));
    REQUIRE(std::get<EmoteIntent>(alert->payload).name == "startled");
    // tick 13（警戒 1.0s 后）：呼叫支援台词
    const auto call = intent_at(13);
    REQUIRE(std::holds_alternative<SayIntent>(call->payload));
    REQUIRE(std::get<SayIntent>(call->payload).text == "呼叫支援！请求增援");
    // tick 28（呼叫 1.5s 后）：搜寻移动
    const auto search = intent_at(28);
    REQUIRE(std::holds_alternative<MoveIntent>(search->payload));
    REQUIRE(std::get<MoveIntent>(search->payload).target.x == 5.0f);
    // tick 48（搜寻 2.0s 后）：回到 idle
    REQUIRE(!intent_at(48).has_value());
}

TEST_CASE("验收场景：感知旗标驱动 FSM（heard_gunshot）", "[acceptance]") {
    AcceptanceScenario scenario;
    scenario.run(4); // 枪声在 tick 3 注入并派发
    const auto* heard = scenario.agent().blackboard().get("heard_gunshot");
    REQUIRE(heard != nullptr);
    REQUIRE(heard->get<bool>());
}

TEST_CASE("验收场景：trace 回放断言序列一致（R8 严格一致阈值）", "[acceptance]") {
    AcceptanceScenario a;
    DecisionTrace trace_a;
    a.system().set_trace(&trace_a);
    a.run(50);

    AcceptanceScenario b;
    DecisionTrace trace_b;
    b.system().set_trace(&trace_b);
    b.run(50);

    REQUIRE(trace_a.size() == 50);
    std::size_t diff = 0;
    const auto err = DecisionTrace::compare(trace_a, trace_b, &diff);
    REQUIRE_FALSE(err.has_value()); // 同种子同脚本 → 逐字节一致
}

TEST_CASE("验收场景：不同种子 → trace 分歧（rng_seed 字段）", "[acceptance]") {
    AcceptanceScenario a(42);
    DecisionTrace trace_a;
    a.system().set_trace(&trace_a);
    a.run(10);

    AcceptanceScenario b(7);
    DecisionTrace trace_b;
    b.system().set_trace(&trace_b);
    b.run(10);

    std::size_t diff = 0;
    const auto err = DecisionTrace::compare(trace_a, trace_b, &diff);
    REQUIRE(err.has_value()); // 首行 rng_seed 即不同（本场景无随机决策，仅种子字段）
    REQUIRE(diff == 0);
}

TEST_CASE("验收场景：trace 行字段完整（R8 定案字段）", "[acceptance]") {
    AcceptanceScenario scenario;
    DecisionTrace trace;
    scenario.system().set_trace(&trace);
    scenario.run(4); // 4 行：tick 0..3

    const auto& line = trace.lines()[3]; // 枪声 tick
    REQUIRE(line["tick"] == 3);
    REQUIRE(line["agent"] == "guard");
    REQUIRE(line["decision_maker"] == "fsm");
    REQUIRE(line["source"] == "decision_maker");
    REQUIRE(line["rng_seed"].is_number());
    REQUIRE(line["candidates"].is_array());
    REQUIRE(line["intent"]["payload"]["kind"] == "emote");
    REQUIRE(line["action_handle"].is_number());
}
