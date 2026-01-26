#pragma once

#include "global.hpp"
#include "handler_base.hpp"

#include <luxon/photon_types.hpp>

namespace server {
class GameServerHandler : public HandlerBase {
public:
    using HandlerBase::HandlerBase;

    void HandleDisconnect() override;
    void HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) override;

    auto& get_game() { return peer_->persistent->current_game; }

protected:
    GamePeer *game_peer_{};
    bool has_left{};
};
} // namespace server
