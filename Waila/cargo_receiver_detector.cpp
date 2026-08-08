#include "cargo_receiver_detector.h"
#include <unordered_map>
#include "waila_functions.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuItems_classes.hpp"

using namespace SDK;

namespace Waila
{
	bool CargoReceiverDetector::IsReceiver(AActor* actor)
	{
		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ACrItemReceiverBuilding::StaticClass());
	}

	bool CargoReceiverDetector::GetReceiverInfo(AActor* actor, CargoReceiverInfo& outInfo)
	{
		if (!IsReceiver(actor))
			return false;

		ACrBuildingActorBase* building = static_cast<ACrBuildingActorBase*>(actor);

		UCrBuildingData* buildingData = (building->PlacementData && building->PlacementData->IsA(UCrBuildingData::StaticClass()))
			? static_cast<UCrBuildingData*>(building->PlacementData)
			: nullptr;

		outInfo.buildingData = buildingData;

		if (buildingData)
		{
			outInfo.buildingName = UKismetTextLibrary::Conv_TextToString(buildingData->BuildingName).ToString();
			outInfo.buildingDesc = UKismetTextLibrary::Conv_TextToString(buildingData->BuildingDescription).ToString();
		}
		else
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Package Receiver";
			outInfo.buildingDesc = "";
		}

		// Read storage contents via the shared pattern-scanned function
		UCrBuildingItemStorageComponent* itemStorage = building->ItemStorage;
		if (itemStorage)
		{
			auto fnGetContainer = Waila::Functions::GetStoredItemsContainerInternal();
			if (fnGetContainer)
			{
				SDK::UWorld* world = SDK::UWorld::GetWorld();
				if (!world)
				{
					LOG_WARN("CargoReceiverDetector: UWorld::GetWorld() returned null, skipping storage query");
					return true;
				}

				FCrItemsStorageContainer* container = fnGetContainer(itemStorage, world);
				if (container)
				{
					// storedItems is deduplicated by item type, so its size counts
					// the *kinds* held. Occupied slots are counted as we go.
					std::unordered_map<std::string, size_t> itemIndexMap;
					int32_t occupiedSlots = 0;

					for (int i = 0; i < container->Items.Num(); ++i)
					{
						const FCrStorageItem& slot = container->Items[i];
						if (slot.bIsDisabled || slot.Item.Count <= 0)
							continue;

						++occupiedSlots;

						std::string uniqueName;
						std::string displayName;
						UAuItemDataBase* itemData = nullptr;
						if (slot.Item.ItemDataBase && UKismetSystemLibrary::IsValid(slot.Item.ItemDataBase))
						{
							itemData    = slot.Item.ItemDataBase;
							uniqueName  = itemData->UniqueItemName.ToString();
							displayName = UKismetTextLibrary::Conv_TextToString(itemData->ItemName).ToString();
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
							entry.itemData    = itemData;
							outInfo.storedItems.push_back(entry);
						}
					}
					outInfo.usedSlots = occupiedSlots;
				}
			}

			outInfo.maxCapacity = itemStorage->GridColumns * itemStorage->GridRows;
		}

		return true;
	}
}
