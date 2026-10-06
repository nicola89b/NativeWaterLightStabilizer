#include "common/IPrefix.h"
#include "skse64_common/skse_version.h"
#include "skse64/PluginAPI.h"

#include "Hooks.h"
#include "RuntimeAddresses.h"
#include <ShlObj.h>

IDebugLog gLog;

// Maps the packed SKSE version to the corresponding supported runtime.
static Runtime DetectRuntime(UInt32 runtimeVersion)
{
    switch (runtimeVersion)
    {
    case RUNTIME_VERSION_1_1_47:
        return Runtime::SE_1_1_47;
    case RUNTIME_VERSION_1_1_51:
        return Runtime::SE_1_1_51;
    case RUNTIME_VERSION_1_2_39:
        return Runtime::SE_1_2_39;
    case RUNTIME_VERSION_1_3_9:
        return Runtime::SE_1_3_9;
    case RUNTIME_VERSION_1_4_2:
        return Runtime::SE_1_4_2;
    case RUNTIME_VERSION_1_5_3:
        return Runtime::SE_1_5_3;
    case RUNTIME_VERSION_1_5_16:
        return Runtime::SE_1_5_16;
    case RUNTIME_VERSION_1_5_23:
        return Runtime::SE_1_5_23;
    case RUNTIME_VERSION_1_5_39:
        return Runtime::SE_1_5_39;
    case RUNTIME_VERSION_1_5_50:
        return Runtime::SE_1_5_50;
    case RUNTIME_VERSION_1_5_53:
        return Runtime::SE_1_5_53;
    case RUNTIME_VERSION_1_5_62:
        return Runtime::SE_1_5_62;
    case RUNTIME_VERSION_1_5_73:
        return Runtime::SE_1_5_73;
    case RUNTIME_VERSION_1_5_80:
        return Runtime::SE_1_5_80;
    case RUNTIME_VERSION_1_5_97:
        return Runtime::SE_1_5_97;
    case RUNTIME_VERSION_1_6_317:
        return Runtime::AE_1_6_317;
    case RUNTIME_VERSION_1_6_318:
        return Runtime::AE_1_6_318;
    case RUNTIME_VERSION_1_6_323:
        return Runtime::AE_1_6_323;
    case RUNTIME_VERSION_1_6_342:
        return Runtime::AE_1_6_342;
    case RUNTIME_VERSION_1_6_353:
        return Runtime::AE_1_6_353;
    case RUNTIME_VERSION_1_6_629:
        return Runtime::AE_1_6_629;
    case RUNTIME_VERSION_1_6_640:
        return Runtime::AE_1_6_640;
    case RUNTIME_VERSION_1_6_659_GOG:
        return Runtime::AE_1_6_659_GOG;
    case RUNTIME_VERSION_1_6_1130:
        return Runtime::AE_1_6_1130;
    case RUNTIME_VERSION_1_6_1170:
        return Runtime::AE_1_6_1170;
    case RUNTIME_VERSION_1_6_1179_GOG:
        return Runtime::AE_1_6_1179_GOG;
    case RUNTIME_VERSION_1_7_99:
        return Runtime::AE_1_7_99;
    case RUNTIME_VERSION_1_7_104:
        return Runtime::AE_1_7_104;
    case MAKE_EXE_VERSION_EX(1, 4, 15, 1):
        return Runtime::VR_1_4_15;
    default:
        return Runtime::Unsupported;
    }
}

extern "C" {

__declspec(dllexport) SKSEPluginVersionData SKSEPlugin_Version =
{
    SKSEPluginVersionData::kVersion,                          // SKSE structure version
    14,                                                       // plugin version
    "Native Water Light Stabilizer",                          // plugin name
    "nicola89b",                                              // author
    "",                                                       // email
    SKSEPluginVersionData::kVersionIndependentEx_NoStructUse, // it does not use game structures
    SKSEPluginVersionData::kVersionIndependent_Signatures,    // compatibility handled by the plugin
    { 0 },
    0
};


__declspec(dllexport)
bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info)
{
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = "Native Water Light Stabilizer";
    info->version = 14;

    if (skse->isEditor) // Do not load the plugin in the Creation Kit.
        return false;

    return DetectRuntime(skse->runtimeVersion) != Runtime::Unsupported;
}



// ======== Opens the runtime-specific log and installs the matching hook set. ========

__declspec(dllexport)
bool SKSEPlugin_Load(const SKSEInterface* skse)
{
    const bool isVR = skse->runtimeVersion == MAKE_EXE_VERSION_EX(1, 4, 15, 1);
    if (isVR)
    {
        gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim VR\\SKSE\\NativeWaterLightStabilizer.log");
    }
    else
    {
        gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim Special Edition\\SKSE\\NativeWaterLightStabilizer.log");
    }

    const Runtime runtime = DetectRuntime(skse->runtimeVersion);
    // Modern SKSE can reach Load from version metadata without calling Query.
    if (runtime == Runtime::Unsupported)
    {
        _ERROR("Native Water Light Stabilizer - unsupported runtime");
        return false;
    }

    _MESSAGE("Native Water Light Stabilizer %s | %s %d.%d.%d",
             NWLS_VERSION_STRING,
             isVR ? "VR" : "Desktop",
             GET_EXE_VERSION_MAJOR(skse->runtimeVersion),
             GET_EXE_VERSION_MINOR(skse->runtimeVersion),
             GET_EXE_VERSION_BUILD(skse->runtimeVersion));

    if (!InstallHooks(runtime))
        return false;

    return true;
}

}
