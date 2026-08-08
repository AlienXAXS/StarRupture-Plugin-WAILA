#include "card_model.h"
#include "icon_cache.h"
#include "ui_draw.h"

#include "crafter_detector.h"
#include "storage_detector.h"
#include "power_detector.h"
#include "cooler_active_detector.h"
#include "cooler_passive_detector.h"
#include "cargo_sender_detector.h"
#include "cargo_receiver_detector.h"

#include <cmath>
#include <cstdio>

namespace Waila::UI
{
	namespace
	{
		// More than this and the row stops being scannable, which is the whole
		// point of the layout.
		constexpr size_t kMaxStockSlots = 6;

		std::string Num(int32_t v)
		{
			char buf[24];
			snprintf(buf, sizeof(buf), "%d", v);
			return buf;
		}

		std::string Watts(float w)
		{
			char num[24];
			Draw::FormatNumber(num, sizeof(num), w);

			char buf[40];
			snprintf(buf, sizeof(buf), "%s W", num);
			return buf;
		}

		bool g_showDescriptions = true;

		void ApplyDescription(CardModel& out, const std::string& desc)
		{
			if (desc.empty() || desc == "N/A" || !g_showDescriptions)
				return;

			out.description = desc;
		}

		// Shared header for every card: name, icon, and the description line.
		void BeginCard(CardModel& out, Accent accent, const std::string& name,
		               const std::string& desc, SDK::UCrBuildingData* buildingData)
		{
			out.Clear();
			out.valid        = true;
			out.accent       = accent;
			out.title        = name;
			out.buildingIcon = Icons::ForBuilding(buildingData);
			ApplyDescription(out, desc);
		}

		// Stack counts ride the corner of a 36pt tile, so they have to stay short —
		// a raw "12480" would be wider than the icon it belongs to.
		std::string NumCompact(int32_t v)
		{
			char buf[24];
			if (v >= 100000)     snprintf(buf, sizeof(buf), "%dk", v / 1000);
			else if (v >= 10000) snprintf(buf, sizeof(buf), "%.0fk", v / 1000.0f);
			else if (v >= 1000)  snprintf(buf, sizeof(buf), "%.1fk", v / 1000.0f);
			else                 snprintf(buf, sizeof(buf), "%d", v);
			return buf;
		}

		FlowSlot StockSlot(PluginTextureHandle icon, int32_t count)
		{
			FlowSlot slot;
			slot.icon  = icon;
			slot.label = NumCompact(count);
			return slot;
		}
	}

	void Card::SetShowDescriptions(bool show)
	{
		g_showDescriptions = show;
	}

	// -------------------------------------------------------------------------
	// Crafter — the flow body this layout was designed around
	// -------------------------------------------------------------------------

	void Card::FromCrafter(const Waila::CrafterInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Crafter, info.crafterClass, info.buildingDesc, info.buildingData);

		const bool idle = info.currentRecipe == "(idle)" || info.recipeInputs.empty();

		const std::string recipeName = info.currentRecipeDisplayName.empty()
			? info.currentRecipe
			: info.currentRecipeDisplayName;

		out.progress      = info.craftingProgress;
		out.progressValid = !idle;

		// Speed only earns a chip when it is not the plain 1x every building has.
		if (std::fabs(info.craftingSpeed - 1.0f) > 0.005f)
		{
			char buf[24];
			char num[24];
			Draw::FormatNumber(num, sizeof(num), info.craftingSpeed);
			snprintf(buf, sizeof(buf), "%sx", num);
			out.chip = buf;
		}

		if (idle)
		{
			out.status     = Health::Idle;
			out.statusText = "Idle";
			out.footLeft   = "No recipe set";
			return;
		}

		// Output full is the more actionable of the two — say that one first.
		if (info.bOutputFull)
		{
			out.status     = Health::Bad;
			out.statusText = "Output full";
		}
		else if (info.bMissingItems)
		{
			out.status     = Health::Bad;
			out.statusText = "Starved";
		}
		else
		{
			out.status     = Health::Good;
			out.statusText = "Working";
		}

		out.hasFlow = true;

		for (const auto& in : info.recipeInputs)
		{
			FlowSlot slot;
			slot.icon = in.itemData ? Icons::ForItem(in.itemData) : Icons::ForItemClass(in.itemClass);

			// The badge states what the recipe wants. Whether the machine currently
			// holds enough is carried by the tile's dimming and red keyline, so the
			// stock figure would only be a second way of saying the same thing.
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", in.need);
			slot.label = buf;

			if (in.have >= 0)
			{
				slot.dim  = in.have < in.need;
				slot.tone = slot.dim ? Health::Bad : Health::Idle;
			}

			out.inputs.push_back(std::move(slot));
		}

		if (info.recipeOutput.IsValid())
		{
			out.hasOutput = true;
			out.output.icon = Icons::ForRecipe(info.recipeDataPtr);
			if (!out.output.icon)
			{
				out.output.icon = info.recipeOutput.itemData
					? Icons::ForItem(info.recipeOutput.itemData)
					: Icons::ForItemClass(info.recipeOutput.itemClass);
			}

			// Always badged, even at 1 — a bare product tile in a row of counted
			// ingredients reads as an icon whose number failed to load.
			char buf[24];
			snprintf(buf, sizeof(buf), "%d", info.recipeOutputCount > 0 ? info.recipeOutputCount : 1);
			out.output.label = buf;
		}

		if (info.recipeBuildTime > 0.0f)
		{
			const float perMinute = (60.0f / info.recipeBuildTime) * static_cast<float>(info.recipeOutputCount);

			char num[24];
			Draw::FormatNumber(num, sizeof(num), out.status == Health::Bad ? 0.0f : perMinute);
			out.rateValue = num;
			out.rateUnit  = "/min";

			char cycle[32];
			char secs[24];
			Draw::FormatSeconds(secs, sizeof(secs), info.recipeBuildTime);
			snprintf(cycle, sizeof(cycle), "%s cycle", secs);
			out.rateSub = cycle;

			if (out.status != Health::Bad)
			{
				// The countdown takes the header slot instead of "Working" — a
				// pulsing green dot beside a ticking timer already says the machine
				// is running, and the word was costing a footer row to repeat it.
				// The stalled states keep their word; they have no countdown anyway.
				char next[40];
				char remain[24];
				const float remaining = info.recipeBuildTime * (1.0f - info.craftingProgress);
				Draw::FormatSeconds(remain, sizeof(remain), remaining < 0.0f ? 0.0f : remaining);
				snprintf(next, sizeof(next), "next in %s", remain);
				out.statusText = next;
			}
		}

		out.outputName = recipeName;
	}

	// -------------------------------------------------------------------------
	// Containers
	// -------------------------------------------------------------------------

	namespace
	{
		void FillContainer(CardModel& out, int32_t maxCapacity, int32_t usedSlots,
		                   const std::vector<Waila::StoredItemEntry>& items)
		{
			for (size_t i = 0; i < items.size() && i < kMaxStockSlots; ++i)
				out.stock.push_back(StockSlot(Icons::ForItem(items[i].itemData), items[i].count));

			// Say how many kinds got cut rather than silently showing a partial list.
			if (items.size() > kMaxStockSlots)
				out.stockMore = static_cast<int32_t>(items.size() - kMaxStockSlots);

			// Each stack carries its own count on its icon, so a total would only be
			// those numbers added up in front of the player.
			char stacks[32];
			snprintf(stacks, sizeof(stacks), "%d/%d", usedSlots, maxCapacity);
			out.stats.push_back({ stacks, "slots", Health::Idle });

			if (maxCapacity > 0)
			{
				out.progress      = static_cast<float>(usedSlots) / static_cast<float>(maxCapacity);
				out.progressValid = true;

				if (out.progress >= 0.999f)
				{
					out.status     = Health::Warn;
					out.statusText = "Full";
				}
				else
				{
					// The slot counts are already a stat cell — the chip says how
					// full rather than repeating them.
					char pct[16];
					snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(out.progress * 100.0f + 0.5f));
					out.status     = Health::Good;
					out.statusText = pct;
				}
			}
		}
	}

	void Card::FromStorage(const Waila::StorageInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Storage, info.buildingName, info.buildingDesc, info.buildingData);
		FillContainer(out, info.maxCapacity, info.usedSlots, info.storedItems);

		if (info.storedItems.empty())
		{
			out.status     = Health::Idle;
			out.statusText = "Empty";
		}
	}

	void Card::FromCargoReceiver(const Waila::CargoReceiverInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Cargo, info.buildingName, info.buildingDesc, info.buildingData);
		FillContainer(out, info.maxCapacity, info.usedSlots, info.storedItems);
		out.footLeft = "Receiving";

		if (info.storedItems.empty())
		{
			out.status     = Health::Idle;
			out.statusText = "Empty";
		}
	}

	// -------------------------------------------------------------------------
	// Power
	// -------------------------------------------------------------------------

	void Card::FromPower(const Waila::PowerInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Power, info.buildingName, info.buildingDesc, info.buildingData);

		switch (info.gridConnectionStatus)
		{
		case 1:
			out.status     = Health::Good;
			out.statusText = "On grid";
			break;
		case 2:
			out.status     = Health::Warn;
			out.statusText = "Switched off";
			break;
		default:
			out.status     = Health::Bad;
			out.statusText = "Off grid";
			break;
		}

		// The ring reads as grid load: how much of what is being generated is
		// already spoken for.
		if (info.gridAddPower > 0.0f)
		{
			out.progress      = info.gridRemovePower / info.gridAddPower;
			out.progressValid = true;
		}

		const bool overdrawn = info.gridRemovePower > info.gridAddPower;

		out.stats.push_back({ Watts(info.buildingPower),   "output",   Health::Idle });
		out.stats.push_back({ Watts(info.gridAddPower),    "grid gen", Health::Idle });
		out.stats.push_back({ Watts(info.gridRemovePower), "grid use", overdrawn ? Health::Bad : Health::Idle });
		out.stats.push_back({ Watts(info.gridTotalPower),  "grid cap", Health::Idle });

		if (overdrawn)
		{
			out.status     = Health::Bad;
			out.statusText = "Overdrawn";
		}
	}

	// -------------------------------------------------------------------------
	// Coolers
	// -------------------------------------------------------------------------

	namespace
	{
		void FillCooler(CardModel& out, uint8_t state, int32_t connected, int32_t total)
		{
			const char* stateStr = "Unknown";
			Health      tone     = Health::Idle;

			switch (state)
			{
			case 0: stateStr = "Idle";      tone = Health::Idle; break;
			case 1: stateStr = "Working";   tone = Health::Good; break;
			case 2: stateStr = "No fuel";   tone = Health::Bad;  break;
			case 3: stateStr = "Too hot";   tone = Health::Bad;  break;
			default: break;
			}

			out.status     = tone;
			out.statusText = stateStr;

			if (total > 0)
			{
				out.progress      = static_cast<float>(connected) / static_cast<float>(total);
				out.progressValid = true;
			}

			char sockets[32];
			snprintf(sockets, sizeof(sockets), "%d/%d", connected, total);
			out.stats.push_back({ sockets,  "sockets", connected == 0 ? Health::Warn : Health::Idle });
			out.stats.push_back({ stateStr, "state",   tone });
		}
	}

	void Card::FromCoolerActive(const Waila::CoolerActiveInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Cooler, info.buildingName, info.buildingDesc, info.buildingData);
		FillCooler(out, info.state, info.connectedSockets, info.totalSockets);
	}

	void Card::FromCoolerPassive(const Waila::CoolerPassiveInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Cooler, info.buildingName, info.buildingDesc, info.buildingData);
		FillCooler(out, info.state, info.connectedSockets, info.totalSockets);
	}

	// -------------------------------------------------------------------------
	// Cargo sender — the same equation, with a drone instead of a product
	// -------------------------------------------------------------------------

	void Card::FromCargoSender(const Waila::CargoSenderInfo& info, CardModel& out)
	{
		BeginCard(out, Accent::Cargo, info.buildingName, info.buildingDesc, info.buildingData);

		// canSend means the pad is free, i.e. it is loading rather than in flight.
		out.status     = info.canSend ? Health::Warn : Health::Good;
		out.statusText = info.canSend ? "Loading" : "Sending";

		const float progress = info.sendProgress < 0.0f ? 0.0f : info.sendProgress;
		out.progress      = progress;
		out.progressValid = info.sendProgress >= 0.0f;

		out.hasFlow = true;

		if (info.sendingItem)
		{
			FlowSlot slot;
			slot.icon = Icons::ForItem(info.sendingItem);
			out.inputs.push_back(std::move(slot));
		}

		char pct[16];
		snprintf(pct, sizeof(pct), "%d", static_cast<int>(progress * 100.0f + 0.5f));
		out.rateValue = pct;
		out.rateUnit  = "%";
		out.rateSub   = info.sendProgress >= 0.0f ? "loaded" : "idle";

		if (!info.sendingItemName.empty())
			out.outputName = info.sendingItemName;

		if (info.sendingTime > 0.0f)
		{
			char every[40];
			char secs[24];
			Draw::FormatSeconds(secs, sizeof(secs), info.sendingTime);
			snprintf(every, sizeof(every), "every %s", secs);
			out.footRight = every;
		}
	}
}
