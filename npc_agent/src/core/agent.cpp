#include "npc_agent/core/agent.h"

#include <cassert>
#include <sstream>
#include <utility>

namespace npc_agent::core {

Agent::Agent(AgentConfig config) : config_(std::move(config)) {
    rng_.seed(config_.rng_seed);
}

std::string_view Agent::id() const {
    return config_.id;
}

const AgentConfig& Agent::config() const {
    return config_;
}

void Agent::attach_body(IAgentBody& body) {
    body_ = &body;
}

BodyState Agent::body_state() const {
    assert(body_ != nullptr); // 编程错误：未挂身体即访问状态（CS-§9）
    return body_->body_state();
}

void Agent::register_capability(std::unique_ptr<ICapability> cap) {
    assert(cap != nullptr);
    caps_.push_back(std::move(cap));
    scratch_.reserve(caps_.size() + 1); // 随注册增长，tick 内零增长分配
}

void Agent::set_decision_maker(std::unique_ptr<IDecisionMaker> dm) {
    decision_maker_ = std::move(dm);
}

void Agent::enqueue_private(AgentEvent e) {
    private_queue_.push_back(std::move(e));
}

void Agent::route_event(const AgentEvent& e) {
    if (decision_maker_ != nullptr)
        decision_maker_->on_event(e);
    for (const auto& cap : caps_)
        cap->on_event(e);
}

std::optional<Intent> Agent::tick(const TickContext& tc,
                                  std::span<const AgentEvent> global_events) {
    // 1. 事件派发：私有队列先排空（deque::swap O(1)，无分配，CS-§7.5）
    if (!private_queue_.empty()) {
        std::deque<AgentEvent> pending;
        pending.swap(private_queue_);
        for (const auto& e : pending)
            route_event(e);
    }
    for (const auto& e : global_events)
        route_event(e);

    // 2. 根 RNG 确定性推进，派生本 tick 种子（RA-§3.7：模块由 rng_seed + 模块 id 派生）
    TickContext local = tc;
    local.rng_seed = rng_();

    // 3. 决策器权威意图：ready 意图直接胜出（RA-§3.4 管线第 2 条）；
    //    ready=false 时跳过，由下方模块候选兜底。
    std::optional<Intent> winner;
    if (decision_maker_ != nullptr) {
        auto intent = decision_maker_->propose(bb_, local);
        if (intent.has_value() && intent->ready)
            winner = std::move(intent);
    }

    // 4. 其余能力候选（scratch_ 复用，每 tick 仅 clear，不分配）
    scratch_.clear();
    for (const auto& cap : caps_) {
        auto intent = cap->propose(bb_, local);
        if (intent.has_value() && intent->ready) {
            scratch_.push_back(Candidate{cap.get(), std::move(*intent)});
        }
    }

    // 5. 无权威意图时：priority 降序；同分严格大于才替换 → 先注册者胜（管线第 3 条）
    if (!winner.has_value() && !scratch_.empty()) {
        std::size_t best = 0;
        for (std::size_t i = 1; i < scratch_.size(); ++i) {
            if (scratch_[i].intent.priority > scratch_[best].intent.priority)
                best = i;
        }
        winner = std::move(scratch_[best].intent);
    }

    // 缓存供观察/断言。payload 均为小结构（字符串 SSO 覆盖常见文本），
    // 拷贝成本可忽略；如未来引入大文本，将改为共享/池化（阶段 6 评估）。
    last_intent_ = winner;
    return last_intent_;
}

void Agent::execute(const Intent& intent) {
    assert(body_ != nullptr);
    assert(intent.ready); // ready=false 不可执行（调用方保证，CS-§9）
    const IntentPayload& p = intent.payload;
    if (std::holds_alternative<MoveIntent>(p)) {
        const auto& m = std::get<MoveIntent>(p);
        body_->move_to(m.target, m.speed);
    } else if (std::holds_alternative<SayIntent>(p)) {
        const auto& s = std::get<SayIntent>(p);
        body_->say(DialogueLine{s.text, s.tone});
    } else if (std::holds_alternative<EmoteIntent>(p)) {
        body_->play_emote(std::get<EmoteIntent>(p).name);
    } else {
        body_->dispatch_game_event(std::get<GameEventIntent>(p).event);
    }
}

const std::optional<Intent>& Agent::last_intent() const {
    return last_intent_;
}

Blackboard& Agent::blackboard() {
    return bb_;
}

const Blackboard& Agent::blackboard() const {
    return bb_;
}

const AgentSnapshot& Agent::last_snapshot() const {
    return last_snapshot_;
}

void Agent::update_snapshot(AgentSnapshot snap) {
    last_snapshot_ = std::move(snap);
}

void Agent::to_json(nlohmann::json& out) const {
    // 配置（与 parse_agent_config 对称）
    nlohmann::json cfg;
    cfg["id"] = config_.id;
    cfg["decision"] = config_.decision_kind;
    cfg["rng_seed"] = config_.rng_seed;
    cfg["perception"] = nlohmann::json{{"radius", config_.perception.radius},
                                       {"sense_type", config_.perception.sense_type}};
    cfg["extra"] = config_.extra;

    nlohmann::json caps = nlohmann::json::array();
    for (const auto& cap : caps_) {
        nlohmann::json entry;
        entry["id"] = cap->id();
        nlohmann::json state;
        cap->to_json(state);
        entry["state"] = std::move(state);
        caps.push_back(std::move(entry));
    }

    // 根 RNG 状态（RA-§3.7：统一持有并随存档序列化）
    std::ostringstream rng_text;
    rng_text << rng_;

    out = nlohmann::json{{"config", std::move(cfg)},
                         {"blackboard", bb_.entries()},
                         {"rng_state", rng_text.str()},
                         {"capabilities", std::move(caps)}};
    if (decision_maker_ != nullptr) {
        nlohmann::json dm;
        dm["id"] = decision_maker_->id();
        nlohmann::json state;
        decision_maker_->to_json(state);
        dm["state"] = std::move(state);
        out["decision_maker"] = std::move(dm);
    }
}

std::optional<Agent> Agent::restore(const nlohmann::json& in, const CapabilityFactory& factory,
                                    std::string& error) {
    if (!in.is_object()) {
        error = "Agent 存档根节点必须是 JSON 对象";
        return std::nullopt;
    }
    if (!in.contains("config") || !in["config"].is_object()) {
        error = "Agent 存档缺少 config 对象";
        return std::nullopt;
    }
    AgentConfig cfg;
    if (auto err = parse_agent_config(in["config"], "<save>", cfg); err.has_value()) {
        error = to_string(*err);
        return std::nullopt;
    }
    Agent agent(std::move(cfg));

    if (in.contains("blackboard") && in["blackboard"].is_object()) {
        agent.bb_.from_json(in["blackboard"]);
    }
    if (in.contains("rng_state") && in["rng_state"].is_string()) {
        std::istringstream rng_text(in["rng_state"].get<std::string>());
        if (!(rng_text >> agent.rng_)) {
            error = "rng_state 无效: " + in["rng_state"].get<std::string>();
            return std::nullopt;
        }
    }
    if (in.contains("capabilities") && in["capabilities"].is_array()) {
        for (const auto& entry : in["capabilities"]) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string()) {
                error = "capabilities 条目缺少字符串 id";
                return std::nullopt;
            }
            const std::string cap_id = entry["id"].get<std::string>();
            auto cap = factory.create(cap_id);
            if (cap == nullptr) {
                error = "能力未在工厂注册: " + cap_id;
                return std::nullopt;
            }
            cap->from_json(entry.value("state", nlohmann::json::object()));
            agent.caps_.push_back(std::move(cap));
        }
    }
    if (in.contains("decision_maker") && in["decision_maker"].is_object()) {
        const auto& dm = in["decision_maker"];
        if (!dm.contains("id") || !dm["id"].is_string()) {
            error = "decision_maker 缺少字符串 id";
            return std::nullopt;
        }
        const std::string dm_id = dm["id"].get<std::string>();
        auto created = factory.create(dm_id);
        auto* as_dm = created != nullptr ? dynamic_cast<IDecisionMaker*>(created.get()) : nullptr;
        if (as_dm == nullptr) {
            error = "决策器未在工厂注册或未实现 IDecisionMaker: " + dm_id;
            return std::nullopt;
        }
        as_dm->from_json(dm.value("state", nlohmann::json::object()));
        agent.decision_maker_.reset(as_dm);
        created.release();
    }
    return agent;
}

} // namespace npc_agent::core
