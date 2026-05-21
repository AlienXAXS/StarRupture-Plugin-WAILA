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
#include <string>

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

		// ── Toast notification widget ────────────────────────────────────────
		void RenderToastWidget(IModLoaderImGui* imgui);
		void ShowToast(const std::string& message);

		// ── Recipe clipboard / lock ──────────────────────────────────────────
		void ApplyRecipeToCrafter(SDK::AActor* actor, const Waila::RecipeClipboard& clip);
		void SetLockWidgetVisible(bool visible);

		// ── Static C-linkage callbacks for plugin API ────────────────────────
		static void OnTick(float deltaSeconds);
		static void OnRenderWidget(IModLoaderImGui* imgui);
		static void OnRenderLockWidget(IModLoaderImGui* imgui);
		static void OnRenderToastWidget(IModLoaderImGui* imgui);

		// Hotkey callbacks — fired on the input thread, must be fast and lock-free
		static void OnCopyKey(EModKey key, EModKeyEvent event);
		static void OnPasteKey(EModKey key, EModKeyEvent event);
		static void OnLockKey(EModKey key, EModKeyEvent event);

		static void OnConfigChanged(const char* section, const char* key, const char* newValue);

		// ── State ────────────────────────────────────────────────────────────
		IPluginSelf* m_self = nullptr;
		float m_maxDistance = 256.f;
		float m_actionRaycastDistance = 3000.f;
		bool  m_showActionToast       = true;

		WidgetHandle     m_widgetHandle     = nullptr;
		bool             m_widgetVisible    = false;
		PluginWidgetDesc m_widgetDesc       = {};

		WidgetHandle     m_lockWidgetHandle  = nullptr;
		bool             m_lockWidgetVisible = false;
		PluginWidgetDesc m_lockWidgetDesc    = {};

		WidgetHandle      m_toastWidgetHandle  = nullptr;
		PluginWidgetDesc  m_toastWidgetDesc    = {};
		PluginWindowHints m_toastWindowHints   = {};
		std::string       m_toastMessage;
		float             m_toastTimeRemaining = 0.f;

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

		// Pending actions set by input-thread key handlers, consumed by Tick (game thread)
		std::atomic<bool> m_copyPending  { false };
		std::atomic<bool> m_pastePending { false };
		std::atomic<bool> m_lockPending  { false };

		// Singleton pointer used by static callbacks
		static WailaUIManager* s_instance;
	};
}
