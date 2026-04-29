#pragma once

#include "plugin_interface.h"

namespace WailaPluginConfig
{
	// Define config schema with a few simple examples
	static const ConfigEntry CONFIG_ENTRIES[] = {
		{
			"General",
			"Enabled",
			ConfigValueType::Boolean,
			"true",
			"Enable or disable the WAILA plugin"
		},
		{
			"WAILA",
			"Max Distance",
			ConfigValueType::Float,
			"650.0",
			"Maximum distance for WAILA raycast (units)",
			0.0f,
			2500.0f
		},
		{
			"WAILA",
			"Render Building Descriptions",
			ConfigValueType::Boolean,
			"true",
			"Whether to render building descriptions in the WAILA widget"
		}
	};

	static const ConfigSchema SCHEMA = {
		CONFIG_ENTRIES,
		sizeof(CONFIG_ENTRIES) / sizeof(ConfigEntry)
	};

	// Type-safe config accessor class
	class Config
	{
	public:
		static void Initialize(IPluginSelf* self)
		{
			s_self = self;

			// Initialize config from schema - creates file with defaults if missing
			if (s_self)
			{
				s_self->config->InitializeFromSchema(s_self, &SCHEMA);
			}
		}

		// Config accessors
		static bool IsEnabled()
		{
			return s_self ? s_self->config->ReadBool(s_self, "General", "Enabled", true) : true;
		}

		static float GetMaxDistance()
		{
			return s_self ? s_self->config->ReadFloat(s_self, "WAILA", "Max Distance", 650.0f) : 650.0f;
		}

		static bool ShouldRenderDescriptions()
		{
			return s_self ? s_self->config->ReadBool(s_self, "WAILA", "Render Building Descriptions", true) : true;
		}

	private:
		static IPluginSelf* s_self;
	};
}
