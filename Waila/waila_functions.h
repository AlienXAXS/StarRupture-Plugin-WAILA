#pragma once

namespace SDK
{
    class UCrCraftingComponent;
    class UCrBuildingItemStorageComponent;
    class UWorld;
    struct FCrCraftingFragment;
    struct FCrItemsStorageContainer;
}

namespace Waila::Functions
{
    using FGetCraftingFragment =
        const SDK::FCrCraftingFragment* (__fastcall*)(SDK::UCrCraftingComponent*);

    using FGetStoredItemsContainerInternal =
        SDK::FCrItemsStorageContainer* (__fastcall*)(SDK::UCrBuildingItemStorageComponent*,
                                                      const SDK::UWorld*);

    void Init();

    FGetCraftingFragment             GetCraftingFragment();
    FGetStoredItemsContainerInternal GetStoredItemsContainerInternal();
}
