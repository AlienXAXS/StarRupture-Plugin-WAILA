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

#include <cmath>
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

		outInfo.craftingSpeed = Fields::ValueOr<float>(Fields::SettingsSpeed, Fields::CraftingSettings.In(crafter), outInfo.craftingSpeed);

		// Crafting progress (0.0 - 1.0)
		outInfo.craftingProgress = crafter->GetItemCraftingProgress();

		// Recipe — read the first queued item from the CraftComponent
		outInfo.currentRecipe = "(idle)";

		UCrCraftingComponent* comp = Fields::ValueOr<UCrCraftingComponent*>(Fields::CraftComponent, crafter, nullptr);

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

				}
			}
		}

		return true;
	}
}
