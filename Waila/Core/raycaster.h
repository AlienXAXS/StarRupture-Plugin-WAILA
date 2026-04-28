#pragma once

#include "Engine_structs.hpp"

// Forward declarations
namespace SDK { class AActor; class APawn; struct FRotator; }

namespace Waila::Core
{
	struct RaycastHit
	{
		SDK::AActor* actor = nullptr;
		SDK::FVector location;
		SDK::FVector normal;
		SDK::FVector rayStart;   // world-space origin of the trace
		SDK::FVector rayEnd;     // world-space end of the trace (max range or impact point)
		float distance = 0.0f;
		bool hit = false;
	};

	class WailaRaycastSystem
	{
	public:
		// Performs a line trace from the local player's eye position.
		// Returns true if something was hit; actor in outHit may still be null if component has no owner.
		bool PerformRaycast(float maxDistance, RaycastHit& outHit);

	private:
		SDK::APawn* GetLocalPlayer() const;
		bool GetPlayerEyeData(SDK::FVector& outLocation, SDK::FRotator& outRotation) const;
	};
}
