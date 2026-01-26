#include "handler_masterserver.hpp"
#include "global.hpp"
#include "handler_gameserver.hpp"
#include "server_manager.hpp"
#include "authentication.hpp"
#include "codes.hpp"
#include "lobby.hpp"

#include <string>
#include <random>
#include <algorithm>
#include <luxon/photon_protocol.hpp>

namespace server {
namespace {
std::string generate_game_id(std::string prefix) {
    static std::mt19937 gen{std::random_device{}()};
    const std::string_view charset = "0123456789";
    std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);

    std::string suffix(4, '\0');
    std::ranges::generate(suffix, [&] { return charset[dist(gen)]; });
    return prefix + '#' + suffix;
}
} // namespace

void MasterServerHandler::HandleDisconnect() { leave_lobby(); }

void MasterServerHandler::HandleSlowUpdate() {
    if (wants_app_stats_ && last_app_stats_.get() > 8000) {
        send_app_stats();
        last_app_stats_.reset();
    }

    HandlerBase::HandleSlowUpdate();
}

void MasterServerHandler::HandleOperationRequest(photon::OperationRequest& req, bool is_encrypted, const enet::EnetCommandHeader& cmd_header) {
    if (cmd_header.channel_id != 0)
        return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);

    if (!peer_->is_authenticated()) {
        switch (req.operation_code) {

        case OpCodes::Auth::Authenticate:
        case OpCodes::Auth::AuthenticateOnce: {
            // Does the client want lobby stats?
            const bool wants_lobby_stats = req.parameters[DictKeyCodes::AuthAndLobby::LobbyStats].get_or<bool>(false);

            // Try to authenticate
            auto resp = authenticate(server_manager_, *peer_, req, is_encrypted, cmd_header);

            // Add details if authentication was successful
            if (resp.return_code == ErrorCodes::Core::Ok)
                resp.parameters[DictKeyCodes::LoadBalancing::Position] = static_cast<int32_t>(0);

            // Send response
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});

            // Handle successful authentication
            if (peer_->is_authenticated()) {
                auto& app = peer_->persistent->app;

                // Remove player from current game
                peer_->persistent->current_game.reset();

                // Get default lobby
                Lobby *default_lobby = app->get_default_lobby();
                if (!default_lobby) {
                    peer_->log->error("Application has no lobbies. Connection must terminate now.");
                    peer_->disconnect();
                    return;
                }

                // Join default lobby
                join_lobby(default_lobby);

                // Send stats once if requested
                wants_app_stats_ = wants_lobby_stats;
                if (wants_lobby_stats)
                    send_app_stats();
            }
            return;
        }
        }
    } else {
        switch (req.operation_code) {

        case OpCodes::Lobby::JoinLobby: {
            // Get lobby name to join
            const auto lobby_name = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName].get_or<std::string>("");

            // Find lobby
            Lobby *joined_lobby{};
            auto& app = *peer_->persistent->app;
            for (Lobby *lobby : app.get_lobbies())
                if (lobby->name == lobby_name)
                    joined_lobby = lobby;

            // Send response
            photon::OperationResponse resp;
            if (joined_lobby == nullptr) {
                // Lobby not found
                resp = {
                    .operation_code = OpCodes::Lobby::JoinLobby, .return_code = ErrorCodes::Data::InvalidRequestParameters, .debug_message = "Lobby not found"};
            } else {
                // Join the lobby
                join_lobby(joined_lobby);
                resp = {.operation_code = OpCodes::Lobby::JoinLobby, .return_code = ErrorCodes::Core::Ok};
            }
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            peer_->log->info("Joined lobby: {}", lobby_name.empty() ? "(unnamed)" : lobby_name);

            // Send game list
            send_game_list();
            return;
        }

        case OpCodes::Lobby::LeaveLobby: {
            // Get lobby name to leave
            const auto& lobby_name_param = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName];

            // Throw error if lobby name not given correctly
            if (!lobby_name_param.is<std::string>()) {
                photon::OperationResponse resp{.operation_code = OpCodes::Lobby::LeaveLobby,
                                               .return_code = ErrorCodes::Data::InvalidRequestParameters,
                                               .debug_message = "Bad parameter: LobbyName"};
                send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
                return;
            }

            // Actually leave lobby
            leave_lobby();

            // Send response
            photon::OperationResponse resp{.operation_code = OpCodes::Lobby::LeaveLobby, .return_code = ErrorCodes::Core::Ok};
            if (lobby_ == nullptr)
                // Lobby not found
                resp.debug_message = "Not in lobby";
            else
                // Everything is ok
                peer_->log->info("Left lobby: {}", lobby_->name.empty() ? "(unnamed)" : lobby_->name);
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            return;
        }

        case OpCodes::Lobby::LobbyStats: {
            // Get filters
            const auto& lobby_name_param = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName];
            const auto& lobby_type_param = req.parameters[DictKeyCodes::AuthAndLobby::LobbyType];

            // Build response
            photon::OperationResponse resp{.operation_code = OpCodes::Lobby::LobbyStats};
            resp.parameters = get_lobby_stats([&](const Lobby& lobby) {
                if (lobby_name_param.is<std::string>() && lobby.name != lobby_name_param)
                    return false;
                if (lobby_type_param.is<std::string>() && lobby.type != lobby_type_param)
                    return false;
                return true;
            });

            // Send response
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            return;
        }

        case OpCodes::Lobby::GetGameList: {
            // Get filters
            const auto& lobby_name_param = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName];
            const auto& lobby_type_param = req.parameters[DictKeyCodes::AuthAndLobby::LobbyType];

            // Build response
            photon::OperationResponse resp{.operation_code = OpCodes::Lobby::GetGameList};
            resp.parameters[DictKeyCodes::LoadBalancing::GameList] = get_game_list([&](const Lobby& lobby) {
                if (lobby_name_param.is<std::string>() && lobby.name != lobby_name_param)
                    return false;
                if (lobby_type_param.is<std::string>() && lobby.type != lobby_type_param)
                    return false;
                return true;
            });

            // Send response
            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            return;
        }

        case OpCodes::Matchmaking::CreateGame: {
            std::string game_id = req.parameters[DictKeyCodes::GameAndActor::GameId].get_or<std::string>();
            const std::string lobby_name = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName].get_or<std::string>();

            // Make sure user is in lobby
            if (!lobby_ || lobby_->name != lobby_name) {
                send(
                    photon::serialize_operation_response(
                        {.operation_code = OpCodes::Matchmaking::CreateGame, .return_code = ErrorCodes::Core::OperationNotAllowedInCurrentState, .debug_message = "Not in lobby"},
                        is_encrypted, get_crypto()),
                    enet::EnetSendOptions{0});
                return;
            }

            // Generate game ID if empty
            if (game_id.empty())
                game_id = generate_game_id(peer_->persistent->user_id);

            // Make sure no game with given ID already exists
            if (lobby_->games.contains(game_id)) {
                send(photon::serialize_operation_response({.operation_code = OpCodes::Matchmaking::CreateGame,
                                                           .return_code = ErrorCodes::Matchmaking::GameIdAlreadyExists,
                                                           .debug_message = "Game ID already exists"},
                                                          is_encrypted, get_crypto()),
                     enet::EnetSendOptions{0});
                return;
            }

            // Create new game with given ID
            peer_->log->info("Creating game: {}", game_id);
            auto game = lobby_->create_game(std::move(game_id));

            // Join the game
            peer_->persistent->current_game = game;

            // Build and send response
            photon::OperationResponse resp{.operation_code = OpCodes::Matchmaking::CreateGame, .return_code = ErrorCodes::Core::Ok};
            resp.parameters[DictKeyCodes::LoadBalancing::Address] = server_manager_.get_endpoint_of(ServerType::GameServer);
            resp.parameters[DictKeyCodes::GameAndActor::GameId] = game->id;
            resp.parameters[DictKeyCodes::LoadBalancing::Token] = peer_->persistent->token;

            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            peer_->log->info("Joining newly created game: {}", game->id);

            return;
        }

        case OpCodes::Matchmaking::JoinGame: {
            std::string game_id = req.parameters[DictKeyCodes::GameAndActor::GameId].get_or<std::string>();
            const std::string lobby_name = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName].get_or<std::string>();
            const bool create_if_not_exists = req.parameters[DictKeyCodes::AuthAndLobby::CreateIfNotExists].get_or<uint8_t>(false);

            // Make sure user is in lobby
            if (!lobby_ || lobby_->name != lobby_name) {
                send(photon::serialize_operation_response(
                         {.operation_code = OpCodes::Matchmaking::JoinGame, .return_code = ErrorCodes::Core::OperationNotAllowedInCurrentState, .debug_message = "Not in lobby"},
                         is_encrypted, get_crypto()),
                     enet::EnetSendOptions{0});
                return;
            }

            // Find game with given ID
            peer_->log->info("Finding game: {}", game_id);
            auto res = lobby_->games.find(game_id);

            std::shared_ptr<Game> game;
            bool is_new = false;
            if (res == lobby_->games.end()) {
                if (!create_if_not_exists) {
                    send(photon::serialize_operation_response({.operation_code = OpCodes::Matchmaking::JoinGame,
                                                               .return_code = ErrorCodes::Matchmaking::GameIdNotExists,
                                                               .debug_message = "Game ID does not exist"},
                                                              is_encrypted, get_crypto()),
                         enet::EnetSendOptions{0});
                    return;
                }

                game = lobby_->create_game(std::move(game_id));
                is_new = true;
            } else {
                game = res->second.lock();
            }

            // Make sure game isn't expired
            if (!game) {
                send(photon::serialize_operation_response({.operation_code = OpCodes::Matchmaking::JoinGame,
                                                           .return_code = ErrorCodes::Core::InternalServerError,
                                                           .debug_message = "Game has expired"},
                                                          is_encrypted, get_crypto()),
                     enet::EnetSendOptions{0});
                return;
            }

            // Validate join
            const int16_t join_validation_code = game->validate_join(peer_->persistent->user_id);
            if (join_validation_code != ErrorCodes::Core::Ok) {
                send(photon::serialize_operation_response(
                         {.operation_code = OpCodes::Matchmaking::JoinGame, .return_code = join_validation_code, .debug_message = "Game closed or full"},
                         is_encrypted, get_crypto()),
                     enet::EnetSendOptions{0});
                return;
            }

            // Make token valid for this game
            peer_->persistent->current_game = game;

            // Expect user
            game->expected_users.emplace(peer_->persistent->user_id);

            // Build and send response
            photon::OperationResponse resp{.operation_code = OpCodes::Matchmaking::JoinGame, .return_code = ErrorCodes::Core::Ok};
            resp.parameters[DictKeyCodes::LoadBalancing::Address] = server_manager_.get_endpoint_of(ServerType::GameServer);
            resp.parameters[DictKeyCodes::LoadBalancing::Token] = peer_->persistent->token;

            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            peer_->log->info("Joining {} game: {}", game->id, is_new ? "newly created" : "existing");
            return;
        }

        case OpCodes::Matchmaking::JoinRandomGame: {
            const std::string lobby_name = req.parameters[DictKeyCodes::AuthAndLobby::LobbyName].get_or<std::string>("");
            const uint8_t matchmaking_type = req.parameters[DictKeyCodes::LoadBalancing::MatchmakingType].get_or<uint8_t>(0); // 0 = FillRoom
            const auto& expected_props_param = req.parameters[DictKeyCodes::Properties::GameProperties];
            auto expected_users = req.parameters[DictKeyCodes::Properties::GameProperties].get_or<std::vector<std::string>>();

            // Make sure user is in lobby
            if (!lobby_ || lobby_->name != lobby_name) {
                send(photon::serialize_operation_response({.operation_code = OpCodes::Matchmaking::JoinRandomGame,
                                                           .return_code = ErrorCodes::Matchmaking::NoRandomMatchFound,
                                                           .debug_message = "Not in lobby"},
                                                          is_encrypted, get_crypto()),
                     enet::EnetSendOptions{0});
                return;
            }

            photon::Hashtable expected_props;
            if (auto p = expected_props_param.get_or<photon::HashtablePtr>())
                expected_props = *p;

            // Collect candidates
            std::vector<std::shared_ptr<Game>> candidates;

            // Better to allocate more than less?
            candidates.reserve(lobby_->games.size());

            for (auto& [id, weak_game] : lobby_->games) {
                auto game = weak_game.lock();
                if (!game)
                    continue;

                // Make sure game is joinable
                if (!game->validate_join(peer_->persistent->user_id, expected_users.size()))
                    continue;

                // Property filter
                if (!game->expect_game_props(expected_props))
                    continue;

                candidates.push_back(std::move(game));
            }

            if (candidates.empty()) {
                send(photon::serialize_operation_response({.operation_code = OpCodes::Matchmaking::JoinRandomGame,
                                                           .return_code = ErrorCodes::Matchmaking::NoRandomMatchFound,
                                                           .debug_message = "No matching game found"},
                                                          is_encrypted, get_crypto()),
                     enet::EnetSendOptions{0});
                return;
            }

            // The previous allocation might've been quite a bit overzealous, fix that
            candidates.shrink_to_fit();

            // Select Game based on matchmaking type
            std::shared_ptr<Game> selected_game;

            switch (matchmaking_type) {
            case MatchmakingType::SerialMatching: {
                // Priorize games with fewer players
                std::ranges::sort(candidates, [](const std::shared_ptr<Game>& a, const std::shared_ptr<Game>& b) { return a->peers.size() < b->peers.size(); });
                selected_game = candidates.front();
            } break;
            case MatchmakingType::FillRoom: {
                // Priorize games with more players
                std::ranges::sort(candidates, [](const std::shared_ptr<Game>& a, const std::shared_ptr<Game>& b) { return a->peers.size() > b->peers.size(); });
                selected_game = candidates.front();

            } break;
            case MatchmakingType::RandomMatching: {
                // Uniform distribution
                static std::mt19937 rng(peer_->enet_peer->bytes_out());
                std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
                selected_game = candidates[dist(rng)];
            } break;
            }

            // Return error if no game was selected
            if (!selected_game) {
                photon::OperationResponse resp{.operation_code = OpCodes::Matchmaking::JoinRandomGame,
                                               .return_code = ErrorCodes::Matchmaking::NoRandomMatchFound,
                                               .debug_message = "No match found"};
                send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
                return;
            }

            // Make token valid for this game
            peer_->persistent->current_game = selected_game;

            // Expect users
            selected_game->expected_users.emplace(peer_->persistent->user_id);
            for (auto&& expected_user : expected_users)
                selected_game->expected_users.emplace(std::move(expected_user));

            // Send Response
            photon::OperationResponse resp;
            resp.operation_code = OpCodes::Matchmaking::JoinRandomGame;
            resp.return_code = ErrorCodes::Core::Ok;

            // Payload similar to Create/Join Game
            resp.parameters[DictKeyCodes::LoadBalancing::Address] = server_manager_.get_endpoint_of(ServerType::GameServer);
            resp.parameters[DictKeyCodes::GameAndActor::GameId] = selected_game->id;
            resp.parameters[DictKeyCodes::LoadBalancing::Token] = peer_->persistent->token;

            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            peer_->log->info("Matchmaking success. Joining game: {}", selected_game->id);
            return;
        }

        case OpCodes::RpcAndMisc::Settings: {
            // Does the client want lobby stats?
            wants_app_stats_ = req.parameters[DictKeyCodes::AuthAndLobby::LobbyStats].get_or<bool>(false);

            // No response
            return;
        }

        case OpCodes::Social::FindFriends: {
            // TODO: Stub. Reverse engineer and implement properly
            photon::OperationResponse resp;
            resp.operation_code = OpCodes::Social::FindFriends;
            resp.return_code = ErrorCodes::Core::Ok;

            resp.parameters[DictKeyCodes::AuthAndLobby::FindFriendsResponseOnlineList] = std::vector<bool>{false};
            resp.parameters[DictKeyCodes::AuthAndLobby::FindFriendsResponseRoomIdList] = std::vector<std::string>{""};

            send(photon::serialize_operation_response(resp, is_encrypted, get_crypto()), enet::EnetSendOptions{0});
            return;
        }
        }
    }

    return HandlerBase::HandleOperationRequest(req, is_encrypted, cmd_header);
}

void MasterServerHandler::join_lobby(Lobby *lobby) {
    if (lobby_)
        leave_lobby();

    lobby_ = lobby;
    lobby->game_list_update_handlers.emplace_front(GameListUpdateHandler{
        .game_create =
            [this](const std::shared_ptr<Game>& game) {
                // Send game creation
                photon::EventData event;
                event.code = EventCodes::GameList;
                event.parameters[DictKeyCodes::LoadBalancing::GameList] = get_game_list([game](const Game& o) { return &o == game.get(); });

                send(photon::serialize_event_data(event), enet::EnetSendOptions{0});
            },
        .game_change =
            [this](const std::shared_ptr<Game>& game) {
                // Send game property change
                photon::EventData event;
                event.code = EventCodes::GameList;
                event.parameters[DictKeyCodes::LoadBalancing::GameList] = get_game_list([game](const Game& o) { return &o == game.get(); });

                send(photon::serialize_event_data(event), enet::EnetSendOptions{0});
            },
        .game_delete =
            [this](Game *game) {
                // Send game removal
                photon::EventData event;
                event.code = EventCodes::GameList;
                auto& game_list =
                    *(event.parameters[DictKeyCodes::LoadBalancing::GameList] = std::make_shared<photon::Hashtable>()).get<photon::HashtablePtr>();
                auto& game_props = *(game_list[game->id] = std::make_shared<photon::Hashtable>()).get<photon::HashtablePtr>();
                game_props[GameProps::Removed] = true;

                send(photon::serialize_event_data(event), enet::EnetSendOptions{0});
            }});
    game_list_update_handler_ = lobby->game_list_update_handlers.begin();
}

void MasterServerHandler::leave_lobby() {
    if (game_list_update_handler_.has_value()) {
        lobby_->game_list_update_handlers.erase(*game_list_update_handler_);
        game_list_update_handler_.reset();
    }
    lobby_ = nullptr;
}

void MasterServerHandler::send_app_stats() {
    photon::EventData event;

    event.code = EventCodes::AppStats;
    event.parameters[DictKeyCodes::LoadBalancing::GameCount] = [this]() {
        int32_t fres = 0;
        for (auto&& app : App::get_all(server_manager_))
            for (Lobby *lobby : app->get_lobbies())
                fres += lobby->games.size();
        return fres;
    }();
    event.parameters[DictKeyCodes::LoadBalancing::PeerCount] = static_cast<int32_t>(server_manager_.get_connection_count<GameServerHandler>());
    event.parameters[DictKeyCodes::LoadBalancing::MasterPeerCount] = static_cast<int32_t>(server_manager_.get_connection_count<MasterServerHandler>());

    send(photon::serialize_event_data(event), enet::EnetSendOptions{0, enet::EnetDeliveryMode::Unreliable});
}

photon::Dictionary MasterServerHandler::get_lobby_stats(std::function<bool(const Lobby&)> lobby_filter) {
    photon::Dictionary fres;

    auto& peer_count_arr = (fres[DictKeyCodes::LoadBalancing::PeerCount] = photon::ObjectArray()).get<photon::ObjectArray>();
    auto& game_count_arr = (fres[DictKeyCodes::LoadBalancing::GameCount] = photon::ObjectArray()).get<photon::ObjectArray>();
    auto& lobby_type_arr = (fres[DictKeyCodes::AuthAndLobby::LobbyType] = photon::ByteArray()).get<photon::ByteArray>();
    auto& lobby_name_arr = (fres[DictKeyCodes::AuthAndLobby::LobbyName] = photon::ObjectArray()).get<photon::ObjectArray>();

    auto& app = *peer_->persistent->app;
    for (Lobby *lobby : app.get_lobbies()) {
        if (lobby_filter && !lobby_filter(*lobby))
            continue;

        lobby_name_arr.emplace_back(lobby->name);
        lobby_type_arr.emplace_back(lobby->type);
        game_count_arr.emplace_back(static_cast<int32_t>(lobby->games.size()));
        peer_count_arr.emplace_back(static_cast<int32_t>(lobby->get_peer_count()));
    }

    return fres;
}

void MasterServerHandler::send_lobby_stats() {
    photon::EventData event;

    event.code = EventCodes::LobbyStats;
    event.parameters = get_lobby_stats();

    send(photon::serialize_event_data(event), enet::EnetSendOptions{0});
}

photon::HashtablePtr MasterServerHandler::get_game_list(std::function<bool(const Lobby&)> lobby_filter, std::function<bool(const Game&)> game_filter) {
    auto fres = std::make_shared<photon::Hashtable>();

    if (lobby_filter && !lobby_filter(*lobby_))
        return fres;

    for (auto& [name, weak_game] : lobby_->games) {
        auto game = weak_game.lock();
        if (!game)
            continue;

        if (game_filter && !game_filter(*game))
            continue;

        (*fres)[std::string(name)] = std::make_shared<photon::Hashtable>(game->get_basic_game_props());
    }

    return fres;
}

void MasterServerHandler::send_game_list() {
    photon::EventData event;

    event.code = EventCodes::GameList;
    event.parameters[DictKeyCodes::LoadBalancing::GameList] = get_game_list();

    send(photon::serialize_event_data(event), enet::EnetSendOptions{0});
}
} // namespace server
