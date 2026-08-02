#pragma once

#include "PCH.h"

namespace Settings
{
	struct Values
	{
		bool enabled{ true };
		bool stableWaterSelection{ true };
		bool cameraCenteredReflections{ true };
	};

	[[nodiscard]] const Values& Load();
	[[nodiscard]] const Values& Get();
	[[nodiscard]] bool RuntimeEnabled();
	void SetRuntimeEnabled(bool enabled);
}
