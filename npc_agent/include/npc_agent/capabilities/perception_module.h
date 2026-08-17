// PerceptionModule —— 感知模块（npc_agent/capabilities/，阶段 2，RA 路线图 2.x 第 2 条）：
// on_event 订阅刺激事件（type 前缀 "stimulus."，由 AgentSystem::inject_stimulus 注入），
// 记录近期刺激（环缓冲）；on_tick（ICapability R9 钩子）打包写入黑板：
//   - "heard_<type>"：布尔旗标，最近 window_seconds 内出现过该类型刺激
//     （供 FSM/条件求值使用：{"bb": {"heard_gunshot": true}}）；
//   - "perception"：{"stimuli":[{type,source,game_time,payload}...], "window_seconds": N}
//     供决策器/LLM 提示构建消费的打包视图。
// propose 恒为 nullopt（感知模块不产出行为候选）。
// 确定性（RA-§3.7）：旗标衰减仅依据 tc.game_time，无随机源；位置等空间信息由宿主
// 放入刺激 payload（v1 约定，事件 payload 透传自 Stimulus::payload）。
// 线程契约：全部方法【驱动线程】。
#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/core/event.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::capabilities {

struct PerceptionModuleParams {
    double stimulus_window_seconds = 5.0; // 刺激旗标保持时长（秒）
    std::size_t max_stimuli = 16;         // 近期刺激环缓冲容量
};

class PerceptionModule final : public ICapability {
public:
    explicit PerceptionModule(PerceptionModuleParams params = {});

    std::string_view id() const override { return "perception"; }

    void on_event(const core::AgentEvent& e) override;
    void on_tick(core::Blackboard& bb, const TickContext& tc) override;
    std::optional<Intent> propose(const core::Blackboard&, const TickContext&) override {
        return std::nullopt;
    }

    void to_json(nlohmann::json& out) const override;  // 近期刺激记录
    void from_json(const nlohmann::json& in) override; // 对称恢复（参数由构造注入）

private:
    struct StimulusRecord {
        std::string type; // 去掉 "stimulus." 前缀的类型（如 gunshot）
        std::string source;
        double game_time = 0.0;
        nlohmann::json payload = nlohmann::json::object();
    };

    // 清除超过时间窗的记录；返回仍在窗口内的记录数。
    std::size_t prune(double game_time);
    void write_flags(core::Blackboard& bb);
    void write_perception(core::Blackboard& bb);

    PerceptionModuleParams params_;
    std::deque<StimulusRecord> recent_; // 环缓冲（容量 params_.max_stimuli）
};

} // namespace npc_agent::capabilities
