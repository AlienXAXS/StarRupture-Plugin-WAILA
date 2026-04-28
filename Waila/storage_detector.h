#pragma once

#include <string>
#include <cstdint>

// Forward declare to avoid pulling in heavy SDK headers in this header
namespace SDK { class AActor; }

namespace Waila
{
	struct StorageInfo
	{
		std::string buildingName;   // Localized name from PlacementData
		std::string buildingDesc;   // Localized description from PlacementData
		int32_t     maxCapacity = 0; // ItemStorage->GridColumns * GridRows

		bool IsValid() const
		{
			return !buildingName.empty();
		}
	};

	class StorageDetector
	{
	public:
		// Returns true if actor is or inherits from ACrStorage
		static bool IsStorage(SDK::AActor* actor);

		// Fills outInfo if actor is a storage building. Returns false if not.
		static bool GetStorageInfo(SDK::AActor* actor, StorageInfo& outInfo);
	};
}
