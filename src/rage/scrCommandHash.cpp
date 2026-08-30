#include "scrCommandHash.hpp"
#include "pointers.hpp"

namespace sysObfuscatedTypes {
    rage_u32 obfRand() {
        static rage_u32 next = 0x12345678;
        next = next * 214013 + 2531011;
        return next;
    }
}

template<>
void scrCommandHash<void(*)(rage::scrNativeCallContext *)>::Insert(
    rage_u64 hashcode,
    void (*cmd)(rage::scrNativeCallContext *)
) {
    {
        if (Lookup(hashcode)) {
            LOG(FATAL) << "Duplicate hashcode insertion attempt detected in script command hash table: " << std::hex << hashcode;
            return;
        }
        const auto bucket_index = hashcode & (ToplevelSize - 1);
        Bucket *b = m_Buckets[bucket_index];
        auto count = b ? b->GetCount() : 0;
        if (count > PerBucket) {
            LOG(FATAL) << "Invalid native registration bucket count: " << count;
            return;
        }

        // If this chain is empty, or the first bucket is full, allocate and patch in a new bucket
        if (!b || count == PerBucket) {
            Bucket *nb = (Bucket *) new char[sizeof(Bucket)];
            nb->obf_Next.Set(m_Buckets[bucket_index]);
            nb->SetCount(0);
            b = m_Buckets[bucket_index] = nb;
            count = 0;
        }
        b->obf_Hashes[count].Set(hashcode);
        b->Data[count] = cmd;
        b->SetCount(count + 1);

        LOG(INFO) << "Inserted native hash 0x" << std::hex << hashcode << " into bucket " << std::dec << bucket_index << " at slot " << count;

        if (!Lookup(hashcode)) {
            LOG(FATAL) << "Post-insertion lookup failed for hashcode: " << std::hex << hashcode;
        }
    }
}
