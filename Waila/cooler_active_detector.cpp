#include "cooler_active_detector.h"
#include "plugin_helpers.h"
#include "waila_fields.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "BP_CoolerActive_classes.hpp"
#include "AuActorPlacement_classes.hpp"
#include "AuActorPlacement_structs.hpp"

using namespace SDK;

namespace Waila
{
	bool CoolerActiveDetector::IsCoolerActive(AActor* actor)
	{
		// Disable cooler detection for now, it doesnt work as expected.
		return false;

		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ABP_CoolerActive_C::StaticClass());
	}

	bool CoolerActiveDetector::GetCoolerActiveInfo(AActor* actor, CoolerActiveInfo& outInfo)
	{
		if (!IsCoolerActive(actor))
			return false;

		ABP_CoolerActive_C* cooler = static_cast<ABP_CoolerActive_C*>(actor);
		ACrBuildingActorBase* building = static_cast<ACrBuildingActorBase*>(actor);

		UCrBuildingData* buildingData = Fields::BuildingDataOf(building);

		outInfo.buildingData = buildingData;

		if (!Fields::ReadBuildingTexts(buildingData, outInfo.buildingName, outInfo.buildingDesc))
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Active Cooler";
			outInfo.buildingDesc = "";
		}

		outInfo.state = static_cast<uint8_t>(cooler->GetHeaterCoolerState());

		// The Blueprint class's own component pointer, found by name; the sockets are
		// the component's, which is an SDK class.
		UAuActorPlacementSocketsComponent** socketsAt = Fields::ActiveCoolerSockets.In(Fields::ObjectProperties(), cooler);
		UAuActorPlacementSocketsComponent* socketsComponent = socketsAt ? *socketsAt : nullptr;
		if (socketsComponent && UKismetSystemLibrary::IsValid(socketsComponent))
			Fields::CountSockets(socketsComponent, outInfo.totalSockets, outInfo.connectedSockets);

		return true;
	}
}
