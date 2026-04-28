#include "crafter_detector.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "AuCrafting_classes.hpp"
#include "AuItems_classes.hpp"

using namespace SDK;

namespace Waila
{
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
			}
		}

		return true;
	}
}
