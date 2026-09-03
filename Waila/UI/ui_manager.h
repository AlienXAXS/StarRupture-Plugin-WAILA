#pragma once

#include "plugin_interface.h"
#include "Core/raycaster.h"
#include "crafter_detector.h"
#include "storage_detector.h"
#include "power_detector.h"
#include "cooler_active_detector.h"
#include "cooler_passive_detector.h"
#include "cargo_sender_detector.h"
#include "cargo_receiver_detector.h"
#include "UI/card_model.h"
#include "Engine_structs.hpp"
#include <mutex>
#include <string>

// Forward declarations to avoid including heavy SDK headers here
namespace SDK
{
	class AActor;
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
		// ── WAILA info card ──────────────────────────────────────────────────
		void RenderWidget(IModLoaderImGui* imgui);

		void RegisterMainWidget();
		void UnregisterMainWidget();
		void UpdateWindowHints(float width, float height);

		// Full read from the ini, for startup only.
		void ApplySettings();
		void ClampSettings();

		// ── Static C-linkage callbacks for plugin API ────────────────────────
		static void OnTick(float deltaSeconds);
		static void OnRenderWidget(IModLoaderImGui* imgui);

		static void OnConfigChanged(const char* section, const char* key, const char* newValue);

		// ── State ────────────────────────────────────────────────────────────
		IPluginSelf* m_self = nullptr;
		float m_maxDistance = 256.f;

		// Card presentation, refreshed from config on change.
		float m_scale            = 1.f;
		float m_opacity          = 0.92f;
		bool  m_showDescriptions = true;

		WidgetHandle      m_widgetHandle     = nullptr;
		bool              m_widgetVisible    = false;
		PluginWidgetDesc  m_widgetDesc       = {};
		PluginWindowHints m_widgetHints      = {};

		Waila::Core::WailaRaycastSystem m_raycaster;

		// Built by Tick (game thread), consumed by RenderWidget (render thread).
		// The model holds no engine pointers, so the render side never touches a
		// UObject that may have gone away since the raycast.
		std::mutex           m_infoMutex;
		Waila::UI::CardModel m_pendingCard;

		// Debug ray for HUD visualisation
		struct DebugRay
		{
			SDK::FVector start;
			SDK::FVector end;
			bool         hit   = false;
			bool         valid = false;
		};
		DebugRay m_debugRay;

		// Singleton pointer used by static callbacks
		static WailaUIManager* s_instance;
	};
}
