#include "common/IPrefix.h"
#include "skse64_common/BranchTrampoline.h"
#include <cstdint>
#include "CameraReflectionFix.h"

namespace
{
    // Minimal three-float position used by the hooked Skyrim function.
    struct Point3
    {
        float x{};
        float y{};
        float z{};
    };

    typedef Point3* (*_CameraPosition)(void*, Point3*, int, float);
    _CameraPosition g_cameraPosition = NULL;
    uintptr_t g_playerCameraSingleton{};

    // Calls the original position function and replaces its result with the camera root position.
    Point3* CameraPositionHook(void* player, Point3* target, int unknown1, float unknown2)
    {
        Point3* result = g_cameraPosition(player, target, unknown1, unknown2);
        void* camera = *reinterpret_cast<void**>(g_playerCameraSingleton);
        void* cameraRoot = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(camera) + 0x20);
        *result = *reinterpret_cast<const Point3*>(reinterpret_cast<const std::uint8_t*>(cameraRoot) + 0xA0);
        return result;
    }
}

// Decodes the original CALL target and replaces only the Water reflection callsite.
bool InstallCameraReflectionFix(const RuntimeAddresses& addresses)
{
    const uintptr_t callsite = addresses.cameraReflectionCallsite;
    const auto displacement = *reinterpret_cast<const std::int32_t*>(callsite + 1);
    g_cameraPosition = reinterpret_cast<_CameraPosition>(callsite + 5 + displacement);
    g_playerCameraSingleton = addresses.playerCameraSingleton;
    return g_branchTrampoline.Write5Call(callsite, reinterpret_cast<uintptr_t>(&CameraPositionHook));
}
