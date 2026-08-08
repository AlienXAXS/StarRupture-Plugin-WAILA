#include "ui_draw.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Waila::UI
{
	namespace
	{
		constexpr float kPi = 3.14159265358979323846f;

		float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

		// CalcTextSize measures at the atlas font size; everything here is drawn
		// with DL_AddTextSized, so widths have to be scaled by the same ratio.
		float SizeRatio(IModLoaderImGui* ui, float fontSize)
		{
			const float base = ui->GetFontSize();
			return (fontSize > 0.0f && base > 0.0f) ? fontSize / base : 1.0f;
		}
	}

	void Draw::Panel(IModLoaderImGui* ui, PluginDrawList dl, const Rect& r,
	                 const Palette& pal, float rounding, float stripHeight)
	{
		ui->DL_AddRectFilled(dl, r.x0, r.y0, r.x1, r.y1, Theme::Pack(pal.background), rounding,
			PluginDrawFlags_RoundCornersAll);

		// The band has to follow the card's corner arc exactly. A clip rect can't do
		// that — it's rectangular, so a band drawn to the strip height keeps its own
		// square corners and they survive as accent-coloured nubs outside the card's
		// rounded top. Drawing a rect tall enough for ImGui to honour the full radius
		// (it clamps rounding to half the height) and clipping it back to the strip
		// leaves the arc intact.
		if (stripHeight > 0.0f)
		{
			const float bandH = stripHeight > rounding * 2.0f ? stripHeight : rounding * 2.0f;

			ui->DL_PushClipRect(dl, r.x0, r.y0, r.x1, r.y0 + stripHeight, true);
			ui->DL_AddRectFilled(dl, r.x0, r.y0, r.x1, r.y0 + bandH,
				Theme::Pack(pal.accentBand), rounding, PluginDrawFlags_RoundCornersTop);
			ui->DL_PopClipRect(dl);
		}

		ui->DL_AddRect(dl, r.x0, r.y0, r.x1, r.y1, Theme::Pack(pal.border), rounding,
			PluginDrawFlags_RoundCornersAll, 1.0f);
	}

	void Draw::Ring(IModLoaderImGui* ui, PluginDrawList dl, float cx, float cy, float radius,
	                float thickness, float fraction, const Color& track, const Color& fill)
	{
		// ImGui angles run from +X counter-clockwise, so 12 o'clock is -pi/2.
		constexpr float kStart = -kPi * 0.5f;

		ui->DL_PathClear(dl);
		ui->DL_PathArcTo(dl, cx, cy, radius, kStart, kStart + kPi * 2.0f, 96);
		ui->DL_PathStroke(dl, Theme::Pack(track), PluginDrawFlags_None, thickness);

		const float f = Clamp01(fraction);
		if (f <= 0.0f) return;

		ui->DL_PathClear(dl);
		ui->DL_PathArcTo(dl, cx, cy, radius, kStart, kStart + kPi * 2.0f * f,
			static_cast<int>(16.0f + 80.0f * f));
		ui->DL_PathStroke(dl, Theme::Pack(fill), PluginDrawFlags_None, thickness);

		// A dot on the leading edge makes the sweep direction obvious at a glance.
		const float a = kStart + kPi * 2.0f * f;
		ui->DL_AddCircleFilled(dl, cx + std::cos(a) * radius, cy + std::sin(a) * radius,
			thickness * 0.62f, Theme::Pack(fill), 12);
	}

	void Draw::IconTile(IModLoaderImGui* ui, PluginDrawList dl, const Rect& r,
	                    PluginTextureHandle tex, const Palette& pal, float rounding,
	                    float alpha, bool drawSlot)
	{
		if (drawSlot)
		{
			ui->DL_AddRectFilled(dl, r.x0, r.y0, r.x1, r.y1,
				Theme::Pack(Color(1.0f, 1.0f, 1.0f, 0.07f * alpha)), rounding,
				PluginDrawFlags_RoundCornersAll);
			ui->DL_AddRect(dl, r.x0, r.y0, r.x1, r.y1,
				Theme::Pack(Color(1.0f, 1.0f, 1.0f, 0.09f * alpha)), rounding,
				PluginDrawFlags_RoundCornersAll, 1.0f);
		}

		IconBare(ui, dl, r, tex, pal, alpha);
	}

	void Draw::IconBare(IModLoaderImGui* ui, PluginDrawList dl, const Rect& r,
	                    PluginTextureHandle tex, const Palette& pal, float alpha)
	{
		if (tex)
		{
			ui->DL_AddImage(dl, tex, r.x0, r.y0, r.x1, r.y1, 0.0f, 0.0f, 1.0f, 1.0f,
				Theme::Pack(Color(1.0f, 1.0f, 1.0f, alpha)));
			return;
		}

		// No texture yet (or the asset has none) — a dimmed accent lozenge keeps the
		// slot occupied so nothing reflows once the real icon arrives.
		const float inset = (r.x1 - r.x0) * 0.22f;
		ui->DL_AddRectFilled(dl, r.x0 + inset, r.y0 + inset, r.x1 - inset, r.y1 - inset,
			Theme::Pack(pal.accentDim, 0.75f * alpha), (r.x1 - r.x0) * 0.14f,
			PluginDrawFlags_RoundCornersAll);
	}

	float Draw::CountBadgeFont(float tileSize)
	{
		const float f = tileSize * 0.30f;
		return f < 10.0f ? 10.0f : (f > 14.0f ? 14.0f : f);
	}

	void Draw::CountBadge(IModLoaderImGui* ui, PluginDrawList dl, const Rect& tile,
	                      const char* text, const Color& fg, float fontSize)
	{
		if (!text || !text[0]) return;

		const float padX = fontSize * 0.36f;
		const float padY = fontSize * 0.14f;
		const float w    = TextWidth(ui, text, fontSize);

		// Overhangs the tile by a pixel so it reads as sitting on top of the icon
		// rather than inside a margin the icon left for it.
		const float x1 = tile.x1 + 1.0f;
		const float y1 = tile.y1 + 1.0f;
		const float x0 = x1 - w - padX * 2.0f;
		const float y0 = y1 - fontSize - padY * 2.0f;

		ui->DL_AddRectFilled(dl, x0, y0, x1, y1,
			Theme::Pack(Color(0.04f, 0.05f, 0.07f, 0.90f)), fontSize * 0.34f,
			PluginDrawFlags_RoundCornersAll);

		TextAt(ui, dl, x0 + padX, y0 + padY, text, fg, fontSize);
	}

	void Draw::TextAt(IModLoaderImGui* ui, PluginDrawList dl, float x, float y,
	                  const char* text, const Color& c, float fontSize)
	{
		if (!text || !text[0]) return;
		const float base = ui->GetFontSize();
		ui->DL_AddTextSized(dl, fontSize > 0.0f ? fontSize : base, x, y, Theme::Pack(c), text);
	}

	float Draw::TextWidth(IModLoaderImGui* ui, const char* text, float fontSize)
	{
		if (!text || !text[0]) return 0.0f;
		float tw = 0.0f, th = 0.0f;
		ui->CalcTextSize(text, &tw, &th, false, 0.0f);
		return tw * SizeRatio(ui, fontSize);
	}

	void Draw::TextCentered(IModLoaderImGui* ui, PluginDrawList dl, float cx, float y,
	                        const char* text, const Color& c, float fontSize)
	{
		if (!text || !text[0]) return;
		TextAt(ui, dl, cx - TextWidth(ui, text, fontSize) * 0.5f, y, text, c, fontSize);
	}

	void Draw::TextRight(IModLoaderImGui* ui, PluginDrawList dl, float rightX, float y,
	                     const char* text, const Color& c, float fontSize)
	{
		if (!text || !text[0]) return;
		TextAt(ui, dl, rightX - TextWidth(ui, text, fontSize), y, text, c, fontSize);
	}

	void Draw::TextFitted(IModLoaderImGui* ui, PluginDrawList dl, float x, float y, float maxWidth,
	                      const char* text, const Color& c, float fontSize)
	{
		if (!text || !text[0] || maxWidth <= 0.0f) return;

		if (TextWidth(ui, text, fontSize) <= maxWidth)
		{
			TextAt(ui, dl, x, y, text, c, fontSize);
			return;
		}

		// Shrink one byte at a time from the end. Building names are short enough
		// that a linear walk costs nothing, and it keeps multi-byte UTF-8 safe by
		// never cutting inside a continuation sequence.
		char buf[192];
		const size_t len = strlen(text);
		size_t take = len < sizeof(buf) - 4 ? len : sizeof(buf) - 4;

		while (take > 0)
		{
			// Don't split a UTF-8 sequence.
			while (take > 0 && (static_cast<unsigned char>(text[take]) & 0xC0) == 0x80)
				--take;

			memcpy(buf, text, take);
			buf[take]     = '.';
			buf[take + 1] = '.';
			buf[take + 2] = '.';
			buf[take + 3] = '\0';

			if (TextWidth(ui, buf, fontSize) <= maxWidth)
			{
				TextAt(ui, dl, x, y, buf, c, fontSize);
				return;
			}
			--take;
		}
	}

	void Draw::StatusDot(IModLoaderImGui* ui, PluginDrawList dl, float cx, float cy,
	                     float radius, const Color& c, float glow)
	{
		if (glow > 0.0f)
			ui->DL_AddCircleFilled(dl, cx, cy, radius * (1.9f + glow * 0.9f),
				Theme::Pack(c, 0.16f * glow + 0.05f), 16);
		ui->DL_AddCircleFilled(dl, cx, cy, radius, Theme::Pack(c), 16);
	}

	namespace
	{
		// Shared geometry so BadgeWidth and Badge can never drift apart.
		void BadgeMetrics(IModLoaderImGui* ui, const char* text, float fontSize,
		                  float& w, float& h, float& padX, float& padY)
		{
			float tw = 0.0f, th = 0.0f;
			ui->CalcTextSize(text, &tw, &th, false, 0.0f);

			const float k = SizeRatio(ui, fontSize);
			w    = tw * k;
			h    = th * k;
			padX = h * 0.45f;
			padY = h * 0.16f;
		}
	}

	float Draw::BadgeWidth(IModLoaderImGui* ui, const char* text, float fontSize)
	{
		if (!text || !text[0]) return 0.0f;
		float w, h, padX, padY;
		BadgeMetrics(ui, text, fontSize, w, h, padX, padY);
		return w + padX * 2.0f;
	}

	float Draw::Badge(IModLoaderImGui* ui, PluginDrawList dl, float x, float y,
	                  const char* text, const Color& bg, const Color& fg, float fontSize)
	{
		if (!text || !text[0]) return 0.0f;

		float w, h, padX, padY;
		BadgeMetrics(ui, text, fontSize, w, h, padX, padY);

		const float base = ui->GetFontSize();
		const float x1   = x + w + padX * 2.0f;
		const float y1   = y + h + padY * 2.0f;

		ui->DL_AddRectFilled(dl, x, y, x1, y1, Theme::Pack(bg), (y1 - y) * 0.5f,
			PluginDrawFlags_RoundCornersAll);
		ui->DL_AddTextSized(dl, fontSize > 0.0f ? fontSize : base, x + padX, y + padY,
			Theme::Pack(fg), text);

		return x1 - x;
	}

	void Draw::Arrow(IModLoaderImGui* ui, PluginDrawList dl, float cx, float cy,
	                 float size, const Color& c, float thickness)
	{
		const float half = size * 0.5f;
		const uint32_t col = Theme::Pack(c);

		ui->DL_AddLine(dl, cx - half, cy, cx + half, cy, col, thickness);
		ui->DL_AddLine(dl, cx + half - size * 0.34f, cy - size * 0.30f, cx + half, cy, col, thickness);
		ui->DL_AddLine(dl, cx + half - size * 0.34f, cy + size * 0.30f, cx + half, cy, col, thickness);
	}

	void Draw::FormatNumber(char* buf, size_t bufSize, float value)
	{
		if (!buf || bufSize == 0) return;

		if (!std::isfinite(value))
		{
			snprintf(buf, bufSize, "--");
			return;
		}

		if (std::fabs(value - std::floor(value + 0.5f)) < 0.05f)
			snprintf(buf, bufSize, "%d", static_cast<int>(std::floor(value + 0.5f)));
		else
			snprintf(buf, bufSize, "%.1f", value);
	}

	void Draw::FormatSeconds(char* buf, size_t bufSize, float seconds)
	{
		if (!buf || bufSize == 0) return;

		if (!std::isfinite(seconds) || seconds < 0.0f)
		{
			snprintf(buf, bufSize, "--");
			return;
		}

		if (seconds >= 60.0f)
		{
			const int total = static_cast<int>(seconds + 0.5f);
			snprintf(buf, bufSize, "%d:%02d", total / 60, total % 60);
		}
		else if (seconds >= 10.0f)
		{
			snprintf(buf, bufSize, "%ds", static_cast<int>(seconds + 0.5f));
		}
		else
		{
			snprintf(buf, bufSize, "%.1fs", seconds);
		}
	}
}
