#pragma once

#include "plugin_interface.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuActorPlacement_classes.hpp"
#include "AuCrafting_classes.hpp"
#include "AuCrafting_structs.hpp"
#include "AuItems_classes.hpp"
#include "BP_CoolerActive_classes.hpp"
#include "BP_CoolerPassive_classes.hpp"
#include "BP_PackageSender_classes.hpp"
#include "plugin_map.h"
#include "plugin_helpers.h"
#include "rail_mass.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// Every member of a game class WAILA reads, taken from the loader's map for the
// running game build (see plugin_map.h). Resolved once in OnPluginLoadHooks by
// Waila::Functions::Resolve; read on the game thread with In(object), which is
// null when the object is null.
//
// A struct WAILA steps through as an array's element is stepped by the size the
// loader reports for it on the running build (StructSize, StructArray), and
// every member read inside it is a field of its own. No game offset, stride or
// struct size is compiled in.

#define WAILA_FIELD(Class, Member) SR_FIELD(decltype(SDK::Class::Member), Class, Member, 0)

namespace Waila::Fields
{
	// Buildings
	inline auto PlacementData    = WAILA_FIELD(ACrBuildingActorBase, PlacementData);
	inline auto ItemStorage      = WAILA_FIELD(ACrBuildingActorBase, ItemStorage);
	inline auto InItemStorage    = WAILA_FIELD(ACrBuildingActorBase, InItemStorage);
	inline auto BuildingName     = WAILA_FIELD(UCrBuildingData, BuildingName);
	inline auto BuildingDesc     = WAILA_FIELD(UCrBuildingData, BuildingDescription);
	inline auto BuildingIcon     = WAILA_FIELD(UCrBuildingData, Icon);

	// Items
	inline auto ItemName         = WAILA_FIELD(UAuItemDataBase, ItemName);
	inline auto UniqueItemName   = WAILA_FIELD(UAuItemDataBase, UniqueItemName);
	inline auto GridColumns      = WAILA_FIELD(UCrBuildingItemStorageComponent, GridColumns);
	inline auto GridRows         = WAILA_FIELD(UCrBuildingItemStorageComponent, GridRows);
	inline auto ItemIcon         = WAILA_FIELD(UAuItemDataBase, ItemIcon);
	inline auto StorageItems     = WAILA_FIELD(FCrItemsStorageContainer, Items);
	inline auto SlotDisabled     = WAILA_FIELD(FCrStorageItem, bIsDisabled);
	inline auto SlotItem         = WAILA_FIELD(FCrStorageItem, Item);
	inline auto SimpleItemCount  = WAILA_FIELD(FAuSimpleItem, Count);
	inline auto SimpleItemData   = WAILA_FIELD(FAuSimpleItem, ItemDataBase);

	// Crafters
	inline auto CraftComponent   = WAILA_FIELD(ACrCrafter, CraftComponent);
	inline auto CraftingSettings = WAILA_FIELD(ACrCrafter, CraftingSettings);
	inline auto ItemsToCraft     = WAILA_FIELD(UCrCraftingComponent, ItemsToCraft);
	inline auto NeededResources  = WAILA_FIELD(UAuItemRecipeData, NeededResources);
	inline auto RecipeOutputItem = WAILA_FIELD(UAuItemRecipeData, OutputItem);
	inline auto RecipeBuildTime  = WAILA_FIELD(UAuItemRecipeData, BuildTime);
	inline auto RecipeIcon       = WAILA_FIELD(UCrItemRecipeData, Icon);
	inline auto SettingsSpeed    = WAILA_FIELD(FCrCraftingSettings, CraftingSpeed);
	inline auto QueuedRecipe     = WAILA_FIELD(FAuCraftItem, RecipeData);
	inline auto QueuedOutput     = WAILA_FIELD(FAuCraftItem, OutputItem);
	inline auto OrderItem        = WAILA_FIELD(FAuItemRecipeOrder, Item);
	inline auto OrderCount       = WAILA_FIELD(FAuItemRecipeOrder, Count);
	inline auto FragMultiplier   = WAILA_FIELD(FCrCraftingFragment, CraftingMultiplier);
	inline auto FragMissingItems = WAILA_FIELD(FCrCraftingFragment, bIsMissingItems);
	inline auto FragOutputFull   = WAILA_FIELD(FCrCraftingFragment, bOutputFull);
	inline auto FragSelected     = WAILA_FIELD(FCrCraftingFragment, SelectedRecipe);
	inline auto FragSpeed        = WAILA_FIELD(FCrCraftingFragment, CraftingSpeed);

	// Drone rails
	inline auto SocketsData      = WAILA_FIELD(ACrDronePathPoint, SocketsData);
	inline auto Spline           = WAILA_FIELD(ACrBuildingSplineActorBase, Spline);
	inline auto SocketRef        = WAILA_FIELD(FBuildingSocketData, Socket);
	inline auto SocketTransform  = WAILA_FIELD(FBuildingSocketData, WorldTransform);

	// Package transport. SendingTime has no member in the Game-SDK, so it is a
	// native field: the loader's row for the running build, or none and the
	// plugin is refused.
	inline auto PackageTransportReplicator = WAILA_FIELD(ACrGameStateBase, PackageTransportReplicator);
	inline auto SendingTime = PluginField<float>{ { "ACrPackageTransportReplicator::SendingTime",
		"ACrPackageTransportReplicator", "SendingTime", PLUGIN_FIELD_NATIVE, sizeof(float), 0x2C0 } };
	inline auto SenderReceiversContainer = WAILA_FIELD(ACrPackageTransportReplicator, SenderReceiversContainer);
	inline auto AllSenderReceiversData   = WAILA_FIELD(FCrSenderReceiversContainer, AllSenderReceiversData);
	inline auto ConnectionsContainer     = WAILA_FIELD(ACrPackageTransportReplicator, ConnectionsContainer);
	inline auto ConnectionsData          = WAILA_FIELD(FCrPackageTransportConnectionsContainer, ConnectionsData);
	inline auto ConnectionSender         = WAILA_FIELD(FCrPackageTransportConnectionData, Sender);
	inline auto ConnectionReceiver       = WAILA_FIELD(FCrPackageTransportConnectionData, Receiver);
	inline auto ConnectionAmount         = WAILA_FIELD(FCrPackageTransportConnectionData, RequestedAmount);
	inline auto ConnectionItem           = WAILA_FIELD(FCrPackageTransportConnectionData, Item);
	// The send start time is a float on one game build and a double on another, so it
	// is asked for with no size and read by the size the loader reports (see
	// StartSendTimeOf).
	inline auto ConnectionStartSendTime  = SR_FIELD_ANY_SIZE(FCrPackageTransportConnectionData, StartSendServerTime, 0);
	inline auto HelperNetID              = WAILA_FIELD(FCrMassEntityReplicationHelper, NetID);
	inline auto HelperEntity             = WAILA_FIELD(FCrMassEntityReplicationHelper, Entity);
	inline auto PlacementSockets         = WAILA_FIELD(UAuActorPlacementSocketsComponent, Sockets);
	inline auto SocketUseCount           = WAILA_FIELD(FAuActorPlacementSingleSocketData, UseCount);
	inline auto SenderIsSender           = WAILA_FIELD(FCrSenderReceiverData, bIsSender);
	inline auto SenderLocation           = WAILA_FIELD(FCrSenderReceiverData, Location);
	inline auto SenderItemType           = WAILA_FIELD(FCrSenderReceiverData, ItemType);
	inline auto SenderEntity             = WAILA_FIELD(FCrSenderReceiverData, Entity);

	// Blueprint (_C) members have no compiled offset: found by name on first use.
	inline PluginLazyField<SDK::UAuActorPlacementSocketsComponent*> ActiveCoolerSockets{ "AuActorPlacementSockets" };
	inline PluginLazyField<SDK::UAuActorPlacementSocketsComponent*> PassiveCoolerSockets{ "AuActorPlacementSockets" };
	inline PluginLazyField<bool> SenderCanSend{ "CanSend" };

	// The size of each struct WAILA steps through as an array's element, as the
	// loader reports it for the running build. The row is filled in by Resolve;
	// size 0 means it did not resolve, and the array is then read as empty.
	inline PluginFieldSpec StructSize(const char* name)
	{
		return PluginFieldSpec{ name, name, nullptr, PLUGIN_FIELD_FLAG_NONE, 0, -1 };
	}

	inline auto StorageItemSize        = StructSize("FCrStorageItem");
	inline auto CraftItemSize          = StructSize("FAuCraftItem");
	inline auto RecipeOrderSize        = StructSize("FAuItemRecipeOrder");
	inline auto SocketDataSize         = StructSize("FBuildingSocketData");
	inline auto PlacementSocketSize    = StructSize("FAuActorPlacementSingleSocketData");
	inline auto SenderReceiverSize     = StructSize("FCrSenderReceiverData");
	inline auto ConnectionDataSize     = StructSize("FCrPackageTransportConnectionData");
	inline auto HelperSize             = StructSize("FCrMassEntityReplicationHelper");

	// The map keys of the two game functions WAILA calls, so the resolves and the
	// feature declarations below spell each one once.
	namespace Keys
	{
		inline constexpr const char* GetCraftingFragment = "UCrCraftingComponent::GetCraftingFragment";
		inline constexpr const char* GetStoredItemsContainerInternal =
			"UCrBuildingItemStorageComponent::GetStoredItemsContainerInternal";
	}

	// The SDK's helpers, under the names the detectors use.
	using PluginMap::KeysOf;
	using PluginMap::ValueOr;

	// Asks the loader for every row above. False when any did not resolve; the
	// loader has then recorded the failure, and turns off the feature that
	// lists the key (or refuses the plugin, for a key no feature lists).
	bool Resolve(IPluginSelf* self, IPluginHookScanner* scanner);

	// Asks the loader for a StructSize row's size. False when it did not resolve.
	inline bool ResolveSize(IPluginHookScanner* scanner, const IPluginSelf* self, PluginFieldSpec& row)
	{
		PluginFieldRequest request = PLUGIN_FIELD_REQUEST_INIT;
		request.key       = row.key;
		request.className = row.className;
		request.builtCL   = PLUGIN_GAME_BUILT_CL;
		PluginFieldResult result{};
		if (!scanner->ResolveField(self, &request, &result))
			return false;
		row.size   = result.size;
		row.offset = 0;
		return true;
	}

	// The server time a connection's latest send started, in seconds, read as the
	// float or double the loader reports the member to be. 0 when it did not
	// resolve (or Resolve found a size it cannot read), the same as no send yet.
	inline double StartSendTimeOf(const void* connection)
	{
		if (!connection || ConnectionStartSendTime.offset < 0)
			return 0.0;
		const uint8_t* at = static_cast<const uint8_t*>(connection) + ConnectionStartSendTime.offset;
		if (ConnectionStartSendTime.resolvedSize == sizeof(float))
		{
			float value;
			std::memcpy(&value, at, sizeof(value));
			return value;
		}
		if (ConnectionStartSendTime.resolvedSize == sizeof(double))
		{
			double value;
			std::memcpy(&value, at, sizeof(value));
			return value;
		}
		return 0.0;
	}

	// The elements of a game TArray of structs, stepped by the loader's size for
	// the struct, not by a sizeof. At(i) is the address of element i, to be read
	// with the struct's fields. Empty when the size did not resolve.
	class StructArray
	{
	public:
		template <class Element>
		StructArray(const SDK::TArray<Element>* array, const PluginFieldSpec& size)
		{
			if (!array || size.size == 0)
				return;
			// TArray is the engine's own layout: the data pointer, then Num.
			struct Header { const uint8_t* data; int32_t num; int32_t max; } header;
			std::memcpy(&header, array, sizeof(header));
			if (header.data && header.num > 0)
			{
				m_data   = header.data;
				m_num    = header.num;
				m_stride = size.size;
			}
		}

		int32_t Num() const { return m_num; }
		const uint8_t* At(int32_t index) const { return m_data + static_cast<size_t>(index) * m_stride; }

	private:
		const uint8_t* m_data   = nullptr;
		int32_t        m_num    = 0;
		uint32_t       m_stride = 0;
	};

	// The loader's property reader for Blueprint members, or null.
	inline const IPluginObjectProperties* ObjectProperties()
	{
		IPluginHooks* hooks = GetHooks();
		return hooks ? hooks->ObjectProperties : nullptr;
	}

	// The building's UCrBuildingData, or null.
	inline SDK::UCrBuildingData* BuildingDataOf(SDK::ACrBuildingActorBase* building)
	{
		SDK::UAuActorPlacementData* placement = ValueOr<SDK::UAuActorPlacementData*>(PlacementData, building, nullptr);
		return placement && placement->IsA(SDK::UCrBuildingData::StaticClass())
			? static_cast<SDK::UCrBuildingData*>(placement)
			: nullptr;
	}

	// The brush a building, a recipe or an item shows as its icon, or null.
	inline const SDK::FSlateBrush* IconOf(const SDK::UCrBuildingData* data)   { return BuildingIcon.In(static_cast<const void*>(data)); }
	inline const SDK::FSlateBrush* IconOf(const SDK::UCrItemRecipeData* data) { return RecipeIcon.In(static_cast<const void*>(data)); }
	inline const SDK::FSlateBrush* IconOf(const SDK::UAuItemDataBase* data)   { return ItemIcon.In(static_cast<const void*>(data)); }

	// The localized name and description of a building. False without data.
	inline bool ReadBuildingTexts(SDK::UCrBuildingData* data, std::string& name, std::string& description)
	{
		const SDK::FText* nameText = BuildingName.In(data);
		const SDK::FText* descText = BuildingDesc.In(data);
		if (!nameText || !descText)
			return false;

		name        = SDK::UKismetTextLibrary::Conv_TextToString(*nameText).ToString();
		description = SDK::UKismetTextLibrary::Conv_TextToString(*descText).ToString();
		return true;
	}

	// An item's unique name and its localized name. Empty when unresolved.
	inline std::string UniqueNameOf(const SDK::UAuItemDataBase* item)
	{
		const SDK::FName* name = UniqueItemName.In(static_cast<const void*>(item));
		return name ? name->ToString() : std::string();
	}

	inline std::string DisplayNameOf(const SDK::UAuItemDataBase* item)
	{
		const SDK::FText* text = ItemName.In(static_cast<const void*>(item));
		return text ? SDK::UKismetTextLibrary::Conv_TextToString(*text).ToString() : std::string();
	}

	// The item class of a recipe order (an element of NeededResources, or a
	// recipe's output), or null.
	inline SDK::UClass* OrderItemClass(const void* order)
	{
		const auto* item = OrderItem.In(order);
		SDK::UClass* itemClass = item ? *item : nullptr;
		return itemClass;
	}

	// A recipe stores ingredients as item *classes*; everything worth showing
	// (icon, display name, unique name) lives on the class default object.
	inline SDK::UAuItemDataBase* ItemDefaultsOf(SDK::UClass* itemClass)
	{
		if (!itemClass)
			return nullptr;

		SDK::UObject* cdo = itemClass->ClassDefaultObject;
		if (!cdo)
			return nullptr;

		bool isItem = false;
		try { isItem = cdo->IsA(SDK::UAuItemDataBase::StaticClass()); }
		catch (...) { return nullptr; }

		return isItem ? static_cast<SDK::UAuItemDataBase*>(cdo) : nullptr;
	}

	// A recipe's output: how many it makes, and which item.
	inline int32_t RecipeOutputCount(const SDK::UAuItemRecipeData* recipe)
	{
		return ValueOr<int32_t>(OrderCount, RecipeOutputItem.In(static_cast<const void*>(recipe)));
	}

	inline SDK::UClass* RecipeOutputClass(const SDK::UAuItemRecipeData* recipe)
	{
		return OrderItemClass(RecipeOutputItem.In(static_cast<const void*>(recipe)));
	}

	// What a crafting component works on first: the recipe, and the output count
	// its queue entry carries (0 when it carries none). A null recipe = idle.
	struct QueuedCraft
	{
		SDK::UAuItemRecipeData* recipe      = nullptr;
		int32_t                 outputCount = 0;
	};

	inline QueuedCraft FirstQueuedCraft(const SDK::UCrCraftingComponent* component)
	{
		QueuedCraft first;
		const StructArray queue(ItemsToCraft.In(static_cast<const void*>(component)), CraftItemSize);
		if (queue.Num() > 0)
		{
			first.recipe      = ValueOr<SDK::UAuItemRecipeData*>(QueuedRecipe, queue.At(0), nullptr);
			first.outputCount = ValueOr<int32_t>(SimpleItemCount, QueuedOutput.In(queue.At(0)));
		}
		return first;
	}

	// Counts a placement sockets component's sockets, and those in use.
	inline void CountSockets(const SDK::UAuActorPlacementSocketsComponent* component, int32_t& total, int32_t& used)
	{
		const StructArray sockets(PlacementSockets.In(static_cast<const void*>(component)), PlacementSocketSize);
		total = sockets.Num();
		used  = 0;
		for (int32_t i = 0; i < sockets.Num(); ++i)
		{
			if (ValueOr<int32_t>(SocketUseCount, sockets.At(i)) > 0)
				++used;
		}
	}
}

// The parts of WAILA that can be off while the rest runs. A key is listed in
// every part that uses it, so its miss turns off exactly those. Buildings (a
// building's name and description, and the power and cooler cards) and Items
// (item names and what a container holds) sit under every card that names a
// building or lists items, so Items lists Buildings' keys too; a crafter card
// adds Crafters' keys, a drone rail Drone rails', a package sender Package
// transport's, and the rail sources (rail_mass.cpp) Items'. Declared in
// OnPluginLoadHooks before any key is resolved. WAILA's config has no entry
// that belongs to one part.
namespace Waila::Features
{
	using namespace Waila::Fields;

	using KeyList = std::vector<const char*>;

	// The lists joined, each key once.
	template <class... List>
	KeyList Join(const List&... lists)
	{
		KeyList all;
		for (const KeyList* list : { static_cast<const KeyList*>(&lists)... })
		{
			for (const char* key : *list)
			{
				if (std::find_if(all.begin(), all.end(), [key](const char* have) { return std::strcmp(have, key) == 0; }) == all.end())
					all.push_back(key);
			}
		}
		return all;
	}

	inline const KeyList BuildingKeys = KeysOf(PlacementData, BuildingName, BuildingDesc, BuildingIcon,
		PlacementSockets, PlacementSocketSize, SocketUseCount);

	inline const KeyList ItemKeys = Join(BuildingKeys,
		KeysOf(Keys::GetStoredItemsContainerInternal, ItemName, UniqueItemName, ItemIcon, ItemStorage, GridColumns,
			GridRows, StorageItems, SlotDisabled, SlotItem, SimpleItemCount, SimpleItemData, StorageItemSize));

	inline const KeyList CrafterKeys = Join(ItemKeys,
		KeysOf(Keys::GetCraftingFragment, CraftComponent, CraftingSettings, SettingsSpeed, ItemsToCraft,
			NeededResources, RecipeOutputItem, RecipeBuildTime, RecipeIcon, InItemStorage, SimpleItemCount,
			QueuedRecipe, QueuedOutput, OrderItem, OrderCount, FragMultiplier, FragMissingItems, FragOutputFull,
			FragSelected, FragSpeed, CraftItemSize, RecipeOrderSize));

	inline PluginMap::Feature Buildings{ "buildings", "Building names and descriptions", BuildingKeys, {} };

	inline PluginMap::Feature Items{ "items", "Item names and stored items", ItemKeys, {} };

	inline PluginMap::Feature Crafters{ "crafters", "Crafter recipes and progress", CrafterKeys, {} };

	inline PluginMap::Feature DroneRails{ "drone-rails", "Drone rails",
		Join(CrafterKeys, KeysOf(SocketsData, Spline, SocketRef, SocketTransform, SocketDataSize)), {} };

	inline PluginMap::Feature PackageTransport{ "package-transport", "Package transport",
		Join(ItemKeys, KeysOf(PackageTransportReplicator, SendingTime, SenderReceiversContainer, AllSenderReceiversData,
			ConnectionsContainer, ConnectionsData, ConnectionSender, ConnectionItem, ConnectionStartSendTime,
			HelperNetID, HelperEntity, HelperSize, SenderIsSender, SenderLocation, SenderItemType, SenderEntity,
			SenderReceiverSize, ConnectionDataSize)), {} };

	// The send rate of each cargo connection, for the rail card's Cargo Receivers
	// and Senders. Its own feature: a name the game drops turns off those rates
	// and leaves the Cargo Sender card as it is.
	inline PluginMap::Feature PackageRates{ "package-rates", "Cargo send rates on the rail card",
		Join(ItemKeys, KeysOf(PackageTransportReplicator, SendingTime, ConnectionsContainer, ConnectionsData,
			ConnectionSender, ConnectionReceiver, ConnectionAmount, ConnectionItem, ConnectionStartSendTime,
			HelperEntity, HelperSize, ConnectionDataSize)), {} };

	// Declares them all. Call first in OnPluginLoadHooks. The rail sources
	// feature (rail_mass.cpp) lists the keys of its own reader, which are many
	// and live with the code that reads them, and the Items keys it builds on.
	inline bool Declare(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		const bool listed = PluginMap::DeclareFeatures(scanner, self, Buildings, Items, Crafters, DroneRails, PackageTransport, PackageRates);
		return RailMass::Declare(self, scanner, ItemKeys) && listed;
	}

	// What each card can show: every key it uses is on.
	inline bool ShowBuildings()        { return Buildings.On(); }
	inline bool ShowItems()            { return Items.On(); }
	inline bool ShowCrafters()         { return Crafters.On(); }
	inline bool ShowDroneRails()       { return DroneRails.On(); }
	inline bool ShowPackageTransport() { return PackageTransport.On(); }
	inline bool ShowPackageRates()     { return PackageRates.On(); }

	// A drone rail's sources and destinations from the game's Mass data.
	inline bool ShowRailSources()      { return RailMass::Available(); }

	// The rail's max, from the lane's own speed and spacing.
	inline bool ShowRailCapacity()     { return RailMass::CapacityAvailable(); }
}
