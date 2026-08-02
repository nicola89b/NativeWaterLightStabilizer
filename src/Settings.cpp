#include "Settings.h"

namespace Settings
{
	namespace
	{
		Values g_values{};
		std::atomic_bool g_runtimeEnabled{ true };

		[[nodiscard]] int ReadInt(
			const std::wstring& file, const wchar_t* key, int fallback)
		{
			return static_cast<int>(GetPrivateProfileIntW(
				L"General", key, fallback, file.c_str()));
		}
	}

	const Values& Load()
	{
		std::error_code error;
		const auto absolute = std::filesystem::absolute(
			std::filesystem::path("Data") / "SKSE" / "Plugins" /
				"NativeWaterLightStabilizer.ini", error);
		if (error) {
			g_runtimeEnabled.store(g_values.enabled, std::memory_order_release);
			return g_values;
		}

		const auto file = absolute.wstring();
		g_values.enabled = ReadInt(file, L"Enabled", 1) != 0;
		g_values.stableWaterSelection =
			ReadInt(file, L"StableWaterSelection", 1) != 0;
		g_values.cameraCenteredReflections =
			ReadInt(file, L"CameraCenteredReflections", 1) != 0;
		g_runtimeEnabled.store(g_values.enabled, std::memory_order_release);
		logger::info(
			"[Settings] enabled={} selection={} reflections={}",
			g_values.enabled, g_values.stableWaterSelection,
			g_values.cameraCenteredReflections);
		return g_values;
	}

	const Values& Get()
	{
		return g_values;
	}

	bool RuntimeEnabled()
	{
		return g_runtimeEnabled.load(std::memory_order_acquire);
	}

	void SetRuntimeEnabled(bool enabled)
	{
		const auto previous = g_runtimeEnabled.exchange(
			enabled, std::memory_order_acq_rel);
		if (previous != enabled)
			logger::info("[Runtime] {}", enabled ? "enabled" : "disabled");
	}
}
