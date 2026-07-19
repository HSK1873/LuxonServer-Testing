// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "peer.hpp"
#include "coro_support.hpp"

#include <memory>
#include <luxon/ser_types.hpp>
#include <luxon/enet_protocol.hpp>
#include <luxon/enet_peer.hpp>
#include <luxon/ser_encryption.hpp>
#include <luxon/http_parser.hpp>

namespace server {
class ServerManager;

class HandlerBase {
public:
    HandlerBase(ServerManager& server_manager, std::shared_ptr<Peer> peer) : server_manager_(server_manager), peer_(std::move(peer)), proto_(peer_->protocol) {}
    virtual ~HandlerBase();

    virtual Awaitable<> HandleConnect();
    virtual Awaitable<> HandleDisconnect();
    virtual void HandleSlowUpdate();
    virtual Awaitable<> HandleENetConnectionStateChange(enet::EnetConnectionState state);
    virtual Awaitable<> HandleENetCommand(enet::EnetCommand&& cmd);
    virtual Awaitable<> HandleHTTPRequest(HttpRequest&& request, const enet::EnetCommandHeader& cmd_header);
    virtual Awaitable<> HandleInitRequest(ser::InitMessage&& req, const enet::EnetCommandHeader& cmd_header);
    virtual Awaitable<> HandleOperationRequest(ser::OperationRequestMessage&& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header);
    virtual Awaitable<> HandleInternalOperationRequest(ser::InternalOperationRequestMessage&& req, bool is_encrypted,
                                                       const enet::EnetCommandHeader& cmd_header);

    const std::shared_ptr<Peer>& get_peer() const { return peer_; }
    ServerManager& get_server_manager() const { return server_manager_; }

    void send(const ser::ByteArray& payload, const enet::EnetSendOptions& opt = {0});
    void send(const std::expected<ser::ByteArray, ser::Error>& expected_payload, const enet::EnetSendOptions& opt = {0});

    void set_allow_unsolicited(bool enable = true) { allow_unsolicited_ = enable; }

protected:
    ServerManager& server_manager_;
    std::shared_ptr<Peer> peer_;
    std::unique_ptr<ser::IProtocol>& proto_;
    bool allow_unsolicited_ = false;
};
} // namespace server
