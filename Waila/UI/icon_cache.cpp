#include "icon_cache.h"
#include "plugin_helpers.h"
#include "waila_fields.h"

#include "Engine_classes.hpp"
#include "Chimera_classes.hpp"
#include "AuItems_classes.hpp"
#include "SlateCore_structs.hpp"

#include "object_ref.h"

#include <cstdint>
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

		struct Entry
		{
			// Nothing keeps the texture's package loaded, so it can be freed
			// between ticks: read it only through Get(). The handle is the
			// loader's own copy and outlives the texture.
			ObjectRef<UTexture2D> tex;

			// Null means "seen, not loadable yet" — the slot stays in g_pending
			// until a later Tick manages to register it.
			PluginTextureHandle handle = nullptr;
		};

		// Keyed by the texture's GObjects slot rather than its address. The slot
		// number is only an identity here and is never turned back into a pointer.
		std::unordered_map<int32_t, Entry> g_entries;
		std::unordered_set<int32_t>        g_pending;

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

			if (g_entries.size() >= kMaxTextures)
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

		// tex must be live this tick.
		PluginTextureHandle Resolve(UTexture2D* tex)
		{
			if (!g_available || !tex)
				return nullptr;

			const ObjectRef<UTexture2D> ref(tex);
			const int32_t               slot = tex->Index;

			auto it = g_entries.find(slot);
			if (it != g_entries.end())
			{
				if (it->second.tex == ref)
					return it->second.handle;

				// The texture this slot held was freed and another object took
				// it; the cached icon is the old texture's.
				if (it->second.handle)
				{
					if (IPluginImGuiTextures* textures = Textures())
						textures->FreeTexture(it->second.handle);
				}
				g_pending.erase(slot);
				g_entries.erase(it);
			}

			PluginTextureHandle handle = TryLoad(tex);
			g_entries.emplace(slot, Entry{ ref, handle });
			if (!handle)
				g_pending.insert(slot);

			return handle;
		}

		PluginTextureHandle ResolveBrush(const FSlateBrush* brush)
		{
			return brush ? Resolve(BrushTexture(*brush)) : nullptr;
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
			for (auto& [slot, entry] : g_entries)
			{
				if (entry.handle)
					textures->FreeTexture(entry.handle);
			}
		}

		g_entries.clear();
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
			auto entry = g_entries.find(*it);

			// A texture the game freed while it waited just loses its icon.
			UTexture2D* tex = entry != g_entries.end() ? entry->second.tex.Get() : nullptr;
			if (!tex)
			{
				if (entry != g_entries.end())
					g_entries.erase(entry);
				it = g_pending.erase(it);
				continue;
			}

			if (PluginTextureHandle handle = TryLoad(tex))
			{
				entry->second.handle = handle;
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
		return ResolveBrush(Fields::IconOf(building));
	}

	PluginTextureHandle Icons::ForRecipe(UCrItemRecipeData* recipe)
	{
		if (!g_available || !recipe || !UKismetSystemLibrary::IsValid(recipe))
			return nullptr;

		// A recipe carries its own presentation icon; fall back to the item it
		// produces when the recipe brush is empty.
		if (PluginTextureHandle handle = ResolveBrush(Fields::IconOf(recipe)))
			return handle;

		return ForItem(recipe->GetItemDataBase());
	}

	PluginTextureHandle Icons::ForItem(UAuItemDataBase* item)
	{
		if (!g_available || !item || !UKismetSystemLibrary::IsValid(item))
			return nullptr;
		return ResolveBrush(Fields::IconOf(item));
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
