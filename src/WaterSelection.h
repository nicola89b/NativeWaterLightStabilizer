#pragma once

#include "PCH.h"

namespace WaterSelection
{
	void SetCommunityShadersMode(bool enabled);
	bool Install();
	[[nodiscard]] bool OwnsSelector();
	[[nodiscard]] std::uint32_t CurrentSceneCapacity();
}
