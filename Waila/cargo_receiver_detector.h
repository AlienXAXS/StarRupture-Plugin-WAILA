#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "storage_detector.h"

namespace SDK { class AActor; }

namespace Waila
{
	struct CargoReceiverInfo
	{
		std::string buildingName;
		std::string buildingDesc;
		int32_t     maxCapacity = 0;
		int32_t     usedSlots   = 0;
		std::vector<StoredItemEntry> storedItems;

		bool IsValid() const { return !buildingName.empty(); }
	};

	class CargoReceiverDetector
	{
	public:
		static bool IsReceiver(SDK::AActor* actor);
		static bool GetReceiverInfo(SDK::AActor* actor, CargoReceiverInfo& out);
	};
}
