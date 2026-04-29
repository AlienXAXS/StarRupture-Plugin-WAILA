#include "storage_detector.h"
#include <unordered_map>
#include "waila_functions.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuItems_classes.hpp"

using namespace SDK;

namespace Waila
{
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
		UCrBuildingData* buildingData = (building->PlacementData && building->PlacementData->IsA(UCrBuildingData::StaticClass()))
			? static_cast<UCrBuildingData*>(building->PlacementData)
			: nullptr;

		if (buildingData)
		{
			outInfo.buildingName = UKismetTextLibrary::Conv_TextToString(buildingData->BuildingName).ToString();
			outInfo.buildingDesc = UKismetTextLibrary::Conv_TextToString(buildingData->BuildingDescription).ToString();
		}
		else
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
					SDK::UWorld* world = SDK::UWorld::GetWorld();
					if (!world)
					{
						LOG_WARN("StorageDetector: UWorld::GetWorld() returned null, skipping storage query");
						return false;
					}
					FCrItemsStorageContainer* container = fnGetContainer(itemStorage, world);
					if (container)
					{
						std::unordered_map<std::string, size_t> itemIndexMap;
						for (int i = 0; i < container->Items.Num(); ++i)
						{
							const FCrStorageItem& slot = container->Items[i];
							if (slot.bIsDisabled || slot.Item.Count <= 0)
								continue;

							std::string uniqueName;
							std::string displayName;
							if (slot.Item.ItemDataBase && UKismetSystemLibrary::IsValid(slot.Item.ItemDataBase))
							{
								uniqueName  = slot.Item.ItemDataBase->UniqueItemName.ToString();
								displayName = UKismetTextLibrary::Conv_TextToString(slot.Item.ItemDataBase->ItemName).ToString();
							}

							auto it = itemIndexMap.find(uniqueName);
							if (it != itemIndexMap.end())
							{
								outInfo.storedItems[it->second].count += slot.Item.Count;
							}
							else
							{
								itemIndexMap[uniqueName] = outInfo.storedItems.size();
								StoredItemEntry entry;
								entry.uniqueName  = uniqueName;
								entry.displayName = displayName;
								entry.count       = slot.Item.Count;
								outInfo.storedItems.push_back(entry);
							}
						}
						outInfo.usedSlots = static_cast<int32_t>(outInfo.storedItems.size());
					}
				}
			}
		}

		// Capacity from the main storage component
		if (building->ItemStorage)
		{
			outInfo.maxCapacity = building->ItemStorage->GridColumns * building->ItemStorage->GridRows;
		}

		return true;
	}
}
