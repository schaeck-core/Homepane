#ifndef APP_CPP_NET_CONNECTIONMANAGER_H
#define APP_CPP_NET_CONNECTIONMANAGER_H

#include "WebSocketConnection.h"

#include <map>
#include <memory>
#include <string>

namespace app::net {

// Owns named WebSocketConnection instances (HA, Music Assistant, later
// Voice/Intercom — CPP_MIGRATION_PLAN.md §3.1). Enforces unique names
// unless the caller explicitly opts into replacing an existing one, so a
// repeated dashboard load cannot silently accumulate duplicate clients
// (the exact bug documented for the legacy createWSConnectionNamed()).
class ConnectionManager {
public:
    enum class RegisterOutcome { Created, Replaced, RejectedDuplicate };

    RegisterOutcome register_connection(const std::string& name,
                                         std::unique_ptr<WebSocketConnection> connection,
                                         bool replace_existing = false) {
        auto it = connections_.find(name);
        if (it != connections_.end()) {
            if (!replace_existing) return RegisterOutcome::RejectedDuplicate;
            it->second->close();
            it->second = std::move(connection);
            return RegisterOutcome::Replaced;
        }
        connections_.emplace(name, std::move(connection));
        return RegisterOutcome::Created;
    }

    WebSocketConnection* find(const std::string& name) {
        auto it = connections_.find(name);
        return it == connections_.end() ? nullptr : it->second.get();
    }

    void remove(const std::string& name) {
        auto it = connections_.find(name);
        if (it == connections_.end()) return;
        it->second->close();
        connections_.erase(it);
    }

    size_t count() const { return connections_.size(); }

private:
    // std::map (not unordered_map): deterministic iteration order helps
    // reproducible tests/diagnostics; connection count is always small
    // (HA, Music Assistant, maybe Voice/Intercom later).
    std::map<std::string, std::unique_ptr<WebSocketConnection>> connections_;
};

} // namespace app::net

#endif // APP_CPP_NET_CONNECTIONMANAGER_H
