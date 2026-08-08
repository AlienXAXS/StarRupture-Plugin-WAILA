#pragma once

#include "plugin_interface.h"
#include "ui_theme.h"

#include <cstddef>

namespace Waila::UI
{
	struct Rect { float x0, y0, x1, y1; };

	// Draw-list primitives for the WAILA card. Every one of these must be called
	// from inside a render callback — the PluginDrawList handle is only valid for
	// the frame it was fetched in.
	namespace Draw
	{
		// Card shell: rounded body, accent gradient strip along the top edge, border.
		void Panel(IModLoaderImGui* ui, PluginDrawList dl, const Rect& r,
		           const Palette& pal, float rounding, float stripHeight);

		// Circular gauge starting at 12 o'clock and sweeping clockwise.
		void Ring(IModLoaderImGui* ui, PluginDrawList dl, float cx, float cy, float radius,
		          float thickness, float fraction, const Color& track, const Color& fill);

		// Square icon slot. Draws `tex` when it is ready and a tinted placeholder
		// when it is not, so the layout never jumps while textures stream in.
		void IconTile(IModLoaderImGui* ui, PluginDrawList dl, const Rect& r,
		              PluginTextureHandle tex, const Palette& pal, float rounding,
		              float alpha, bool drawSlot);

		// Bare image with no slot behind it — for the building icon inside the ring.
		void IconBare(IModLoaderImGui* ui, PluginDrawList dl, const Rect& r,
		              PluginTextureHandle tex, const Palette& pal, float alpha);

		// Quantity tucked into an icon tile's bottom-right corner, the way an
		// inventory stack carries its count. Drawn at full strength even over a
		// dimmed tile, so a missing ingredient still says how many it wants.
		void CountBadge(IModLoaderImGui* ui, PluginDrawList dl, const Rect& tile,
		                const char* text, const Color& fg, float fontSize);

		// Font size a count badge should use inside a tile of the given edge length.
		float CountBadgeFont(float tileSize);

		// Text helpers that position against a point rather than the layout cursor.
		void TextAt(IModLoaderImGui* ui, PluginDrawList dl, float x, float y,
		            const char* text, const Color& c, float fontSize);
		void TextCentered(IModLoaderImGui* ui, PluginDrawList dl, float cx, float y,
		                  const char* text, const Color& c, float fontSize);
		void TextRight(IModLoaderImGui* ui, PluginDrawList dl, float rightX, float y,
		               const char* text, const Color& c, float fontSize);

		// Draws `text` truncated with an ellipsis so it never spills past maxWidth.
		void TextFitted(IModLoaderImGui* ui, PluginDrawList dl, float x, float y, float maxWidth,
		                const char* text, const Color& c, float fontSize);

		// Width `text` would occupy when drawn at fontSize.
		float TextWidth(IModLoaderImGui* ui, const char* text, float fontSize);

		// Small status dot with an optional soft halo.
		void StatusDot(IModLoaderImGui* ui, PluginDrawList dl, float cx, float cy,
		               float radius, const Color& c, float glow);

		// Uppercase chip. Returns its width so several can be laid out right-to-left.
		float Badge(IModLoaderImGui* ui, PluginDrawList dl, float x, float y,
		            const char* text, const Color& bg, const Color& fg, float fontSize);
		float BadgeWidth(IModLoaderImGui* ui, const char* text, float fontSize);

		// Right-pointing chevron used between the flow columns.
		void Arrow(IModLoaderImGui* ui, PluginDrawList dl, float cx, float cy,
		           float size, const Color& c, float thickness);

		// "12.5" / "12" — trailing .0 dropped, because a rate of 12.0 reads as noise.
		void FormatNumber(char* buf, size_t bufSize, float value);

		// "1.8s" / "45s" / "2:30" for anything a player waits through.
		void FormatSeconds(char* buf, size_t bufSize, float seconds);
	}
}
