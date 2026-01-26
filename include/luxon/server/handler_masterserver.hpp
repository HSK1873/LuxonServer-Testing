#pragma once

#include "global.hpp"
#include "handler_base.hpp"

#include <unordered_set>
#include <functional>
#include <optional>
#include <list>
#include <commoncpp/timer.hpp>
#include <luxon/photon_types.hpp>

namespace server {
struct Lobby;

class MasterServerHandler : public HandlerBase {
public:
    using HandlerBase::HandlerBase;

    void HandleDisconnect() override;
    void HandleSlowUpdate() override;
    void HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) override;

protected:
    Lobby *lobby_;
    common::Timer last_app_stats_;
    bool wants_app_stats_ = false;
    std::optional<std::list<GameListUpdateHandler>::iterator> game_list_update_handler_;

    void join_lobby(Lobby *lobby);
    void leave_lobby();
    void send_app_stats();
    photon::Dictionary get_lobby_stats(std::function<bool(const Lobby&)> lobby_filter = nullptr);
    void send_lobby_stats();
    photon::HashtablePtr get_game_list(std::function<bool(const Lobby&)> lobby_filter = nullptr, std::function<bool(const Game&)> game_filter = nullptr);
    photon::HashtablePtr get_game_list(std::function<bool(const Game&)> game_filter) { return get_game_list(nullptr, game_filter); }
    void send_game_list();
};
} // namespace server
