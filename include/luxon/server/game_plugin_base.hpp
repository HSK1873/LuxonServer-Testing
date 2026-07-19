// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "coro_support.hpp"
#include "logger.hpp"

#include <string_view>
#include <memory>
#include <optional>
#include <cstdint>
#include <luxon/ser_interface.hpp>
/// Designed after:
/// https://doc.photonengine.com/server/current/plugins/manual (2026-02-02)
/// https://web.archive.org/web/20260202102255/https://doc.photonengine.com/server/current/plugins/manual

namespace server {
class ServerManager;
struct Game;
struct GamePeer;
struct Peer;
struct Event;

namespace game_plugins {
enum class Result { Continue, Fail, Cancel };

struct OnCreateGameCallInfo {
    const std::shared_ptr<Peer>& creator;
    const bool is_join, create_if_not_exist;
};

struct BeforeJoinGameCallInfo {
    const std::shared_ptr<Peer>& joiner;
};
struct OnJoinGameCallInfo {
    GamePeer *const joiner;
    std::optional<bool> publish_user_id, broadcast_actor_props;
};

struct OnLeaveGameCallInfo {
    GamePeer *const leaver;
};

struct OnRaiseEventCallInfo {
    GamePeer *const raiser;
    Event& event;
    const uint8_t cache_op;
};

struct BeforeSetPropertiesCallInfo {
    GamePeer *const setter;
    bool broadcast;
    int32_t target_actor_id;
    const ser::HashtablePtr update, expected;
};
struct OnSetPropertiesCallInfo {
    GamePeer *const setter;
    bool broadcast;
    int32_t target_actor_id;
    const ser::HashtablePtr update, expected;
};

struct BeforeCloseGameCallInfo {
    const bool failed_on_create;
};

struct OnCloseGameCallInfo {
    const bool failed_on_create;
};

class PluginBase {
public:
    PluginBase(Game *game, std::string_view plugin_name);
    virtual ~PluginBase() {}

    virtual Awaitable<> OnAttach() { lco_return; }
    virtual Awaitable<Result> OnCreateGame(luxon::ser::OperationRequestMessage& req, OnCreateGameCallInfo&) { lco_return Result::Continue; }
    virtual Awaitable<Result> BeforeJoin(luxon::ser::OperationRequestMessage& req, BeforeJoinGameCallInfo&) { lco_return Result::Continue; }
    virtual Awaitable<Result> OnJoinGame(luxon::ser::OperationRequestMessage& req, OnJoinGameCallInfo&) { lco_return Result::Continue; }
    virtual Awaitable<Result> OnLeave(luxon::ser::OperationRequestMessage& req, OnLeaveGameCallInfo&) { lco_return Result::Continue; }
    virtual Awaitable<Result> OnRaiseEvent(luxon::ser::OperationRequestMessage& req, OnRaiseEventCallInfo&) { lco_return Result::Continue; }
    virtual Awaitable<Result> BeforeSetProperties(luxon::ser::OperationRequestMessage& req, BeforeSetPropertiesCallInfo&) { lco_return Result::Continue; }
    virtual Awaitable<Result> OnSetProperties(luxon::ser::OperationRequestMessage& req, OnSetPropertiesCallInfo&) { lco_return Result::Continue; }
    virtual Result BeforeCloseGame(BeforeCloseGameCallInfo&) { return Result::Continue; }
    virtual Result OnCloseGame(OnCloseGameCallInfo&) { return Result::Continue; }

protected:
    Game *const game_;
    std::shared_ptr<logger> log_;

    ServerManager& get_server_manager();
};
} // namespace game_plugins
} // namespace server
