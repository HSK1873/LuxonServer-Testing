#include "handler_nameserver.hpp"
#include "global.hpp"
#include "server_manager.hpp"
#include "authentication.hpp"
#include "codes.hpp"

#include <luxon/photon_protocol.hpp>

namespace server {
void NameServerHandler::HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    if (cmd_header.channel_id != 0)
        return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);

    if (!peer_->is_authenticated()) {
        switch (req.operation_code) {

        case OpCodes::Auth::Authenticate:
        case OpCodes::Auth::AuthenticateOnce: {
            // Try to authenticate
            auto resp = authenticate(server_manager_, *peer_, req, is_encrypted, cmd_header);

            // Add details if authentication was successful
            if (resp.return_code == ErrorCodes::Core::Ok) {
                resp.parameters[DictKeyCodes::LoadBalancing::UserId] = peer_->persistent->user_id;
                resp.parameters[DictKeyCodes::LoadBalancing::Address] = server_manager_.get_endpoint_of(ServerType::MasterServer);
            }

            // Send payload
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            return;
        }

        case OpCodes::RpcAndMisc::GetRegions: {
            // Give dummy response  TODO: Give real response
            photon::OperationResponse resp{.operation_code = OpCodes::RpcAndMisc::GetRegions, .return_code = 0};
            resp.parameters[DictKeyCodes::AuthAndLobby::Region] = std::vector<std::string>{"eu"};
            resp.parameters[DictKeyCodes::LoadBalancing::Address] = std::vector<std::string>{server_manager_.get_endpoint_of(ServerType::MasterServer)};
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            return;
        }
        }
    }

    return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);
}
} // namespace server
