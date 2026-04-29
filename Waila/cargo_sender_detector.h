#pragma once

#include <string>
#include <cstdint>

namespace SDK { class AActor; }

namespace Waila
{
	struct CargoSenderInfo
	{
		std::string buildingName;
		std::string buildingDesc;
		bool        canSend         = false;
		float       sendingTime     = 0.f;   // ACrPackageTransportReplicator::SendingTime (raw 0x2C0) — send interval
		float       sendProgress    = -1.f;  // 0..1, negative if unavailable
		std::string sendingItemName;          // from ConnectionsContainer entry +88

		bool IsValid() const { return !buildingName.empty(); }
	};

	class CargoSenderDetector
	{
	public:
		static bool IsSender(SDK::AActor* actor);
		static bool GetSenderInfo(SDK::AActor* actor, CargoSenderInfo& out);
	};
}
