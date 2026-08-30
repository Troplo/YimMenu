#include "matchmaking_networking.hpp"

#include "http_client/http_client.hpp"
#include "thread_pool.hpp"

// Yes it's hyper insecure.
namespace big::matchmaking_networking {
    static void post_to_matchmaking_bot(const std::string &endpoint, nlohmann::json payload) {
        if (!g_thread_pool) {
            return;
        }

        const std::string url = "https://pvp.ros.troplo.com" + endpoint;

        g_thread_pool->push([url, payload = std::move(payload)] {
            const auto response = g_http_client.post(url, {
                                                         {"Content-Type", "application/json"},
                                                         {"User-Agent", "ParagonLegacy"}
                                                     }, payload.dump());

            if (response.status_code < 200 || response.status_code >= 300) {
                LOG(WARNING) << "Matchmaking request to " << url << " failed with status " << response.status_code;
            }
        });
    }

    void report_matchmaking_kill(rage::scrNativeCallContext *ctx) {
        const auto killer_name = ctx->get_arg<const char *>(0);
        const auto victim_name = ctx->get_arg<const char *>(1);

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json payload = {
            {"victimUsername", killer_name},
            {"killerUsername", victim_name},
            {"timestamp", std::to_string(now_ms)}
        };
        post_to_matchmaking_bot("/report_kill", payload);

        LOG(INFO) << "REPORT_MATCHMAKING_KILL | killerName: " << (killer_name) << ", victimName: " << (victim_name);
    }

    void send_matchmaking_heartbeat(rage::scrNativeCallContext *ctx) {
        const auto username = ctx->get_arg<const char *>(0);

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json payload = {
            {"username", username},
            {"timestamp", std::to_string(now_ms)}
        };
        post_to_matchmaking_bot("/heartbeat", payload);

        LOG(INFO) << "SEND_MATCHMAKING_HEARTBEAT | username: " << (username);
    }
}
