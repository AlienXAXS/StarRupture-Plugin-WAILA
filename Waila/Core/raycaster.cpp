#include "plugin_interface.h"
#include "raycaster.h"
#include "plugin_helpers.h"
#include "Engine_classes.hpp"
#include "Engine_structs.hpp"
#include "CoreUObject_structs.hpp"
#include "Chimera_classes.hpp"
#include "plugin_map.h"

using namespace SDK;

namespace Waila::Core
{
	APawn* WailaRaycastSystem::GetLocalPlayer() const
	{
		UWorld* world = PluginSdkRuntime::GetWorld();
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

	void WailaRaycastSystem::IgnoreWithHierarchy(AActor* actor, TArray<AActor*>& ignoreActors)
	{
		ignoreActors.Add(actor);

		// Also add all parent actors in the hierarchy
		for (AActor* a = actor; a; )
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

		// Add all child actors attached to the actor
		TArray<AActor*> attachedActors;
		actor->GetAttachedActors(&attachedActors, true, true);
		for (AActor* child : attachedActors)
		{
			if (child)
			{
				ignoreActors.Add(child);
			}
		}
	}

	// Capture log for the rail-drone aim fix: one line per distinct hit
	// (owner, component) while a rail is or was the target, with the reason a
	// hit was traced through. The pointers are only compared, never followed.
	void WailaRaycastSystem::LogAimHit(const FHitResult& hit, AActor* owner, const char* skipReason)
	{
		static uintptr_t s_lastOwner = 0;
		static uintptr_t s_lastComp  = 0;
		static bool      s_railSeen  = false;

		UPrimitiveComponent* comp = hit.Component.Get();
		const uintptr_t ownerKey = reinterpret_cast<uintptr_t>(owner);
		const uintptr_t compKey  = reinterpret_cast<uintptr_t>(comp);
		if (ownerKey == s_lastOwner && compKey == s_lastComp)
			return;
		s_lastOwner = ownerKey;
		s_lastComp  = compKey;

		const bool isRail = owner && UKismetSystemLibrary::IsValid(owner) && owner->IsA(ACrDronePathPointConnection::StaticClass());
		const bool wasRail = s_railSeen;
		if (!skipReason)
			s_railSeen = isRail;
		if (!isRail && !wasRail)
			return;

		std::string ownerClass = owner && owner->Class ? owner->Class->GetName() : "(null)";
		std::string ownerName  = owner ? owner->GetName() : "(null)";
		std::string compClass  = comp && comp->Class ? comp->Class->GetName() : "(null)";
		std::string compName   = comp ? comp->GetName() : "(null)";
		std::string meshName   = "-";
		if (comp && comp->IsA(UStaticMeshComponent::StaticClass()))
		{
			UStaticMesh* mesh = static_cast<UStaticMeshComponent*>(comp)->StaticMesh;
			meshName = mesh ? mesh->GetName() : "(none)";
		}
		LOG_INFO("[AimCapture] owner=%s (%s) comp=%s (%s) mesh=%s item=%d dist=%.0f -> %s",
			ownerName.c_str(), ownerClass.c_str(), compName.c_str(), compClass.c_str(),
			meshName.c_str(), hit.Item, hit.Distance, skipReason ? skipReason : "target");
	}

	bool WailaRaycastSystem::PerformRaycast(float maxDistance, RaycastHit& outHit)
	{
		outHit = {};

		// Obtain world and pawn once — both can become null during a map transition
		UWorld* world = PluginSdkRuntime::GetWorld();
		if (!world)
			return false;

		APawn* localPawn = GetLocalPlayer();
		if (!localPawn)
		{
			LOG_DEBUG("PerformRaycast: no local pawn");
			return false;
		}

		// Aim from the camera, where the crosshair is, so the ray also follows the
		// building drone's camera. Max Distance still counts from what the camera
		// looks through (the drone, or the player's eyes), so pulling the camera
		// back does not shorten the reach. On foot both points are the same.
		APlayerController* pc = UGameplayStatics::GetPlayerController(world, 0);
		AActor* viewTarget = pc ? pc->GetViewTarget() : nullptr;
		if (!viewTarget || !UKismetSystemLibrary::IsValid(viewTarget))
			viewTarget = localPawn;

		FVector eyeLocation;
		FRotator eyeRotation;
		viewTarget->GetActorEyesViewPoint(&eyeLocation, &eyeRotation);

		FVector cameraLocation = eyeLocation;
		FRotator cameraRotation = eyeRotation;
		if (pc)
			pc->GetPlayerViewPoint(&cameraLocation, &cameraRotation);

		FVector direction = UKismetMathLibrary::GetForwardVector(cameraRotation);
		const double reach = static_cast<double>(maxDistance) + UKismetMathLibrary::Vector_Distance(cameraLocation, eyeLocation);

		// Push the ray origin slightly forward so it never starts inside the player's own geometry.
		// The ignore-actor list covers most cases but some attached components still block at the
		// exact eye position; 80 units is well past the player capsule radius and imperceptible to aim.
		constexpr double kStartOffset = 80.0;
		FVector startLocation = cameraLocation + direction * kStartOffset;
		FVector endLocation = cameraLocation + direction * reach;

		// Build ignore list — exclude the local player pawn, the actor the camera
		// follows, and their hierarchies
		// The SDK's TArray::Add only fills existing slack, so the list needs its
		// own buffer: on an empty TArray every Add fails and nothing is ignored.
		constexpr int32 kMaxIgnoredActors = 64;
		TAllocatedArray<AActor*> ignoreActors(kMaxIgnoredActors);
		IgnoreWithHierarchy(localPawn, ignoreActors);
		if (viewTarget != localPawn)
			IgnoreWithHierarchy(viewTarget, ignoreActors);

		FHitResult hitResult;
		FLinearColor noColor = { 0.f, 0.f, 0.f, 0.f };

		// A drone riding a rail is not what the player aims at: when the trace lands
		// on one, ignore it and trace again, so the hit falls on the rail behind it.
		// The bound only guards a rail with a queue of drones in front of the camera.
		constexpr int kMaxDroneRetraces = 8;
		bool bHit = false;
		for (int attempt = 0; attempt <= kMaxDroneRetraces; ++attempt)
		{
			hitResult = {};
			bHit = UKismetSystemLibrary::LineTraceSingle(
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
			if (!bHit)
				break;

			UPrimitiveComponent* hitComp = hitResult.Component.Get();
			AActor* hitOwner = hitComp ? hitComp->GetOwner() : nullptr;
			const char* skipReason = nullptr;
			if (hitOwner && UKismetSystemLibrary::IsValid(hitOwner))
			{
				if (hitOwner->IsA(ACrDroneBase::StaticClass()))
					skipReason = "drone actor";
			}
			LogAimHit(hitResult, hitOwner, skipReason);
			if (!skipReason)
				break;
			ignoreActors.Add(hitOwner);
			if (attempt == kMaxDroneRetraces)
				bHit = false;   // still a drone after the last retrace: no target rather than a drone
		}

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
			//LOG_TRACE("Raycast hit | actor=\"%s\" comp=\"%s\" bone=\"%s\" dist=%.1f", actorName.c_str(), compName.c_str(), boneName.c_str(), hitResult.Distance);
		}

		outHit.actor    = hitActor;
		outHit.location = hitResult.ImpactPoint;
		outHit.normal   = hitResult.ImpactNormal;
		outHit.distance = hitResult.Distance;
		outHit.hit      = true;

		return true;
	}
}
