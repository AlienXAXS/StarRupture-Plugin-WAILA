#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward declare to avoid pulling in heavy SDK headers in this header
namespace SDK { class AActor; class UClass; class UCrItemRecipeData; class UAuItemDataBase; class UCrBuildingData; }

namespace Waila
{
	// One side of a recipe — an ingredient the crafter consumes, or the item it
	// produces. The class pointer is what the recipe actually stores; itemData is
	// its resolved default object, kept so callers can reach the icon and name
	// without repeating the lookup.
	struct RecipeItemEntry
	{
		SDK::UClass*          itemClass = nullptr;
		SDK::UAuItemDataBase* itemData  = nullptr;
		std::string           displayName;
		int32_t               need = 0;
		int32_t               have = -1;   // -1 when the input store could not be read

		bool IsValid() const { return itemClass != nullptr || itemData != nullptr; }
	};

	struct CrafterInfo
	{
		std::string crafterName;             // Actor display name (internal, not shown in UI)
		std::string crafterClass;            // Localized building name (e.g. "Furnace")
		std::string buildingDesc;            // Localized building description
		std::string currentRecipe;           // UniqueItemName of first queued recipe, or "(idle)"
		std::string currentRecipeDisplayName;// Localized item display name
		float craftingProgress = 0.f;        // 0.0 - 1.0
		float craftingSpeed = 1.f;           // CraftingSettings.CraftingSpeed multiplier
		float recipeBuildTime = 0.f;         // UAuItemRecipeData::BuildTime in seconds
		int32_t recipeOutputCount = 1;       // Items produced per cycle
		int32_t craftingMultiplier = 1;      // FCrCraftingFragment::CraftingMultiplier
		bool    bMissingItems = false;       // FCrCraftingFragment::bIsMissingItems
		bool    bOutputFull   = false;       // FCrCraftingFragment::bOutputFull

		// Recipe inputs and output, in the order the recipe declares them.
		// Empty while the crafter is idle.
		std::vector<RecipeItemEntry> recipeInputs;
		RecipeItemEntry              recipeOutput;

		// Populated for clipboard/paste use — null when building is idle
		SDK::UCrItemRecipeData* recipeDataPtr = nullptr;

		// Building definition the icon in the ring centre comes from. Null when
		// PlacementData is missing or is not a building definition.
		SDK::UCrBuildingData*   buildingData  = nullptr;

		bool IsValid() const
		{
			return !crafterName.empty();
		}
	};

	class CrafterDetector
	{
	public:
		// Returns true if actor is or inherits from ACrCrafter
		static bool IsCrafter(SDK::AActor* actor);

		// Fills outInfo if actor is a crafter. Returns false if not a crafter.
		static bool GetCrafterInfo(SDK::AActor* actor, CrafterInfo& outInfo);
	};
}
