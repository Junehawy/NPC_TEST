#include "npc_agent/core/capability_factory.h"

#include <utility>

namespace npc_agent::core {

void CapabilityFactory::register_creator(std::string id, Creator creator) {
    creators_[std::move(id)] = std::move(creator);
}

std::unique_ptr<ICapability> CapabilityFactory::create(std::string_view id) const {
    const auto it = creators_.find(id);
    if (it == creators_.end() || !it->second)
        return nullptr;
    return it->second();
}

bool CapabilityFactory::knows(std::string_view id) const {
    return creators_.find(id) != creators_.end();
}

} // namespace npc_agent::core
