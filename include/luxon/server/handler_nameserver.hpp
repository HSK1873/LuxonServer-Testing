#pragma once

#include "global.hpp"
#include "handler_base.hpp"

namespace server {
class NameServerHandler : public HandlerBase {
public:
    using HandlerBase::HandlerBase;

    void HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) override;
};
} // namespace server
