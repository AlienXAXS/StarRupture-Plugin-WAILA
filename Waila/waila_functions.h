#pragma once

#include <cstdint>

namespace SDK
{
    class UCrCraftingComponent;
    class UCrBuildingItemStorageComponent;
    class UWorld;
    class AActor;
    class ACrPlayerControllerBase;
    class UCrItemRecipeData;
    struct FCrCraftingFragment;
    struct FCrItemsStorageContainer;
    struct FCrMassActorReplicationHelper;
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

    // ── Recipe copy/paste patterns ───────────────────────────────────────────
    //
    // FCrMassActorReplicationHelper::FCrMassActorReplicationHelper(AActor* InActor)
    // Constructs a mass-entity handle from a raw actor pointer.
    // IDA: ??0FCrMassActorReplicationHelper@@QEAA@PEAVAActor@@@Z
    // AOB: "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 33 ED
    //       48 8D 05 ?? ?? ?? ?? ?? ?? ?? 48 8B DA"
    using FMassActorHelperCtor =
        SDK::FCrMassActorReplicationHelper* (__fastcall*)(SDK::FCrMassActorReplicationHelper* self,
                                                           SDK::AActor* actor);

    // ACrPlayerControllerBase::AddItemToCraft(FCrMassActorReplicationHelper, UCrItemRecipeData*, int)
    // Sets the crafting recipe on a building via the player controller RPC path.
    // Handles client->server routing internally; no manual clear needed.
    // AOB: "48 89 5C 24 ?? 48 89 74 24 ?? 55 57 41 54 41 55 41 56 48 8D 6C 24 ??
    //       48 81 EC ?? ?? ?? ?? 80 3D"
    using FAddItemToCraft =
        void (__fastcall*)(SDK::ACrPlayerControllerBase* pc,
                           SDK::FCrMassActorReplicationHelper helper,
                           SDK::UCrItemRecipeData* recipe,
                           int32_t count);

    void Init();

    FGetCraftingFragment             GetCraftingFragment();
    FGetStoredItemsContainerInternal GetStoredItemsContainerInternal();
    FMassActorHelperCtor             GetMassActorHelperCtor();
    FAddItemToCraft                  GetAddItemToCraft();
}
