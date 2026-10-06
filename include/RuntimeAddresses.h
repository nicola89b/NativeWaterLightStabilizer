#pragma once

#include <stdint.h>

// Exact Skyrim runtimes supported by the internal RVA table.
enum class Runtime : unsigned char
{
    SE_1_1_47,
    SE_1_1_51,
    SE_1_2_39,
    SE_1_3_9,
    SE_1_4_2,
    SE_1_5_3,
    SE_1_5_16,
    SE_1_5_23,
    SE_1_5_39,
    SE_1_5_50,
    SE_1_5_53,
    SE_1_5_62,
    SE_1_5_73,
    SE_1_5_80,
    SE_1_5_97,
    AE_1_6_317,
    AE_1_6_318,
    AE_1_6_323,
    AE_1_6_342,
    AE_1_6_353,
    AE_1_6_629,
    AE_1_6_640,
    AE_1_6_659_GOG,
    AE_1_6_1130,
    AE_1_6_1170,
    AE_1_6_1179_GOG,
    AE_1_7_99,
    AE_1_7_104,
    VR_1_4_15,
    Unsupported = 0xFF
};

// Effective addresses required by the release hooks.
struct RuntimeAddresses
{
    uintptr_t waterSceneLightSelectorCallsite{};
    uintptr_t bsWaterShaderPropertyVtable{};
    uintptr_t cameraReflectionCallsite{};
    uintptr_t playerCameraSingleton{};
};

// Resolves the selected RVA row against the current Skyrim image base.
void ResolveRuntimeAddresses(Runtime runtime, RuntimeAddresses& addresses);

