// Blackboard 单元测试（TS-§2 命名：[模块] 行为_条件 → 期望）。
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "npc_agent/core/blackboard.h"

using namespace npc_agent::core;
using nlohmann::json;

TEST_CASE("[blackboard] set/get/contains/erase_基本操作 → 行为正确") {
    Blackboard bb;
    REQUIRE_FALSE(bb.contains("k"));
    REQUIRE(bb.get("k") == nullptr);

    bb.set("k", 42);
    REQUIRE(bb.contains("k"));
    const json* v = bb.get("k");
    REQUIRE(v != nullptr);
    REQUIRE(v->get<int>() == 42);

    bb.set("k", "v2"); // 覆盖
    REQUIRE(bb.get("k")->get<std::string>() == "v2");

    bb.erase("k");
    REQUIRE_FALSE(bb.contains("k"));
    REQUIRE(bb.get("k") == nullptr);
}

TEST_CASE("[blackboard] 空黑板序列化往返 → 等价") {
    Blackboard a;
    json out;
    a.to_json(out);
    REQUIRE(out.is_object());
    REQUIRE(out.empty());

    Blackboard b;
    b.from_json(out);
    REQUIRE(b.size() == 0);
}

TEST_CASE("[blackboard] 非空序列化往返 → 等价") {
    Blackboard a;
    a.set("n", 3);
    a.set("s", "x");
    a.set("arr", json::array({1, 2}));

    json out;
    a.to_json(out);

    Blackboard b;
    b.from_json(out);

    json out2;
    b.to_json(out2);
    REQUIRE(out == out2);
}

TEST_CASE("[blackboard] from_json 非对象输入 → 安全清空") {
    Blackboard b;
    b.set("k", 1);
    b.from_json(json::array()); // 前置违约输入：清空而非崩溃（CS-§9 防御）
    REQUIRE(b.size() == 0);
}

TEST_CASE("[blackboard] clear 与 size → 正确") {
    Blackboard b;
    b.set("a", 1);
    b.set("b", 2);
    REQUIRE(b.size() == 2);
    b.clear();
    REQUIRE(b.size() == 0);
}
