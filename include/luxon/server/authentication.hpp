#pragma once

#include "global.hpp"

#include <luxon/enet_protocol.hpp>
#include <luxon/photon_types.hpp>

namespace server {
class ServerManager;
class Peer;

photon::OperationResponse authenticate(ServerManager& server_manager, Peer& peer, const photon::OperationRequest& req, bool is_encrypted,
                                       const enet::EnetCommandHeader& cmd_header, bool refresh_token = true);
} // namespace server
