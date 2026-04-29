#include "power_detector.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"

using namespace SDK;

namespace Waila
{
	bool PowerDetector::IsGenerator(AActor* actor)
	{
		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ACrSolarPanel::StaticClass());
	}

	bool PowerDetector::GetPowerInfo(AActor* actor, PowerInfo& outInfo)
	{
		if (!IsGenerator(actor))
			return false;

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
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Generator";
			outInfo.buildingDesc = "";
		}

		outInfo.buildingPower          = building->GetBuildingPower();
		outInfo.gridAddPower           = building->GetGridAddPower();
		outInfo.gridRemovePower        = building->GetGridRemovePower();
		outInfo.gridTotalPower         = building->GetGridPower();
		outInfo.gridConnectionStatus   = static_cast<uint8_t>(building->GetGridConnectionStatus());

		return true;
	}
}
