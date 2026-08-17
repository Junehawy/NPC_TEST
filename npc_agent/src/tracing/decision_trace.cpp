#include "npc_agent/tracing/decision_trace.h"

#include <variant>

namespace npc_agent::tracing {

nlohmann::json intent_to_json(const std::optional<Intent>& intent) {
    if (!intent.has_value())
        return nlohmann::json(nullptr);
    nlohmann::json out;
    out["ready"] = intent->ready;
    out["priority"] = intent->priority;
    if (std::holds_alternative<MoveIntent>(intent->payload)) {
        const auto& m = std::get<MoveIntent>(intent->payload);
        out["payload"] =
            nlohmann::json{{"kind", "move_to"},
                           {"target", nlohmann::json::array({m.target.x, m.target.y, m.target.z})},
                           {"speed", m.speed}};
    } else if (std::holds_alternative<SayIntent>(intent->payload)) {
        const auto& s = std::get<SayIntent>(intent->payload);
        out["payload"] = nlohmann::json{{"kind", "say"}, {"text", s.text}, {"tone", s.tone}};
    } else if (std::holds_alternative<EmoteIntent>(intent->payload)) {
        out["payload"] = nlohmann::json{{"kind", "emote"},
                                        {"name", std::get<EmoteIntent>(intent->payload).name}};
    } else {
        const auto& g = std::get<GameEventIntent>(intent->payload);
        out["payload"] = nlohmann::json{
            {"kind", "game_event"}, {"type", g.event.type}, {"payload", g.event.payload}};
    }
    return out;
}

void DecisionTrace::append(nlohmann::json line) {
    lines_.push_back(std::move(line));
}

const std::vector<nlohmann::json>& DecisionTrace::lines() const {
    return lines_;
}

std::size_t DecisionTrace::size() const {
    return lines_.size();
}

void DecisionTrace::clear() {
    lines_.clear();
}

std::string DecisionTrace::dump() const {
    std::string out;
    out.reserve(lines_.size() * 128); // 粗略预留（行体较小，减少重分配）
    for (const auto& line : lines_) {
        out += line.dump();
        out += '\n';
    }
    return out;
}

bool DecisionTrace::load(std::string_view text, DecisionTrace& out, std::string& error) {
    DecisionTrace trace;
    std::size_t line_no = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line =
            end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
        ++line_no;
        if (!line.empty()) {
            const nlohmann::json parsed =
                nlohmann::json::parse(line, nullptr, false); // 非抛接口（CS-§9）
            if (parsed.is_discarded()) {
                error = "第 " + std::to_string(line_no) + " 行 JSON 解析失败: " + std::string(line);
                return false;
            }
            trace.append(parsed);
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    out = std::move(trace);
    return true;
}

std::optional<std::string> DecisionTrace::compare(const DecisionTrace& a, const DecisionTrace& b,
                                                  std::size_t* first_diff) {
    const std::size_t common = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < common; ++i) {
        if (a.lines_[i] != b.lines_[i]) {
            if (first_diff != nullptr)
                *first_diff = i;
            return "trace 分歧于第 " + std::to_string(i) + " 行（tick 序列不一致）\n" +
                   "  录制: " + a.lines_[i].dump() + "\n" + "  回放: " + b.lines_[i].dump();
        }
    }
    if (a.size() != b.size()) {
        if (first_diff != nullptr)
            *first_diff = common;
        return "trace 行数不一致: 录制 " + std::to_string(a.size()) + " 行, 回放 " +
               std::to_string(b.size()) + " 行";
    }
    if (first_diff != nullptr)
        *first_diff = static_cast<std::size_t>(-1);
    return std::nullopt; // 严格一致（R8 阈值）
}

} // namespace npc_agent::tracing
