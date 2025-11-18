#include "hooking/hooking.hpp"

namespace big
{
    bool hooks::http_start_request(void* request, const char* uri)
    {
        if (g.debug.logs.http_start_request_logs)
            LOG(INFO) << uri;

        return g_hooking->get_original<hooks::http_start_request>()(request, uri);
    }
}