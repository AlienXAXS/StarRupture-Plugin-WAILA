#pragma once

#include "plugin_interface.h"
#include "Core/raycaster.h"
#include "Core/recipe_clipboard.h"
#include "crafter_detector.h"
#include "storage_detector.h"
#include "power_detector.h"
#include "cooler_active_detector.h"
#include "cooler_passive_detector.h"
#include "cargo_sender_detector.h"
#include "cargo_receiver_detector.h"
#include "Engine_structs.hpp"
#include <atomic>
#include <mutex>
#include <unordered_set>

// Forward declarations to avoid including heavy SDK headers here
namespace SDK
{
	class AActor;
	class UCrItemRecipeData;
}

namespace Waila::UI
{
	class WailaUIManager
	{
	public:
		WailaUIManager() = default;
		~WailaUIManager() = default;

		void Initialize(IPluginSelf* self);
		void Shutdown();

		// Enable/disable without destroying — used for per-map activation
		void Enable(IPluginSelf* self);
		void Disable();

		// Called by the static engine tick callback every frame
		void Tick(float deltaSeconds);

	private:
		// ── WAILA info widget ────────────────────────────────────────────────
		void RenderWidget(IModLoaderImGui* imgui);
		void RenderCrafterInfo(IModLoaderImGui* imgui, const Waila::CrafterInfo& info);
		void RenderStorageInfo(IModLoaderImGui* imgui, const Waila::StorageInfo& info);
		void RenderPowerInfo(IModLoaderImGui* imgui, const Waila::PowerInfo& info);
		void RenderCoolerActiveInfo(IModLoaderImGui* imgui, const Waila::CoolerActiveInfo& info);
		void RenderCoolerPassiveInfo(IModLoaderImGui* imgui, const Waila::CoolerPassiveInfo& info);
		void RenderCargoSenderInfo(IModLoaderImGui* imgui, const Waila::CargoSenderInfo& info);
		void RenderCargoReceiverInfo(IModLoaderImGui* imgui, const Waila::CargoReceiverInfo& info);

		// ── Lock-mode banner widget ──────────────────────────────────────────
		void RenderLockWidget(IModLoaderImGui* imgui);

		// ── Recipe clipboard / lock ──────────────────────────────────────────
		void ApplyRecipeToCrafter(SDK::AActor* actor, const Waila::RecipeClipboard& clip);
		void SetLockWidgetVisible(bool visible);

		// ── Static C-linkage callbacks for plugin API ────────────────────────
		static void OnTick(float deltaSeconds);
		static void OnRenderWidget(IModLoaderImGui* imgui);
		static void OnRenderLockWidget(IModLoaderImGui* imgui);

		// Hotkey callbacks — fired on the input thread, must be fast and lock-free
		static void OnCopyKey(EModKey key, EModKeyEvent event);
		static void OnPasteKey(EModKey key, EModKeyEvent event);
		static void OnLockKey(EModKey key, EModKeyEvent event);

		// ── State ────────────────────────────────────────────────────────────
		IPluginSelf* m_self = nullptr;
		float m_maxDistance = 256.f;

		WidgetHandle     m_widgetHandle     = nullptr;
		bool             m_widgetVisible    = false;
		PluginWidgetDesc m_widgetDesc       = {};

		WidgetHandle     m_lockWidgetHandle  = nullptr;
		bool             m_lockWidgetVisible = false;
		PluginWidgetDesc m_lockWidgetDesc    = {};

		Waila::Core::WailaRaycastSystem m_raycaster;

		// Written by Tick (game thread), read by RenderWidget (render thread).
		std::mutex               m_infoMutex;
		Waila::CrafterInfo       m_pendingInfo;
		Waila::StorageInfo       m_pendingStorageInfo;
		Waila::PowerInfo         m_pendingPowerInfo;
		Waila::CoolerActiveInfo  m_pendingCoolerActiveInfo;
		Waila::CoolerPassiveInfo m_pendingCoolerPassiveInfo;
		Waila::CargoSenderInfo   m_pendingCargoSenderInfo;
		Waila::CargoReceiverInfo m_pendingCargoReceiverInfo;

		// Last-hit actor & recipe pointer for hotkey callbacks (under m_infoMutex)
		SDK::AActor*            m_lastHitActor   = nullptr;
		SDK::UCrItemRecipeData* m_lastRecipeData  = nullptr;

		// Debug ray for HUD visualisation
		struct DebugRay
		{
			SDK::FVector start;
			SDK::FVector end;
			bool         hit   = false;
			bool         valid = false;
		};
		DebugRay m_debugRay;

		// Clipboard & lock (game thread only — no mutex needed)
		Waila::RecipeClipboard              m_clipboard;
		bool                                m_lockActive  = false;
		Waila::RecipeClipboard              m_lockRecipe;
		// Tracks actors already pasted to in lock mode so we only paste each new building once
		std::unordered_set<void*>           m_pastedActors;

		// Singleton pointer used by static callbacks
		static WailaUIManager* s_instance;
	};
}
