#pragma once

#include "plugin_interface.h"

namespace SDK
{
	class UClass;
	class UTexture2D;
	class UCrBuildingData;
	class UCrItemRecipeData;
	class UAuItemDataBase;
}

namespace Waila::UI
{
	// Resolves the game's own Slate icons to ImGui texture handles.
	//
	// WAILA already holds the exact building, recipe and item objects it needs
	// icons for, so unlike ProductionViewer there is no asset-registry sweep here
	// — each brush is resolved the first time it is asked for and cached by
	// texture pointer thereafter.
	//
	// Everything in here is game-thread only. Resolve during Tick and hand the
	// finished handles to the render thread inside the card model; never call
	// these from a render callback.
	namespace Icons
	{
		void Init();
		void Shutdown();

		// Retries textures the GPU wasn't ready for on an earlier frame.
		void Tick();

		PluginTextureHandle ForBuilding(SDK::UCrBuildingData* building);
		PluginTextureHandle ForRecipe(SDK::UCrItemRecipeData* recipe);
		PluginTextureHandle ForItem(SDK::UAuItemDataBase* item);

		// Recipe ingredient entries carry the item's class, not an instance —
		// resolves through the class default object.
		PluginTextureHandle ForItemClass(SDK::UClass* itemClass);
	}
}
