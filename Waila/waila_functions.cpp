#include "waila_functions.h"
#include "plugin_helpers.h"

namespace Waila::Functions
{
    static FGetCraftingFragment             s_getCraftingFragment             = nullptr;
    static FGetStoredItemsContainerInternal s_getStoredItemsContainerInternal = nullptr;
    static FMassActorHelperCtor             s_massActorHelperCtor             = nullptr;
    static FAddItemToCraft                  s_addItemToCraft                  = nullptr;

    void Init()
    {
        IPluginScanner* scanner = GetScanner();
        if (!scanner)
        {
            LOG_WARN("WailaFunctions::Init: scanner unavailable, function pointers will be null");
            return;
        }

        uintptr_t addr = scanner->FindPatternInMainModule(
            "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B 99 ?? ?? ?? ?? ?? ?? ?? 48 8B B8 ?? ?? ?? ?? "
            "E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 48 89 44 24 ?? 74 ?? 48 85 C0 74 ?? 48 8B C8 "
            "E8 ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B CB FF D7 48 85 C0 74 ?? 48 8B C8 E8 ?? ?? ?? ?? "
            "48 85 C0 75 ?? 33 C0 48 8B 5C 24 ?? 48 83 C4 ?? 5F C3 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 48 83 EC");
        if (addr)
        {
            s_getCraftingFragment = reinterpret_cast<FGetCraftingFragment>(addr);
            LOG_INFO("WailaFunctions: GetCraftingFragment found at 0x%llX", addr);
        }
        else
        {
            LOG_WARN("WailaFunctions: GetCraftingFragment pattern not found");
        }

        addr = scanner->FindPatternInMainModule(
            "40 53 48 83 EC ?? 48 8B 81 ?? ?? ?? ?? 48 8B D9 48 85 C0 75 ?? E8 ?? ?? ?? ?? 48 8B C8");
        if (addr)
        {
            s_getStoredItemsContainerInternal = reinterpret_cast<FGetStoredItemsContainerInternal>(addr);
            LOG_INFO("WailaFunctions: GetStoredItemsContainerInternal found at 0x%llX", addr);
        }
        else
        {
            LOG_WARN("WailaFunctions: GetStoredItemsContainerInternal pattern not found");
        }

        // FCrMassActorReplicationHelper::FCrMassActorReplicationHelper(AActor* InActor)
        addr = scanner->FindPatternInMainModule(
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 33 ED "
            "48 8D 05 ?? ?? ?? ?? ?? ?? ?? 48 8B DA");
        if (addr)
        {
            s_massActorHelperCtor = reinterpret_cast<FMassActorHelperCtor>(addr);
            LOG_INFO("WailaFunctions: MassActorHelperCtor found at 0x%llX", addr);
        }
        else
        {
            LOG_WARN("WailaFunctions: MassActorHelperCtor pattern not found");
        }

        // ACrPlayerControllerBase::AddItemToCraft(FCrMassActorReplicationHelper, UCrItemRecipeData*, int)
        addr = scanner->FindPatternInMainModule(
            "48 89 5C 24 ?? 48 89 74 24 ?? 55 57 41 54 41 55 41 56 48 8D 6C 24 ?? "
            "48 81 EC ?? ?? ?? ?? 80 3D");
        if (addr)
        {
            s_addItemToCraft = reinterpret_cast<FAddItemToCraft>(addr);
            LOG_INFO("WailaFunctions: AddItemToCraft found at 0x%llX", addr);
        }
        else
        {
            LOG_WARN("WailaFunctions: AddItemToCraft pattern not found");
        }
    }

    FGetCraftingFragment GetCraftingFragment()
    {
        return s_getCraftingFragment;
    }

    FGetStoredItemsContainerInternal GetStoredItemsContainerInternal()
    {
        return s_getStoredItemsContainerInternal;
    }

    FMassActorHelperCtor GetMassActorHelperCtor()
    {
        return s_massActorHelperCtor;
    }

    FAddItemToCraft GetAddItemToCraft()
    {
        return s_addItemToCraft;
    }
}
