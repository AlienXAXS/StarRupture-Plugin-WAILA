#include "waila_functions.h"
#include "plugin_helpers.h"
#include "waila_fields.h"

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
        "40 53 48 83 EC 50 48 8B 81 A8 00 00 00 48 8B D9 48 85 C0 75 ?? E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 48 8B 8B 50 02 00 00";

    void Resolve(IPluginSelf* self, IPluginHookScanner* scanner)
    {
        if (!self || !scanner)
            return;

        Features::Declare(self, scanner);

        // Optional: each backs one detector panel that already degrades on its
        // own, which is what the old scan-at-init path did. The loader still
        // lists a miss for the user.
        // Both addresses are called directly with a component `this` in RCX, so
        // both are function starts: the loader checks the match against the
        // executable's exception directory rather than trusting that a prologue
        // shape lined up somewhere. Calling an address that merely looked like a
        // function is the failure this is here to prevent.
        if (uintptr_t addr = PluginMap::Function(scanner, self,
                Fields::Keys::GetCraftingFragment, kGetCraftingFragmentPattern,
                PLUGIN_SCAN_FUNCTION_START, PLUGIN_SCAN_FLAG_OPTIONAL))
            s_getCraftingFragment = reinterpret_cast<FGetCraftingFragment>(addr);
        else
            LOG_WARN("WailaFunctions: GetCraftingFragment unresolved");

        if (uintptr_t addr = PluginMap::Function(scanner, self,
                Fields::Keys::GetStoredItemsContainerInternal, kGetStoredItemsContainerInternalPattern, PLUGIN_SCAN_FUNCTION_START, PLUGIN_SCAN_FLAG_OPTIONAL))
            s_getStoredItemsContainerInternal = reinterpret_cast<FGetStoredItemsContainerInternal>(addr);
        else
            LOG_WARN("WailaFunctions: GetStoredItemsContainerInternal unresolved");

        Fields::Resolve(self, scanner);
        RailMass::Resolve(self, scanner);
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

namespace Waila::Fields
{
    bool Resolve(IPluginSelf* self, IPluginHookScanner* scanner)
    {
        // Every key is asked even after a miss, so the log lists them all.
        bool all = PluginMap::ResolveFields(scanner, self,
            PlacementData, ItemStorage, InItemStorage, BuildingName, BuildingDesc, BuildingIcon,
            ItemName, UniqueItemName, ItemIcon, GridColumns, GridRows,
            StorageItems, SlotDisabled, SlotItem, SimpleItemCount, SimpleItemData,
            CraftComponent, CraftingSettings, SettingsSpeed, ItemsToCraft, NeededResources, RecipeOutputItem,
            RecipeBuildTime, RecipeIcon, QueuedRecipe, QueuedOutput, OrderItem, OrderCount,
            FragMultiplier, FragMissingItems, FragOutputFull, FragSelected, FragSpeed,
            SocketsData, Spline, SocketRef, SocketTransform,
            PackageTransportReplicator, SendingTime, SenderReceiversContainer, AllSenderReceiversData,
            ConnectionsContainer, ConnectionsData, ConnectionSender, ConnectionReceiver, ConnectionAmount, ConnectionItem,
            ConnectionStartSendTime,
            HelperNetID, HelperEntity, PlacementSockets, SocketUseCount,
            SenderIsSender, SenderLocation, SenderItemType, SenderEntity);

        // The send start time is read as a float or a double, by the size the loader
        // reports. Any other size is not read: the features that list it are off.
        if (ConnectionStartSendTime.offset >= 0 && ConnectionStartSendTime.resolvedSize != sizeof(float)
            && ConnectionStartSendTime.resolvedSize != sizeof(double))
        {
            scanner->ReportFailure(self, ConnectionStartSendTime.key,
                ("StartSendServerTime is " + std::to_string(ConnectionStartSendTime.resolvedSize)
                    + " bytes on this build; WAILA reads a float or a double").c_str());
            ConnectionStartSendTime.offset = -1;
            all = false;
        }

        for (PluginFieldSpec* size : { &StorageItemSize, &CraftItemSize, &RecipeOrderSize, &SocketDataSize,
                                       &PlacementSocketSize, &SenderReceiverSize, &ConnectionDataSize, &HelperSize })
            all = ResolveSize(scanner, self, *size) && all;
        return all;
    }
}
