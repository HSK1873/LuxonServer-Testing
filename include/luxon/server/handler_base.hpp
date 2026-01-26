#pragma once

#include "global.hpp"
#include "peer.hpp"

#include <memory>
#include <luxon/photon_types.hpp>
#include <luxon/enet_protocol.hpp>
#include <luxon/enet_peer.hpp>
#include <luxon/photon_crypto.hpp>
#include <luxon/http_parser.hpp>

namespace server {
class ServerManager;

class HandlerBase {
public:
    HandlerBase(ServerManager& server_manager, std::shared_ptr<Peer> peer) : server_manager_(server_manager), peer_(std::move(peer)) {}
    virtual ~HandlerBase();

    virtual void HandleConnect();
    virtual void HandleDisconnect();
    virtual void HandleUpdate();
    virtual void HandleSlowUpdate();
    virtual void HandleENetConnectionStateChange(enet::EnetConnectionState state);
    virtual void HandleENetCommand(const enet::EnetCommand& cmd);
    virtual void HandleHTTPRequest(const HttpRequest& request, const enet::EnetCommandHeader& cmd_header);
    virtual void HandleInitRequest(photon::InitRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header);
    virtual void HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header);
    virtual void HandleInternalOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header);

    const std::shared_ptr<Peer>& get_peer() const { return peer_; }

    void send(const photon::ByteArray& payload, const enet::EnetSendOptions& opt);
    photon::ICryptoProvider *get_crypto() { return peer_->crypto.get(); }

protected:
    ServerManager& server_manager_;
    std::shared_ptr<Peer> peer_;
};
} // namespace server
