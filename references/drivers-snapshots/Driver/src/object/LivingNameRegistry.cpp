#include "amlp/object/LivingNameRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include <algorithm>
#include <vector>

namespace amlp {

namespace {
struct Entry {
    std::weak_ptr<LpcObject> obj;
    std::string name;
};
std::vector<Entry> g_registry;
}

void LivingNameRegistry::set(const std::shared_ptr<LpcObject>& obj, const std::string& name) {
    if (!obj) return;
    remove(obj);
    g_registry.push_back(Entry{obj, name});
}

void LivingNameRegistry::remove(const std::shared_ptr<LpcObject>& obj) {
    if (!obj) return;
    g_registry.erase(
        std::remove_if(g_registry.begin(), g_registry.end(),
                        [&obj](const Entry& e) {
                            auto locked = e.obj.lock();
                            return !locked || locked == obj;
                        }),
        g_registry.end());
}

std::shared_ptr<LpcObject> LivingNameRegistry::find(const std::string& name, bool requireOnceInteractive) {
    for (auto& e : g_registry) {
        if (e.name != name) continue;
        auto locked = e.obj.lock();
        if (!locked || locked->isDestructed()) continue;
        if (requireOnceInteractive && !locked->wasEverInteractive()) continue;
        if (!locked->commandsEnabled()) continue;
        return locked;
    }
    return nullptr;
}

std::vector<std::shared_ptr<LpcObject>> LivingNameRegistry::allWithCommandsEnabled() {
    std::vector<std::shared_ptr<LpcObject>> result;
    for (auto& e : g_registry) {
        auto locked = e.obj.lock();
        if (!locked || locked->isDestructed()) continue;
        if (!locked->commandsEnabled()) continue;
        result.push_back(std::move(locked));
    }
    return result;
}

} // namespace amlp
