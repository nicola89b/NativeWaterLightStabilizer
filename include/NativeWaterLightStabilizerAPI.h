#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#  define NWLS_CALL __cdecl
#else
#  define NWLS_CALL
#endif

namespace NWLS_API
{
	inline constexpr std::uint32_t kVersion = 1;
	inline constexpr wchar_t kModuleName[] = L"NativeWaterLightStabilizer.dll";
	inline constexpr char kExportName[] = "NWLS_GetInterface";
	inline constexpr std::uint32_t kVanillaSceneCapacity = 7;
	inline constexpr std::uint32_t kMaximumSceneCapacity = 26;

	enum class ShadowOrderMode : std::int32_t
	{
		kDefault = -1,
		kDisabled = 0,
		kEnabled = 1
	};

	struct SelectionViewV1
	{
		std::uint32_t structSize{};
		std::uint32_t apiVersion{};
		void* shaderProperty{};
		void* geometry{};
		void* const* lights{};
		std::uint32_t lightCount{};
		std::uint32_t engineOutputCount{};
		std::uint32_t reserved{};
	};

	using QuerySceneCapacityFn = std::uint32_t(NWLS_CALL*)(void* context);
	using OnSelectionFn = void(NWLS_CALL*)(
		const SelectionViewV1* selection, void* context);

	struct ConsumerV1
	{
		std::uint32_t structSize{};
		std::uint32_t apiVersion{};
		QuerySceneCapacityFn querySceneCapacity{};
		OnSelectionFn onSelection{};
		void* context{};
	};

	struct InterfaceV1
	{
		std::uint32_t structSize{};
		std::uint32_t apiVersion{};
		std::uint32_t(NWLS_CALL* registerConsumer)(const ConsumerV1* consumer){};
		void(NWLS_CALL* unregisterConsumer)(void* context){};
		std::uint32_t(NWLS_CALL* currentSceneCapacity)(){};
		std::uint32_t(NWLS_CALL* ownsWaterSelector)(){};
		std::uint32_t(NWLS_CALL* runtimeEnabled)(){};
		std::uint32_t(NWLS_CALL* setRuntimeEnabled)(std::uint32_t enabled){};
		std::int32_t(NWLS_CALL* shadowOrderMode)(){};
		std::uint32_t(NWLS_CALL* setShadowOrderMode)(std::int32_t mode){};
	};

	inline constexpr std::size_t kInterfaceV1BaseSize =
		offsetof(InterfaceV1, runtimeEnabled);
	inline constexpr std::size_t kInterfaceV1RuntimeControlSize =
		offsetof(InterfaceV1, shadowOrderMode);
	inline constexpr std::size_t kInterfaceV1ShadowOrderControlSize =
		sizeof(InterfaceV1);

	using GetInterfaceFn = const InterfaceV1*(NWLS_CALL*)(
		std::uint32_t requestedVersion);

#if INTPTR_MAX == INT64_MAX
	static_assert(sizeof(SelectionViewV1) == 48);
	static_assert(sizeof(ConsumerV1) == 32);
	static_assert(kInterfaceV1BaseSize == 40);
	static_assert(kInterfaceV1RuntimeControlSize == 56);
	static_assert(sizeof(InterfaceV1) == 72);
#endif
}

#undef NWLS_CALL
