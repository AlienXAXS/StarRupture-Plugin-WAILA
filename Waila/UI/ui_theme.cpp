#include "ui_theme.h"

#include <cmath>

namespace Waila::UI
{
	namespace
	{
		// One accent per building family. Cool blue is the "neutral" production
		// colour; the others pull toward what the building physically does.
		Color AccentFor(Accent a)
		{
			switch (a)
			{
			case Accent::Power:   return Color(0.96f, 0.79f, 0.23f);  // grid yellow
			case Accent::Cooler:  return Color(0.42f, 0.78f, 0.98f);  // ice
			case Accent::Storage: return Color(0.61f, 0.55f, 0.94f);  // violet
			case Accent::Cargo:   return Color(0.96f, 0.65f, 0.14f);  // amber
			case Accent::Crafter:
			default:              return Color(0.22f, 0.71f, 0.96f);  // signal cyan
			}
		}
	}

	uint32_t Theme::Pack(const Color& c)
	{
		return Pack(c, 1.0f);
	}

	uint32_t Theme::Pack(const Color& c, float alphaMul)
	{
		auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
		auto to8     = [&](float v) { return static_cast<uint32_t>(clamp01(v) * 255.0f + 0.5f); };
		return (to8(c.a * alphaMul) << 24) | (to8(c.b) << 16) | (to8(c.g) << 8) | to8(c.r);
	}

	Color Theme::Mix(const Color& a, const Color& b, float t)
	{
		const float k = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		return Color(a.r + (b.r - a.r) * k,
		             a.g + (b.g - a.g) * k,
		             a.b + (b.b - a.b) * k,
		             a.a + (b.a - a.a) * k);
	}

	float Theme::Pulse(double time, float periodSeconds)
	{
		if (periodSeconds <= 0.0f) return 0.0f;
		const float phase = static_cast<float>(std::fmod(time, static_cast<double>(periodSeconds))) / periodSeconds;
		return phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
	}

	Palette Theme::Build(Accent accent, float opacity)
	{
		Palette p;
		p.accent = AccentFor(accent);

		// A tint of the accent rather than the accent itself — at full strength the
		// band reads as a stray element instead of the card's top edge.
		const Color ground(0.05f, 0.06f, 0.085f);
		p.accentBand = Mix(ground, p.accent, 0.62f).WithAlpha(opacity);

		p.accentDim   = p.accent.Scaled(0.32f);
		p.background  = Color(0.05f, 0.06f, 0.085f, opacity);
		p.border      = Mix(Color(1.0f, 1.0f, 1.0f), p.accent, 0.6f).WithAlpha(0.22f * opacity + 0.06f);

		p.textPrimary = Color(0.93f, 0.95f, 0.97f);
		p.textMuted   = Color(0.65f, 0.69f, 0.76f);
		p.textFaint   = Color(0.43f, 0.47f, 0.54f);

		p.good        = Color(0.31f, 0.82f, 0.48f);
		p.warn        = Color(0.96f, 0.65f, 0.14f);
		p.bad         = Color(0.95f, 0.33f, 0.30f);

		return p;
	}

	Color Theme::Of(const Palette& pal, Health h)
	{
		switch (h)
		{
		case Health::Good: return pal.good;
		case Health::Warn: return pal.warn;
		case Health::Bad:  return pal.bad;
		case Health::Idle:
		default:           return pal.textFaint;
		}
	}
}
