#include "npc_agent/testing/intent_desc.h"

#include <variant>

namespace npc_agent::testing {

std::string describe_intent(const std::optional<Intent>& intent) {
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

} // namespace npc_agent::testing
