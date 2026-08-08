#pragma once

#include <string>
#include <cstdint>

namespace SDK { class AActor; class UCrBuildingData; class UAuItemDataBase; }

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

		// Item currently on the pad, kept so the card can show its icon.
		SDK::UAuItemDataBase* sendingItem = nullptr;

		// Building definition the card's icon comes from.
		SDK::UCrBuildingData* buildingData = nullptr;

		bool IsValid() const { return !buildingName.empty(); }
	};

	class CargoSenderDetector
	{
	public:
		static bool IsSender(SDK::AActor* actor);
		static bool GetSenderInfo(SDK::AActor* actor, CargoSenderInfo& out);
	};
}
