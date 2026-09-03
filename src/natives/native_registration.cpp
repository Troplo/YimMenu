#include "native_registration.hpp"

#include "pointers.hpp"
#include "services/matchmaking_networking/matchmaking_networking.hpp"

namespace big
{
	namespace
	{
		void print_varargs(rage::scrNativeCallContext* ctx, al::eLogLevel level, std::uint32_t skip = 0)
		{
			if (!ctx || !ctx->m_args || !ctx->m_arg_count || skip > 15 || ctx->m_arg_count - 1 < skip)
				return;

			auto types       = ctx->get_arg<std::uint32_t>(0) >> (skip * 2);
			const auto* args = static_cast<const rage::scrValue*>(ctx->m_args) + 1 + skip;
			const auto count = std::min(ctx->m_arg_count - 1 - skip, 16U - skip);
			std::ostringstream message;

			for (std::uint32_t i = 0; i < count; ++i, types >>= 2)
			{
				switch (types & 3)
				{
				case 0: message << args[i].Int; break;
				case 1: message << args[i].Float; break;
				case 2: message << (args[i].String ? args[i].String : "(null)"); break;
				case 3:
					if (const auto* vector = args[i].Reference)
						message << "<< " << vector[0].Float << ", " << vector[1].Float << ", " << vector[2].Float << " >>";
					else
						message << "(null)";
					break;
				}
			}

			if (skip)
				LOG(level) << "SCRIPT[" << ctx->get_arg<int>(1) << "] | " << message.str();
			else
				LOG(level) << "SCRIPT | " << message.str();
		}

		void print_channel(rage::scrNativeCallContext* ctx, al::eLogLevel level)
		{
			if (ctx && ctx->m_arg_count >= 3 && ctx->get_arg<int>(1) >= 0 && ctx->get_arg<int>(1) < 192)
				print_varargs(ctx, level, 1);
		}

		void print_ln_final(rage::scrNativeCallContext* ctx) { print_varargs(ctx, INFO); }
		void print_ln(rage::scrNativeCallContext* ctx) { print_varargs(ctx, INFO); }
		void assert_ln(rage::scrNativeCallContext* ctx) { print_varargs(ctx, FATAL); }
		void cprint_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, INFO); }
		void cwarning_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, WARNING); }
		void cerror_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, FATAL); }
		void cassert_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, FATAL); }
		void cdebug1_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, VERBOSE); }
		void cdebug2_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, VERBOSE); }
		void cdebug3_ln(rage::scrNativeCallContext* ctx) { print_channel(ctx, VERBOSE); }

		void clog_ln(rage::scrNativeCallContext* ctx)
		{
			if (!ctx || ctx->m_arg_count < 4 || ctx->get_arg<int>(1) < 0 || ctx->get_arg<int>(1) >= 192)
				return;

			switch (ctx->get_arg<int>(2))
			{
			case 0:
			case 1:
			case 2: print_varargs(ctx, FATAL, 2); break;
			case 3: print_varargs(ctx, WARNING, 2); break;
			case 4: print_varargs(ctx, INFO, 2); break;
			case 5:
			case 6:
			case 7: print_varargs(ctx, VERBOSE, 2); break;
			}
		}

		void log_debug(rage::scrNativeCallContext* ctx) { print_channel(ctx, INFO); }
	}

	void native_registration::init()
	{
		auto* command_hash = reinterpret_cast<scrCommandHash<scrCmd>*>(g_pointers->m_gta.m_native_registration_table);
		if (!command_hash)
		{
			LOG(FATAL) << "Native registration table is unavailable";
			return;
		}

		command_hash->Insert(0x7D2E9B14F0A6C385ULL, matchmaking_networking::report_matchmaking_kill);
		command_hash->Insert(0x2ED4B9D53994053EULL, matchmaking_networking::send_matchmaking_heartbeat);
		command_hash->Insert(0x7DCC55AEE55E3F8EULL, log_debug);

		const auto replace = [command_hash](rage_u64 hash, scrCmd handler) {
			if (!command_hash->Replace(hash, handler))
				command_hash->Insert(hash, handler);
		};

		replace(0x91A49DBAD86281F8ULL, print_ln_final);
		replace(0x4BE6713CECFCFFF3ULL, print_ln);
		replace(0xCF8D79ECFCF47473ULL, assert_ln);
		replace(0xEF256AE8A5A27966ULL, clog_ln);
		replace(0xF0783374333FD8CEULL, cprint_ln);
		replace(0x9A6C65DDDBEC9C52ULL, cwarning_ln);
		replace(0xD9911C7B5F8CD69CULL, cerror_ln);
		replace(0x83407B92D46F25C3ULL, cassert_ln);
		replace(0xA308F935BDECCEC0ULL, cdebug1_ln);
		replace(0x4DC69742196F818AULL, cdebug2_ln);
		replace(0x1B08D1EB9D8C4931ULL, cdebug3_ln);
	}
}
