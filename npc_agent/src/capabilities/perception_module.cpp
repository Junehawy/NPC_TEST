#include "npc_agent/capabilities/perception_module.h"

#include <utility>
#include <vector>

namespace npc_agent::capabilities {

namespace {
constexpr std::string_view kStimulusPrefix = "stimulus.";
constexpr std::string_view kHeardPrefix = "heard_";
} // namespace

PerceptionModule::PerceptionModule(PerceptionModuleParams params) : params_(params) {
}

void PerceptionModule::on_event(const core::AgentEvent& e) {
    if (e.type.rfind(kStimulusPrefix, 0) != 0)
        return; // 只订阅刺激事件
    StimulusRecord record;
    record.type = e.type.substr(kStimulusPrefix.size());
    record.source = e.source;
    record.game_time = e.game_time;
    record.payload = e.payload;
    recent_.push_back(std::move(record));
    while (recent_.size() > params_.max_stimuli)
        recent_.pop_front();
}

void PerceptionModule::on_tick(core::Blackboard& bb, const TickContext& tc) {
    prune(tc.game_time);
    write_flags(bb);
    write_perception(bb);
}

std::size_t PerceptionModule::prune(double game_time) {
    while (!recent_.empty() &&
           game_time - recent_.front().game_time > params_.stimulus_window_seconds) {
        recent_.pop_front();
    }
    return recent_.size();
}

void PerceptionModule::write_flags(core::Blackboard& bb) {
    // 活跃类型集合（窗口内的记录）。
    std::vector<std::string> active_types;
    active_types.reserve(recent_.size());
    for (const auto& record : recent_) {
        bool exists = false;
        for (const auto& type : active_types)
            exists = exists || (type == record.type);
        if (!exists)
            active_types.push_back(record.type);
    }
    // 置位：窗口内有记录的类型。
    for (const auto& type : active_types)
        bb.set(std::string(kHeardPrefix) + type, true);
    // 复位：既有 heard_* 旗标中已过期/不存在的类型（键保留为 false，条件可判否）。
    for (const auto& [key, value] : bb.entries()) {
        if (key.rfind(kHeardPrefix, 0) != 0 || !value.is_boolean() || !value.get<bool>())
            continue;
        const std::string type = key.substr(kHeardPrefix.size());
        bool active = false;
        for (const auto& candidate : active_types)
            active = active || (candidate == type);
        if (!active)
            bb.set(key, false);
    }
}

void PerceptionModule::write_perception(core::Blackboard& bb) {
    nlohmann::json stimuli = nlohmann::json::array();
    for (const auto& record : recent_) {
        stimuli.push_back(nlohmann::json{{"type", record.type},
                                         {"source", record.source},
                                         {"game_time", record.game_time},
                                         {"payload", record.payload}});
    }
    bb.set("perception", nlohmann::json{{"stimuli", std::move(stimuli)},
                                        {"window_seconds", params_.stimulus_window_seconds}});
}

void PerceptionModule::to_json(nlohmann::json& out) const {
    nlohmann::json stimuli = nlohmann::json::array();
    for (const auto& record : recent_) {
        stimuli.push_back(nlohmann::json{{"type", record.type},
                                         {"source", record.source},
                                         {"game_time", record.game_time},
                                         {"payload", record.payload}});
    }
    out = nlohmann::json{{"stimuli", std::move(stimuli)}};
}

void PerceptionModule::from_json(const nlohmann::json& in) {
    recent_.clear();
    if (!in.is_object() || !in.contains("stimuli") || !in["stimuli"].is_array())
        return;
    for (const auto& entry : in["stimuli"]) {
        if (!entry.is_object())
            continue;
        StimulusRecord record;
        record.type = entry.value("type", "");
        record.source = entry.value("source", "");
        record.game_time = entry.value("game_time", 0.0);
        record.payload = entry.value("payload", nlohmann::json::object());
        if (!record.type.empty())
            recent_.push_back(std::move(record));
    }
    while (recent_.size() > params_.max_stimuli)
        recent_.pop_front();
}

} // namespace npc_agent::capabilities
