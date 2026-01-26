#include "peer.hpp"
#include "global.hpp"

#include <luxon/visualizer.hpp>

namespace server {
void Peer::send(const photon::ByteArray& payload, const enet::EnetSendOptions& opt) {
#ifndef NDEBUG
    log->trace("Sending message using mode {} on channel {}:", static_cast<int>(opt.mode), opt.channel);
    visualizer::print_photon_message(payload, 2, crypto.get());
#endif
    enet_peer->send_payload(payload, opt);
}

void Peer::disconnect() { enet_peer->disconnect(); }
} // namespace server
