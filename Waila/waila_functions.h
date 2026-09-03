#pragma once

#include <cstdint>

struct IPluginSelf;
struct IPluginHookScanner;

namespace SDK
{
    class UCrCraftingComponent;
    class UCrBuildingItemStorageComponent;
    class UWorld;
    class AActor;
    struct FCrCraftingFragment;
    struct FCrItemsStorageContainer;
}

namespace Waila::Functions
{
    // ── Existing patterns ────────────────────────────────────────────────────
    //
    // UCrCraftingComponent::GetCraftingFragment(UCrCraftingComponent*)
    // Returns FCrCraftingFragment* with multiplier/missing/outputFull flags.
    // AOB: "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B 99 ..."
    using FGetCraftingFragment =
        const SDK::FCrCraftingFragment* (__fastcall*)(SDK::UCrCraftingComponent*);

    // UCrBuildingItemStorageComponent::GetStoredItemsContainerInternal(UWorld*)
    // Returns FCrItemsStorageContainer* for reading stored item arrays.
    // AOB: "40 53 48 83 EC ?? 48 8B 81 ?? ?? ?? ?? 48 8B D9 48 85 C0 75 ??"
    using FGetStoredItemsContainerInternal =
        SDK::FCrItemsStorageContainer* (__fastcall*)(SDK::UCrBuildingItemStorageComponent*,
                                                      const SDK::UWorld*);

    // Resolve both function pointers. Callable only from the plugin's
    // OnPluginLoadHooks export -- the loader refuses scans made anywhere else.
    // Replaces the old Init(), which scanned from PluginInit.
    void Resolve(IPluginSelf* self, IPluginHookScanner* scanner);

    FGetCraftingFragment             GetCraftingFragment();
    FGetStoredItemsContainerInternal GetStoredItemsContainerInternal();
}
