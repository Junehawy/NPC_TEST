// DecisionTrace 单元测试（dump/load 往返、逐行比对、意图序列化）。
#include <catch2/catch_test_macros.hpp>

#include "npc_agent/interfaces/intent.h"
#include "npc_agent/tracing/decision_trace.h"

using namespace npc_agent;
using namespace npc_agent::tracing;

TEST_CASE("intent_to_json：各意图类型与空值", "[trace]") {
    REQUIRE(intent_to_json(std::nullopt).is_null());

    Intent move;
    move.payload = MoveIntent{Vec3{1, 2, 3}, 2.5f};
    const auto move_json = intent_to_json(move);
    REQUIRE(move_json["payload"]["kind"] == "move_to");
    REQUIRE(move_json["payload"]["target"] == nlohmann::json::array({1, 2, 3}));
    REQUIRE(move_json["payload"]["speed"] == 2.5f);

    Intent say;
    say.payload = SayIntent{"你好", "friendly"};
    REQUIRE(intent_to_json(say)["payload"]["text"] == "你好");

    Intent emote;
    emote.payload = EmoteIntent{"startled"};
    REQUIRE(intent_to_json(emote)["payload"]["name"] == "startled");

    Intent game;
    GameEvent e;
    e.type = "call_support";
    e.payload = nlohmann::json{{"n", 2}};
    game.payload = GameEventIntent{e};
    const auto game_json = intent_to_json(game);
    REQUIRE(game_json["payload"]["type"] == "call_support");
    REQUIRE(game_json["payload"]["payload"]["n"] == 2);

    Intent pending;
    pending.ready = false;
    REQUIRE(intent_to_json(pending)["ready"] == false);
}

TEST_CASE("DecisionTrace dump/load 往返", "[trace]") {
    DecisionTrace trace;
    trace.append(nlohmann::json{{"tick", 0}, {"agent", "a"}});
    trace.append(nlohmann::json{{"tick", 1}, {"agent", "a"}, {"intent", nullptr}});

    const std::string text = trace.dump();
    DecisionTrace loaded;
    std::string error;
    REQUIRE(DecisionTrace::load(text, loaded, error));
    REQUIRE(loaded.size() == 2);
    REQUIRE(loaded.lines()[1]["tick"] == 1);
    REQUIRE(loaded.lines()[1]["intent"].is_null());
}

TEST_CASE("DecisionTrace load：非法行报错含行号", "[trace]") {
    DecisionTrace out;
    std::string error;
    REQUIRE_FALSE(DecisionTrace::load("{\"tick\": 0}\nnot-json\n", out, error));
    REQUIRE(error.find("第 2 行") != std::string::npos);
}

TEST_CASE("DecisionTrace compare：严格一致 / 分歧定位", "[trace]") {
    DecisionTrace a;
    a.append(nlohmann::json{{"tick", 0}, {"v", 1}});
    a.append(nlohmann::json{{"tick", 1}, {"v", 2}});
    a.append(nlohmann::json{{"tick", 2}, {"v", 3}});

    DecisionTrace b;
    b.append(nlohmann::json{{"tick", 0}, {"v", 1}});
    b.append(nlohmann::json{{"tick", 1}, {"v", 2}});
    b.append(nlohmann::json{{"tick", 2}, {"v", 3}});

    std::size_t diff = 0;
    REQUIRE_FALSE(DecisionTrace::compare(a, b, &diff).has_value());
    REQUIRE(diff == static_cast<std::size_t>(-1)); // 一致：无分歧下标

    DecisionTrace c;
    c.append(nlohmann::json{{"tick", 0}, {"v", 1}});
    c.append(nlohmann::json{{"tick", 1}, {"v", 999}});
    c.append(nlohmann::json{{"tick", 2}, {"v", 3}});
    const auto err = DecisionTrace::compare(a, c, &diff);
    REQUIRE(err.has_value());
    REQUIRE(diff == 1);
    REQUIRE(err->find("第 1 行") != std::string::npos);

    DecisionTrace short_trace;
    short_trace.append(nlohmann::json{{"tick", 0}, {"v", 1}});
    const auto size_err = DecisionTrace::compare(a, short_trace, &diff);
    REQUIRE(size_err.has_value());
    REQUIRE(diff == 1); // 首分歧 = 短侧行数（首行一致后行数不一致）
}
