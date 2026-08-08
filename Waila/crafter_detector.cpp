#include "crafter_detector.h"
#include "waila_functions.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuCrafting_classes.hpp"
#include "AuCrafting_structs.hpp"
#include "AuItems_classes.hpp"

#include <string>
#include <unordered_map>

using namespace SDK;

namespace Waila
{
	namespace
	{
		// A recipe stores ingredients as item *classes*; everything worth showing
		// (icon, display name, unique name) lives on the class default object.
		UAuItemDataBase* ItemDefaultsOf(UClass* itemClass)
		{
			if (!itemClass)
				return nullptr;

			UObject* cdo = itemClass->ClassDefaultObject;
			if (!cdo)
				return nullptr;

			bool isItem = false;
			try { isItem = cdo->IsA(UAuItemDataBase::StaticClass()); }
			catch (...) { return nullptr; }

			return isItem ? static_cast<UAuItemDataBase*>(cdo) : nullptr;
		}

		void FillEntry(RecipeItemEntry& entry, UClass* itemClass, int32_t count)
		{
			entry.itemClass = itemClass;
			entry.need      = count;
			entry.itemData  = ItemDefaultsOf(itemClass);

			if (entry.itemData)
				entry.displayName = UKismetTextLibrary::Conv_TextToString(entry.itemData->ItemName).ToString();
		}

		// Counts what is actually sitting in the crafter's input store, keyed by
		// the item's unique name so ingredients can be matched against it.
		// Returns false when the store could not be read, which is the difference
		// between "0 of these" and "we don't know".
		bool ReadInputStock(ACrBuildingActorBase* building,
		                    std::unordered_map<std::string, int32_t>& outStock)
		{
			if (!building || !building->InItemStorage)
				return false;

			auto fnGetContainer = Waila::Functions::GetStoredItemsContainerInternal();
			if (!fnGetContainer)
				return false;

			UWorld* world = UWorld::GetWorld();
			if (!world)
				return false;

			FCrItemsStorageContainer* container = fnGetContainer(building->InItemStorage, world);
			if (!container)
				return false;

			for (int i = 0; i < container->Items.Num(); ++i)
			{
				const FCrStorageItem& slot = container->Items[i];
				if (slot.bIsDisabled || slot.Item.Count <= 0)
					continue;
				if (!slot.Item.ItemDataBase || !UKismetSystemLibrary::IsValid(slot.Item.ItemDataBase))
					continue;

				outStock[slot.Item.ItemDataBase->UniqueItemName.ToString()] += slot.Item.Count;
			}

			return true;
		}
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
		UCrBuildingData* buildingData = (building->PlacementData && building->PlacementData->IsA(UCrBuildingData::StaticClass()))
			? static_cast<UCrBuildingData*>(building->PlacementData)
			: nullptr;

		outInfo.buildingData = buildingData;

		if (buildingData)
		{
			outInfo.crafterClass = SDK::UKismetTextLibrary::Conv_TextToString(buildingData->BuildingName).ToString();
			outInfo.buildingDesc = SDK::UKismetTextLibrary::Conv_TextToString(buildingData->BuildingDescription).ToString();
		}
		else
		{
			outInfo.crafterClass = actor->Class ? actor->Class->GetName() : "Unknown";
			outInfo.buildingDesc = "N/A";
		}

		outInfo.craftingSpeed = crafter->CraftingSettings.CraftingSpeed;

		// Crafting progress (0.0 - 1.0)
		outInfo.craftingProgress = crafter->GetItemCraftingProgress();

		// Recipe — read the first queued item from the CraftComponent
		outInfo.currentRecipe = "(idle)";

		UCrCraftingComponent* comp = crafter->CraftComponent;

		if (comp)
		{
			const TArray<FAuCraftItem>& items = comp->ItemsToCraft;

			if (items.Num() > 0 && items[0].RecipeData)
			{
				outInfo.recipeBuildTime = items[0].RecipeData->BuildTime;
				outInfo.recipeOutputCount = items[0].OutputItem.Count > 0 ? items[0].OutputItem.Count : items[0].RecipeData->OutputItem.Count;
				UAuItemDataBase* itemData = items[0].RecipeData->GetItemDataBase();
				if (itemData)
				{
					outInfo.currentRecipe = itemData->UniqueItemName.ToString();
					outInfo.currentRecipeDisplayName = SDK::UKismetTextLibrary::Conv_TextToString(itemData->ItemName).ToString();
				}

				// Store typed recipe pointer for clipboard use (UCrItemRecipeData extends UAuItemRecipeData)
				if (items[0].RecipeData->IsA(UCrItemRecipeData::StaticClass()))
					outInfo.recipeDataPtr = static_cast<UCrItemRecipeData*>(items[0].RecipeData);

				// Recipe inputs and output. The have-counts come from the crafter's
				// own input store, so a starved machine can be spotted without
				// opening it.
				UAuItemRecipeData* recipe = items[0].RecipeData;

				std::unordered_map<std::string, int32_t> stock;
				const bool stockKnown = ReadInputStock(building, stock);

				const TArray<FAuItemRecipeOrder>& needed = recipe->NeededResources;
				outInfo.recipeInputs.reserve(static_cast<size_t>(needed.Num()));

				for (int i = 0; i < needed.Num(); ++i)
				{
					RecipeItemEntry entry;
					FillEntry(entry, needed[i].Item, needed[i].Count);
					if (!entry.IsValid())
						continue;

					if (stockKnown && entry.itemData)
					{
						auto it = stock.find(entry.itemData->UniqueItemName.ToString());
						entry.have = it != stock.end() ? it->second : 0;
					}

					outInfo.recipeInputs.push_back(std::move(entry));
				}

				FillEntry(outInfo.recipeOutput, recipe->OutputItem.Item, outInfo.recipeOutputCount);
			}

			// pattern: UCrCraftingComponent::GetCraftingFragment
			// 48 89 5C 24 ?? 57 48 83 EC ?? 48 8B 99 ...
			auto fnGetFrag = Waila::Functions::GetCraftingFragment();
			if (fnGetFrag)
			{
				const FCrCraftingFragment* frag = fnGetFrag(comp);
				if (frag)
				{
					outInfo.craftingMultiplier = frag->CraftingMultiplier;
					outInfo.bMissingItems      = frag->bIsMissingItems;
					outInfo.bOutputFull        = frag->bOutputFull;
				}
			}
		}

		outInfo.actorPtr = actor;
		return true;
	}
}
