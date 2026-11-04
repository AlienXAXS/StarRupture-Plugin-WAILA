#include "cooler_passive_detector.h"
#include "plugin_helpers.h"
#include "waila_fields.h"
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

		UCrBuildingData* buildingData = Fields::BuildingDataOf(building);

		outInfo.buildingData = buildingData;

		if (!Fields::ReadBuildingTexts(buildingData, outInfo.buildingName, outInfo.buildingDesc))
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Passive Cooler";
			outInfo.buildingDesc = "";
		}

		outInfo.state = static_cast<uint8_t>(cooler->GetHeaterCoolerState());

		// The Blueprint class's own component pointer, found by name; the sockets are
		// the component's, which is an SDK class.
		UAuActorPlacementSocketsComponent** socketsAt = Fields::PassiveCoolerSockets.In(Fields::ObjectProperties(), cooler);
		UAuActorPlacementSocketsComponent* socketsComponent = socketsAt ? *socketsAt : nullptr;
		if (socketsComponent && UKismetSystemLibrary::IsValid(socketsComponent))
			Fields::CountSockets(socketsComponent, outInfo.totalSockets, outInfo.connectedSockets);

		return true;
	}
}
