#include "waila_functions.h"
#include "plugin_helpers.h"

namespace Waila::Functions
{
    static FGetCraftingFragment             s_getCraftingFragment             = nullptr;
    static FGetStoredItemsContainerInternal s_getStoredItemsContainerInternal = nullptr;

    static constexpr const char* kGetCraftingFragmentPattern =
        "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B 99 ?? ?? ?? ?? ?? ?? ?? 48 8B B8 ?? ?? ?? ?? "
        "E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 48 89 44 24 ?? 74 ?? 48 85 C0 74 ?? 48 8B C8 "
        "E8 ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B CB FF D7 48 85 C0 74 ?? 48 8B C8 E8 ?? ?? ?? ?? "
        "48 85 C0 75 ?? 33 C0 48 8B 5C 24 ?? 48 83 C4 ?? 5F C3 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 48 83 EC";

    static constexpr const char* kGetStoredItemsContainerInternalPattern =
        "40 53 48 83 EC ?? 48 8B 81 ?? ?? ?? ?? 48 8B D9 48 85 C0 75 ?? E8 ?? ?? ?? ?? 48 8B C8";

    void Resolve(IPluginSelf* self, IPluginHookScanner* scanner)
    {
        if (!self || !scanner)
            return;

        // Optional: each backs one detector panel that already degrades on its
        // own, which is what the old scan-at-init path did. The loader still
        // lists a miss for the user.
        if (uintptr_t addr = scanner->ResolveOptional(
                self, "UCrCraftingComponent::GetCraftingFragment", kGetCraftingFragmentPattern))
            s_getCraftingFragment = reinterpret_cast<FGetCraftingFragment>(addr);
        else
            LOG_WARN("WailaFunctions: GetCraftingFragment unresolved");

        if (uintptr_t addr = scanner->ResolveOptional(
                self, "UCrBuildingItemStorageComponent::GetStoredItemsContainerInternal",
                kGetStoredItemsContainerInternalPattern))
            s_getStoredItemsContainerInternal = reinterpret_cast<FGetStoredItemsContainerInternal>(addr);
        else
            LOG_WARN("WailaFunctions: GetStoredItemsContainerInternal unresolved");
    }

    FGetCraftingFragment GetCraftingFragment()
    {
        return s_getCraftingFragment;
    }

    FGetStoredItemsContainerInternal GetStoredItemsContainerInternal()
    {
        return s_getStoredItemsContainerInternal;
    }
}
