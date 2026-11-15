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
	// Names the game's own icons to the loader's image library.
	//
	// WAILA already holds the exact building, recipe and item objects it needs
	// icons for, so each brush's texture is handed to the library the moment it
	// is read and only the library's integer id is kept. The render callback
	// turns that id into a texture with hooks->Images->GetSized.
	//
	// Game thread only. Call these during Tick when the card model is built,
	// never from a render callback. 0 means no image.
	namespace Icons
	{
		// Tells the library which sets of images WAILA draws from.
		void Init();

		PluginImageId ForBuilding(SDK::UCrBuildingData* building);
		PluginImageId ForRecipe(SDK::UCrItemRecipeData* recipe);
		PluginImageId ForItem(SDK::UAuItemDataBase* item);

		// Recipe ingredient entries carry the item's class, not an instance —
		// resolves through the class default object.
		PluginImageId ForItemClass(SDK::UClass* itemClass);
	}
}
