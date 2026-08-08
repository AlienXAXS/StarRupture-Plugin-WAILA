#pragma once

#include "plugin_interface.h"
#include "card_model.h"
#include "ui_draw.h"

namespace Waila::UI
{
	namespace Card
	{
		// Size the card wants at this scale. Runs on the game thread with no ImGui
		// context, so it depends only on the model's counts — never on measured
		// text. Draw() lays out to exactly these numbers.
		void Measure(const CardModel& model, float scale, float& outWidth, float& outHeight);

		// Paints the card into `rect`. Render thread; `dl` must be this frame's list.
		void Draw(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& model,
		          const Palette& pal, float scale, const Rect& rect);
	}
}
