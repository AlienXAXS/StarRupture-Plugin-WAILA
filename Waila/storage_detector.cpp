#include "storage_detector.h"
#include <unordered_map>
#include "waila_functions.h"
#include "waila_fields.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuItems_classes.hpp"

using namespace SDK;

namespace Waila
{
	void ReadStoredItems(const FCrItemsStorageContainer* container,
	                     std::vector<StoredItemEntry>& items, int32_t& usedSlots)
	{
		// The list is deduplicated by item type, so its size counts the *kinds*
		// held — 1 for a depot full of a single item. Occupied slots have to be
		// counted as the slots go by.
		std::unordered_map<std::string, size_t> itemIndexMap;

		const Fields::StructArray slots(Fields::StorageItems.In(container), Fields::StorageItemSize);
		for (int32_t i = 0; i < slots.Num(); ++i)
		{
			const uint8_t* slot = slots.At(i);
			const void* simple  = Fields::SlotItem.In(slot);
			const int32_t count = Fields::ValueOr<int32_t>(Fields::SimpleItemCount, simple);
			if (Fields::ValueOr<bool>(Fields::SlotDisabled, slot) || count <= 0)
				continue;

			++usedSlots;

			std::string uniqueName;
			std::string displayName;
			UAuItemDataBase* itemData = Fields::ValueOr<UAuItemDataBase*>(Fields::SimpleItemData, simple, nullptr);
			if (itemData && UKismetSystemLibrary::IsValid(itemData))
			{
				uniqueName  = Fields::UniqueNameOf(itemData);
				displayName = Fields::DisplayNameOf(itemData);
			}
			else
			{
				itemData = nullptr;
			}

			auto it = itemIndexMap.find(uniqueName);
			if (it != itemIndexMap.end())
			{
				items[it->second].count += count;
			}
			else
			{
				itemIndexMap[uniqueName] = items.size();
				StoredItemEntry entry;
				entry.uniqueName  = uniqueName;
				entry.displayName = displayName;
				entry.count       = count;
				entry.itemData    = itemData;
				items.push_back(entry);
			}
		}
	}

	bool StorageDetector::IsStorage(AActor* actor)
	{
		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ACrStorage::StaticClass());
	}

	bool StorageDetector::GetStorageInfo(AActor* actor, StorageInfo& outInfo)
	{
		if (!IsStorage(actor))
			return false;

		ACrBuildingActorBase* building = static_cast<ACrBuildingActorBase*>(actor);

		// Building name and description from PlacementData
		UCrBuildingData* buildingData = Fields::BuildingDataOf(building);

		outInfo.buildingData = buildingData;

		if (!Fields::ReadBuildingTexts(buildingData, outInfo.buildingName, outInfo.buildingDesc))
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Storage";
			outInfo.buildingDesc = "";
		}

		// Probe for CrBuildingMainStorageComponent
		UCrBuildingMainStorageComponent* mainStorage = static_cast<UCrBuildingMainStorageComponent*>(
			building->GetComponentByClass(UCrBuildingMainStorageComponent::StaticClass()));

		if (mainStorage)
		{
			UCrBuildingItemStorageComponent* itemStorage = static_cast<UCrBuildingItemStorageComponent*>(mainStorage);
			if (itemStorage)
			{
				// pattern: UCrBuildingItemStorageComponent::GetStoredItemsContainerInternal
				// 40 53 48 83 EC ?? 48 8B 81 ?? ?? ?? ?? 48 8B D9 48 85 C0 75 ?? E8 ?? ?? ?? ?? 48 8B C8
				auto fnGetContainer = Waila::Functions::GetStoredItemsContainerInternal();
				if (fnGetContainer)
				{
					SDK::UWorld* world = PluginSdkRuntime::GetWorld();
					if (!world)
					{
						LOG_WARN("StorageDetector: GetWorld() returned null, skipping storage query");
						return false;
					}
					FCrItemsStorageContainer* container = fnGetContainer(itemStorage, world);
					if (container)
					{
						ReadStoredItems(container, outInfo.storedItems, outInfo.usedSlots);
					}
				}
			}
		}

		// Capacity has to come from the component the slots were counted in, or
		// used/total would be a ratio of two different grids.
		UCrBuildingItemStorageComponent* capacitySource =
			mainStorage ? static_cast<UCrBuildingItemStorageComponent*>(mainStorage)
			            : Fields::ValueOr<UCrBuildingItemStorageComponent*>(Fields::ItemStorage, building, nullptr);

		if (capacitySource)
		{
			outInfo.maxCapacity = Fields::ValueOr<int32>(Fields::GridColumns, capacitySource) * Fields::ValueOr<int32>(Fields::GridRows, capacitySource);
		}

		return true;
	}
}
