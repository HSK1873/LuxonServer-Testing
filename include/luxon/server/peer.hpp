#pragma once

#include "global.hpp"
#include "logger.hpp"
#include "peer_persistence.hpp"

#include <memory>
#include <luxon/enet_peer.hpp>
#include <luxon/photon_protocol.hpp>
#include <luxon/photon_crypto.hpp>

namespace server {
struct PeerPersistent;

struct Peer {
    std::shared_ptr<enet::EnetPeer> enet_peer;
    std::shared_ptr<logger> log;
    std::unique_ptr<photon::ICryptoProvider> crypto;
    std::unique_ptr<PeerPersistent> persistent;

    bool is_authenticated() const { return persistent != nullptr; }
    void send(const photon::ByteArray& payload, const enet::EnetSendOptions& opt);
    void disconnect();
};
} // namespace server
