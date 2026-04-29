#pragma once

#include "plugin_interface.h"
#include "Core/raycaster.h"
#include "crafter_detector.h"
#include "storage_detector.h"
#include "power_detector.h"
#include "cooler_active_detector.h"
#include "cooler_passive_detector.h"
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
		void RenderWidget(IModLoaderImGui* imgui);
		void RenderCrafterInfo(IModLoaderImGui* imgui, const Waila::CrafterInfo& info);
		void RenderStorageInfo(IModLoaderImGui* imgui, const Waila::StorageInfo& info);
		void RenderPowerInfo(IModLoaderImGui* imgui, const Waila::PowerInfo& info);
		void RenderCoolerActiveInfo(IModLoaderImGui* imgui, const Waila::CoolerActiveInfo& info);
		void RenderCoolerPassiveInfo(IModLoaderImGui* imgui, const Waila::CoolerPassiveInfo& info);

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
		Waila::PowerInfo    m_pendingPowerInfo;
		Waila::CoolerActiveInfo  m_pendingCoolerActiveInfo;
		Waila::CoolerPassiveInfo m_pendingCoolerPassiveInfo;

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
