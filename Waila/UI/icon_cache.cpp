#include "icon_cache.h"
#include "plugin_helpers.h"
#include "waila_fields.h"

#include "Engine_classes.hpp"
#include "Chimera_classes.hpp"
#include "AuItems_classes.hpp"
#include "SlateCore_structs.hpp"

using namespace SDK;

namespace Waila::UI
{
	namespace
	{
		IPluginImages* Images()
		{
			IPluginHooks* hooks = GetHooks();
			return hooks ? hooks->Images : nullptr;
		}

		// ResourceObject may be a UTexture2D, a material, or nothing at all; only
		// the first is something the image library can read.
		UTexture2D* BrushTexture(const FSlateBrush& brush)
		{
			UObject* resource = brush.ResourceObject;
			if (!resource || !UKismetSystemLibrary::IsValid(resource))
				return nullptr;

			bool isTexture = false;
			try { isTexture = resource->IsA(UTexture2D::StaticClass()); }
			catch (...) { return nullptr; }

			return isTexture ? static_cast<UTexture2D*>(resource) : nullptr;
		}

		// The library keeps no pointer, so the texture only has to be live for
		// this call.
		PluginImageId ResolveBrush(const FSlateBrush* brush)
		{
			IPluginImages* images = Images();
			if (!images || !brush)
				return 0;

			UTexture2D* tex = BrushTexture(*brush);
			return tex ? images->FromTexture(GetSelf(), tex) : 0;
		}
	}

	void Icons::Init()
	{
		IPluginImages* images = Images();
		if (!images)
		{
			// Server / generic build, or a loader without the image library, so
			// the cards draw without images.
			LOG_WARN("WAILA Icons: image library unavailable, icons disabled.");
			return;
		}

		images->UseSets(GetSelf(), PLUGIN_IMAGE_SET_ITEMS | PLUGIN_IMAGE_SET_BUILDINGS);
		LOG_INFO("WAILA Icons: ready.");
	}

	PluginImageId Icons::ForBuilding(UCrBuildingData* building)
	{
		if (!Images() || !building || !UKismetSystemLibrary::IsValid(building))
			return 0;
		return ResolveBrush(Fields::IconOf(building));
	}

	PluginImageId Icons::ForRecipe(UCrItemRecipeData* recipe)
	{
		if (!Images() || !recipe || !UKismetSystemLibrary::IsValid(recipe))
			return 0;

		// A recipe carries its own presentation icon; fall back to the item it
		// produces when the recipe brush is empty.
		if (PluginImageId id = ResolveBrush(Fields::IconOf(recipe)))
			return id;

		return ForItem(recipe->GetItemDataBase());
	}

	PluginImageId Icons::ForItem(UAuItemDataBase* item)
	{
		if (!Images() || !item || !UKismetSystemLibrary::IsValid(item))
			return 0;
		return ResolveBrush(Fields::IconOf(item));
	}

	PluginImageId Icons::ForItemClass(UClass* itemClass)
	{
		if (!Images() || !itemClass)
			return 0;

		UObject* cdo = itemClass->ClassDefaultObject;
		if (!cdo)
			return 0;

		bool isItem = false;
		try { isItem = cdo->IsA(UAuItemDataBase::StaticClass()); }
		catch (...) { return 0; }

		return isItem ? ForItem(static_cast<UAuItemDataBase*>(cdo)) : 0;
	}
}
