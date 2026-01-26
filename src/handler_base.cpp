#include "handler_base.hpp"
#include "global.hpp"
#include "codes.hpp"
#include "peer_persistence.hpp"

#include <string_view>
#include <format>
#include <charconv>
#include <commoncpp/utils.hpp>
#include <luxon/photon_protocol.hpp>
#include <luxon/photon_encryption_handshake.hpp>
#include <luxon/http_parser.hpp>
#include <luxon/visualizer.hpp>

namespace server {
namespace {
std::optional<int> fast_stoi(std::string_view sv, int base = 10) {
    int value = 0;

    // from_chars takes pointers: [data, data + size]
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value, base);

    // Check for errors
    if (ec == std::errc::invalid_argument) {
        // No digits were found
        return std::nullopt;
    } else if (ec == std::errc::result_out_of_range) {
        // Value is too large or too small for an int
        return std::nullopt;
    }

    // Check that the entire string was consumed
    if (ptr != sv.data() + sv.size())
        return std::nullopt;

    return value;
}
} // namespace

HandlerBase::~HandlerBase() {
    if (peer_->persistent)
        store_persistent_peer(server_manager_, std::move(peer_->persistent));
}

void HandlerBase::HandleConnect() { peer_->log->info("Client connected!"); }
void HandlerBase::HandleDisconnect() { peer_->log->info("Client disconnected!"); }

void HandlerBase::HandleUpdate() {}
void HandlerBase::HandleSlowUpdate() {}

void HandlerBase::HandleENetConnectionStateChange(enet::EnetConnectionState state) {
    std::string_view state_name;
    switch (state) {
    case enet::EnetConnectionState::Disconnected:
        state_name = "disconnected";
        break;
    case enet::EnetConnectionState::Connecting:
        state_name = "connecting";
        break;
    case enet::EnetConnectionState::Connected:
        state_name = "connected";
        break;
    case enet::EnetConnectionState::Disconnecting:
        state_name = "disconnecting";
        break;
    case enet::EnetConnectionState::Zombie:
        state_name = "zombie";
        break;
    default:
        state_name = "in unknown state";
    }

    peer_->log->info("Client is now {}", state_name);
}

void HandlerBase::HandleENetCommand(const enet::EnetCommand& cmd) {
    // Try to parse header
    photon::SerMessageHeader header;
    if (!photon::try_parse_header(cmd.payload, header)) {
        // Try to parse as HTTP request
        if (auto expected_request = luxon::parse_raw_http(std::string_view{reinterpret_cast<const char *>(cmd.payload.data()), cmd.payload.size()})) {
            HandleHTTPRequest(*expected_request, cmd.header);
        } else {
            // We don't know what this is!
            peer_->log->warn("Invalid packet ({} bytes in length) received", cmd.payload.size());
            luxon::visualizer::helpers::print_hex_dump(cmd.payload, 2);
        }

        return;
    }

    switch (header.message_type) {
    case photon::SerMessageType::Init: {
        photon::InitRequest req;
        if (!photon::try_parse_init_request(cmd.payload, req)) {
            peer_->log->warn("Invalid init request received");
            return;
        }
        HandleInitRequest(req, header.is_encrypted, cmd.header);
    } break;
    case photon::SerMessageType::Operation: {
        photon::OperationRequest req;
        int16_t ec;
        if (!photon::try_parse_operation_request(cmd.payload, req, ec, get_crypto())) {
            peer_->log->warn("Invalid operation request received; error {}", ec);
            return;
        }
        HandleOperationRequest(req, header.is_encrypted, cmd.header);
    } break;
    case photon::SerMessageType::InternalOperationRequest: {
        photon::OperationRequest req;
        int16_t ec;
        if (!photon::try_parse_internal_operation_request(cmd.payload, req, ec, get_crypto())) {
            peer_->log->warn("Invalid internal operation request received; error {}", ec);
            return;
        }
        HandleInternalOperationRequest(req, header.is_encrypted, cmd.header);
    } break;
    default:
        peer_->log->warn("Invalid message type {} received", static_cast<int>(header.message_type));
        return;
    }
}

void HandlerBase::HandleHTTPRequest(const HttpRequest& request, const enet::EnetCommandHeader& cmd_header) {
    // Check if init request
    if (request.path == "/" && request.method == "POST" && request.query_params.contains("init")) {
        // Translate to fake init request
        {
            // Handle init request by first synthesizing photon init request from it
            photon::InitRequest photon_req{};
            if (request.query_params.contains("app"))
                photon_req.app_id = request.query_params.at("app");
            if (request.query_params.contains("clientversion")) {
                // Parse client version
                const auto version_numbers = common::utils::str_split(request.query_params.at("clientversion"), '.');
                if (version_numbers.size() == 4) {
                    photon_req.version_major = fast_stoi(version_numbers[0]).value_or(0);
                    photon_req.version_minor = fast_stoi(version_numbers[1]).value_or(0);
                    photon_req.version_revision = fast_stoi(version_numbers[2]).value_or(0);
                    photon_req.version_patch = fast_stoi(version_numbers[3]).value_or(0);
                }
            }
            if (request.query_params.contains("protocol")) {
                // Parse protocol version
                constexpr std::string_view prefix = "GpBinaryV";
                const std::string& protocol_string = request.query_params.at("protocol");
                if (protocol_string.starts_with(prefix)) {
                    // Remove prefix
                    const std::string_view binary_version = std::string_view(protocol_string).substr(prefix.size());
                    if (binary_version.size() == 2) {
                        photon_req.protocol_major = binary_version[0] - '0';
                        photon_req.protocol_minor = binary_version[1] - '0';
                    }
                }
            }

            // Pass the synthesized init request to our handler
            HandleInitRequest(photon_req, false, cmd_header);
        }

        // Translate to fake authenticate operation request
        if (request.body.size() > 4) {
            std::string token = request.body;
            token.erase(0, 2);

            photon::OperationRequest photon_req{.operation_code = OpCodes::Auth::AuthenticateOnce};
            photon_req.parameters[DictKeyCodes::LoadBalancing::Token] = token;
            HandleOperationRequest(photon_req, false, cmd_header);
        }
    } else {
        // We don't know what this HTTP request is!
        peer_->log->warn("Invalid HTTP request received");
        luxon::visualizer::print_http_message(request, 2);
    }
}

void HandlerBase::HandleInitRequest(photon::InitRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    // Answer init request
    const bool ok = req.protocol_major == 1 && req.protocol_minor == 8;
    if (ok) {
        send(photon::serialize_init_response(), enet::EnetSendOptions{cmd_header.channel_id});
        peer_->log->info("Connection init complete");
    } else {
        peer_->log->error("Connection init failed: Protocol mismatch");
        peer_->disconnect();
    }
}

void HandlerBase::HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    // Only answer unknown operations on channel 0
    if (cmd_header.channel_id != 0)
        return;

    // Handle authentication requests that are coming through despite peer already being authenticated
    if (req.operation_code == OpCodes::Auth::Authenticate && peer_->is_authenticated()) {
        photon::OperationResponse resp{
            .operation_code = req.operation_code, .return_code = ErrorCodes::Core::OperationNotAllowedInCurrentState, .debug_message = "Already authenticated"};
        send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
        return;
    }

    photon::OperationResponse resp{.operation_code = req.operation_code,
                                   .return_code = ErrorCodes::Core::OperationInvalid,
                                   .debug_message = std::format("Unsupported operation {}", req.operation_code)};
    send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
    peer_->log->warn("Client sent operation request with unknown opcode: {}", req.operation_code);
}

void HandlerBase::HandleInternalOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    if (cmd_header.channel_id != 0)
        return;

    if (req.operation_code == photon::PhotonCodes::IOpInitEncryption) {
        // Answer crypto handshake
        photon::OperationResponse resp = photon::handle_init_encryption_request(req, *peer_->crypto);
        send(photon::serialize_internal_operation_response(resp, false, get_crypto()), enet::EnetSendOptions{0});

        peer_->log->info("Established encryption");
    } else if (req.operation_code == photon::PhotonCodes::IOpPing) {
        // Answer internal pings
        photon::OperationResponse resp;
        resp.operation_code = photon::PhotonCodes::IOpPing;
        resp.return_code = ErrorCodes::Core::Ok;

        const photon::Value& client_ts = req.parameters[photon::PhotonCodes::IKeyClientTimestamp];
        resp.parameters[photon::PhotonCodes::IKeyClientTimestamp] = client_ts;
        resp.parameters[photon::PhotonCodes::IKeyServerTimestamp] = static_cast<int32_t>(peer_->enet_peer->get_server_time());
        peer_->log->info("Got internal operation ping: TS={}", client_ts.get<int32_t>());

        send(photon::serialize_internal_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
    } else {
        // Answer unknown operation
        photon::OperationResponse resp{.operation_code = req.operation_code,
                                       .return_code = ErrorCodes::Core::OperationInvalid,
                                       .debug_message = std::format("Unsupported internal operation {}", req.operation_code)};
        send(photon::serialize_internal_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
        peer_->log->warn("Client sent internal operation request with unknown opcode: {}", req.operation_code);
    }
}

void HandlerBase::send(const photon::ByteArray& payload, const enet::EnetSendOptions& opt) { peer_->send(payload, opt); }
} // namespace server
