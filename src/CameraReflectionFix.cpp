#include "CameraReflectionFix.h"
#include "Settings.h"
#include <cstring>
#include <exception>

namespace CameraReflectionFix
{
	namespace
	{
		std::atomic_bool g_attempted{};
		std::atomic_bool g_installed{};

		bool Enabled()
		{
			return g_installed.load(std::memory_order_acquire) &&
				Settings::RuntimeEnabled() &&
				Settings::Get().cameraCenteredReflections;
		}

		std::uintptr_t CallTarget(std::uintptr_t callsite)
		{
			if (!callsite || *reinterpret_cast<const std::uint8_t*>(callsite) != 0xE8)
				return 0;

			std::int32_t displacement{};
			std::memcpy(&displacement,
				reinterpret_cast<const void*>(callsite + 1), sizeof(displacement));
			return static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(callsite + 5) + displacement);
		}

		std::uintptr_t FollowJump(std::uintptr_t address)
		{
			if (!address)
				return 0;
			const auto* code = reinterpret_cast<const std::uint8_t*>(address);
			if (code[0] != 0xFF || code[1] != 0x25)
				return address;

			std::int32_t displacement{};
			std::memcpy(&displacement, code + 2, sizeof(displacement));
			const auto pointer = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(address + 6) + displacement);
			std::uintptr_t target{};
			std::memcpy(&target, reinterpret_cast<const void*>(pointer), sizeof(target));
			return target;
		}

		bool IsSkyrimCode(std::uintptr_t address)
		{
			HMODULE module{};
			return address && GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(address), &module) &&
				module == GetModuleHandleW(nullptr);
		}

		struct Hook
		{
			static RE::NiPoint3* thunk(RE::PlayerCharacter* player,
				RE::NiPoint3* target, int unknown1, float unknown2)
			{
				auto* result = func(player, target, unknown1, unknown2);
				if (!result || !Enabled())
					return result;

				auto* camera = RE::PlayerCamera::GetSingleton();
				if (!camera || !camera->cameraRoot)
					return result;

				*result = camera->cameraRoot->world.translate;
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool Install()
	{
		if (g_attempted.exchange(true, std::memory_order_acq_rel))
			return g_installed.load(std::memory_order_acquire);

		try {
			const auto callsite = REL::RelocationID(31373, 32160).address() +
				REL::Relocate(0x1ADu, 0x1CAu, 0x1EDu);
			const auto target = CallTarget(callsite);
			if (!target) {
				logger::error("[Reflection] invalid callsite 0x{:X}", callsite);
				return false;
			}
			if (!IsSkyrimCode(target) || !IsSkyrimCode(FollowJump(target))) {
				logger::warn("[Reflection] callsite already hooked");
				return false;
			}

			Hook::func = SKSE::GetTrampoline().write_call<5>(callsite, Hook::thunk);
			g_installed.store(true, std::memory_order_release);
			logger::info("[Reflection] installed");
			return true;
		} catch (const std::exception& error) {
			logger::error("[Reflection] install failed: {}", error.what());
			return false;
		}
	}
}
