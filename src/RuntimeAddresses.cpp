#include "common/IPrefix.h"
#include <Windows.h>
#include <cstddef>
#include "RuntimeAddresses.h"

// =============================================  Addresses =============================================

namespace
{
    // Private RVA row
    struct RuntimeAddressesRow
    {
        uintptr_t waterSceneLightSelectorCallsite{};
        uintptr_t bsWaterShaderPropertyVtable{};
        uintptr_t cameraReflectionCallsite{};
        uintptr_t playerCameraSingleton{};
    };

    // Skyrim LE 1.9.32.0 reference only for future support (32-bit ABI, for now not supported).
    //                                                   VA           RVA
    // waterSceneLightSelectorCallsite               0x00C9C6BA   0x0089C6BA
    // bsWaterShaderPropertyVtable                   0x01154440   0x00D54440
    // cameraReflectionCallsite                      0x00637991   0x00237991
    // playerCameraSingleton                         0x012E7288   0x00EE7288
    //
    // Supporting LE references:
    // BSWaterShaderProperty::GetRenderPasses        0x00C9C440   0x0089C440
    // sceneLightSelector                            0x00CBEF80   0x008BEF80
    // TESWaterReflections::Update                   0x006377D0   0x002377D0
    // Actor::CalculateLOSLocation                   0x006C1F10   0x002C1F10

    static const RuntimeAddressesRow kRuntimeAddresses[]{
        RuntimeAddressesRow{ 0x179A1A9, 0x1E517B8, 0x68529D, 0x38E0358 }, // SE 1.1.47
        RuntimeAddressesRow{ 0x179AA09, 0x1E51A18, 0x68553D, 0x38E0358 }, // SE 1.1.51
        RuntimeAddressesRow{ 0x1288869, 0x17F8000, 0x4C0DDD, 0x2E44E98 }, // SE 1.2.39
        RuntimeAddressesRow{ 0x128A3B9, 0x17FA640, 0x4C0EED, 0x2E48118 }, // SE 1.3.9
        RuntimeAddressesRow{ 0x129EE29, 0x18159F0, 0x4C0F1D, 0x2E67618 }, // SE 1.4.2
        RuntimeAddressesRow{ 0x12E2BE9, 0x1870A60, 0x4C42CD, 0x2EDEF38 }, // SE 1.5.3
        RuntimeAddressesRow{ 0x12EE6F9, 0x18797E0, 0x4C434D, 0x2EEB938 }, // SE 1.5.16
        RuntimeAddressesRow{ 0x12EEA59, 0x18797E0, 0x4C437D, 0x2EEB938 }, // SE 1.5.23
        RuntimeAddressesRow{ 0x12EF529, 0x187AA40, 0x4C445D, 0x2EEC9B8 }, // SE 1.5.39
        RuntimeAddressesRow{ 0x12EF199, 0x187AAC0, 0x4C41BD, 0x2EEC9B8 }, // SE 1.5.50
        RuntimeAddressesRow{ 0x12EF199, 0x187AAC0, 0x4C41BD, 0x2EEC9B8 }, // SE 1.5.53
        RuntimeAddressesRow{ 0x12EF199, 0x187AAC0, 0x4C41BD, 0x2EEC9B8 }, // SE 1.5.62
        RuntimeAddressesRow{ 0x12D7919, 0x185EA80, 0x4C3FCD, 0x2EC59B8 }, // SE 1.5.73
        RuntimeAddressesRow{ 0x12D7919, 0x185EA80, 0x4C3FCD, 0x2EC59B8 }, // SE 1.5.80
        RuntimeAddressesRow{ 0x12D7DC9, 0x185E9F0, 0x4C3FCD, 0x2EC59B8 }, // SE 1.5.97
        RuntimeAddressesRow{ 0x13FC00C, 0x19698D8, 0x4DE65A, 0x2F60108 }, // AE 1.6.317
        RuntimeAddressesRow{ 0x13FC01C, 0x19698D8, 0x4DE65A, 0x2F60108 }, // AE 1.6.318
        RuntimeAddressesRow{ 0x13FBF7C, 0x19698D8, 0x4DE5CA, 0x2F60108 }, // AE 1.6.323
        RuntimeAddressesRow{ 0x13FD06C, 0x196AA48, 0x4DE76A, 0x2F61288 }, // AE 1.6.342
        RuntimeAddressesRow{ 0x13FCF2C, 0x196AA48, 0x4DE5CA, 0x2F61288 }, // AE 1.6.353
        RuntimeAddressesRow{ 0x13FA7DC, 0x1968798, 0x4E0B5A, 0x2F5F608 }, // AE 1.6.629
        RuntimeAddressesRow{ 0x13FA70C, 0x1968498, 0x4E0B5A, 0x2F5F608 }, // AE 1.6.640
        RuntimeAddressesRow{ 0x13F5B5C, 0x1962768, 0x4E0AFA, 0x2F59608 }, // AE 1.6.659 GOG
        RuntimeAddressesRow{ 0x14B813C, 0x1AADA18, 0x5205EA, 0x30F04F8 }, // AE 1.6.1130
        RuntimeAddressesRow{ 0x14C082C, 0x1AB7DF8, 0x52073A, 0x30FD7F8 }, // AE 1.6.1170
        RuntimeAddressesRow{ 0x14C18CC, 0x1AB8DA8, 0x52087A, 0x30FEBF8 }, // AE 1.6.1179 GOG
        RuntimeAddressesRow{ 0x152C9EC, 0x1B40D08, 0x52846A, 0x31A5478 }, // AE 1.7.99
        RuntimeAddressesRow{ 0x152CC4C, 0x1B40D28, 0x52846A, 0x31A5478 }, // AE 1.7.104
        RuntimeAddressesRow{ 0x1317251, 0x18FF400, 0x4D416D, 0x2F8A888 }  // VR 1.4.15
    };
}

// Selects the runtime row and converts RVAs to effective VAs.
void ResolveRuntimeAddresses(Runtime runtime, RuntimeAddresses& addresses)
{
    const auto& row    = kRuntimeAddresses[static_cast<std::size_t>(runtime)];
    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));

    addresses.waterSceneLightSelectorCallsite = moduleBase + row.waterSceneLightSelectorCallsite;
    addresses.bsWaterShaderPropertyVtable     = moduleBase + row.bsWaterShaderPropertyVtable;
    addresses.cameraReflectionCallsite        = moduleBase + row.cameraReflectionCallsite;
    addresses.playerCameraSingleton           = moduleBase + row.playerCameraSingleton;
}

