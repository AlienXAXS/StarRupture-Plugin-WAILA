#include "waila_card.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "plugin_images.h"

namespace Waila::UI
{
	namespace
	{
		// ── Geometry, in points at scale 1.0 ─────────────────────────────────
		// Measure() and Draw() both read these, which is the only reason the
		// window ends up exactly the size of what gets painted into it.

		constexpr float kRounding   = 10.0f;
		constexpr float kStrip      = 3.0f;
		constexpr float kPadX       = 14.0f;
		constexpr float kPadY       = 11.0f;

		constexpr float kHeaderH    = 31.0f;
		constexpr float kFooterH    = 20.0f;

		constexpr float kRingD      = 68.0f;
		constexpr float kRingThick  = 5.0f;
		constexpr float kTileOut    = kRingD;   // the product reads as big as the machine
		constexpr float kArrowW     = 22.0f;
		constexpr float kGap        = 10.0f;
		constexpr float kRowGap     = 7.0f;

		// Inputs sit in a two-column grid whose reserved width never changes, so the
		// ring and the product stay put however many ingredients a recipe takes.
		// Tiles grow to fill the ring's height when there are few rows, which keeps
		// a one-ingredient recipe from rendering as a lone stamp beside it.
		constexpr float kInGridW    = 112.0f;
		constexpr float kTileInMin  = 30.0f;
		constexpr float kTileInMax  = 52.0f;

		constexpr float kStatRingD  = 64.0f;
		constexpr float kStatRowH   = 36.0f;
		constexpr float kStatGutter = 18.0f;
		constexpr float kStatSpace  = 16.0f;   // between ring, stats and stock

		// What a container holds, on the same footing as a recipe's ingredients: a
		// grid two rows deep that runs to the right. One kind of item gets a tile as
		// big as the ring beside it.
		constexpr size_t kStockRows    = 2;
		constexpr float  kStockGap     = 7.0f;
		constexpr float  kStockTileMin = 32.0f;
		constexpr float  kStockTileMax = kStatRingD;

		// Rate body: a line of text each, the item's lines under its icon and name.
		constexpr float kRateRowH   = 32.0f;
		constexpr float kRateIcon   = PluginImages::kMinSize;
		constexpr float kRingIcon   = 0.62f;   // the building icon's share of its ring
		constexpr float kRateGap    = 10.0f;
		constexpr float kRateBullet = 2.0f;    // radius of the bullet in front of a detail line
		constexpr float kRateContH  = 17.0f;   // a wrapped line's continuation
		constexpr float kRateIndent = 12.0f;   // and how far it is indented
		constexpr int   kRateMaxLines = 3;
		constexpr float kRateChar   = 0.62f;   // width of a character, in ems, as the title is estimated

		constexpr float kDescLineH  = 17.0f;
		constexpr int   kDescMaxLines = 3;

		// The card is only ever as wide as what it has to say. The bounds stop a
		// bare two-stat building from looking stunted and a long building name from
		// running off the side of the screen.
		constexpr float kCardMinW   = 240.0f;
		constexpr float kCardMaxW   = 620.0f;

		// Font sizes. The card is read at a glance from across a factory floor, so
		// these run larger than a desktop panel's would.
		constexpr float kFontTitle  = 18.0f;
		constexpr float kFontStatus = 13.0f;
		constexpr float kFontChip   = 12.0f;
		constexpr float kFontLabel  = 14.0f;
		constexpr float kFontRate   = 26.0f;
		constexpr float kFontUnit   = 13.0f;
		constexpr float kFontTiny   = 12.0f;
		constexpr float kFontStat   = 19.0f;

		// What an icon box measures at this scale and overlay font. Px() is the
		// icon's size in whole screen pixels: the box at scale s, grown with the
		// font and never under the library's minimum. Box() is the box in card
		// points, which grows to fit the icon when the icon outgrows it. Measure()
		// and Draw() build the same one, so they agree on every size.
		struct IconScale
		{
			float s;
			float fontSize;

			int Px(float box) const
			{
				return static_cast<int>(PluginImages::SizeForFont(box * s, fontSize, PluginImages::kDefaultFontSize));
			}

			float Box(float box) const
			{
				const float need = static_cast<float>(Px(box)) / s;
				return need > box + 0.5f / s ? need : box;
			}

			// A ring that holds the building icon: as it was, or larger when the icon needs it.
			float RingD(float d) const
			{
				const float box = Box(d * kRingIcon);
				return box > d * kRingIcon ? box / kRingIcon : d;
			}
		};

		float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

		float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

		constexpr size_t kInCols = 2;

		// The line above an input tile (what it needs a minute) and the one under
		// it (what it is short by).
		constexpr float kInRateH  = 14.0f;
		constexpr float kInShortH = 14.0f;

		// Measure() runs on the game thread, where there is no ImGui context and so
		// no way to ask what a string is worth in pixels. Widths that decide the
		// card's size are estimated from character counts instead, and Draw() clips
		// its text to the same number — an overestimate costs a little air, an
		// underestimate an ellipsis, and neither can desync the card from its window.
		// 0.55em per character suits the digits and short lowercase words that make
		// up most of the card. The title runs wider on purpose: it is the one string
		// a player will notice being cut short.
		float EstimateW(const std::string& s, float font, float perChar = 0.55f)
		{
			return static_cast<float>(s.size()) * font * perChar;
		}

		bool AnyInputRate(const CardModel& m)
		{
			for (const auto& in : m.inputs)
				if (!in.rate.empty())
					return true;
			return false;
		}

		bool AnyInputShort(const CardModel& m)
		{
			for (const auto& in : m.inputs)
				if (!in.shortage.empty())
					return true;
			return false;
		}

		// A tile's cell is as wide as the wider of the tile and its figures, so
		// neighbouring figures can't run into each other.
		float InputCellWidth(const CardModel& m, float tile)
		{
			float w = tile;
			for (const auto& in : m.inputs)
			{
				w = (std::max)(w, EstimateW(in.rate, kFontTiny));
				w = (std::max)(w, EstimateW(in.shortage, kFontTiny));
			}
			return w;
		}

		size_t InputRows(const CardModel& m)
		{
			return m.inputs.empty() ? 1 : (m.inputs.size() + kInCols - 1) / kInCols;
		}

		// The input column sizes itself to the row count: one ingredient gets a
		// tile as tall as the ring beside it, three share that height and shrink.
		// Measure() and Draw() both go through here so they can never disagree.
		struct FlowMetrics
		{
			float tile;     // input tile edge
			int   tilePx;   // the icon in it, in screen pixels
			float ringD;    // the building's ring
			int   ringPx;   // the building icon inside it
			float outD;     // the product's tile
			int   outPx;    // the icon in it
			float cellW;    // width of one input cell: the tile, or its figures if wider
			float rateH;    // height of the figure above each tile
			float gridW;    // width the cells actually occupy
			float slotW;    // width the grid reserves: gridW, but never under kInGridW
			float shortH;   // height of the shortage line under each tile
			float stackH;   // total height of the input grid
			float rowH;     // height of the flow row, ring floor included
			float bodyH;    // rowH plus breathing room
			float tailW;    // product name / rate column
		};

		FlowMetrics MeasureFlow(const CardModel& m, const IconScale& ic)
		{
			const float rows = static_cast<float>(InputRows(m));
			const size_t used = m.inputs.empty() ? 1 : m.inputs.size();
			const float  cols = static_cast<float>((std::min)(kInCols, used));

			FlowMetrics f{};

			// Whichever runs out first: the width of two columns, or the ring's
			// height shared between the rows.
			const float byWidth  = (kInGridW - (static_cast<float>(kInCols) - 1.0f) * kRowGap)
			                     / static_cast<float>(kInCols);
			const float rateH    = AnyInputRate(m)  ? kInRateH  : 0.0f;
			const float shortH   = AnyInputShort(m) ? kInShortH : 0.0f;
			const float byHeight = (kRingD - (rows - 1.0f) * kRowGap) / rows - rateH - shortH;

			const float tile = Clamp((std::min)(byWidth, byHeight), kTileInMin, kTileInMax);
			f.tile   = ic.Box(tile);
			f.tilePx = ic.Px(tile);
			f.ringD  = ic.RingD(kRingD);
			f.ringPx = ic.Px(kRingD * kRingIcon);
			f.outD   = ic.Box(kTileOut);
			f.outPx  = ic.Px(kTileOut);
			f.rateH  = rateH;
			f.shortH = shortH;
			f.cellW  = InputCellWidth(m, f.tile);
			f.gridW  = cols * f.cellW + (cols - 1.0f) * kRowGap;
			f.slotW  = (std::max)(kInGridW, f.gridW);
			f.stackH = rows * (f.tile + rateH + shortH) + (rows - 1.0f) * kRowGap;
			f.rowH   = (std::max)(f.ringD, f.stackH) + 14.0f;
			f.bodyH  = f.rowH;

			// The tail is as wide as its widest line and no wider — a recipe called
			// "Ore" shouldn't reserve the room "Superconductor Lattice" needs.
			float tail = EstimateW(m.outputName, kFontLabel);
			tail = (std::max)(tail, EstimateW(m.rateValue, kFontRate)
			                      + EstimateW(m.rateUnit,  kFontUnit) + 3.0f);
			tail = (std::max)(tail, EstimateW(m.rateSub,   kFontTiny));
			f.tailW = tail;

			return f;
		}

		// Every term but the input grid and the tail is a constant, so the equation
		// lands on the same pixel on most machines and only the card's right edge moves.
		float FlowGroupWidth(const CardModel& m, const FlowMetrics& f)
		{
			float w = f.slotW
			        + kGap + kArrowW + kGap
			        + f.ringD
			        + kGap + kArrowW + kGap
			        + f.tailW;
			if (m.hasOutput)
				w += f.outD + kGap;
			return w;
		}

		// Stats stack two-wide normally. A container puts its stock grid in that
		// second column's place, so its one figure runs down a single column.
		size_t StatCols(const CardModel& m)
		{
			return m.stock.empty() ? 2u : 1u;
		}

		size_t StatRows(const CardModel& m)
		{
			const size_t cols = StatCols(m);
			return (m.stats.size() + cols - 1) / cols;
		}

		struct StockMetrics
		{
			size_t rows  = 0;
			size_t cols  = 0;
			float  tile  = 0.0f;
			int    tilePx = 0;
			float  ringD = 0.0f;   // the building's ring beside the grid
			int    ringPx = 0;     // the building icon inside it
			float  gridW = 0.0f;
			float  gridH = 0.0f;
		};

		StockMetrics MeasureStock(const CardModel& m, const IconScale& ic)
		{
			StockMetrics st{};
			st.ringD  = ic.RingD(kStatRingD);
			st.ringPx = ic.Px(kStatRingD * kRingIcon);
			const size_t n = m.stock.size();
			if (n == 0)
				return st;

			st.rows = (std::min)(kStockRows, n);
			st.cols = (n + st.rows - 1) / st.rows;

			const float rows = static_cast<float>(st.rows);
			const float tile = Clamp((kStatRingD - (rows - 1.0f) * kStockGap) / rows,
			                         kStockTileMin, kStockTileMax);
			st.tile   = ic.Box(tile);
			st.tilePx = ic.Px(tile);
			st.gridW = static_cast<float>(st.cols) * st.tile + (static_cast<float>(st.cols) - 1.0f) * kStockGap;
			st.gridH = rows * st.tile + (rows - 1.0f) * kStockGap;
			return st;
		}

		// The ring sets a floor: the building stays 64pt tall however little sits
		// beside it, so a storage crate and a generator are the same shape.
		float StatBodyHeight(const CardModel& m, const IconScale& ic)
		{
			const StockMetrics st = MeasureStock(m, ic);
			const float cells = static_cast<float>(StatRows(m)) * kStatRowH;
			float h = (std::max)(st.ringD, cells);
			h = (std::max)(h, st.gridH);
			return h + 6.0f;
		}

		// Width of one stat cell, the widest of its figure and its caption.
		float StatCellWidth(const CardModel& m)
		{
			float w = 0.0f;
			for (const auto& c : m.stats)
			{
				w = (std::max)(w, EstimateW(c.value, kFontStat));
				w = (std::max)(w, EstimateW(c.label, kFontTiny));
			}
			return w + kStatGutter;
		}

		// Room for the "+3 more kinds" tail, sized for two digits.
		float StockTailWidth(const CardModel& m)
		{
			return m.stockMore > 0 ? 6.0f + EstimateW("+99", kFontTiny) : 0.0f;
		}

		// Ring, then the figures, then the stock grid — all on one line, so the card
		// grows sideways as a container fills rather than downwards.
		float StatBodyWidth(const CardModel& m, const IconScale& ic)
		{
			const StockMetrics st = MeasureStock(m, ic);
			float w = st.ringD;

			if (!m.stats.empty())
				w += kStatSpace + static_cast<float>(StatCols(m)) * StatCellWidth(m);

			if (st.cols > 0)
				w += kStatSpace + st.gridW + StockTailWidth(m);

			return w;
		}

		float RateNameWidth(const CardModel& m)
		{
			if (m.rates.empty())
				return EstimateW(m.ratesNote, kFontLabel);
			float w = 0.0f;
			for (const auto& row : m.rates)
				w = (std::max)(w, EstimateW(row.name, kFontLabel, kRateChar));
			return w;
		}

		float RateBodyWidth(const CardModel& m, const IconScale& ic)
		{
			const float nameW = RateNameWidth(m);
			return m.rates.empty() ? nameW : ic.Box(kRateIcon) + kRateGap + nameW;
		}

		// A card narrower than its own title is worse than a card with air in it.
		float HeaderWidth(const CardModel& m)
		{
			float w = EstimateW(m.title, kFontTitle, 0.62f);

			if (!m.titleNote.empty())
				w += EstimateW(m.titleNote, kFontStatus) + 8.0f;

			if (!m.chip.empty())
				w += EstimateW(m.chip, kFontChip) + kFontChip * 0.9f + 8.0f;

			if (!m.statusText.empty())
				w += 23.0f + EstimateW(m.statusText, kFontStatus) + 10.0f;

			return w;
		}

		// Character-count wrapping. Deliberately not measured: Measure() runs on
		// the game thread where there is no ImGui context, and both sides have to
		// agree on the line count or the card is sized for text it doesn't draw.
		int WrapDescription(const std::string& desc, float widthPoints,
		                    std::vector<std::string>* outLines, float font = kFontTiny,
		                    float perChar = 0.5f, int maxLines = kDescMaxLines,
		                    std::vector<size_t>* outStarts = nullptr)
		{
			if (desc.empty())
				return 0;

			// ~0.5em per character is close enough for a proportional face at this size.
			const int perLine = (std::max)(12, static_cast<int>(widthPoints / (font * perChar)));

			int    lines = 0;
			size_t pos   = 0;
			while (pos < desc.size() && lines < maxLines)
			{
				size_t end = pos + static_cast<size_t>(perLine);
				if (end >= desc.size())
				{
					end = desc.size();
				}
				else
				{
					// Break on the last space inside the run, so words stay whole.
					const size_t space = desc.rfind(' ', end);
					if (space != std::string::npos && space > pos)
						end = space;
				}

				if (outLines)
					outLines->push_back(desc.substr(pos, end - pos));
				if (outStarts)
					outStarts->push_back(pos);

				++lines;
				pos = end;
				while (pos < desc.size() && desc[pos] == ' ')
					++pos;
			}

			return lines;
		}

		// The lines a rate row is drawn on: one, or for a row that wraps, as many
		// as it takes at the card's width, the continuations indented. The first
		// line has the whole text column, the rest what the indent leaves. Each
		// line remembers where it starts in the row's text, for the row's spans.
		struct RowLine
		{
			std::string text;
			size_t      start = 0;
		};

		std::vector<RowLine> RowLines(const RateRow& row, float cardW, const IconScale& ic)
		{
			if (!row.wrap)
				return { { row.name, 0 } };
			const float textW = cardW - kPadX * 2.0f - ic.Box(kRateIcon) - kRateGap;
			std::vector<std::string> texts;
			std::vector<size_t>      starts;
			WrapDescription(row.name, textW, &texts, kFontLabel, kRateChar, 1, &starts);
			if (texts.empty())
				return { { row.name, 0 } };
			size_t rest = texts[0].size();
			while (rest < row.name.size() && row.name[rest] == ' ')
				++rest;
			std::vector<size_t> restStarts;
			WrapDescription(row.name.substr(rest), textW - kRateIndent, &texts, kFontLabel, kRateChar, kRateMaxLines - 1, &restStarts);
			for (const size_t at : restStarts)
				starts.push_back(rest + at);

			std::vector<RowLine> lines;
			for (size_t i = 0; i < texts.size(); ++i)
				lines.push_back({ std::move(texts[i]), starts[i] });
			return lines;
		}

		// A rate row is as tall as it was, or as its icon if that is taller.
		float RateRowH(const IconScale& ic)
		{
			return (std::max)(kRateRowH, ic.Box(kRateIcon));
		}

		float RateBodyHeight(const CardModel& m, float cardW, const IconScale& ic)
		{
			float h = 0.0f;
			for (const auto& row : m.rates)
				h += RateRowH(ic) + static_cast<float>(RowLines(row, cardW, ic).size() - 1) * kRateContH;
			return (std::max)(h, RateRowH(ic)) + 6.0f;
		}

		bool HasFooter(const CardModel& m)
		{
			return !m.footLeft.empty() || !m.footRight.empty();
		}
	}

	// -------------------------------------------------------------------------
	// Measure
	// -------------------------------------------------------------------------

	void Card::Measure(const CardModel& model, float scale, float fontSize, float& outWidth, float& outHeight)
	{
		const IconScale ic{ scale, fontSize };
		const FlowMetrics f = MeasureFlow(model, ic);

		const auto bodyWidth = [&model](const IconScale& at, const FlowMetrics& fm)
		{
			return model.hasFlow  ? FlowGroupWidth(model, fm)
			     : model.hasRates ? RateBodyWidth(model, at)
			     : StatBodyWidth(model, at);
		};

		// Icons that grow with the font widen the body, so the cap grows by the same
		// amount; otherwise a large font pushes the equation past the window edge.
		const IconScale base{ scale, PluginImages::kDefaultFontSize };
		const float grown = bodyWidth(ic, f) - bodyWidth(base, MeasureFlow(model, base));
		const float maxW  = kCardMaxW + (std::max)(0.0f, grown);

		const float baseW = Clamp((std::max)(bodyWidth(ic, f), HeaderWidth(model)) + kPadX * 2.0f,
		                          kCardMinW, maxW);

		float h = kStrip + kHeaderH;

		h += model.hasFlow ? f.bodyH : model.hasRates ? RateBodyHeight(model, baseW, ic) : StatBodyHeight(model, ic);

		if (HasFooter(model))
			h += kFooterH;

		if (!model.description.empty())
		{
			const int lines = WrapDescription(model.description, baseW - kPadX * 2.0f, nullptr);
			if (lines > 0)
				h += static_cast<float>(lines) * kDescLineH + 6.0f;
		}

		h += kPadY;

		outWidth  = baseW * scale;
		outHeight = h * scale;
	}

	// -------------------------------------------------------------------------
	// Draw
	// -------------------------------------------------------------------------

	namespace
	{
		void DrawHeader(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& m,
		                const Palette& pal, float s, const Rect& r, float& outBodyTop)
		{
			const float top = r.y0 + kStrip * s;
			const float y   = top + 5.0f * s;

			// Status sits right, and the title gets whatever is left — a long
			// building name is truncated rather than allowed to run under it.
			float rightEdge = r.x1 - kPadX * s;

			if (!m.statusText.empty())
			{
				const Color statusColor = Theme::Of(pal, m.status);
				const float textW = Draw::TextWidth(ui, m.statusText.c_str(), kFontStatus * s);

				Draw::TextAt(ui, dl, rightEdge - textW, y + 3.0f * s,
					m.statusText.c_str(), statusColor, kFontStatus * s);
				rightEdge -= textW + 9.0f * s;

				const float glow = m.status == Health::Good ? Theme::Pulse(ui->GetTime(), 2.4f) : 0.0f;
				Draw::StatusDot(ui, dl, rightEdge - 3.5f * s, y + 9.0f * s, 3.8f * s, statusColor, glow);
				rightEdge -= 14.0f * s;
			}

			float x = r.x0 + kPadX * s;

			if (!m.chip.empty())
			{
				// Drawn after the title would need the title's measured width, so
				// the chip leads instead — it is short and fixed-shape.
				x += Draw::Badge(ui, dl, x, y + 2.0f * s, m.chip.c_str(),
					pal.accent.WithAlpha(0.22f), pal.textPrimary, kFontChip * s);
				x += 8.0f * s;
			}

			const float titleMax = (std::max)(0.0f, rightEdge - x);
			Draw::TextFitted(ui, dl, x, y, titleMax,
				m.title.c_str(), pal.textPrimary, kFontTitle * s);

			// A note follows the title, in the room the title leaves.
			if (!m.titleNote.empty())
			{
				const float noteX = x + (std::min)(Draw::TextWidth(ui, m.title.c_str(), kFontTitle * s), titleMax) + 8.0f * s;
				Draw::TextFitted(ui, dl, noteX, y + 4.0f * s, (std::max)(0.0f, rightEdge - noteX),
					m.titleNote.c_str(), pal.textMuted, kFontStatus * s);
			}

			outBodyTop = top + kHeaderH * s;
		}

		void DrawFlowBody(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& m,
		                  const Palette& pal, const IconScale& ic, const Rect& r, float bodyTop)
		{
			const float s = ic.s;
			const FlowMetrics f = MeasureFlow(m, ic);
			const float cy = bodyTop + f.rowH * s * 0.5f;

			// The card is sized to the equation, so this normally lands flush with
			// the padding; centring only matters when the header forced the card wider.
			const float groupW = FlowGroupWidth(m, f);
			float x = (r.x0 + r.x1) * 0.5f - groupW * s * 0.5f;

			// ── Inputs, two to a row ─────────────────────────────────────────
			const float rounding  = (std::max)(6.0f, f.tile * 0.18f);
			const float badgeFont = Draw::CountBadgeFont(f.tile * s);
			const float gridX     = x + (f.slotW - f.gridW) * 0.5f * s;
			const float rateH     = f.rateH;
			const float gridY     = cy - f.stackH * s * 0.5f;

			for (size_t i = 0; i < m.inputs.size(); ++i)
			{
				const FlowSlot& in = m.inputs[i];

				const float cellX = gridX + (f.cellW + kRowGap) * s * static_cast<float>(i % kInCols);
				const float tx = cellX + (f.cellW - f.tile) * 0.5f * s;
				const float ty = gridY + (rateH + f.tile + f.shortH + kRowGap) * s * static_cast<float>(i / kInCols) + rateH * s;

				const Rect tile = { tx, ty, tx + f.tile * s, ty + f.tile * s };
				Draw::IconTile(ui, dl, tile, f.tilePx, in.icon, pal, rounding * s, in.dim ? 0.45f : 1.0f, true);

				const float midX = cellX + f.cellW * 0.5f * s;

				if (!in.rate.empty())
					Draw::TextCentered(ui, dl, midX, tile.y0 - rateH * s,
						in.rate.c_str(), pal.textMuted, kFontTiny * s);

				if (!in.shortage.empty())
					Draw::TextCentered(ui, dl, midX, tile.y1 + 1.0f * s,
						in.shortage.c_str(), pal.bad, kFontTiny * s);

				// A short input gets a red keyline as well as the dimming, so it
				// still reads when the icon itself is dark.
				if (in.dim)
					ui->DL_AddRect(dl, tile.x0, tile.y0, tile.x1, tile.y1,
						Theme::Pack(pal.bad, 0.85f), rounding * s, PluginDrawFlags_RoundCornersAll, 1.0f);

				if (!in.label.empty())
				{
					const Color c = in.tone == Health::Idle ? pal.textPrimary : Theme::Of(pal, in.tone);
					Draw::CountBadge(ui, dl, tile, in.label.c_str(), c, badgeFont);
				}
			}

			x += (f.slotW + kGap) * s;

			Draw::Arrow(ui, dl, x + kArrowW * s * 0.5f, cy, kArrowW * s * 0.8f,
				pal.textFaint, 1.6f * s);
			x += (kArrowW + kGap) * s;

			// ── The building, wearing its own progress ───────────────────────
			const float ringR = f.ringD * 0.5f * s;
			const float ringX = x + ringR;

			const Color ringFill = m.status == Health::Bad ? pal.bad : pal.accent;
			Draw::Ring(ui, dl, ringX, cy, ringR - kRingThick * 0.5f * s, kRingThick * s,
				m.progressValid ? Clamp01(m.progress) : 0.0f,
				pal.accentDim.WithAlpha(0.45f), ringFill);

			const float iconHalf = static_cast<float>(f.ringPx) * 0.5f;
			const Rect  iconRect = { ringX - iconHalf, cy - iconHalf, ringX + iconHalf, cy + iconHalf };
			Draw::IconBare(ui, dl, iconRect, f.ringPx, m.buildingIcon, pal, 1.0f);

			x += (f.ringD + kGap) * s;

			Draw::Arrow(ui, dl, x + kArrowW * s * 0.5f, cy, kArrowW * s * 0.8f,
				pal.textFaint, 1.6f * s);
			x += (kArrowW + kGap) * s;

			// ── Output ───────────────────────────────────────────────────────
			if (m.hasOutput)
			{
				const Rect tile = { x, cy - f.outD * s * 0.5f, x + f.outD * s, cy + f.outD * s * 0.5f };
				Draw::IconTile(ui, dl, tile, f.outPx, m.output.icon, pal, 10.0f * s, 1.0f, true);

				if (!m.output.label.empty())
					Draw::CountBadge(ui, dl, tile, m.output.label.c_str(), pal.textPrimary,
						Draw::CountBadgeFont(f.outD * s));

				x += (f.outD + kGap) * s;
			}

			// ── What it makes, and how fast ──────────────────────────────────
			// Name, rate and cycle stack into one block centred on the row beside
			// the product's icon. Hanging the name off the bottom of the card
			// instead leaves it floating with nothing to belong to.
			{
				const float colW    = (std::max)(0.0f, r.x1 - kPadX * s - x);
				const float nameH   = m.outputName.empty() ? 0.0f : kFontLabel * 1.4f * s;
				const float valueH  = m.rateValue.empty()  ? 0.0f : kFontRate  * 1.05f * s;
				const float subH    = m.rateSub.empty()    ? 0.0f : kFontTiny  * 1.35f * s;

				float y = cy - (nameH + valueH + subH) * 0.5f;

				if (!m.outputName.empty())
				{
					Draw::TextFitted(ui, dl, x, y, colW, m.outputName.c_str(),
						pal.textMuted, kFontLabel * s);
					y += nameH;
				}

				if (!m.rateValue.empty())
				{
					const Color rateColor = m.status == Health::Bad ? pal.bad : pal.textPrimary;
					Draw::TextAt(ui, dl, x, y, m.rateValue.c_str(), rateColor, kFontRate * s);

					if (!m.rateUnit.empty())
					{
						// Sat on the value's baseline rather than its top edge.
						const float vw = Draw::TextWidth(ui, m.rateValue.c_str(), kFontRate * s);
						Draw::TextAt(ui, dl, x + vw + 3.0f * s,
							y + (kFontRate - kFontUnit) * 0.78f * s,
							m.rateUnit.c_str(), pal.textMuted, kFontUnit * s);
					}

					y += valueH;
				}

				if (!m.rateSub.empty())
					Draw::TextAt(ui, dl, x, y, m.rateSub.c_str(), pal.textFaint, kFontTiny * s);
			}
		}

		void DrawStatBody(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& m,
		                  const Palette& pal, const IconScale& ic, const Rect& r, float bodyTop)
		{
			const float s    = ic.s;
			const float left = r.x0 + kPadX * s;

			const StockMetrics st = MeasureStock(m, ic);

			const float cellsH   = static_cast<float>(StatRows(m)) * kStatRowH * s;
			const float blockH   = (StatBodyHeight(m, ic) - 6.0f) * s;
			const float blockMid = bodyTop + blockH * 0.5f;

			// ── The building, in the same ring the flow body gives it ────────
			// Progress reads as fullness or grid load for buildings with no cycle.
			const float ringR = st.ringD * 0.5f * s;
			const float ringX = left + ringR;

			Draw::Ring(ui, dl, ringX, blockMid, ringR - kRingThick * 0.5f * s, kRingThick * s,
				m.progressValid ? Clamp01(m.progress) : 0.0f,
				pal.accentDim.WithAlpha(0.45f),
				m.status == Health::Bad ? pal.bad : pal.accent);

			const float iconHalf = static_cast<float>(st.ringPx) * 0.5f;
			Draw::IconBare(ui, dl, { ringX - iconHalf, blockMid - iconHalf,
			                         ringX + iconHalf, blockMid + iconHalf },
				st.ringPx, m.buildingIcon, pal, 1.0f);

			float x = left + st.ringD * s;

			// ── The figures, beside the ring ─────────────────────────────────
			if (!m.stats.empty())
			{
				x += kStatSpace * s;

				const size_t cols     = StatCols(m);
				const float  colW     = StatCellWidth(m) * s;
				const float  statsTop = blockMid - cellsH * 0.5f;

				for (size_t i = 0; i < m.stats.size(); ++i)
				{
					const float cx = x + colW * static_cast<float>(i % cols);
					const float cy = statsTop + kStatRowH * s * static_cast<float>(i / cols);

					const Color valueColor = m.stats[i].tone == Health::Idle
						? pal.textPrimary
						: Theme::Of(pal, m.stats[i].tone);

					Draw::TextAt(ui, dl, cx, cy, m.stats[i].value.c_str(), valueColor, kFontStat * s);
					Draw::TextAt(ui, dl, cx, cy + 20.0f * s, m.stats[i].label.c_str(),
						pal.textFaint, kFontTiny * s);
				}

				x += static_cast<float>(cols) * colW;
			}

			// ── What it holds, two rows deep, running right ──────────────────
			if (st.cols > 0)
			{
				x += kStatSpace * s;

				const float badgeFont = Draw::CountBadgeFont(st.tile * s);
				const float rounding  = (std::max)(6.0f, st.tile * 0.14f);
				const float gridTop   = blockMid - st.gridH * s * 0.5f;

				for (size_t i = 0; i < m.stock.size(); ++i)
				{
					const float tx = x + (st.tile + kStockGap) * s * static_cast<float>(i % st.cols);
					const float ty = gridTop + (st.tile + kStockGap) * s * static_cast<float>(i / st.cols);

					const Rect tile = { tx, ty, tx + st.tile * s, ty + st.tile * s };

					// The stack's own count rides its icon, the way a recipe's
					// ingredients carry theirs — no second row of figures.
					Draw::IconTile(ui, dl, tile, st.tilePx, m.stock[i].icon, pal, rounding * s, 1.0f, true);
					if (!m.stock[i].label.empty())
						Draw::CountBadge(ui, dl, tile, m.stock[i].label.c_str(),
							pal.textPrimary, badgeFont);
				}

				// Say how many kinds got cut rather than showing a partial list silently.
				if (m.stockMore > 0)
				{
					char buf[16];
					snprintf(buf, sizeof(buf), "+%d", m.stockMore);
					Draw::TextAt(ui, dl, x + (st.gridW + 6.0f) * s,
						blockMid - kFontTiny * 0.5f * s, buf, pal.textFaint, kFontTiny * s);
				}
			}
		}
	}

	namespace
	{
		// One line of a rate row. The stretches the row names a health colour are
		// drawn in it, one after another from where the last ended, the rest in
		// the plain colour; a line that runs past maxW is cut with an ellipsis
		// like any other.
		void DrawRateText(IModLoaderImGui* ui, PluginDrawList dl, const RateRow& row, const RowLine& line,
		                  const Palette& pal, float x, float y, float maxW, float fontSize)
		{
			const size_t from = line.start;
			const size_t to   = line.start + line.text.size();
			float used = 0.0f;
			size_t at  = from;
			auto piece = [&](size_t end, const Color& c)
			{
				if (end <= at)
					return;
				const std::string text = row.name.substr(at, end - at);
				const float w = Draw::TextWidth(ui, text.c_str(), fontSize);
				if (used + w > maxW)
					Draw::TextFitted(ui, dl, x + used, y, maxW - used, text.c_str(), c, fontSize);
				else
					Draw::TextAt(ui, dl, x + used, y, text.c_str(), c, fontSize);
				used += w;
				at = end;
			};
			for (const RateRow::Span& span : row.spans)
			{
				const size_t begin = (std::max)(span.begin, from);
				const size_t end   = (std::min)(span.end, to);
				if (begin >= end)
					continue;
				piece(begin, pal.textPrimary);
				piece(end, Theme::Of(pal, span.tone));
			}
			piece(to, pal.textPrimary);
		}

		void DrawRateBody(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& m,
		                  const Palette& pal, const IconScale& ic, const Rect& r, float bodyTop)
		{
			const float s        = ic.s;
			const float rateBox  = ic.Box(kRateIcon);
			const float rateRowH = RateRowH(ic);
			const float left  = r.x0 + kPadX * s;
			const float right = r.x1 - kPadX * s;

			if (m.rates.empty())
			{
				Draw::TextAt(ui, dl, left, bodyTop + (rateRowH - kFontLabel) * 0.5f * s,
					m.ratesNote.c_str(), pal.textMuted, kFontLabel * s);
				return;
			}

			const float nameMaxW = right - (left + (rateBox + kRateGap) * s);

			float y = bodyTop;
			for (const RateRow& row : m.rates)
			{
				const float midY = y + rateRowH * 0.5f * s;
				const float textY = midY - kFontLabel * 0.5f * s;

				if (row.lead == RateRow::Lead::Icon)
					Draw::IconBare(ui, dl, { left, midY - rateBox * 0.5f * s,
					                         left + rateBox * s, midY + rateBox * 0.5f * s },
						ic.Px(kRateIcon), row.icon, pal, 1.0f);
				else if (row.lead == RateRow::Lead::Bullet)
					Draw::StatusDot(ui, dl, left + rateBox * 0.5f * s, midY, kRateBullet * s, pal.textPrimary, 0.0f);

				const std::vector<RowLine> lines = RowLines(row, (r.x1 - r.x0) / s, ic);
				for (size_t k = 0; k < lines.size(); ++k)
				{
					const float indent = k ? kRateIndent * s : 0.0f;
					DrawRateText(ui, dl, row, lines[k], pal, left + (rateBox + kRateGap) * s + indent,
						textY + kRateContH * s * static_cast<float>(k), nameMaxW - indent, kFontLabel * s);
				}
				y += (rateRowH + kRateContH * static_cast<float>(lines.size() - 1)) * s;
			}
		}
	}

	void Card::Draw(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& model,
	                const Palette& pal, float scale, const Rect& rect)
	{
		if (!ui || !model.valid)
			return;

		const float      s = scale;
		const IconScale ic{ scale, ui->GetFontSize() };

		Draw::Panel(ui, dl, rect, pal, kRounding * s, kStrip * s);

		float bodyTop = 0.0f;
		DrawHeader(ui, dl, model, pal, s, rect, bodyTop);

		if (model.hasFlow)
		{
			DrawFlowBody(ui, dl, model, pal, ic, rect, bodyTop);
			bodyTop += MeasureFlow(model, ic).bodyH * s;
		}
		else if (model.hasRates)
		{
			DrawRateBody(ui, dl, model, pal, ic, rect, bodyTop);
			bodyTop += RateBodyHeight(model, (rect.x1 - rect.x0) / s, ic) * s;
		}
		else
		{
			DrawStatBody(ui, dl, model, pal, ic, rect, bodyTop);
			bodyTop += StatBodyHeight(model, ic) * s;
		}

		if (HasFooter(model))
		{
			if (!model.footLeft.empty())
			{
				const float maxW = (rect.x1 - rect.x0) * 0.6f;
				Draw::TextFitted(ui, dl, rect.x0 + kPadX * s, bodyTop, maxW,
					model.footLeft.c_str(), pal.textMuted, kFontTiny * s);
			}
			if (!model.footRight.empty())
				Draw::TextRight(ui, dl, rect.x1 - kPadX * s, bodyTop,
					model.footRight.c_str(), pal.textFaint, kFontTiny * s);

			bodyTop += kFooterH * s;
		}

		if (!model.description.empty())
		{
			std::vector<std::string> lines;
			WrapDescription(model.description, (rect.x1 - rect.x0) / s - kPadX * 2.0f, &lines);

			float y = bodyTop + 4.0f * s;
			for (const auto& line : lines)
			{
				Draw::TextAt(ui, dl, rect.x0 + kPadX * s, y, line.c_str(), pal.textFaint, kFontTiny * s);
				y += kDescLineH * s;
			}
		}
	}
}
