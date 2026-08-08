#include "waila_card.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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

		float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

		float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

		constexpr size_t kInCols = 2;

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
			float gridW;    // width the tiles actually occupy, <= kInGridW
			float stackH;   // total height of the input grid
			float rowH;     // height of the flow row, ring floor included
			float bodyH;    // rowH plus breathing room
			float tailW;    // product name / rate column
		};

		FlowMetrics MeasureFlow(const CardModel& m)
		{
			const float rows = static_cast<float>(InputRows(m));
			const size_t used = m.inputs.empty() ? 1 : m.inputs.size();
			const float  cols = static_cast<float>((std::min)(kInCols, used));

			FlowMetrics f{};

			// Whichever runs out first: the width of two columns, or the ring's
			// height shared between the rows.
			const float byWidth  = (kInGridW - (static_cast<float>(kInCols) - 1.0f) * kRowGap)
			                     / static_cast<float>(kInCols);
			const float byHeight = (kRingD - (rows - 1.0f) * kRowGap) / rows;

			f.tile   = Clamp((std::min)(byWidth, byHeight), kTileInMin, kTileInMax);
			f.gridW  = cols * f.tile + (cols - 1.0f) * kRowGap;
			f.stackH = rows * f.tile + (rows - 1.0f) * kRowGap;
			f.rowH   = (std::max)(kRingD, f.stackH) + 14.0f;
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

		// Every term but the tail is a constant, so the equation lands on the same
		// pixel on every machine and only the card's right edge moves.
		float FlowGroupWidth(const CardModel& m, const FlowMetrics& f)
		{
			float w = kInGridW
			        + kGap + kArrowW + kGap
			        + kRingD
			        + kGap + kArrowW + kGap
			        + f.tailW;
			if (m.hasOutput)
				w += kTileOut + kGap;
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
			float  gridW = 0.0f;
			float  gridH = 0.0f;
		};

		StockMetrics MeasureStock(const CardModel& m)
		{
			StockMetrics st{};
			const size_t n = m.stock.size();
			if (n == 0)
				return st;

			st.rows = (std::min)(kStockRows, n);
			st.cols = (n + st.rows - 1) / st.rows;

			const float rows = static_cast<float>(st.rows);
			st.tile  = Clamp((kStatRingD - (rows - 1.0f) * kStockGap) / rows,
			                 kStockTileMin, kStockTileMax);
			st.gridW = static_cast<float>(st.cols) * st.tile + (static_cast<float>(st.cols) - 1.0f) * kStockGap;
			st.gridH = rows * st.tile + (rows - 1.0f) * kStockGap;
			return st;
		}

		// The ring sets a floor: the building stays 64pt tall however little sits
		// beside it, so a storage crate and a generator are the same shape.
		float StatBodyHeight(const CardModel& m)
		{
			const float cells = static_cast<float>(StatRows(m)) * kStatRowH;
			float h = (std::max)(kStatRingD, cells);
			h = (std::max)(h, MeasureStock(m).gridH);
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
		float StatBodyWidth(const CardModel& m)
		{
			float w = kStatRingD;

			if (!m.stats.empty())
				w += kStatSpace + static_cast<float>(StatCols(m)) * StatCellWidth(m);

			const StockMetrics st = MeasureStock(m);
			if (st.cols > 0)
				w += kStatSpace + st.gridW + StockTailWidth(m);

			return w;
		}

		// A card narrower than its own title is worse than a card with air in it.
		float HeaderWidth(const CardModel& m)
		{
			float w = EstimateW(m.title, kFontTitle, 0.62f);

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
		                    std::vector<std::string>* outLines)
		{
			if (desc.empty())
				return 0;

			// ~0.5em per character is close enough for a proportional face at this size.
			const int perLine = (std::max)(12, static_cast<int>(widthPoints / (kFontTiny * 0.5f)));

			int    lines = 0;
			size_t pos   = 0;
			while (pos < desc.size() && lines < kDescMaxLines)
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

				++lines;
				pos = end;
				while (pos < desc.size() && desc[pos] == ' ')
					++pos;
			}

			return lines;
		}

		bool HasFooter(const CardModel& m)
		{
			return !m.footLeft.empty() || !m.footRight.empty();
		}
	}

	// -------------------------------------------------------------------------
	// Measure
	// -------------------------------------------------------------------------

	void Card::Measure(const CardModel& model, float scale, float& outWidth, float& outHeight)
	{
		const FlowMetrics f = MeasureFlow(model);

		const float bodyW = model.hasFlow ? FlowGroupWidth(model, f) : StatBodyWidth(model);
		const float baseW = Clamp((std::max)(bodyW, HeaderWidth(model)) + kPadX * 2.0f,
		                          kCardMinW, kCardMaxW);

		float h = kStrip + kHeaderH;

		h += model.hasFlow ? f.bodyH : StatBodyHeight(model);

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

			Draw::TextFitted(ui, dl, x, y, (std::max)(0.0f, rightEdge - x),
				m.title.c_str(), pal.textPrimary, kFontTitle * s);

			outBodyTop = top + kHeaderH * s;
		}

		void DrawFlowBody(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& m,
		                  const Palette& pal, float s, const Rect& r, float bodyTop)
		{
			const FlowMetrics f = MeasureFlow(m);
			const float cy = bodyTop + f.rowH * s * 0.5f;

			// The card is sized to the equation, so this normally lands flush with
			// the padding; centring only matters when the header forced the card wider.
			const float groupW = FlowGroupWidth(m, f);
			float x = (r.x0 + r.x1) * 0.5f - groupW * s * 0.5f;

			// ── Inputs, two to a row ─────────────────────────────────────────
			const float rounding  = (std::max)(6.0f, f.tile * 0.18f);
			const float badgeFont = Draw::CountBadgeFont(f.tile * s);
			const float gridX     = x + (kInGridW - f.gridW) * 0.5f * s;
			const float gridY     = cy - f.stackH * s * 0.5f;

			for (size_t i = 0; i < m.inputs.size(); ++i)
			{
				const FlowSlot& in = m.inputs[i];

				const float tx = gridX + (f.tile + kRowGap) * s * static_cast<float>(i % kInCols);
				const float ty = gridY + (f.tile + kRowGap) * s * static_cast<float>(i / kInCols);

				const Rect tile = { tx, ty, tx + f.tile * s, ty + f.tile * s };
				Draw::IconTile(ui, dl, tile, in.icon, pal, rounding * s, in.dim ? 0.45f : 1.0f, true);

				// A starved input gets a red keyline as well as the dimming, so it
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

			x += (kInGridW + kGap) * s;

			Draw::Arrow(ui, dl, x + kArrowW * s * 0.5f, cy, kArrowW * s * 0.8f,
				pal.textFaint, 1.6f * s);
			x += (kArrowW + kGap) * s;

			// ── The building, wearing its own progress ───────────────────────
			const float ringR = kRingD * 0.5f * s;
			const float ringX = x + ringR;

			const Color ringFill = m.status == Health::Bad ? pal.bad : pal.accent;
			Draw::Ring(ui, dl, ringX, cy, ringR - kRingThick * 0.5f * s, kRingThick * s,
				m.progressValid ? Clamp01(m.progress) : 0.0f,
				pal.accentDim.WithAlpha(0.45f), ringFill);

			const float iconHalf = ringR * 0.62f;
			const Rect  iconRect = { ringX - iconHalf, cy - iconHalf, ringX + iconHalf, cy + iconHalf };
			Draw::IconBare(ui, dl, iconRect, m.buildingIcon, pal, 1.0f);

			x += (kRingD + kGap) * s;

			Draw::Arrow(ui, dl, x + kArrowW * s * 0.5f, cy, kArrowW * s * 0.8f,
				pal.textFaint, 1.6f * s);
			x += (kArrowW + kGap) * s;

			// ── Output ───────────────────────────────────────────────────────
			if (m.hasOutput)
			{
				const Rect tile = { x, cy - kTileOut * s * 0.5f, x + kTileOut * s, cy + kTileOut * s * 0.5f };
				Draw::IconTile(ui, dl, tile, m.output.icon, pal, 10.0f * s, 1.0f, true);

				if (!m.output.label.empty())
					Draw::CountBadge(ui, dl, tile, m.output.label.c_str(), pal.textPrimary,
						Draw::CountBadgeFont(kTileOut * s));

				x += (kTileOut + kGap) * s;
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
		                  const Palette& pal, float s, const Rect& r, float bodyTop)
		{
			const float left = r.x0 + kPadX * s;

			const StockMetrics st = MeasureStock(m);

			const float cellsH   = static_cast<float>(StatRows(m)) * kStatRowH * s;
			const float blockH   = (StatBodyHeight(m) - 6.0f) * s;
			const float blockMid = bodyTop + blockH * 0.5f;

			// ── The building, in the same ring the flow body gives it ────────
			// Progress reads as fullness or grid load for buildings with no cycle.
			const float ringR = kStatRingD * 0.5f * s;
			const float ringX = left + ringR;

			Draw::Ring(ui, dl, ringX, blockMid, ringR - kRingThick * 0.5f * s, kRingThick * s,
				m.progressValid ? Clamp01(m.progress) : 0.0f,
				pal.accentDim.WithAlpha(0.45f),
				m.status == Health::Bad ? pal.bad : pal.accent);

			const float iconHalf = ringR * 0.62f;
			Draw::IconBare(ui, dl, { ringX - iconHalf, blockMid - iconHalf,
			                         ringX + iconHalf, blockMid + iconHalf },
				m.buildingIcon, pal, 1.0f);

			float x = left + kStatRingD * s;

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
					Draw::IconTile(ui, dl, tile, m.stock[i].icon, pal, rounding * s, 1.0f, true);
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

	void Card::Draw(IModLoaderImGui* ui, PluginDrawList dl, const CardModel& model,
	                const Palette& pal, float scale, const Rect& rect)
	{
		if (!ui || !model.valid)
			return;

		const float s = scale;

		Draw::Panel(ui, dl, rect, pal, kRounding * s, kStrip * s);

		float bodyTop = 0.0f;
		DrawHeader(ui, dl, model, pal, s, rect, bodyTop);

		if (model.hasFlow)
		{
			DrawFlowBody(ui, dl, model, pal, s, rect, bodyTop);
			bodyTop += MeasureFlow(model).bodyH * s;
		}
		else
		{
			DrawStatBody(ui, dl, model, pal, s, rect, bodyTop);
			bodyTop += StatBodyHeight(model) * s;
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
