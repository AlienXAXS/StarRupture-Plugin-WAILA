#pragma once

#include "plugin_interface.h"
#include "Core/raycaster.h"
#include "crafter_detector.h"
#include "storage_detector.h"
#include "Engine_structs.hpp"
#include <atomic>
#include <mutex>

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
		// Renders crafter info into the plugin widget via IModLoaderImGui*
		void RenderWidget(IModLoaderImGui* imgui);

		// Static C-linkage callbacks for plugin API registration
		static void OnTick(float deltaSeconds);
		static void OnRenderWidget(IModLoaderImGui* imgui);

		IPluginSelf* m_self = nullptr;
		float m_maxDistance = 256.f;

		WidgetHandle m_widgetHandle = nullptr;
		bool m_widgetVisible = false;
		PluginWidgetDesc m_widgetDesc = {};  // must outlive the registered widget handle

		Waila::Core::WailaRaycastSystem m_raycaster;

		// Written by Tick (game thread), read by RenderWidget (render thread) and HUD callback.
		std::mutex          m_infoMutex;
		Waila::CrafterInfo  m_pendingInfo;
		Waila::StorageInfo  m_pendingStorageInfo;

		// Last ray data for HUD debug visualisation — written by Tick, read by OnHUDPostRender
		struct DebugRay
		{
			SDK::FVector start;
			SDK::FVector end;
			bool         hit = false;
			bool         valid = false;
		};
		DebugRay m_debugRay;

		// Singleton pointer used by static callbacks
		static WailaUIManager* s_instance;
	};
}
