#pragma once

#include "script/scrNativeHandler.hpp"

#include <nlohmann/json.hpp>

#include <string_view>

namespace big::matchmaking_networking {
    void post_to_matchmaking_bot(const std::string &endpoint, nlohmann::json payload);

    void report_matchmaking_kill(rage::scrNativeCallContext *ctx);

    void send_matchmaking_heartbeat(rage::scrNativeCallContext *ctx);
}
