#include "amlp/object/LiveObjectRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/vm/Value.hpp"
#include <algorithm>

namespace amlp {

namespace {
std::vector<std::weak_ptr<LpcObject>> g_registry;
}

void LiveObjectRegistry::add(const std::shared_ptr<LpcObject>& obj) {
    if (!obj) return;
    g_registry.push_back(obj);
}

void LiveObjectRegistry::remove(const std::shared_ptr<LpcObject>& obj) {
    if (!obj) return;
    g_registry.erase(
        std::remove_if(g_registry.begin(), g_registry.end(),
                        [&obj](const std::weak_ptr<LpcObject>& w) {
                            auto locked = w.lock();
                            return !locked || locked == obj;
                        }),
        g_registry.end());
}

std::vector<std::shared_ptr<LpcObject>> LiveObjectRegistry::all() {
    std::vector<std::shared_ptr<LpcObject>> result;
    result.reserve(g_registry.size());
    for (auto& w : g_registry) {
        if (auto locked = w.lock()) {
            if (!locked->isDestructed()) result.push_back(std::move(locked));
        }
    }
    return result;
}

void LiveObjectRegistry::releaseAll() {
    // Iterate every entry, destructed or not: a destructObject() leaves
    // an object's own variables in place (only reload_object() zeroes
    // them), so a destructed-but-cycle-pinned object still needs the
    // same break here. reload_object()'s own "back to int 0" is the
    // shape reused (ObjectManager::reloadObject()), not a raw vector
    // clear, so the variable slot count stays consistent with the
    // object's program in the unlikely event anything reads it during
    // its own later teardown.
    for (auto& w : g_registry) {
        if (auto ob = w.lock()) {
            for (auto& v : ob->variables()) v = Value(int64_t{0});
            ob->inventory().clear();
        }
    }
    g_registry.clear();
    g_registry.shrink_to_fit();
}

} // namespace amlp
