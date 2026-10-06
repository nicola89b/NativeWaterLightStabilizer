#pragma once

#include <cstddef>
#include <cstdint>

namespace NWLS_API
{
    static const std::uint32_t kVersion = 1;
    static const wchar_t kModuleName[] = L"Native_Water_Light_Stabilizer.dll";
    static const char kExportName[] = "NWLS_GetInterface";
    static const std::uint32_t kVanillaSceneCapacity = 7;
    static const std::uint32_t kMaximumSceneCapacity = 26;

    enum ShadowOrderMode
    {
        kShadowOrderDefault  = -1,
        kShadowOrderDisabled = 0,
        kShadowOrderEnabled  = 1
    };

    struct SelectionViewV1
    {
        std::uint32_t structSize;
        std::uint32_t apiVersion;
        void* shaderProperty;
        void* geometry;
        void* const* lights;
        std::uint32_t lightCount;
        std::uint32_t engineOutputCount;
        std::uint32_t reserved;
    };

    typedef std::uint32_t (__cdecl *QuerySceneCapacityFn)(void* context);

    // Keep callback code and context alive until unregister returns; do not register or unregister from a callback.
    typedef void (__cdecl *OnSelectionFn)(const SelectionViewV1* selection, void* context);

    struct ConsumerV1
    {
        std::uint32_t structSize;
        std::uint32_t apiVersion;
        QuerySceneCapacityFn querySceneCapacity;
        OnSelectionFn onSelection;
        void* context;
    };

    struct InterfaceV1
    {
        std::uint32_t structSize;
        std::uint32_t apiVersion;
        std::uint32_t (__cdecl *registerConsumer)(const ConsumerV1* consumer);
        void (__cdecl *unregisterConsumer)(void* context);
        std::uint32_t (__cdecl *currentSceneCapacity)();
        std::uint32_t (__cdecl *ownsWaterSelector)();
        std::uint32_t (__cdecl *runtimeEnabled)();
        std::uint32_t (__cdecl *setRuntimeEnabled)(
        std::uint32_t enabled);
        std::int32_t (__cdecl *shadowOrderMode)();
        std::uint32_t (__cdecl *setShadowOrderMode)(std::int32_t mode);
    };

    static const std::size_t kInterfaceV1BaseSize = offsetof(InterfaceV1, runtimeEnabled);
    static const std::size_t kInterfaceV1RuntimeControlSize = offsetof(InterfaceV1, shadowOrderMode);
    static const std::size_t kInterfaceV1ShadowOrderControlSize = sizeof(InterfaceV1);
    typedef const InterfaceV1* (__cdecl *GetInterfaceFn)(std::uint32_t requestedVersion);

    static_assert(sizeof(void*) == 8, "NWLS requires x64");
    static_assert(sizeof(SelectionViewV1) == 48, "NWLS SelectionViewV1 ABI");
    static_assert(sizeof(ConsumerV1) == 32, "NWLS ConsumerV1 ABI");
    static_assert(kInterfaceV1BaseSize == 40, "NWLS InterfaceV1 base ABI");
    static_assert(kInterfaceV1RuntimeControlSize == 56, "NWLS InterfaceV1 runtime ABI");
    static_assert(sizeof(InterfaceV1) == 72, "NWLS InterfaceV1 ABI");
}
