#include "common/IPrefix.h"
#include "skse64_common/SafeWrite.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "WaterSelection.h"

namespace
{
    static const std::uint32_t kWaterVanillaCapacity = NWLS_API::kVanillaSceneCapacity;
    static const std::uint32_t kWaterSelectionMaximumCapacity = NWLS_API::kMaximumSceneCapacity;

    // Minimal three-float vector used by the raw render data.
    struct Point3
    {
        float x{};
        float y{};
        float z{};
    };

    // Candidate light together with its deterministic selection score.
    struct ScoredLight
    {
        void* light{};
        float score{};
        uintptr_t tieBreak{};
    };

    struct CandidateLight
    {
        void* light{};
        void* niLight{};
        float radius{};
        Point3 position{};
    };

    typedef void* (*_WaterGetRenderPasses)(void*, void*, std::uint32_t, void*);
    typedef bool (*_IsShadowLight)(void*);

    // Runtime-selected ABI offsets and shared hook state.
    std::size_t g_niLightRuntimeOffset{};
    std::size_t g_niLightFlagsOffset{};
    std::size_t g_activeLightsOffset{};
    _WaterGetRenderPasses g_waterGetRenderPasses = NULL;
    thread_local void* g_activeGeometry{};
    //
    SRWLOCK g_consumerLock = SRWLOCK_INIT;
    NWLS_API::ConsumerV1 g_consumer = {};
    bool g_consumerRegistered = false;
    volatile LONG g_consumerRegisteredFast = FALSE;
    volatile LONG g_consumerReaders = 0;
    volatile LONG g_selectorInstalled = FALSE;
    volatile LONG g_runtimeEnabled = TRUE;
    volatile LONG g_shadowOrderMode = NWLS_API::kShadowOrderDefault;

    struct ConsumerLease
    {
        NWLS_API::ConsumerV1 consumer{};
        bool active{};
    };

    ConsumerLease AcquireConsumer()
    {
        ConsumerLease lease{};
        if (InterlockedCompareExchange(&g_consumerRegisteredFast, 0, 0) == 0)
            return lease;

        AcquireSRWLockShared(&g_consumerLock);
        if (g_consumerRegistered)
        {
            lease.consumer = g_consumer;
            InterlockedIncrement(&g_consumerReaders);
            lease.active = true;
        }
        ReleaseSRWLockShared(&g_consumerLock);
        return lease;
    }

    void ReleaseConsumer(ConsumerLease& lease)
    {
        if (lease.active)
        InterlockedDecrement(&g_consumerReaders);
    }

    bool ShadowOrderEnabled(){ return InterlockedCompareExchange(&g_shadowOrderMode, 0, 0) != NWLS_API::kShadowOrderDisabled; }
    std::uint32_t ClampCapacity(std::uint32_t capacity)
    {
        if (capacity < kWaterVanillaCapacity)
        return kWaterVanillaCapacity;

        if (capacity > kWaterSelectionMaximumCapacity)
        return kWaterSelectionMaximumCapacity;
        return capacity;
    }

    // Reads a field from a raw Skyrim object at the supplied ABI offset
    template <class T>
    T Read(const void* object, std::size_t offset){ return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(object) + offset); }
    float SqrLength(const Point3& value){ return value.x * value.x + value.y * value.y + value.z * value.z; }   // Returns the squared length of a three-dimensional vector!
    Point3 Subtract(const Point3& left, const Point3& right){ return Point3{ left.x - right.x, left.y - right.y, left.z - right.z }; }
    void* GetNiLight(void* light){ return Read<void*>(light, 0x48); }                                           // Returns the NiLight owned by a BSLight
    Point3 NiLightWorldTranslate(void* niLight){ return Read<Point3>(niLight, 0xA0); }                          // Reads the current world-space position of a NiLight
    Point3 NiLightDiffuse(void* niLight){ return Read<Point3>(niLight, g_niLightRuntimeOffset + 0x0C); }        // Reads the diffuse light color from the runtime-specific NiLight data block
    float NiLightRadius(void* niLight){ return Read<float>(niLight, g_niLightRuntimeOffset + 0x18); }           // Reads the effective radius from the runtime-specific NiLight data block
    bool NiLightHidden(void* niLight){ return (Read<std::uint32_t>(niLight, g_niLightFlagsOffset) & 1u) != 0; } // Tests the hidden flag using the runtime-specific NiAVObject layout

    // Rejects lights that cannot contribute to the Water pass
    bool ReadCandidateLight(void* light, CandidateLight& candidate)
    {
        if (!Read<bool>(light, 0x44) || Read<bool>(light, 0x45) || !Read<bool>(light, 0x62))
        return false;

        candidate.light = light;
        candidate.niLight = GetNiLight(light);
        if (!candidate.niLight || NiLightHidden(candidate.niLight))
        return false;

        candidate.radius = NiLightRadius(candidate.niLight);
        if (candidate.radius <= 0.0f)
        return false;

        candidate.position = NiLightWorldTranslate(candidate.niLight);
        return true;
    }

    // Identifies non-portal-restricted lights available from the active scene list
    bool IsGlobalLight(void* light){ return !Read<bool>(light, 0x47) && Read<void*>(light, 0x120) != NULL; }

    // Tests whether a light radius reaches the active geometry bound
    bool ReachesBound(const CandidateLight& candidate, const Point3& center, float boundRadius)
    {
        const Point3 delta = Subtract(candidate.position, center);
        const float reach = candidate.radius + boundRadius;
        return SqrLength(delta) <= reach * reach;
    }

    // Scores a light by diffuse intensity and distance from the geometry center.
    ScoredLight Score(const CandidateLight& candidate, const Point3& center)
    {
        const float radius   = (std::max)(candidate.radius, 1.0f);
        const Point3 delta   = Subtract(candidate.position, center);
        const Point3 diffuse = NiLightDiffuse(candidate.niLight);
        return ScoredLight
            {
                candidate.light,(diffuse.x + diffuse.y + diffuse.z) / (1.0f + SqrLength(delta) / (radius * radius)),
                reinterpret_cast<uintptr_t>(candidate.niLight)
            };
    }

    // Calls the native BSLight shadow test through virtual slot 3.
    bool IsShadowLight(void* light)
    {
        auto** vtable = *reinterpret_cast<void***>(light);
        return reinterpret_cast<_IsShadowLight>(vtable[3])(light);
    }

    // Prevents the same light from entering the candidate list twice.
    bool Seen(const std::vector<ScoredLight>& candidates, void* light)
    {
        for (const auto& candidate : candidates)
            if (candidate.light == light)
            return true;
        return false;
    }

    // Keeps the first light fixed and moves shadow lights ahead of non-shadow lights. Maths!
    template <std::size_t N>
    std::uint32_t NormalizeShadowOrder(std::array<void*, N>& lights, std::uint32_t count)
    {
        std::array<void*, N> ordered;
        std::array<bool, N> shadow;
        ordered[0] = lights[0];
        std::uint32_t shadowCount{};
        std::size_t writeIndex = 1;

        for (std::size_t index = 1; index < count; ++index)
        {
            shadow[index] = IsShadowLight(lights[index]);
            if (shadow[index])
            {
                ordered[writeIndex++] = lights[index];
                ++shadowCount;
            }
        }

        for (std::size_t index = 1; index < count; ++index)
            if (!shadow[index])
                ordered[writeIndex++] = lights[index];

        std::copy_n(ordered.begin(), count, lights.begin());
        return shadowCount;
    }

    // Tracks the geometry currently being processed by the Water render pass.
    void* WaterGetRenderPassesHook(void* property, void* geometry, std::uint32_t renderMode, void* accumulator)
    {
        void* previous   = g_activeGeometry;
        g_activeGeometry = geometry;
        void* result     = g_waterGetRenderPasses(property, geometry, renderMode, accumulator);
        g_activeGeometry = previous;
        return result;
    }

    void PublishSelection(const NWLS_API::ConsumerV1& consumer, void* property, void* const* lights, std::uint32_t lightCount, std::uint32_t engineOutputCount)
    {
        NWLS_API::SelectionViewV1 selection = {};
        selection.structSize = sizeof(selection);
        selection.apiVersion = NWLS_API::kVersion;
        selection.shaderProperty = property;
        selection.geometry = g_activeGeometry;
        selection.lights = lights;
        selection.lightCount = lightCount;
        selection.engineOutputCount = engineOutputCount;
        consumer.onSelection(&selection, consumer.context);
    }
}

// Selects ABI offsets once and hooks BSWaterShaderProperty::GetRenderPasses.
bool InstallWaterSelection(const RuntimeAddresses& addresses, bool isVR, bool communityShadersMode)
{
    g_niLightRuntimeOffset = isVR ? 0x138 : 0x110;
    g_niLightFlagsOffset   = isVR ? 0x10C : 0xF4;
    g_activeLightsOffset   = communityShadersMode ? 0 : (isVR ? 0x158 : 0x130);
    const uintptr_t slot   = addresses.bsWaterShaderPropertyVtable + 0x2A * sizeof(uintptr_t);
    g_waterGetRenderPasses = reinterpret_cast<_WaterGetRenderPasses>(*reinterpret_cast<const uintptr_t*>(slot));
    SafeWrite64(slot, static_cast<UInt64>(reinterpret_cast<uintptr_t>(&WaterGetRenderPassesHook)));
    if (*reinterpret_cast<const uintptr_t*>(slot) == reinterpret_cast<uintptr_t>(&WaterGetRenderPassesHook))
    return true;
    SafeWrite64(slot, static_cast<UInt64>(reinterpret_cast<uintptr_t>(g_waterGetRenderPasses)));
    return false;
}

void RestoreWaterSelection(const RuntimeAddresses& addresses)
{
    const uintptr_t slot = addresses.bsWaterShaderPropertyVtable + 0x2A * sizeof(uintptr_t);
    SafeWrite64(slot, static_cast<UInt64>(reinterpret_cast<uintptr_t>(g_waterGetRenderPasses)));
}

// Builds the final Water list for the current geometry
std::uint32_t StabilizeWaterSelection(void* const* engineLights, std::uint32_t engineCount, std::uint32_t requestedCapacity, void** output, std::uint32_t* shadowLightCount, void* shadowSceneNode, void* property)
{
    if (g_activeGeometry == NULL || engineLights == NULL || output == NULL || engineCount == 0 || engineLights[0] == NULL)
        return 0;

    ConsumerLease consumerLease = AcquireConsumer();
    const std::uint32_t outputCapacity = consumerLease.active ? ClampCapacity(consumerLease.consumer.querySceneCapacity(consumerLease.consumer.context)) : kWaterVanillaCapacity;
    if (requestedCapacity == 0)
        requestedCapacity = kWaterVanillaCapacity;
    if (requestedCapacity > kWaterVanillaCapacity)
        requestedCapacity = kWaterVanillaCapacity;

    std::array<void*, kWaterSelectionMaximumCapacity> selected = {};
    std::uint32_t selectedCount = 0;
    std::uint32_t selectedShadowCount = 0;

    if (InterlockedCompareExchange(&g_runtimeEnabled, 0, 0) == 0)
    {
        selectedCount = (std::min)(engineCount, outputCapacity);
        for (std::uint32_t index = 0; index < selectedCount; ++index)
            selected[index] = engineLights[index];
        if (ShadowOrderEnabled())
            selectedShadowCount = NormalizeShadowOrder(selected, selectedCount);
    }

    else
    {
        const Point3 center = Read<Point3>(g_activeGeometry, 0xE4);
        const float boundRadius = Read<float>(g_activeGeometry, 0xF0);
        thread_local std::vector<ScoredLight> candidates;
        candidates.clear();
        auto consider = [&](void* light) 
        {
            if (light == NULL || light == engineLights[0] || Seen(candidates, light))
                return;

            CandidateLight candidate{};
            if (!ReadCandidateLight(light, candidate) || !ReachesBound(candidate, center, boundRadius))
                return;

            candidates.push_back(Score(candidate, center));
        };

        for (std::uint32_t index = 1; index < engineCount; ++index)
            consider(engineLights[index]);

        if (g_activeLightsOffset && shadowSceneNode != NULL)
        {
            void** activeLights = Read<void**>(shadowSceneNode, g_activeLightsOffset);
            const std::uint32_t activeLightCount = Read<std::uint32_t>(shadowSceneNode, g_activeLightsOffset + 0x10);

            if (activeLights != NULL)
            {
                for (std::uint32_t index = 0; index < activeLightCount; ++index)
                {
                    void* light = activeLights[index];
                    if (light && IsGlobalLight(light))
                        consider(light);
                }
            }
        }

        const std::size_t wanted = (std::min)(static_cast<std::size_t>(outputCapacity - 1u), candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + wanted, candidates.end(), [](const ScoredLight& left, const ScoredLight& right) 
        {
            if (left.score != right.score)
                return left.score > right.score;
                return left.tieBreak < right.tieBreak;
            });

        selected[0] = engineLights[0];
        for (std::size_t index = 0; index < wanted; ++index)
            selected[index + 1u] = candidates[index].light;

        selectedCount = static_cast<std::uint32_t>(wanted + 1u);
        if (ShadowOrderEnabled())
            selectedShadowCount = NormalizeShadowOrder(selected, selectedCount);
    }

    const std::uint32_t visibleCount = (std::min)(selectedCount, requestedCapacity);
    for (std::uint32_t index = 0; index < visibleCount; ++index)
        output[index] = selected[index];
    if (shadowLightCount != NULL)
    {
        const std::uint32_t visiblePointCount = visibleCount > 0 ? visibleCount - 1u : 0u;
        *shadowLightCount = (std::min)(selectedShadowCount, visiblePointCount);
    }

    if (consumerLease.active)
        PublishSelection(consumerLease.consumer, property, selected.data(), selectedCount, visibleCount);

    ReleaseConsumer(consumerLease);
    return visibleCount;
}

std::uint32_t __cdecl RegisterConsumer(const NWLS_API::ConsumerV1* consumer)
{
    if (!OwnsSelector() || consumer == NULL)
        return 0;
    if (consumer->structSize < sizeof(NWLS_API::ConsumerV1) || consumer->apiVersion != NWLS_API::kVersion || consumer->querySceneCapacity == NULL || consumer->onSelection == NULL)
        return 0;

    AcquireSRWLockExclusive(&g_consumerLock);
    if (g_consumerRegistered && g_consumer.context != consumer->context)
    {
        ReleaseSRWLockExclusive(&g_consumerLock);
        return 0;
    }
    while (InterlockedCompareExchange(&g_consumerReaders, 0, 0) != 0)
        SwitchToThread();

    g_consumer = *consumer;
    g_consumerRegistered = true;
    InterlockedExchange(&g_consumerRegisteredFast, TRUE);
    ReleaseSRWLockExclusive(&g_consumerLock);
    return 1;
}

void __cdecl UnregisterConsumer(void* context)
{
    AcquireSRWLockExclusive(&g_consumerLock);
    if (g_consumerRegistered && g_consumer.context == context)
    {
        InterlockedExchange(&g_consumerRegisteredFast, FALSE);
        g_consumerRegistered = false;
        while (InterlockedCompareExchange(&g_consumerReaders, 0, 0) != 0)
            SwitchToThread();
        g_consumer = NWLS_API::ConsumerV1{};
    }
    ReleaseSRWLockExclusive(&g_consumerLock);
}

std::uint32_t __cdecl CurrentSceneCapacity()
{
    ConsumerLease consumerLease = AcquireConsumer();
    if (!consumerLease.active)
        return kWaterVanillaCapacity;

    const std::uint32_t capacity = ClampCapacity(consumerLease.consumer.querySceneCapacity(consumerLease.consumer.context));
    ReleaseConsumer(consumerLease);
    return capacity;
}

std::uint32_t __cdecl OwnsSelector(){ return InterlockedCompareExchange(&g_selectorInstalled, 0, 0) != 0 ? 1u : 0u; }
std::uint32_t __cdecl RuntimeEnabled(){ return InterlockedCompareExchange(&g_runtimeEnabled, 0, 0) != 0 ? 1u : 0u; }
std::uint32_t __cdecl SetRuntimeEnabled(std::uint32_t enabled){ InterlockedExchange(&g_runtimeEnabled, enabled != 0 ? 1 : 0); return 1; }
std::int32_t __cdecl GetShadowOrderMode(){ return static_cast<std::int32_t>(InterlockedCompareExchange(&g_shadowOrderMode, 0, 0)); }
std::uint32_t __cdecl SetShadowOrderMode(std::int32_t mode)
{
    if (mode < NWLS_API::kShadowOrderDefault || mode > NWLS_API::kShadowOrderEnabled)
    return 0;

    InterlockedExchange(&g_shadowOrderMode, static_cast<LONG>(mode));
    return 1;
}

void SetSelectorInstalled(){ InterlockedExchange(&g_selectorInstalled, TRUE); }
extern "C" __declspec(dllexport)
const NWLS_API::InterfaceV1* __cdecl NWLS_GetInterface(std::uint32_t requestedVersion)
{
    if (requestedVersion != NWLS_API::kVersion)
        return NULL;

    static const NWLS_API::InterfaceV1 api =
    {
        sizeof(NWLS_API::InterfaceV1),
        NWLS_API::kVersion,
        RegisterConsumer,
        UnregisterConsumer,
        CurrentSceneCapacity,
        OwnsSelector,
        RuntimeEnabled,
        SetRuntimeEnabled,
        GetShadowOrderMode,
        SetShadowOrderMode
    };
    return &api;
}
