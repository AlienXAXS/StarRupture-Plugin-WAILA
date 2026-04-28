#include "storage_detector.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"

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
				/*
				  GetStoredItemsContainerInternal:
					struct FCrItemsStorageContainer *__fastcall UCrBuildingItemStorageComponent::GetStoredItemsContainerInternal(
						UCrBuildingItemStorageComponent *this,
						const struct UWorld *a2)

					40 53 48 83 EC ?? 48 8B 81 ?? ?? ?? ?? 48 8B D9 48 85 C0 75 ?? E8 ?? ?? ?? ?? 48 8B C8

				  Once the function has been found, we can call it to get the items that the container has, then display how many of that item exist.
				*/
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
