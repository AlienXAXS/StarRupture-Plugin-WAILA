#pragma once

#include <cstdint>

namespace Waila::UI
{
	struct Color
	{
		float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

		Color() = default;
		Color(float rr, float gg, float bb, float aa = 1.0f) : r(rr), g(gg), b(bb), a(aa) {}

		Color WithAlpha(float alpha) const { return Color(r, g, b, alpha); }
		Color Scaled(float k)        const { return Color(r * k, g * k, b * k, a); }
	};

	// Which family of building the card is describing. Drives the accent so the
	// card's colour alone says "this is a power thing" before any text is read.
	enum class Accent
	{
		Crafter,
		Power,
		Cooler,
		Storage,
		Cargo
	};

	// Health of whatever the card is showing, kept separate from the accent —
	// semantic colour must not move when the building type changes.
	enum class Health
	{
		Idle,
		Good,
		Warn,
		Bad
	};

	struct Palette
	{
		Color accentBand;    // the band along the card's top edge
		Color accent;        // ring fill, key figures
		Color accentDim;     // ring track, placeholders
		Color background;
		Color border;
		Color textPrimary;
		Color textMuted;
		Color textFaint;
		Color good;
		Color warn;
		Color bad;
	};

	namespace Theme
	{
		Palette Build(Accent accent, float opacity);

		// Colour for a health state, resolved against the palette.
		Color   Of(const Palette& pal, Health h);

		// Pack for the ImDrawList API (0xAABBGGRR).
		uint32_t Pack(const Color& c);
		uint32_t Pack(const Color& c, float alphaMul);

		Color Mix(const Color& a, const Color& b, float t);

		// 0..1 triangle wave, for the pulse on a live status dot.
		float Pulse(double time, float periodSeconds);
	}
}
