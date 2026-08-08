#include "icon_cache.h"
#include "plugin_helpers.h"

#include "Engine_classes.hpp"
#include "Chimera_classes.hpp"
#include "AuItems_classes.hpp"
#include "SlateCore_structs.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace SDK;

namespace Waila::UI
{
	namespace
	{
		// The ModLoader's texture table is finite (4096 slots shared by every
		// plugin). WAILA only ever shows what the player is looking at, so a few
		// hundred is the realistic ceiling — the cap is here to make a runaway
		// loop fail loudly instead of exhausting the table.
		constexpr size_t kMaxTextures      = 512;
		constexpr int    kRetriesPerTick   = 8;

		bool g_available = false;
		bool g_capWarned = false;

		// Null value means "seen, not loadable yet" — the pointer stays in
		// g_pending until a later Tick manages to register it.
		std::unordered_map<UTexture2D*, PluginTextureHandle> g_handles;
		std::unordered_set<UTexture2D*>                      g_pending;

		IPluginImGuiTextures* Textures()
		{
			IPluginHooks* hooks = GetHooks();
			return hooks ? hooks->ImGuiTextures : nullptr;
		}

		// ResourceObject may be a UTexture2D, a material, or nothing at all; only
		// the first is something ImGui can sample.
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

		PluginTextureHandle TryLoad(UTexture2D* tex)
		{
			IPluginImGuiTextures* textures = Textures();
			if (!textures || !tex)
				return nullptr;

			if (g_handles.size() >= kMaxTextures)
			{
				if (!g_capWarned)
				{
					g_capWarned = true;
					LOG_WARN("WAILA Icons: texture cache hit its %zu entry cap — new icons will not load.",
						kMaxTextures);
				}
				return nullptr;
			}

			// Returns null rather than throwing when D3D12 isn't ready for the
			// resource yet; the caller keeps it pending and retries.
			const std::string name = tex->GetName();
			return textures->LoadFromUTexture2D(tex, name.c_str());
		}

		PluginTextureHandle Resolve(UTexture2D* tex)
		{
			if (!g_available || !tex)
				return nullptr;

			auto it = g_handles.find(tex);
			if (it != g_handles.end())
				return it->second;

			PluginTextureHandle handle = TryLoad(tex);
			g_handles.emplace(tex, handle);
			if (!handle)
				g_pending.insert(tex);

			return handle;
		}

		PluginTextureHandle ResolveBrush(const FSlateBrush& brush)
		{
			return Resolve(BrushTexture(brush));
		}
	}

	void Icons::Init()
	{
		if (!Textures())
		{
			// Server / generic build has no texture interface — stay dark rather
			// than retrying a lookup that can never succeed.
			LOG_WARN("WAILA Icons: ImGui texture hooks unavailable — icons disabled.");
			g_available = false;
			return;
		}

		g_available = true;
		g_capWarned = false;
		LOG_INFO("WAILA Icons: ready.");
	}

	void Icons::Shutdown()
	{
		if (IPluginImGuiTextures* textures = Textures())
		{
			for (auto& [tex, handle] : g_handles)
			{
				if (handle)
					textures->FreeTexture(handle);
			}
		}

		g_handles.clear();
		g_pending.clear();
		g_available = false;
		g_capWarned = false;
	}

	void Icons::Tick()
	{
		if (!g_available || g_pending.empty())
			return;

		// Budgeted so a large backlog costs a predictable amount per frame.
		int budget = kRetriesPerTick;
		for (auto it = g_pending.begin(); it != g_pending.end() && budget > 0; --budget)
		{
			UTexture2D* tex = *it;

			bool alive = false;
			try { alive = tex && UKismetSystemLibrary::IsValid(tex); }
			catch (...) { alive = false; }

			if (!alive)
			{
				g_handles.erase(tex);
				it = g_pending.erase(it);
				continue;
			}

			if (PluginTextureHandle handle = TryLoad(tex))
			{
				g_handles[tex] = handle;
				it = g_pending.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	PluginTextureHandle Icons::ForBuilding(UCrBuildingData* building)
	{
		if (!g_available || !building || !UKismetSystemLibrary::IsValid(building))
			return nullptr;
		return ResolveBrush(building->Icon);
	}

	PluginTextureHandle Icons::ForRecipe(UCrItemRecipeData* recipe)
	{
		if (!g_available || !recipe || !UKismetSystemLibrary::IsValid(recipe))
			return nullptr;

		// A recipe carries its own presentation icon; fall back to the item it
		// produces when the recipe brush is empty.
		if (PluginTextureHandle handle = ResolveBrush(recipe->Icon))
			return handle;

		return ForItem(recipe->GetItemDataBase());
	}

	PluginTextureHandle Icons::ForItem(UAuItemDataBase* item)
	{
		if (!g_available || !item || !UKismetSystemLibrary::IsValid(item))
			return nullptr;
		return ResolveBrush(item->ItemIcon);
	}

	PluginTextureHandle Icons::ForItemClass(UClass* itemClass)
	{
		if (!g_available || !itemClass)
			return nullptr;

		UObject* cdo = itemClass->ClassDefaultObject;
		if (!cdo)
			return nullptr;

		bool isItem = false;
		try { isItem = cdo->IsA(UAuItemDataBase::StaticClass()); }
		catch (...) { return nullptr; }

		return isItem ? ForItem(static_cast<UAuItemDataBase*>(cdo)) : nullptr;
	}
}
