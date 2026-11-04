#include "cargo_receiver_detector.h"
#include "waila_functions.h"
#include "waila_fields.h"
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

		UCrBuildingData* buildingData = Fields::BuildingDataOf(building);

		outInfo.buildingData = buildingData;

		if (!Fields::ReadBuildingTexts(buildingData, outInfo.buildingName, outInfo.buildingDesc))
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Package Receiver";
			outInfo.buildingDesc = "";
		}

		// Read storage contents via the shared pattern-scanned function
		UCrBuildingItemStorageComponent* itemStorage = Fields::ValueOr<UCrBuildingItemStorageComponent*>(Fields::ItemStorage, building, nullptr);
		if (itemStorage)
		{
			auto fnGetContainer = Waila::Functions::GetStoredItemsContainerInternal();
			if (fnGetContainer)
			{
				SDK::UWorld* world = PluginSdkRuntime::GetWorld();
				if (!world)
				{
					LOG_WARN("CargoReceiverDetector: GetWorld() returned null, skipping storage query");
					return true;
				}

				FCrItemsStorageContainer* container = fnGetContainer(itemStorage, world);
				if (container)
				{
					ReadStoredItems(container, outInfo.storedItems, outInfo.usedSlots);
				}
			}

			outInfo.maxCapacity = Fields::ValueOr<int32>(Fields::GridColumns, itemStorage) * Fields::ValueOr<int32>(Fields::GridRows, itemStorage);
		}

		return true;
	}
}
