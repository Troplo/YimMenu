#pragma once

#include "gta/joaat.hpp"

#include <cstdint>

#define BASE_GLOBALS_HASH "BASE_GLOBALS"_J
#define MP_GLOBAL_HASH "MP_GLOBAL"_J
#define CD_GLOBAL_HASH "CD_GLOBAL"_J

// Proxy for the internal CTunables class. This uses hardcoded RVAs so it will definitely not survive a binary update lol
class CTunables {
public:
    using u32 = std::uint32_t;

    enum eUnionType : std::int32_t {
        kINVALID = -1,
        kINT = 0,
        kFLOAT = 1,
        kBOOL = 2,
        kTYPE_MAX,
    };

    union sUnionValue {
        std::int32_t m_INT;
        std::uint32_t m_UINT;
        float m_FLOAT;
        std::uint8_t m_BOOL;
    };

    static_assert(sizeof(sUnionValue) == sizeof(std::uint32_t));

    static CTunables *GetInstance() {
        if (!s_tunables_singleton_slot) {
            const auto module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!module_base)
                return nullptr;

            s_tunables_singleton_slot = reinterpret_cast<CTunables **>(module_base + 0x1EC0AE0);
            s_access_internal = reinterpret_cast<access_internal_fn>(module_base + 0x1094E08);
        }

        return *s_tunables_singleton_slot;
    }

    static constexpr u32 TunableHash(u32 context_hash, u32 tunable_hash) noexcept {
        return context_hash + tunable_hash;
    }

    bool Access(u32 hash, eUnionType type) const {
        return AccessInternal(hash, type) != nullptr;
    }

    bool Access(u32 hash, bool &value) const {
        const auto *result = AccessInternal(hash, kBOOL);
        if (!result)
            return false;

        value = result->m_BOOL != 0;
        return true;
    }

    bool Access(u32 hash, int &value) const {
        const auto *result = AccessInternal(hash, kINT);
        if (!result)
            return false;

        value = result->m_INT;
        return true;
    }

    bool Access(u32 hash, u32 &value) const {
        const auto *result = AccessInternal(hash, kINT);
        if (!result)
            return false;

        value = result->m_UINT;
        return true;
    }

    bool Access(u32 hash, float &value) const {
        const auto *result = AccessInternal(hash, kFLOAT);
        if (!result)
            return false;

        value = result->m_FLOAT;
        return true;
    }

    bool GetBool(u32 hash, bool fallback) const {
        return TryAccess(hash, fallback);
    }

    bool TryAccess(u32 hash, bool fallback) const {
        const auto *result = AccessInternal(hash, kBOOL);
        return result ? result->m_BOOL != 0 : fallback;
    }

    int TryAccess(u32 hash, int fallback) const {
        const auto *result = AccessInternal(hash, kINT);
        return result ? result->m_INT : fallback;
    }

    u32 TryAccess(u32 hash, u32 fallback) const {
        const auto *result = AccessInternal(hash, kINT);
        return result ? result->m_UINT : fallback;
    }

    float TryAccess(u32 hash, float fallback) const {
        const auto *result = AccessInternal(hash, kFLOAT);
        return result ? result->m_FLOAT : fallback;
    }

    bool CheckExists(u32 context_hash, u32 tunable_hash) const {
        return Access(TunableHash(context_hash, tunable_hash), kINVALID);
    }

    bool Access(u32 context_hash, u32 tunable_hash, bool &value) const {
        return Access(TunableHash(context_hash, tunable_hash), value);
    }

    bool Access(u32 context_hash, u32 tunable_hash, int &value) const {
        return Access(TunableHash(context_hash, tunable_hash), value);
    }

    bool Access(u32 context_hash, u32 tunable_hash, u32 &value) const {
        return Access(TunableHash(context_hash, tunable_hash), value);
    }

    bool Access(u32 context_hash, u32 tunable_hash, float &value) const {
        return Access(TunableHash(context_hash, tunable_hash), value);
    }

    bool GetBool(u32 context_hash, u32 tunable_hash, bool fallback) const {
        return TryAccess(TunableHash(context_hash, tunable_hash), fallback);
    }

    bool TryAccess(u32 context_hash, u32 tunable_hash, bool fallback) const {
        return TryAccess(TunableHash(context_hash, tunable_hash), fallback);
    }

    int TryAccess(u32 context_hash, u32 tunable_hash, int fallback) const {
        return TryAccess(TunableHash(context_hash, tunable_hash), fallback);
    }

    u32 TryAccess(u32 context_hash, u32 tunable_hash, u32 fallback) const {
        return TryAccess(TunableHash(context_hash, tunable_hash), fallback);
    }

    float TryAccess(u32 context_hash, u32 tunable_hash, float fallback) const {
        return TryAccess(TunableHash(context_hash, tunable_hash), fallback);
    }

private:
    using access_internal_fn = const sUnionValue* (__fastcall*)(CTunables *, u32, eUnionType);

    const sUnionValue *AccessInternal(u32 hash, eUnionType type) const {
        return s_access_internal ? s_access_internal(const_cast<CTunables *>(this), hash, type) : nullptr;
    }

    CTunables() = delete;

    inline static CTunables **s_tunables_singleton_slot{};
    inline static access_internal_fn s_access_internal{};
};
