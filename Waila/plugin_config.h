#pragma once

#include "plugin_interface.h"
#include <string>

namespace WailaPluginConfig
{
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
			"Action Distance",
			ConfigValueType::Float,
			"3000.0",
			"Maximum distance for copy/paste/lock raycasts (units)",
			650.0f,
			10000.0f
		},
		{
			"WAILA",
			"Show Action Toast",
			ConfigValueType::Boolean,
			"true",
			"Show a toast notification when copying, pasting, or locking a recipe"
		},
		{
			"WAILA",
			"Render Building Descriptions",
			ConfigValueType::Boolean,
			"true",
			"Whether to render building descriptions in the WAILA widget"
		},
		{
			"WAILA",
			"Scale",
			ConfigValueType::Float,
			"1.0",
			"Size of the WAILA card. 1.0 is the design size; raise it for 4K displays",
			0.5f,
			3.0f
		},
		{
			"WAILA",
			"Opacity",
			ConfigValueType::Float,
			"0.92",
			"Opacity of the WAILA card background",
			0.30f,
			1.0f
		},
		{
			"Hotkeys",
			"Copy Recipe Key",
			ConfigValueType::Keybind,
			"C",
			"Key to copy recipe from the looked-at building"
		},
		{
			"Hotkeys",
			"Paste Recipe Key",
			ConfigValueType::Keybind,
			"V",
			"Key to paste clipboard recipe onto the looked-at building"
		},
		{
			"Hotkeys",
			"Lock Recipe Key",
			ConfigValueType::Keybind,
			"L",
			"Key to toggle recipe lock mode (auto-paste on new placements)"
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

		static float GetActionDistance()
		{
			return s_self ? s_self->config->ReadFloat(s_self, "WAILA", "Action Distance", 3000.0f) : 3000.0f;
		}

		static bool GetShowActionToast()
		{
			return s_self ? s_self->config->ReadBool(s_self, "WAILA", "Show Action Toast", true) : true;
		}

		static bool ShouldRenderDescriptions()
		{
			return s_self ? s_self->config->ReadBool(s_self, "WAILA", "Render Building Descriptions", true) : true;
		}

		static float GetScale()
		{
			return s_self ? s_self->config->ReadFloat(s_self, "WAILA", "Scale", 1.0f) : 1.0f;
		}

		static float GetOpacity()
		{
			return s_self ? s_self->config->ReadFloat(s_self, "WAILA", "Opacity", 0.92f) : 0.92f;
		}

		static std::string GetCopyKey()
		{
			char buf[32] = "C";
			if (s_self) s_self->config->ReadString(s_self, "Hotkeys", "Copy Recipe Key", buf, sizeof(buf), "C");
			return buf;
		}

		static std::string GetPasteKey()
		{
			char buf[32] = "V";
			if (s_self) s_self->config->ReadString(s_self, "Hotkeys", "Paste Recipe Key", buf, sizeof(buf), "V");
			return buf;
		}

		static std::string GetLockKey()
		{
			char buf[32] = "L";
			if (s_self) s_self->config->ReadString(s_self, "Hotkeys", "Lock Recipe Key", buf, sizeof(buf), "L");
			return buf;
		}

	private:
		static IPluginSelf* s_self;
	};
}
