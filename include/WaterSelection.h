#pragma once

#include <cstdint>
#include "RuntimeAddresses.h"
#include "NativeWaterLightStabilizerAPI.h"

// Expanded temporary capacity used only while collecting Water candidates
static const std::uint32_t kWaterMaximumCapacity = 26;

// Installs the Water render-pass hook and selects the runtime ABI offsets once
bool InstallWaterSelection(const RuntimeAddresses& addresses, bool isVR, bool communityShadersMode);
void RestoreWaterSelection(const RuntimeAddresses& addresses);

// Selects a stable seven-light Water set from the candidates returned by Skyrim
std::uint32_t StabilizeWaterSelection(void* const* engineLights, std::uint32_t engineCount, std::uint32_t requestedCapacity, void** output, std::uint32_t* shadowLightCount, void* shadowSceneNode, void* property);

// Optional consumer API used by the expanded Water render-pass owner
std::uint32_t __cdecl RegisterConsumer(const NWLS_API::ConsumerV1* consumer);
void __cdecl UnregisterConsumer(void* context);
std::uint32_t __cdecl CurrentSceneCapacity();
std::uint32_t __cdecl OwnsSelector();
std::uint32_t __cdecl RuntimeEnabled();
std::uint32_t __cdecl SetRuntimeEnabled(std::uint32_t enabled);
std::int32_t __cdecl GetShadowOrderMode();
std::uint32_t __cdecl SetShadowOrderMode(std::int32_t mode);

// Marks ownership only after the raw selector callsite has been patched
void SetSelectorInstalled();
