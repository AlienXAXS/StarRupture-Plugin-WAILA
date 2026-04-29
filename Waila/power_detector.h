#pragma once

#include <string>
#include <cstdint>

namespace SDK { class AActor; }

namespace Waila
{
	struct PowerInfo
	{
		std::string buildingName;
		std::string buildingDesc;

		float buildingPower     = 0.f; // this generator's own output
		float gridAddPower      = 0.f; // power contributed to the grid
		float gridRemovePower   = 0.f; // power drawn from the grid
		float gridTotalPower    = 0.f; // total grid power balance

		// 0=NotConnected, 1=Connected, 2=ConnectedAndOff
		uint8_t gridConnectionStatus = 0;

		bool IsValid() const { return !buildingName.empty(); }
	};

	class PowerDetector
	{
	public:
		static bool IsGenerator(SDK::AActor* actor);
		static bool GetPowerInfo(SDK::AActor* actor, PowerInfo& outInfo);
	};
}
