#include "PCH.h"
#include "CameraReflectionFix.h"
#include "Settings.h"
#include "WaterSelection.h"
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	bool g_communityShadersMode{};

	bool HasPlugin(const wchar_t* name)
	{
		if (GetModuleHandleW(name))
			return true;

		wchar_t executable[32768]{};
		const auto length = GetModuleFileNameW(
			nullptr, executable, static_cast<DWORD>(std::size(executable)));
		if (!length || length >= std::size(executable))
			return false;

		std::error_code error;
		const auto path = std::filesystem::path(executable).parent_path() /
			L"Data" / L"SKSE" / L"Plugins" / name;
		return std::filesystem::is_regular_file(path, error);
	}

	void InitializeLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path)
			return;
		*path /= "NativeWaterLightStabilizer.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
			path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	const char* RuntimeLabel()
	{
		if (REL::Module::IsVR())
			return "VR";
		return REL::Module::IsAE() ? "AE" : "SE";
	}

	void MessageHandler(SKSE::MessagingInterface::Message* message)
	{
		if (message->type != SKSE::MessagingInterface::kDataLoaded ||
			g_communityShadersMode)
			return;
		if (HasPlugin(L"SkyReflectionFix.dll")) {
			logger::info("[Reflection] SkyReflectionFix detected");
			return;
		}
		CameraReflectionFix::Install();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	InitializeLog();
	SKSE::Init(skse);

	logger::info("Native Water Light Stabilizer {} | {} {}",
		NWLS_VERSION_STRING, RuntimeLabel(),
		REL::Module::get().version().string("."));

	(void)Settings::Load();
	g_communityShadersMode = HasPlugin(L"CommunityShaders.dll");
	WaterSelection::SetCommunityShadersMode(g_communityShadersMode);
	if (g_communityShadersMode)
		logger::info("[Compatibility] Community Shaders mode enabled");

	if (REL::Module::IsVR())
		REL::IDDatabase::get().IsVRAddressLibraryAtLeastVersion("0.228.0", true);

	SKSE::AllocTrampoline(1 << 8);
	WaterSelection::Install();

	if (auto* messaging = SKSE::GetMessagingInterface())
		messaging->RegisterListener(MessageHandler);
	return true;
}
