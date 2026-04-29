#pragma once

#include <string>

// Forward declare to avoid pulling in heavy SDK headers in this header
namespace SDK { class AActor; }

namespace Waila
{
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

		// Returns true if this represents a valid crafter
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
