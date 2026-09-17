#include "amlp/net/InteractiveRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include <algorithm>

namespace amlp {

namespace {
struct Entry {
    std::weak_ptr<LpcObject> obj;
    Connection* conn;
};
std::vector<Entry> g_registry;
}

void InteractiveRegistry::add(const std::shared_ptr<LpcObject>& obj, Connection* conn) {
    if (!obj) return;
    g_registry.push_back(Entry{obj, conn});
}

void InteractiveRegistry::remove(const std::shared_ptr<LpcObject>& obj) {
    if (!obj) return;
    g_registry.erase(
        std::remove_if(g_registry.begin(), g_registry.end(),
                        [&obj](const Entry& e) {
                            auto locked = e.obj.lock();
                            return !locked || locked == obj;
                        }),
        g_registry.end());
}

std::vector<std::shared_ptr<LpcObject>> InteractiveRegistry::all() {
    std::vector<std::shared_ptr<LpcObject>> result;
    result.reserve(g_registry.size());
    for (auto& e : g_registry) {
        if (auto locked = e.obj.lock()) result.push_back(std::move(locked));
    }
    return result;
}

Connection* InteractiveRegistry::find(const std::shared_ptr<LpcObject>& obj) {
    if (!obj) return nullptr;
    for (auto& e : g_registry) {
        auto locked = e.obj.lock();
        if (locked == obj) return e.conn;
    }
    return nullptr;
}

} // namespace amlp
