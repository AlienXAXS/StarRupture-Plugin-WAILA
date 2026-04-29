#include "cooler_passive_detector.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "BP_CoolerPassive_classes.hpp"
#include "AuActorPlacement_classes.hpp"
#include "AuActorPlacement_structs.hpp"

using namespace SDK;

namespace Waila
{
	bool CoolerPassiveDetector::IsCoolerPassive(AActor* actor)
	{
		// Disable cooler detection for now, it doesnt work as expected.
		return false;

		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ABP_CoolerPassive_C::StaticClass());
	}

	bool CoolerPassiveDetector::GetCoolerPassiveInfo(AActor* actor, CoolerPassiveInfo& outInfo)
	{
		if (!IsCoolerPassive(actor))
			return false;

		ABP_CoolerPassive_C* cooler = static_cast<ABP_CoolerPassive_C*>(actor);
		ACrBuildingActorBase* building = static_cast<ACrBuildingActorBase*>(actor);

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
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Passive Cooler";
			outInfo.buildingDesc = "";
		}

		outInfo.state = static_cast<uint8_t>(cooler->GetHeaterCoolerState());

		if (cooler->AuActorPlacementSockets && UKismetSystemLibrary::IsValid(cooler->AuActorPlacementSockets))
		{
			const TArray<FAuActorPlacementSingleSocketData>& sockets = cooler->AuActorPlacementSockets->Sockets;
			outInfo.totalSockets = sockets.Num();
			for (int32 i = 0; i < sockets.Num(); ++i)
			{
				if (sockets[i].UseCount > 0)
					++outInfo.connectedSockets;
			}
		}

		return true;
	}
}
