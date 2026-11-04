#include "crafter_detector.h"
#include "waila_functions.h"
#include "waila_fields.h"
#include "storage_detector.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuCrafting_classes.hpp"
#include "AuCrafting_structs.hpp"
#include "AuItems_classes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SDK;

namespace Waila
{
	namespace
	{
		void FillEntry(RecipeItemEntry& entry, UClass* itemClass, int32_t count)
		{
			entry.itemClass = itemClass;
			entry.need      = count;
			entry.itemData  = Fields::ItemDefaultsOf(itemClass);

			if (entry.itemData)
				entry.displayName = Fields::DisplayNameOf(entry.itemData);
		}

		// Counts what is actually sitting in the crafter's input store, keyed by
		// the item's unique name so ingredients can be matched against it.
		// Returns false when the store could not be read, which is the difference
		// between "0 of these" and "we don't know".
		bool ReadInputStock(ACrBuildingActorBase* building,
		                    std::unordered_map<std::string, int32_t>& outStock)
		{
			UCrBuildingItemStorageComponent* inStorage = Fields::ValueOr<UCrBuildingItemStorageComponent*>(Fields::InItemStorage, building, nullptr);
			if (!inStorage)
				return false;

			auto fnGetContainer = Waila::Functions::GetStoredItemsContainerInternal();
			if (!fnGetContainer)
				return false;

			UWorld* world = PluginSdkRuntime::GetWorld();
			if (!world)
				return false;

			FCrItemsStorageContainer* container = fnGetContainer(inStorage, world);
			if (!container)
				return false;

			std::vector<StoredItemEntry> stored;
			int32_t usedSlots = 0;
			ReadStoredItems(container, stored, usedSlots);
			for (const StoredItemEntry& entry : stored)
			{
				if (entry.itemData)
					outStock[entry.uniqueName] += entry.count;
			}

			return true;
		}

		// What has been arriving in the input store of the machine the player is
		// aiming at, per ingredient. Fed from the tick; nothing is kept for any
		// other machine and nothing is written out.
		//
		// The store only shows its count, so an arrival is a count that went up
		// between two samples. Consumption shows up as a drop and is ignored.
		class ArrivalWindow
		{
		public:
			using Clock = std::chrono::steady_clock;

			// Drone deliveries come in bursts, so the rate is averaged over this long.
			static constexpr std::chrono::seconds kSpan{ 60 };
			// Until the window has run this long it is divided by this long anyway,
			// so a fresh window reads low (and the card calls the input fully short)
			// and settles to the measured rate instead of leaping on one delivery.
			static constexpr std::chrono::seconds kSettle{ 20 };
			// Looking away this long drops the window; the stock moved unseen.
			static constexpr std::chrono::seconds kGap{ 3 };

			void Sample(const std::string& key, const std::vector<RecipeItemEntry>& inputs, Clock::time_point now)
			{
				const bool known = std::all_of(inputs.begin(), inputs.end(),
					[](const RecipeItemEntry& e) { return e.have >= 0; });

				if (!known || key != m_key || inputs.size() != m_prev.size() || now - m_last > kGap)
				{
					m_key = known ? key : std::string();
					m_start = now;
					m_prev.assign(inputs.size(), 0);
					m_events.assign(inputs.size(), {});
					for (size_t i = 0; known && i < inputs.size(); ++i)
						m_prev[i] = inputs[i].have;
					m_last = now;
					return;
				}

				for (size_t i = 0; i < inputs.size(); ++i)
				{
					if (inputs[i].have > m_prev[i])
						m_events[i].push_back({ now, inputs[i].have - m_prev[i] });
					m_prev[i] = inputs[i].have;

					while (!m_events[i].empty() && now - m_events[i].front().first > kSpan)
						m_events[i].pop_front();
				}
				m_last = now;
			}

			// Items a minute that arrived at ingredient i; -1 while nothing is sampled.
			float PerMin(size_t i, Clock::time_point now) const
			{
				if (m_key.empty() || i >= m_events.size())
					return -1.0f;

				const float spanSec = std::chrono::duration<float>(
					(std::min)((std::max)(now - m_start, Clock::duration(kSettle)), Clock::duration(kSpan))).count();

				int32_t arrived = 0;
				for (const auto& e : m_events[i])
					arrived += e.second;

				return static_cast<float>(arrived) / spanSec * 60.0f;
			}

		private:
			std::string                                                  m_key;
			Clock::time_point                                            m_start{};
			Clock::time_point                                            m_last{};
			std::vector<int32_t>                                         m_prev;
			std::vector<std::deque<std::pair<Clock::time_point, int32_t>>> m_events;
		};

		ArrivalWindow g_arrivals;
	}

	bool CountRate(UAuItemRecipeData* recipe, int32_t count, float speed, float& outRate)
	{
		const float buildTime = Fields::ValueOr<float>(Fields::RecipeBuildTime, recipe);
		if (count <= 0 || buildTime <= 0.0f || !(speed > 0.0f) || !std::isfinite(speed))
			return false;

		outRate = static_cast<float>(count) / buildTime * speed * 60.0f;
		return true;
	}

	bool CrafterDetector::IsCrafter(AActor* actor)
	{
		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ACrCrafter::StaticClass());
	}

	bool CrafterDetector::GetCrafterInfo(AActor* actor, CrafterInfo& outInfo)
	{
		if (!IsCrafter(actor))
		{
			return false;
		}

		ACrCrafter* crafter = static_cast<ACrCrafter*>(actor);

		outInfo.crafterName = actor->GetName();

		// Resolve localized building name from PlacementData if available
		ACrBuildingActorBase* building = static_cast<ACrBuildingActorBase*>(actor);
		UCrBuildingData* buildingData = Fields::BuildingDataOf(building);

		outInfo.buildingData = buildingData;

		if (!Fields::ReadBuildingTexts(buildingData, outInfo.crafterClass, outInfo.buildingDesc))
		{
			outInfo.crafterClass = actor->Class ? actor->Class->GetName() : "Unknown";
			outInfo.buildingDesc = "N/A";
		}

		// The game's own reason a building stands still for want of power; read
		// here, on the tick, and kept as a plain flag.
		outInfo.bNoPower = building->GetBuildingState() == ECrBuildingState::NoElectricity;

		outInfo.craftingSpeed = Fields::ValueOr<float>(Fields::SettingsSpeed, Fields::CraftingSettings.In(crafter), outInfo.craftingSpeed);

		// Crafting progress (0.0 - 1.0)
		outInfo.craftingProgress = crafter->GetItemCraftingProgress();

		// Recipe — read the first queued item from the CraftComponent
		outInfo.currentRecipe = "(idle)";

		UCrCraftingComponent* comp = Fields::ValueOr<UCrCraftingComponent*>(Fields::CraftComponent, crafter, nullptr);
		UAuItemRecipeData* activeRecipe = nullptr;

		if (comp)
		{
			const Fields::QueuedCraft first = Fields::FirstQueuedCraft(comp);

			if (first.recipe)
			{
				outInfo.recipeBuildTime = Fields::ValueOr<float>(Fields::RecipeBuildTime, first.recipe);
				outInfo.recipeOutputCount = first.outputCount > 0 ? first.outputCount : Fields::RecipeOutputCount(first.recipe);
				UAuItemDataBase* itemData = first.recipe->GetItemDataBase();
				if (itemData)
				{
					outInfo.currentRecipe = Fields::UniqueNameOf(itemData);
					outInfo.currentRecipeDisplayName = Fields::DisplayNameOf(itemData);
				}

				// Store typed recipe pointer for the output icon (UCrItemRecipeData extends UAuItemRecipeData)
				if (first.recipe->IsA(UCrItemRecipeData::StaticClass()))
					outInfo.recipeDataPtr = static_cast<UCrItemRecipeData*>(first.recipe);

				// Recipe inputs and output. The have-counts come from the crafter's
				// own input store, so a starved machine can be spotted without
				// opening it.
				UAuItemRecipeData* recipe = first.recipe;
				activeRecipe = recipe;

				std::unordered_map<std::string, int32_t> stock;
				const bool stockKnown = ReadInputStock(building, stock);

				const Fields::StructArray needed(Fields::NeededResources.In(recipe), Fields::RecipeOrderSize);
				outInfo.recipeInputs.reserve(static_cast<size_t>(needed.Num()));

				for (int32_t i = 0; i < needed.Num(); ++i)
				{
					const uint8_t* order = needed.At(i);
					RecipeItemEntry entry;
					FillEntry(entry, Fields::OrderItemClass(order), Fields::ValueOr<int32_t>(Fields::OrderCount, order));
					if (!entry.IsValid())
						continue;

					if (stockKnown && entry.itemData)
					{
						auto it = stock.find(Fields::UniqueNameOf(entry.itemData));
						entry.have = it != stock.end() ? it->second : 0;
					}

					outInfo.recipeInputs.push_back(std::move(entry));
				}

				FillEntry(outInfo.recipeOutput, Fields::RecipeOutputClass(recipe), outInfo.recipeOutputCount);
			}

			// pattern: UCrCraftingComponent::GetCraftingFragment
			// 48 89 5C 24 ?? 57 48 83 EC ?? 48 8B 99 ...
			auto fnGetFrag = Waila::Functions::GetCraftingFragment();
			if (fnGetFrag)
			{
				const FCrCraftingFragment* frag = fnGetFrag(comp);
				if (frag)
				{
					outInfo.craftingMultiplier = Fields::ValueOr<int32_t>(Fields::FragMultiplier, frag, outInfo.craftingMultiplier);
					outInfo.bMissingItems      = Fields::ValueOr<bool>(Fields::FragMissingItems, frag);
					outInfo.bOutputFull        = Fields::ValueOr<bool>(Fields::FragOutputFull, frag);

					// The live speed, as the rail card takes it; the settings value
					// above stands in when the fragment can't be read.
					const float liveSpeed = Fields::ValueOr<float>(Fields::FragSpeed, frag);
					if (liveSpeed > 0.0f)
						outInfo.craftingSpeed = liveSpeed;
				}
			}
		}

		float outRate = 0.0f;
		if (activeRecipe && CountRate(activeRecipe, outInfo.recipeOutputCount, outInfo.craftingSpeed, outRate))
			outInfo.outputPerMin = outRate;

		// What each ingredient costs a minute, once the live speed is known.
		for (RecipeItemEntry& entry : outInfo.recipeInputs)
		{
			float rate = 0.0f;
			if (activeRecipe && CountRate(activeRecipe, entry.need, outInfo.craftingSpeed, rate))
				entry.perMin = rate;
		}

		// Game thread only: this runs from the tick, and the card is built from
		// the same call.
		if (activeRecipe)
		{
			const auto now = ArrivalWindow::Clock::now();
			g_arrivals.Sample(outInfo.crafterName + '|' + outInfo.currentRecipe, outInfo.recipeInputs, now);
			for (size_t i = 0; i < outInfo.recipeInputs.size(); ++i)
				outInfo.recipeInputs[i].arrivalPerMin = g_arrivals.PerMin(i, now);
		}

		return true;
	}
}
