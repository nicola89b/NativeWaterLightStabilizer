#include "common/IPrefix.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64_common/Utilities.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>
#include "CameraReflectionFix.h"
#include "RuntimeAddresses.h"
#include "Hooks.h"
#include "WaterSelection.h"

namespace
{
    typedef std::uint32_t (*_DesktopSelectSceneLights)( void*, void**, std::uint32_t, std::uint32_t*, void*, void*, std::uint8_t, std::uint8_t*, std::uint8_t, std::uint32_t);
    typedef std::uint32_t (*_VRSelectSceneLights)( void*, void**, std::uint32_t, std::uint32_t*, void*, void*, std::uint8_t, std::uint8_t*, std::uint8_t, std::uint32_t, std::uint8_t);
    _DesktopSelectSceneLights g_desktopSelectSceneLights = NULL;
    _VRSelectSceneLights g_vrSelectSceneLights = NULL;

    // Checks both loaded modules and installed SKSE plugin files.
    bool HasPlugin(const char* name)
    {
        if (GetModuleHandleA(name))
        return true;

        std::string path = GetRuntimeDirectory();
        path += "Data\\SKSE\\Plugins\\";
        path += name;
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool ValidateHooks(const RuntimeAddresses& addresses)
    {
        if (*reinterpret_cast<const std::uint8_t*>(addresses.waterSceneLightSelectorCallsite) != 0xE8)
        {
            _ERROR("Hook validation failed: Water selector callsite");
            return false;
        }

        const uintptr_t waterSlot = addresses.bsWaterShaderPropertyVtable + 0x2A * sizeof(uintptr_t);
        if (*reinterpret_cast<const uintptr_t*>(waterSlot) == 0)
        {
            _ERROR("Hook validation failed: Water render-pass slot");
            return false;
        }

        return true;
    }



    // Expands the Desktop Water selector call, then returns the negotiated vanilla-sized prefix 
    std::uint32_t DesktopSelectSceneLightsHook( void* lightData, void** output, std::uint32_t maximumLights, std::uint32_t* shadowLightCount, void* shadowSceneNode, void* property,  std::uint8_t scanShadowLights, std::uint8_t* shadowListState,  std::uint8_t useMask,  std::uint32_t mask)
    {
        std::array<void*, kWaterMaximumCapacity> engineLights;
        const std::uint32_t engineCount = g_desktopSelectSceneLights(lightData, engineLights.data(), kWaterMaximumCapacity, shadowLightCount, shadowSceneNode, property, scanShadowLights, shadowListState, useMask, mask);
        return StabilizeWaterSelection(engineLights.data(), engineCount, maximumLights, output, shadowLightCount, shadowSceneNode, property);
    }

    // Expands the VR Water selector call, then returns the negotiated vanilla-sized prefix 
    std::uint32_t VRSelectSceneLightsHook(void* lightData, void** output, std::uint32_t maximumLights, std::uint32_t* shadowLightCount, void* shadowSceneNode, void* property, std::uint8_t scanShadowLights, std::uint8_t* shadowListState, std::uint8_t useMask, std::uint32_t mask, std::uint8_t skipAccumulation)
    {
        std::array<void*, kWaterMaximumCapacity> engineLights;
        const std::uint32_t engineCount = g_vrSelectSceneLights(lightData, engineLights.data(), kWaterMaximumCapacity, shadowLightCount, shadowSceneNode, property, scanShadowLights, shadowListState, useMask,mask, skipAccumulation);
        return StabilizeWaterSelection(engineLights.data(), engineCount, maximumLights, output, shadowLightCount, shadowSceneNode, property);
    }



    // Choosen runtime
    bool InstallSceneLightSelectorHook(const RuntimeAddresses& addresses, bool isVR)
    {
        const uintptr_t callsite = addresses.waterSceneLightSelectorCallsite;
        const auto displacement = *reinterpret_cast<const std::int32_t*>(callsite + 1);
        const uintptr_t target = callsite + 5 + displacement;
        uintptr_t hook{};

        if (isVR)
        {
            g_vrSelectSceneLights = reinterpret_cast<_VRSelectSceneLights>(target);
            hook = reinterpret_cast<uintptr_t>(&VRSelectSceneLightsHook);
        }

        else
        {
            g_desktopSelectSceneLights = reinterpret_cast<_DesktopSelectSceneLights>(target);
            hook = reinterpret_cast<uintptr_t>(&DesktopSelectSceneLightsHook);
        }

        if (!g_branchTrampoline.Write5Call(callsite, hook))
            return false;

        SetSelectorInstalled();
        return true;
    }
}

// Resolves, validates and installs all hooks required by the specific version
bool InstallHooks(Runtime runtime)
{
    const bool isVR = runtime == Runtime::VR_1_4_15;
    const bool communityShaders = HasPlugin("CommunityShaders.dll");
    const bool installCameraFix = !communityShaders && !HasPlugin("SkyReflectionFix.dll");

    RuntimeAddresses addresses;
    ResolveRuntimeAddresses(runtime, addresses);

    if (!ValidateHooks(addresses))
        return false;

    if (!g_branchTrampoline.Create(1024 * 64))
    {
        _ERROR("Hook setup failed: trampoline allocation");
        return false;
    }

    if (!InstallWaterSelection(addresses, isVR, communityShaders))
    {
        _ERROR("Hook setup failed: Water render-pass hook");
        return false;
    }

    if (!InstallSceneLightSelectorHook(addresses, isVR))
    {
        RestoreWaterSelection(addresses);
        _ERROR("Scene-light selector hook installation failed");
        return false;
    }

    if (installCameraFix)
    {
        if (*reinterpret_cast<const std::uint8_t*>(addresses.cameraReflectionCallsite) != 0xE8)
            _WARNING("Camera reflection hook skipped: callsite validation failed");
        else if (!InstallCameraReflectionFix(addresses))
            _WARNING("Camera reflection hook installation failed");
    }

    _MESSAGE("Water selector hooks active");
    return true;
}
