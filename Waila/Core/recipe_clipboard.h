#pragma once

#include <string>

namespace SDK { class UCrItemRecipeData; }

namespace Waila
{
	struct RecipeClipboard
	{
		std::string             buildingClass;     // crafterClass string for type-matching
		std::string             recipeDisplayName; // for HUD display
		SDK::UCrItemRecipeData* recipeData = nullptr;

		bool IsValid() const { return !buildingClass.empty() && recipeData != nullptr; }
		void Clear()         { buildingClass.clear(); recipeDisplayName.clear(); recipeData = nullptr; }
	};
}
