#include "WaterSelection.h"
#include "NativeWaterLightStabilizerAPI.h"
#include "Settings.h"
#include <detours/detours.h>
#include <exception>

namespace WaterSelection
{
	namespace
	{
		constexpr std::uint32_t kVanillaCapacity =
			NWLS_API::kVanillaSceneCapacity;
		constexpr std::uint32_t kMaximumCapacity =
			NWLS_API::kMaximumSceneCapacity;

		template <class Hook>
		bool InstallDetour(std::uintptr_t address)
		{
			if (!address)
				return false;
			Hook::func = reinterpret_cast<decltype(Hook::func)>(address);
			if (DetourTransactionBegin() != NO_ERROR)
				return false;
			if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) {
				DetourTransactionAbort();
				return false;
			}
			if (DetourAttach(reinterpret_cast<PVOID*>(&Hook::func),
				reinterpret_cast<PVOID>(Hook::thunk)) != NO_ERROR) {
				DetourTransactionAbort();
				return false;
			}
			return DetourTransactionCommit() == NO_ERROR;
		}

		bool IsSafeLight(const RE::BSLight* light)
		{
			if (!light)
				return false;
			const auto address = reinterpret_cast<std::uintptr_t>(light);
			if (address < 0x10000 || (address & 0x7) != 0)
				return false;

			struct ModuleRanges
			{
				std::uintptr_t rdataBegin{};
				std::uintptr_t rdataEnd{};
			};
			static const ModuleRanges ranges = [] {
				const auto segment = REL::Module::get().segment(REL::Segment::rdata);
				return ModuleRanges{
					segment.address(), segment.address() + segment.size()
				};
			}();

			const auto vtable = *reinterpret_cast<const std::uintptr_t*>(light);
			if (vtable < ranges.rdataBegin || vtable >= ranges.rdataEnd)
				return false;
			auto* niLight = light->light.get();
			if (!niLight)
				return false;
			const auto niAddress = reinterpret_cast<std::uintptr_t>(niLight);
			if (niAddress < 0x10000 || (niAddress & 0x7) != 0)
				return false;
			const auto niVtable = *reinterpret_cast<const std::uintptr_t*>(niLight);
			return niVtable >= ranges.rdataBegin && niVtable < ranges.rdataEnd;
		}

		bool ReachesBound(
			const RE::BSLight* light, const RE::NiPoint3& center, float boundRadius)
		{
			auto* niLight = light ? light->light.get() : nullptr;
			if (!niLight)
				return false;
			const auto radius = niLight->GetLightRuntimeData().radius.x;
			const auto delta = niLight->world.translate - center;
			const auto reach = radius + boundRadius;
			return delta.SqrLength() <= reach * reach;
		}

		template <class T, std::size_t N, class ShadowPredicate>
		std::uint32_t NormalizeShadowOrder(
			std::array<T*, N>& lights,
			std::uint32_t count,
			ShadowPredicate&& isShadow)
		{
			count = std::min<std::uint32_t>(
				count, static_cast<std::uint32_t>(N));
			if (count <= 1)
				return 0;

			std::array<T*, N> ordered{};
			std::array<bool, N> shadow{};
			ordered[0] = lights[0];
			std::uint32_t shadowCount{};
			std::size_t writeIndex = 1;
			for (std::size_t index = 1; index < count; ++index) {
				auto* light = lights[index];
				shadow[index] = light && isShadow(light);
				if (!shadow[index])
					continue;
				ordered[writeIndex++] = light;
				++shadowCount;
			}
			for (std::size_t index = 1; index < count; ++index) {
				if (!shadow[index])
					ordered[writeIndex++] = lights[index];
			}
			std::copy_n(ordered.begin(), count, lights.begin());
			return shadowCount;
		}

		std::atomic_bool g_attempted{};
		std::atomic_bool g_installed{};
		std::atomic_bool g_geometryHook{};
		std::atomic_bool g_communityShadersMode{};
		std::atomic<std::int32_t> g_shadowOrderMode{
			static_cast<std::int32_t>(NWLS_API::ShadowOrderMode::kDefault)
		};
		thread_local RE::BSGeometry* g_activeGeometry{};

		std::mutex g_consumerMutex;
		NWLS_API::ConsumerV1 g_consumer{};
		bool g_consumerRegistered{};
		std::atomic_bool g_consumerErrorLogged{};

		struct ScoredLight
		{
			RE::BSLight* light{};
			float score{};
			std::uintptr_t tieBreak{};
		};

		bool IsWaterShaderProperty(const RE::BSShaderProperty* property)
		{
			if (!property)
				return false;
			static const auto waterVtable = REL::Relocation<std::uintptr_t>{
				RE::VTABLE_BSWaterShaderProperty[0]
			}.address();
			return *reinterpret_cast<const std::uintptr_t*>(property) ==
				waterVtable;
		}

		bool IsUsablePointLight(RE::BSLight* light)
		{
			if (!IsSafeLight(light) || !light->pointLight ||
				light->ambientLight || !light->affectWater)
				return false;
			auto* niLight = light->light.get();
			if (!niLight || niLight->GetFlags().any(RE::NiAVObject::Flag::kHidden))
				return false;
			return niLight->GetLightRuntimeData().radius.x > 0.0f;
		}

		bool IsGlobalLight(const RE::BSLight* light)
		{
			return light && !light->portalStrict && light->portalGraph;
		}

		ScoredLight Score(RE::BSLight* light, const RE::NiPoint3& center)
		{
			auto* niLight = light ? light->light.get() : nullptr;
			if (!niLight)
				return {};
			const auto& data = niLight->GetLightRuntimeData();
			const auto radius = std::max(data.radius.x, 1.0f);
			const auto delta = niLight->world.translate - center;
			const auto luminance =
				data.diffuse.red + data.diffuse.green + data.diffuse.blue;
			return {
				light,
				luminance / (1.0f + delta.SqrLength() / (radius * radius)),
				reinterpret_cast<std::uintptr_t>(niLight)
			};
		}

		bool SelectStable(
			const std::array<RE::BSLight*, kMaximumCapacity>& engineLights,
			std::uint32_t engineCount,
			std::uint32_t outputCapacity,
			std::array<RE::BSLight*, kMaximumCapacity>& output,
			std::uint32_t& outputCount)
		{
			auto* geometry = g_activeGeometry;
			if (!geometry || engineCount == 0 || !engineLights[0])
				return false;

			const auto center = geometry->worldBound.center;
			const auto boundRadius = geometry->worldBound.radius;
			thread_local std::vector<ScoredLight> candidates;
			thread_local std::unordered_set<RE::BSLight*> seen;
			candidates.clear();
			seen.clear();

			auto consider = [&](RE::BSLight* light, bool engineApprovedPortal) {
				if (!IsUsablePointLight(light) ||
					!ReachesBound(light, center, boundRadius) ||
					(!engineApprovedPortal && !IsGlobalLight(light)) ||
					!seen.insert(light).second)
					return;
				candidates.push_back(Score(light, center));
			};

			for (std::uint32_t index = 1; index < engineCount; ++index)
				consider(engineLights[index], true);

			if (!g_communityShadersMode.load(std::memory_order_acquire)) {
				auto& state = RE::BSShaderManager::State::GetSingleton();
				if (auto* node = state.shadowSceneNode[0]) {
					for (auto& entry : node->GetRuntimeData().activeLights)
						consider(entry.get(), false);
				}
			}

			const auto pointCapacity =
				outputCapacity > 0 ? outputCapacity - 1u : 0u;
			const auto wanted =
				std::min<std::size_t>(pointCapacity, candidates.size());
			std::partial_sort(candidates.begin(), candidates.begin() + wanted,
				candidates.end(), [](const auto& left, const auto& right) {
					if (left.score != right.score)
						return left.score > right.score;
					return left.tieBreak < right.tieBreak;
				});

			output = {};
			output[0] = engineLights[0];
			for (std::size_t index = 0; index < wanted; ++index)
				output[index + 1u] = candidates[index].light;
			outputCount = static_cast<std::uint32_t>(wanted + 1u);
			return true;
		}

		NWLS_API::ShadowOrderMode GetShadowOrderMode()
		{
			return static_cast<NWLS_API::ShadowOrderMode>(
				g_shadowOrderMode.load(std::memory_order_acquire));
		}

		bool ShadowOrderEnabled()
		{
			return GetShadowOrderMode() !=
				NWLS_API::ShadowOrderMode::kDisabled;
		}

		template <std::size_t N>
		std::uint32_t CountShadowPrefix(
			const std::array<RE::BSLight*, N>& lights,
			std::uint32_t count)
		{
			count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(N));
			std::uint32_t shadowCount{};
			for (std::uint32_t index = 1; index < count; ++index) {
				auto* light = lights[index];
				if (!light || !light->IsShadowLight())
					break;
				++shadowCount;
			}
			return shadowCount;
		}

		NWLS_API::ConsumerV1 GetConsumer()
		{
			std::scoped_lock lock(g_consumerMutex);
			return g_consumerRegistered ? g_consumer : NWLS_API::ConsumerV1{};
		}

		void Publish(
			RE::BSShaderProperty* property,
			const std::array<RE::BSLight*, kMaximumCapacity>& lights,
			std::uint32_t lightCount,
			std::uint32_t engineOutputCount)
		{
			const auto consumer = GetConsumer();
			if (!consumer.onSelection)
				return;
			NWLS_API::SelectionViewV1 view{};
			view.structSize = sizeof(view);
			view.apiVersion = NWLS_API::kVersion;
			view.shaderProperty = property;
			view.geometry = g_activeGeometry;
			view.lights = reinterpret_cast<void* const*>(lights.data());
			view.lightCount = lightCount;
			view.engineOutputCount = engineOutputCount;
			try {
				consumer.onSelection(&view, consumer.context);
			} catch (...) {
				if (!g_consumerErrorLogged.exchange(true))
					logger::error(
						"[API] consumer callback failed");
			}
		}

		struct GetRenderPassesHook
		{
			using Fn = RE::BSShaderProperty::RenderPassArray* (*)(
				RE::BSWaterShaderProperty*, RE::BSGeometry*, std::uint32_t,
				RE::BSShaderAccumulator*);

			static RE::BSShaderProperty::RenderPassArray* thunk(
				RE::BSWaterShaderProperty* property,
				RE::BSGeometry* geometry,
				std::uint32_t renderMode,
				RE::BSShaderAccumulator* accumulator)
			{
				auto* previous = g_activeGeometry;
				g_activeGeometry = geometry;
				auto* result = func(property, geometry, renderMode, accumulator);
				g_activeGeometry = previous;
				return result;
			}

			static inline REL::Relocation<Fn> func;
		};

		template <class CallOriginal>
		std::uint32_t RunSelector(
			CallOriginal&& callOriginal,
			RE::BSLight** output,
			std::uint32_t maximumLights,
			std::uint32_t* shadowLightCount,
			RE::BSShaderProperty* property,
			std::uint8_t* shadowListState)
		{
			const bool waterSelector =
				maximumLights == kVanillaCapacity &&
				IsWaterShaderProperty(property);
			if (!waterSelector)
				return callOriginal(output, maximumLights, shadowLightCount,
					shadowListState);

			const auto consumer = GetConsumer();
			const bool hasConsumer = consumer.querySceneCapacity &&
				consumer.onSelection;
			const bool stable = Settings::RuntimeEnabled() &&
				Settings::Get().stableWaterSelection &&
				g_geometryHook.load(std::memory_order_acquire) &&
				g_activeGeometry;
			const bool forceShadowOrder = GetShadowOrderMode() ==
				NWLS_API::ShadowOrderMode::kEnabled;
			if (!stable && !hasConsumer && !forceShadowOrder)
				return callOriginal(output, maximumLights, shadowLightCount,
					shadowListState);

			const auto capacity = CurrentSceneCapacity();
			const auto gatherCapacity = stable ? kMaximumCapacity : capacity;
			std::array<RE::BSLight*, kMaximumCapacity> engineLights{};
			std::uint32_t engineShadowCount{};
			std::uint8_t engineShadowState =
				shadowListState ? *shadowListState : 0;
			const auto engineCount = std::min<std::uint32_t>(
				callOriginal(engineLights.data(), gatherCapacity,
					&engineShadowCount, &engineShadowState),
				gatherCapacity);

			auto selected = engineLights;
			auto selectedCount = std::min(engineCount, capacity);
			if (stable) {
				std::array<RE::BSLight*, kMaximumCapacity> stableLights{};
				std::uint32_t stableCount{};
				if (SelectStable(engineLights, engineCount, capacity,
						stableLights, stableCount)) {
					selected = stableLights;
					selectedCount = stableCount;
				}
			}

			const auto selectedShadowCount = ShadowOrderEnabled() ?
				NormalizeShadowOrder(
					selected, selectedCount,
					[](RE::BSLight* light) { return light->IsShadowLight(); }) :
				CountShadowPrefix(selected, selectedCount);

			const auto returnedCount =
				std::min(selectedCount, maximumLights);
			if (output && returnedCount)
				std::copy_n(selected.begin(), returnedCount, output);
			if (shadowLightCount) {
				const auto returnedPointCount =
					returnedCount > 0 ? returnedCount - 1u : 0u;
				*shadowLightCount =
					std::min(selectedShadowCount, returnedPointCount);
			}
			if (shadowListState)
				*shadowListState = engineShadowState;

			Publish(property, selected, selectedCount, returnedCount);

			return returnedCount;
		}

		struct SelectorHook
		{
			static std::uint32_t __fastcall thunk(
				void* lightData,
				RE::BSLight** output,
				std::uint32_t maximumLights,
				std::uint32_t* shadowLightCount,
				RE::ShadowSceneNode* shadowSceneNode,
				RE::BSShaderProperty* property,
				std::uint8_t scanShadowLights,
				std::uint8_t* shadowListState,
				std::uint8_t useMask,
				std::uint32_t mask)
			{
				auto callOriginal = [&](RE::BSLight** forwardedOutput,
					std::uint32_t forwardedMaximum,
					std::uint32_t* forwardedShadowCount,
					std::uint8_t* forwardedShadowState) {
					return func(lightData, forwardedOutput, forwardedMaximum,
						forwardedShadowCount, shadowSceneNode, property,
						scanShadowLights, forwardedShadowState, useMask, mask);
				};
				return RunSelector(callOriginal, output, maximumLights,
					shadowLightCount, property, shadowListState);
			}

			static inline decltype(&thunk) func{};
		};

		struct SelectorHookVR
		{
			static std::uint32_t __fastcall thunk(
				void* lightData,
				RE::BSLight** output,
				std::uint32_t maximumLights,
				std::uint32_t* shadowLightCount,
				RE::ShadowSceneNode* shadowSceneNode,
				RE::BSShaderProperty* property,
				std::uint8_t scanShadowLights,
				std::uint8_t* shadowListState,
				std::uint8_t useMask,
				std::uint32_t mask,
				std::uint8_t skipAccumulation)
			{
				if (skipAccumulation != 0) {
					return func(lightData, output, maximumLights, shadowLightCount,
						shadowSceneNode, property, scanShadowLights, shadowListState,
						useMask, mask, skipAccumulation);
				}

				auto callOriginal = [&](RE::BSLight** forwardedOutput,
					std::uint32_t forwardedMaximum,
					std::uint32_t* forwardedShadowCount,
					std::uint8_t* forwardedShadowState) {
					return func(lightData, forwardedOutput, forwardedMaximum,
						forwardedShadowCount, shadowSceneNode, property,
						scanShadowLights, forwardedShadowState, useMask, mask,
						skipAccumulation);
				};
				return RunSelector(callOriginal, output, maximumLights,
					shadowLightCount, property, shadowListState);
			}

			static inline decltype(&thunk) func{};
		};

	}

	void SetCommunityShadersMode(bool enabled)
	{
		g_communityShadersMode.store(enabled, std::memory_order_release);
	}

	bool Install()
	{
		if (g_attempted.exchange(true, std::memory_order_acq_rel))
			return OwnsSelector();
		try {
			const auto selector = REL::RelocationID(100997, 107784).address();
			const bool selectorInstalled = selector &&
				(REL::Module::IsVR() ?
					InstallDetour<SelectorHookVR>(selector) :
					InstallDetour<SelectorHook>(selector));
			if (!selectorInstalled) {
				logger::error(
					"[Water] selector hook failed (0x{:X})", selector);
				return false;
			}
			g_installed.store(true, std::memory_order_release);

			try {
				REL::Relocation<std::uintptr_t> vtable{
					RE::VTABLE_BSWaterShaderProperty[0]
				};
				constexpr std::size_t getRenderPassesIndex = 0x2Au;
				GetRenderPassesHook::func = vtable.write_vfunc(
					getRenderPassesIndex, GetRenderPassesHook::thunk);
				g_geometryHook.store(true, std::memory_order_release);
				logger::info(
					"[Water] geometry hook installed (vfunc 0x{:X})",
					getRenderPassesIndex);
			} catch (const std::exception& error) {
				logger::error(
					"[Water] geometry hook failed: {}",
					error.what());
			} catch (...) {
				logger::error(
					"[Water] geometry hook failed");
			}

			logger::info(
				"[Water] selector installed; geometry={} capacity={}/{} communityShaders={}",
				g_geometryHook.load(std::memory_order_acquire),
				kVanillaCapacity, kMaximumCapacity,
				g_communityShadersMode.load(std::memory_order_acquire));
			return true;
		} catch (const std::exception& error) {
			logger::error(
				"[Water] installation failed: {}", error.what());
			return false;
		} catch (...) {
			logger::error("[Water] installation failed");
			return false;
		}
	}

	bool OwnsSelector()
	{
		return g_installed.load(std::memory_order_acquire);
	}

	std::uint32_t CurrentSceneCapacity()
	{
		const auto consumer = GetConsumer();
		if (!consumer.querySceneCapacity)
			return kVanillaCapacity;
		try {
			return std::clamp(consumer.querySceneCapacity(consumer.context),
				kVanillaCapacity, kMaximumCapacity);
		} catch (...) {
			if (!g_consumerErrorLogged.exchange(true))
				logger::error(
					"[API] capacity callback failed");
			return kVanillaCapacity;
		}
	}

	std::uint32_t RegisterConsumer(const NWLS_API::ConsumerV1* consumer)
	{
		if (!OwnsSelector())
			return 0;
		if (!consumer || consumer->structSize < sizeof(NWLS_API::ConsumerV1) ||
			consumer->apiVersion != NWLS_API::kVersion ||
			!consumer->querySceneCapacity || !consumer->onSelection)
			return 0;
		std::scoped_lock lock(g_consumerMutex);
		if (g_consumerRegistered && g_consumer.context != consumer->context)
			return 0;
		g_consumer = *consumer;
		g_consumerRegistered = true;
		g_consumerErrorLogged.store(false, std::memory_order_release);
		logger::info("[API] consumer registered");
		return 1;
	}

	void UnregisterConsumer(void* context)
	{
		std::scoped_lock lock(g_consumerMutex);
		if (!g_consumerRegistered || g_consumer.context != context)
			return;
		g_consumer = {};
		g_consumerRegistered = false;
		logger::info("[API] consumer unregistered");
	}

	std::int32_t ShadowOrderMode()
	{
		return g_shadowOrderMode.load(std::memory_order_acquire);
	}

	bool SetShadowOrderMode(std::int32_t mode)
	{
		if (mode < static_cast<std::int32_t>(NWLS_API::ShadowOrderMode::kDefault) ||
			mode > static_cast<std::int32_t>(NWLS_API::ShadowOrderMode::kEnabled))
			return false;
		const auto previous = g_shadowOrderMode.exchange(
			mode, std::memory_order_acq_rel);
		if (previous != mode) {
			const auto label = mode < 0 ? "default" :
				(mode == 0 ? "disabled" : "enabled");
			logger::info("[Runtime] shadow order={}", label);
		}
		return true;
	}
}

extern "C" __declspec(dllexport)
const NWLS_API::InterfaceV1* __cdecl NWLS_GetInterface(
	std::uint32_t requestedVersion)
{
	if (requestedVersion != NWLS_API::kVersion)
		return nullptr;
	static const NWLS_API::InterfaceV1 api{
		sizeof(NWLS_API::InterfaceV1),
		NWLS_API::kVersion,
		WaterSelection::RegisterConsumer,
		WaterSelection::UnregisterConsumer,
		WaterSelection::CurrentSceneCapacity,
		[]() -> std::uint32_t { return WaterSelection::OwnsSelector() ? 1u : 0u; },
		[]() -> std::uint32_t { return Settings::RuntimeEnabled() ? 1u : 0u; },
		[](std::uint32_t enabled) -> std::uint32_t {
			Settings::SetRuntimeEnabled(enabled != 0);
			return 1u;
		},
		WaterSelection::ShadowOrderMode,
		[](std::int32_t mode) -> std::uint32_t {
			return WaterSelection::SetShadowOrderMode(mode) ? 1u : 0u;
		}
	};
	return &api;
}
