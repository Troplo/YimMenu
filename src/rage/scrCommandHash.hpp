#ifndef SCRCOMMANDHASH_HPP
#define SCRCOMMANDHASH_HPP
#define rage_u32 unsigned int
#define rage_u64 unsigned long long
#include "script/scrNativeHandler.hpp"

#include <cstddef>

namespace sysObfuscatedTypes {
    rage_u32 obfRand();
}

typedef void (*scrCmd)(rage::scrNativeCallContext *);

template<typename T, bool mutate = true>
class sysObfuscated {
public:
    void Init() {
        m_xor = sysObfuscatedTypes::obfRand();
        if (mutate) {
            m_mutate = sysObfuscatedTypes::obfRand();
        }
    }

    T Get() {
        rage_u32 xorVal = m_xor ^ (rage_u32) (size_t) this;
        rage_u32 ret[sizeof(T) / sizeof(rage_u32)];
        rage_u32 *src = const_cast<rage_u32 *>(&m_data[0]);
        rage_u32 *dest = (rage_u32 *) &ret;
        for (size_t i = 0; i < sizeof(T) / 4; ++i) {
            if (mutate) {
                // Extract valid data from two words of storage
                rage_u32 a = *src & m_mutate;
                rage_u32 b = src[sizeof(T) / 4] & (~m_mutate);
                // Apply entropy in the unused bits: Just flip the two u16's in the rage_u32. We can't do a
                // huge amount more without knowledge of the mutation mask.
                rage_u32 entropyA = ((*src & (~m_mutate)) << 16) | ((*src & (~m_mutate)) >> 16);
                rage_u32 entropyB = ((src[sizeof(T) / 4] & m_mutate) << 16) | ((src[sizeof(T) / 4] & m_mutate) >> 16);
                *src = (*src & m_mutate) | entropyA;
                src[sizeof(T) / 4] = (src[sizeof(T) / 4] & (~m_mutate)) | entropyB;

                *dest++ = a | b;
                ++src;
            } else {
                *dest++ = *src++ ^ xorVal;
            }
        }
        // Call Set() to reset the xor and mutate keys on every call to Get()
        if (mutate) {
            const_cast<sysObfuscated<T, mutate> *>(this)->Set(*(T *) &ret);
        }
        return *(T *) &ret;
    }

    void Set(T data) {
        Init();
        rage_u32 xorVal = m_xor ^ (rage_u32) (size_t) this;
        rage_u32 *src = (rage_u32 *) &data;
        rage_u32 *dest = &m_data[0];
        for (size_t i = 0; i < sizeof(T) / 4; ++i) {
            if (mutate) {
                rage_u32 a = *src & m_mutate;
                rage_u32 b = *src & (~m_mutate);
                ++src;
                *dest = a;
                dest[sizeof(T) / 4] = b;
                ++dest;
            } else {
                *dest++ = *src++ ^ xorVal;
            }
        }
    }

    void operator=(T data) {
        Set(data);
    }

    operator T() {
        return Get();
    }

    bool operator==(sysObfuscated<T> data) {
        return Get() == data.Get();
    }

    bool operator!=(sysObfuscated<T> data) {
        return Get() != data.get();
    }

    bool operator==(T data) {
        return Get() == data;
    }

    bool operator!=(T data) {
        return Get() != data;
    }

private:
    rage_u32 m_data[(mutate ? sizeof(T) * 2 : sizeof(T)) / sizeof(rage_u32)];
    // XOR and mutate keys for this type
    rage_u32 m_xor;
    rage_u32 m_mutate;
};

template<typename T>
class scrCommandHash {
private:
    static const int ToplevelSize = 256; // Must be power of two
    static const int PerBucket = 7;

    struct Bucket {
        sysObfuscated<Bucket *, false> obf_Next;
        T Data[PerBucket];
        // Gen8 stores the count as two XOR-obfuscated uint32 values, not a NodeEnc<uint64_t>.
        rage_u32 numEntries1;
        rage_u32 numEntries2;
        char m_Pad1[0x04];
        sysObfuscated<rage_u64, false> obf_Hashes[PerBucket];
        char m_Pad2[0x38];

        rage_u32 GetCount() const {
            return numEntries1 ^ numEntries2 ^ static_cast<rage_u32>(reinterpret_cast<size_t>(&numEntries1));
        }

        void SetCount(rage_u32 count) {
            numEntries2 = 0;
            numEntries1 = count ^ static_cast<rage_u32>(reinterpret_cast<size_t>(&numEntries1));
        }
    };

    static_assert(offsetof(Bucket, numEntries1) == 0x48);
    static_assert(offsetof(Bucket, obf_Hashes) == 0x54);
    static_assert(sizeof(Bucket) == 0x100);

public:
    void RegistrationComplete(bool val) {
        m_bRegistrationComplete = val;
    }

    void Init() {
        m_Occupancy = 0;
        m_bRegistrationComplete = false;
        for (int i{}; i < ToplevelSize; i++)
            m_Buckets[i] = NULL;
    }

    void Kill() {
        for (int i = 0; i < ToplevelSize; i++) {
            Bucket *b = m_Buckets[i];
            while (b) {
                char *old = (char *) b;
                b = b->obf_Next.Get();
                delete[] old;
            }
            m_Buckets[i] = NULL;
        }
        m_Occupancy = 0;
    }

    void Insert(rage_u64 hashcode, T cmd);

    T Lookup(rage_u64 hashcode) {
        Bucket *b = m_Buckets[hashcode & (ToplevelSize - 1)];
        while (b) {
            const auto count = b->GetCount();
            if (count > PerBucket)
                return T{};

            for (rage_u32 i{}; i != count; i++)
                if (b->obf_Hashes[i].Get() == hashcode)
                    return b->Data[i];
            b = b->obf_Next.Get();
        }
        return T{};
    }

    Bucket *m_Buckets[ToplevelSize];
    int m_Occupancy;
    bool m_bRegistrationComplete;
};
#endif //SCRCOMMANDHASH_HPP
