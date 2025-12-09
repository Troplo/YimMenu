#include "scrCommandHash.hpp"
#include "pointers.hpp"

namespace sysObfuscatedTypes
{
		rage_u32 obfRand()
	{
		rage_u32& next = *big::g_pointers->m_gta.m_sys_obf_rand_next;
		next = next * 214013 + 2531011;
		return next;
	}
}

template<>
void scrCommandHash<void(*)(rage::scrNativeCallContext*)>::Insert(
	rage_u64 hashcode,
	void(*cmd)(rage::scrNativeCallContext*)
)
{
	{
		if (Lookup(hashcode))
		{
			LOG(FATAL) << "Duplicate hashcode insertion attempt detected in script command hash table: " << std::hex << hashcode;
			return;
		}
		LOG(INFO) << "Dump of scrCommandHash before insertion:";
		for (int i = 0; i < ToplevelSize; i++)
		{
			LOG(INFO) << " Bucket " <<  m_Buckets[i]->obf_Count.Get() << "";
		}
		LOG(INFO) << "[scrCommandHash debug]  Inserting hashcode: " << std::hex << hashcode;
		Bucket * b = m_Buckets[hashcode & (ToplevelSize-1)];
		// If this chain is empty, or the first bucket is full, allocate and patch in a new bucket
		if (!b || b->obf_Count.Get() == PerBucket) {
			Bucket *nb = (Bucket*) new char[sizeof(Bucket)];
			nb->obf_Next.Set(m_Buckets[hashcode & (ToplevelSize-1)]);
			nb->obf_Count.Set(0);
			b = m_Buckets[hashcode & (ToplevelSize-1)] = nb;
		}
		b->obf_Hashes[b->obf_Count.Get()].Set(hashcode);
		b->plainText_Hashes[b->obf_Count.Get()] = 0; //hashcode;
		b->Data[b->obf_Count] = cmd;
		b->obf_Count.Set(b->obf_Count.Get()+1);		//inc count

		rage_u32 i = b->obf_Count.Get();
		LOG(INFO) << "[scrCommandHash debug]  Insert to bucket (%p) -> plaintext (%" << b << "x)    obfuscated (%" << b->plainText_Hashes[i] << "x : %" << b->obf_Hashes[i].Get() << "x @ " << &b->obf_Hashes[i] << ")";

		if (!Lookup(hashcode))
		{
			LOG(FATAL) << "Post-insertion lookup failed for hashcode: " << std::hex << hashcode;
		}
	}
}
