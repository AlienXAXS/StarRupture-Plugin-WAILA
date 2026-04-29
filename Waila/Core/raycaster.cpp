#include "raycaster.h"
#include "plugin_helpers.h"
#include "Engine_classes.hpp"
#include "Engine_structs.hpp"
#include "CoreUObject_structs.hpp"

using namespace SDK;

namespace Waila::Core
{
	APawn* WailaRaycastSystem::GetLocalPlayer() const
	{
		UWorld* world = UWorld::GetWorld();
		if (!world || !UKismetSystemLibrary::IsValid(world) || !world->GameState)
			return nullptr;

		APlayerController* pc = UGameplayStatics::GetPlayerController(world, 0);
		if (!pc)
			return nullptr;

		if (!pc->Pawn)
		{
			LOG_DEBUG("GetLocalPlayer: player controller has no pawn");
			return nullptr;
		}

		return pc->Pawn;
	}

	bool WailaRaycastSystem::GetPlayerEyeData(FVector& outLocation, FRotator& outRotation) const
	{
		APawn* pawn = GetLocalPlayer();
		if (!pawn)
		{
			LOG_DEBUG("GetPlayerEyeData: no local player pawn");
			return false;
		}

		pawn->GetActorEyesViewPoint(&outLocation, &outRotation);
		return true;
	}

	bool WailaRaycastSystem::PerformRaycast(float maxDistance, RaycastHit& outHit)
	{
		outHit = {};

		// Obtain world and pawn once — both can become null during a map transition
		UWorld* world = UWorld::GetWorld();
		if (!world)
			return false;

		APawn* localPawn = GetLocalPlayer();

		FVector eyeLocation;
		FRotator eyeRotation;
		if (!localPawn)
		{
			LOG_DEBUG("PerformRaycast: no local pawn");
			return false;
		}
		localPawn->GetActorEyesViewPoint(&eyeLocation, &eyeRotation);

		FVector direction = UKismetMathLibrary::GetForwardVector(eyeRotation);

		// Push the ray origin slightly forward so it never starts inside the player's own geometry.
		// The ignore-actor list covers most cases but some attached components still block at the
		// exact eye position; 80 units is well past the player capsule radius and imperceptible to aim.
		constexpr double kStartOffset = 80.0;
		FVector startLocation = eyeLocation + direction * kStartOffset;
		FVector endLocation = eyeLocation + direction * static_cast<double>(maxDistance);

		// Build ignore list — exclude local player pawn and all its hierarchy
		TArray<AActor*> ignoreActors;
		ignoreActors.Add(localPawn);

		// Also add all parent actors in the hierarchy
		for (AActor* a = localPawn; a; )
		{
			AActor* parent = a->GetAttachParentActor();
			if (!parent)
				parent = a->GetOwner();

			if (parent && parent != a)
			{
				ignoreActors.Add(parent);
			}
			a = (parent != a) ? parent : nullptr;
		}

		// Add all child actors attached to the pawn
		TArray<AActor*> attachedActors;
		localPawn->GetAttachedActors(&attachedActors, true, true);
		for (AActor* child : attachedActors)
		{
			if (child)
			{
				ignoreActors.Add(child);
			}
		}

		FHitResult hitResult;
		FLinearColor noColor = { 0.f, 0.f, 0.f, 0.f };

		bool bHit = UKismetSystemLibrary::LineTraceSingle(
			world,
			startLocation,
			endLocation,
			ETraceTypeQuery::TraceTypeQuery1,  // Visibility channel
			false,                              // bTraceComplex
			ignoreActors,
			EDrawDebugTrace::None,
			&hitResult,
			true,                              // bIgnoreSelf
			noColor,
			noColor,
			0.f                                // DrawTime
		);

		// Always expose the ray extents so callers can visualise even a no-hit trace
		outHit.rayStart = startLocation;
		outHit.rayEnd   = bHit ? hitResult.ImpactPoint : endLocation;

		if (!bHit)
			return false;

		// Get the actor from the hit component and validate it is still alive
		AActor* hitActor = nullptr;
		UPrimitiveComponent* comp = hitResult.Component.Get();
		if (comp)
		{
			AActor* owner = comp->GetOwner();
			if (UKismetSystemLibrary::IsValid(owner))
				hitActor = owner;
		}

		// Log everything about the hit so unexpected geometry can be identified and filtered
		{
			std::string actorName  = hitActor ? UKismetSystemLibrary::GetObjectName(hitActor).ToString()  : "(null actor)";
			std::string compName   = comp     ? UKismetSystemLibrary::GetObjectName(comp).ToString()      : "(null comp)";
			std::string boneName   = hitResult.BoneName.ToString();
			LOG_TRACE("Raycast hit | actor=\"%s\" comp=\"%s\" bone=\"%s\" dist=%.1f",
				actorName.c_str(), compName.c_str(), boneName.c_str(), hitResult.Distance);
		}

		outHit.actor    = hitActor;
		outHit.location = hitResult.ImpactPoint;
		outHit.normal   = hitResult.ImpactNormal;
		outHit.distance = hitResult.Distance;
		outHit.hit      = true;

		return true;
	}
}
