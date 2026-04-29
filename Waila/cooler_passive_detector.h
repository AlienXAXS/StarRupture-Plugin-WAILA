#pragma once

#include <string>
#include <cstdint>

namespace SDK { class AActor; }

namespace Waila
{
	struct CoolerPassiveInfo
	{
		std::string buildingName;
		std::string buildingDesc;
		uint8_t     state            = 0;  // ECrMassHeaterCoolerState: 0=Idle 1=Working 2=NoFuel 3=TooHotCold
		int32_t     connectedSockets = 0;  // sockets with UseCount > 0
		int32_t     totalSockets     = 0;  // total sockets on the component

		bool IsValid() const { return !buildingName.empty(); }
	};

	class CoolerPassiveDetector
	{
	public:
		static bool IsCoolerPassive(SDK::AActor* actor);
		static bool GetCoolerPassiveInfo(SDK::AActor* actor, CoolerPassiveInfo& outInfo);
	};
}
