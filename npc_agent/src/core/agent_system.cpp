#include "npc_agent/core/agent_system.h"

#include <utility>

namespace npc_agent::core {

void AgentSystem::set_current_world(IWorld& world) {
    world_ = &world;
}

IWorld* AgentSystem::current_world() const {
    return world_;
}

void AgentSystem::set_capability_factory(const CapabilityFactory* factory) {
    factory_ = factory;
}

void AgentSystem::tick() {
    if (world_ == nullptr)
        return;
    const TickContext tc = world_->tick_context();

    // 全局事件一次性取出（global_scratch_ 复用，仅 clear）
    global_scratch_.clear();
    if (!global_queue_.empty()) {
        global_scratch_.assign(global_queue_.begin(), global_queue_.end());
        global_queue_.clear();
    }

    for (const auto& agent : agents_) {
        // 感知注入（R5-3）：AgentSystem 代执行 IWorld 查询并写入黑板；
        // 能力模块只从黑板读取，不持有 IWorld（Blackboard-only 契约）。
        const AgentConfig& cfg = agent->config();
        if (cfg.perception.radius > 0.0f) {
            PerceptionQuery query;
            query.origin = agent->body_state().position;
            query.radius = cfg.perception.radius;
            query.sense_type = cfg.perception.sense_type;
            const PerceptionResult result = world_->sense(query);
            nlohmann::json ids = nlohmann::json::array();
            for (const auto& entity : result.entities)
                ids.push_back(entity.id);
            agent->blackboard().set("perceived_entities", std::move(ids));
        }

        // 快照组装（RA-§3.1）：工作线程/决策日志/存档复用，绝不在 tick 外传递接口
        AgentSnapshot snap;
        snap.world = world_->snapshot();
        snap.self = agent->body_state();
        agent->update_snapshot(std::move(snap));

        // 决策 → 执行（ready=false 的意图不执行，由决策器内部兜底语义处理）
        const auto intent = agent->tick(tc, global_scratch_);
        if (intent.has_value() && intent->ready)
            agent->execute(*intent);
    }
}

void AgentSystem::inject_stimulus(const Stimulus& s) {
    if (world_ != nullptr)
        world_->inject_stimulus(s);
    AgentEvent e;
    e.type = "stimulus." + s.type;
    e.source = s.source_id;
    e.payload = s.payload;
    e.game_time = world_ != nullptr ? world_->tick_context().game_time : 0.0;
    global_queue_.push_back(std::move(e));
}

void AgentSystem::broadcast(AgentEvent e) {
    global_queue_.push_back(std::move(e));
}

Agent& AgentSystem::create_agent(AgentConfig config, IAgentBody& body) {
    auto agent = std::make_unique<Agent>(std::move(config));
    agent->attach_body(body);
    agents_.push_back(std::move(agent));
    return *agents_.back();
}

Agent* AgentSystem::find_agent(std::string_view id) {
    for (const auto& agent : agents_) {
        if (agent->id() == id)
            return agent.get();
    }
    return nullptr;
}

std::size_t AgentSystem::agent_count() const {
    return agents_.size();
}

const std::vector<std::unique_ptr<Agent>>& AgentSystem::agents() const {
    return agents_;
}

nlohmann::json AgentSystem::to_json() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& agent : agents_) {
        nlohmann::json entry;
        agent->to_json(entry);
        list.push_back(std::move(entry));
    }
    return nlohmann::json{{"agents", std::move(list)}};
}

bool AgentSystem::restore(const nlohmann::json& in, AgentSystem& out, std::string& error) {
    if (!in.is_object()) {
        error = "AgentSystem 存档根节点必须是 JSON 对象";
        return false;
    }
    if (!in.contains("agents") || !in["agents"].is_array()) {
        error = "AgentSystem 存档缺少 agents 数组";
        return false;
    }
    if (out.factory_ == nullptr) {
        error = "未设置 CapabilityFactory（先 set_capability_factory）";
        return false;
    }

    std::vector<std::unique_ptr<Agent>> restored;
    for (const auto& entry : in["agents"]) {
        auto agent = Agent::restore(entry, *out.factory_, error);
        if (!agent.has_value())
            return false;
        restored.push_back(std::make_unique<Agent>(std::move(*agent)));
    }
    out.agents_ = std::move(restored);
    return true;
}

} // namespace npc_agent::core
