#include "plugin.h"
#include "plugin_helpers.h"
#include "plugin_config.h"
#include "UI/ui_manager.h"
#include "Engine_classes.hpp"
#include <cstring>

// Global plugin self pointer — stable for the plugin's lifetime, retained from PluginInit
static IPluginSelf* g_self = nullptr;

IPluginSelf* GetSelf() { return g_self; }

// Global UI manager instance
static Waila::UI::WailaUIManager* g_uiManager = nullptr;

// Track whether WAILA is currently active (ChimeraMain is loaded)
static bool g_isActive = false;

static void OnWorldBeginPlay(SDK::UWorld* world, const char* worldName)
{
	if (worldName && strcmp(worldName, "ChimeraMain") == 0)
	{
		LOG_INFO("ChimeraMain loaded — activating WAILA");
		LOG_DEBUG("OnWorldBeginPlay: g_uiManager=%p g_self=%p", g_uiManager, g_self);
		if (!g_uiManager)
		{
			LOG_ERROR("OnWorldBeginPlay: g_uiManager is null, cannot enable");
			return;
		}
		g_uiManager->Enable(g_self);
		g_isActive = true;
		LOG_DEBUG("OnWorldBeginPlay: WAILA active");
	}
}

static void OnWorldEndPlay(SDK::UWorld* world, const char* worldName)
{
	if (g_isActive)
	{
		LOG_INFO("World ending — deactivating WAILA");
		g_uiManager->Disable();
		g_isActive = false;
		LOG_DEBUG("OnWorldEndPlay: WAILA deactivated");
	}
}

// Plugin metadata
#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

static PluginInfo s_pluginInfo = {
	"Waila",
	MODLOADER_BUILD_TAG,
	"AlienX",
	"Minecraft style What Am I Looking At?",
	PLUGIN_INTERFACE_VERSION
};

extern "C" {

	__declspec(dllexport) PluginInfo* GetPluginInfo()
	{
		return &s_pluginInfo;
	}

	__declspec(dllexport) bool PluginInit(IPluginSelf* self)
	{
		try
		{
			g_self = self;

			LOG_INFO("Plugin initializing...");
			LOG_DEBUG("PluginInit: self = %p", self);
			LOG_DEBUG("PluginInit: hooks = %p", self ? self->hooks : nullptr);
			LOG_DEBUG("PluginInit: logger = %p", self ? self->logger : nullptr);
			LOG_DEBUG("PluginInit: config = %p", self ? self->config : nullptr);

			// Initialize config system
			LOG_DEBUG("PluginInit: initializing config schema");
			WailaPluginConfig::Config::Initialize(self);
			LOG_DEBUG("PluginInit: config schema initialized");

			// Check if plugin is enabled via config
			LOG_DEBUG("PluginInit: reading enabled flag");
			bool enabled = WailaPluginConfig::Config::IsEnabled();
			LOG_DEBUG("PluginInit: enabled = %s", enabled ? "true" : "false");
			if (!enabled)
			{
				LOG_WARN("Plugin is disabled in config file");
				return true;
			}

			// Log config values
			float maxDist = WailaPluginConfig::Config::GetMaxDistance();
			float opacity = WailaPluginConfig::Config::GetWindowOpacity();
			LOG_INFO("WAILA Configuration:");
			LOG_INFO("  MaxDistance: %.1f units", maxDist);
			LOG_INFO("  WindowOpacity: %.2f", opacity);

			// Create UI manager
			LOG_DEBUG("PluginInit: allocating WailaUIManager");
			g_uiManager = new Waila::UI::WailaUIManager();
			LOG_DEBUG("PluginInit: WailaUIManager allocated at %p", g_uiManager);

			LOG_DEBUG("PluginInit: calling WailaUIManager::Initialize");
			g_uiManager->Initialize(self);
			LOG_DEBUG("PluginInit: WailaUIManager::Initialize returned");

			// Register world callbacks
			LOG_DEBUG("PluginInit: checking world hooks (hooks=%p)", self->hooks);
			if (self->hooks)
			{
				LOG_DEBUG("PluginInit: hooks->World = %p", self->hooks->World);
				if (self->hooks->World)
				{
					LOG_DEBUG("PluginInit: registering OnAnyWorldBeginPlay");
					self->hooks->World->RegisterOnAnyWorldBeginPlay(&OnWorldBeginPlay);
					LOG_DEBUG("PluginInit: registering OnAfterWorldEndPlay");
					self->hooks->World->RegisterOnAfterWorldEndPlay(&OnWorldEndPlay);
					LOG_DEBUG("PluginInit: world callbacks registered");

					// Hot-reload: if ChimeraMain is already active, activate now
					SDK::UWorld* currentWorld = SDK::UWorld::GetWorld();
					if (currentWorld)
					{
						std::string worldName = currentWorld->GetName();
						LOG_DEBUG("PluginInit: current world = '%s'", worldName.c_str());
						if (worldName == "ChimeraMain")
						{
							LOG_INFO("PluginInit: ChimeraMain already active — triggering OnWorldBeginPlay for hot-reload");
							OnWorldBeginPlay(currentWorld, worldName.c_str());
						}
					}
				}
				else
				{
					LOG_ERROR("PluginInit: hooks->World is null");
				}
			}
			else
			{
				LOG_ERROR("PluginInit: self->hooks is null");
			}

			LOG_INFO("Plugin initialized successfully");
			return true;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("PluginInit: caught exception: %s", e.what());
			return false;
		}
		catch (...)
		{
			LOG_ERROR("PluginInit: caught unknown exception");
			return false;
		}
	}

	__declspec(dllexport) void PluginShutdown()
	{
		LOG_INFO("Plugin shutting down...");

		// Unregister world callbacks before teardown
		if (g_self && g_self->hooks && g_self->hooks->World)
		{
			g_self->hooks->World->UnregisterOnAnyWorldBeginPlay(&OnWorldBeginPlay);
			g_self->hooks->World->UnregisterOnAfterWorldEndPlay(&OnWorldEndPlay);
			LOG_DEBUG("PluginShutdown: world callbacks unregistered");
		}

		// Shutdown and delete UI manager (Disable is called internally if active)
		if (g_uiManager)
		{
			g_uiManager->Shutdown();
			delete g_uiManager;
			g_uiManager = nullptr;
			LOG_DEBUG("PluginShutdown: UI manager destroyed");
		}

		g_isActive = false;
		g_self = nullptr;
		LOG_DEBUG("PluginShutdown: plugin self cleared");
	}

} // extern "C"
