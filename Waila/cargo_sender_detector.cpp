#include "cargo_sender_detector.h"
#include "waila_functions.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuItems_classes.hpp"
#include "BP_PackageSender_classes.hpp"
#include "Engine_classes.hpp"

using namespace SDK;

// Raw offsets derived from native analysis — not exported by SDK headers
static constexpr ptrdiff_t OFFSET_SENDING_TIME         = 0x2C0; // float on ACrPackageTransportReplicator
static constexpr ptrdiff_t OFFSET_ALL_SENDER_RECV_DATA = 0x490; // TArray<FCrSenderReceiverData> (SenderReceiversContainer+0x108)
static constexpr ptrdiff_t OFFSET_CONNECTIONS_DATA     = 0x5B0; // TArray raw (ConnectionsContainer+0x108), 112-byte entries
static constexpr ptrdiff_t CONN_ENTRY_NETID_DWORD0     = 16;    // DWORD matching NetID bytes 0-3
static constexpr ptrdiff_t CONN_ENTRY_NETID_DWORD2     = 24;    // DWORD matching NetID bytes 8-11
static constexpr ptrdiff_t CONN_ENTRY_ITEM             = 88;    // UCrItemDataBase* pointer
static constexpr ptrdiff_t CONN_ENTRY_START_SEND_TIME  = 100;   // float: GetStartSendServerTime result
static constexpr int32_t   CONN_ENTRY_STRIDE           = 112;

namespace Waila
{
	bool CargoSenderDetector::IsSender(AActor* actor)
	{
		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ACrItemSenderBuilding::StaticClass());
	}

	bool CargoSenderDetector::GetSenderInfo(AActor* actor, CargoSenderInfo& outInfo)
	{
		if (!IsSender(actor))
			return false;

		ACrBuildingActorBase* building = static_cast<ACrBuildingActorBase*>(actor);

		UCrBuildingData* buildingData = (building->PlacementData && building->PlacementData->IsA(UCrBuildingData::StaticClass()))
			? static_cast<UCrBuildingData*>(building->PlacementData)
			: nullptr;

		if (buildingData)
		{
			outInfo.buildingName = UKismetTextLibrary::Conv_TextToString(buildingData->BuildingName).ToString();
			outInfo.buildingDesc = UKismetTextLibrary::Conv_TextToString(buildingData->BuildingDescription).ToString();
		}
		else
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Package Sender";
			outInfo.buildingDesc = "";
		}

		// CanSend from blueprint class
		if (actor->IsA(ABP_PackageSender_C::StaticClass()))
			outInfo.canSend = static_cast<ABP_PackageSender_C*>(actor)->CanSend;

		// Get ACrPackageTransportReplicator via GameState
		UWorld* world = UWorld::GetWorld();
		if (!world)
		{
			LOG_WARN("CargoSenderDetector: UWorld::GetWorld() returned null");
			return true;
		}

		AGameStateBase* gameStateBase = UGameplayStatics::GetGameState(world);
		if (!gameStateBase || !gameStateBase->IsA(ACrGameStateBase::StaticClass()))
			return true;

		ACrGameStateBase* gameState = static_cast<ACrGameStateBase*>(gameStateBase);
		ACrPackageTransportReplicator* replicator = gameState->PackageTransportReplicator;
		if (!replicator || !UKismetSystemLibrary::IsValid(replicator))
			return true;	

		// SendingTime — raw offset, not exported by SDK
		outInfo.sendingTime = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(replicator) + OFFSET_SENDING_TIME);

		// AllSenderReceiversData — private, accessed via raw offset
		auto& allData = *reinterpret_cast<TArray<FCrSenderReceiverData>*>(reinterpret_cast<uint8_t*>(replicator) + OFFSET_ALL_SENDER_RECV_DATA);

		// Match by world-space proximity: find the sender entry closest to this building
		FVector actorLoc = actor->K2_GetActorLocation();
		const FCrSenderReceiverData* bestMatch = nullptr;
		double bestDistSq = 1e30;

		for (int i = 0; i < allData.Num(); ++i)
		{
			const FCrSenderReceiverData& data = allData[i];
			if (!data.bIsSender)
				continue;

			double dx = data.Location.X - actorLoc.X;
			double dy = data.Location.Y - actorLoc.Y;
			double dz = data.Location.Z - actorLoc.Z;
			double distSq = dx * dx + dy * dy + dz * dz;
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				bestMatch  = &data;
			}
		}

		if (bestMatch)
		{
			// Primary: ItemType on the sender data entry (may be null)
			if (bestMatch->ItemType && UKismetSystemLibrary::IsValid(bestMatch->ItemType))
			{
				outInfo.sendingItemName = UKismetTextLibrary::Conv_TextToString(bestMatch->ItemType->ItemName).ToString();
			}
			// Replicate GetSenderItem + GetStartSendServerTime inline in one pass.
			// Both scan ConnectionsContainer.ConnectionsData (112-byte entries) by the same
			// NetID match (DWORD0 at +16, DWORD2 at +24). Item at +88, start time at +100.
			{
				struct RawTArray { void* Data; int32_t Num; int32_t Max; };
				auto& connArray = *reinterpret_cast<RawTArray*>(reinterpret_cast<uint8_t*>(replicator) + OFFSET_CONNECTIONS_DATA);
				uint8_t* connData = reinterpret_cast<uint8_t*>(connArray.Data);

				// Extract DWORD0 and DWORD2 from the sender's 16-byte NetID.
				// FCrMassEntityReplicationHelper starts with FCrMassEntityNetID (16 bytes).
				const uint8_t* netIdBytes = reinterpret_cast<const uint8_t*>(&bestMatch->Entity);
				uint32_t netIdDword0 = *reinterpret_cast<const uint32_t*>(netIdBytes + 0);
				uint32_t netIdDword2 = *reinterpret_cast<const uint32_t*>(netIdBytes + 8);

				for (int32_t i = 0; connData && i < connArray.Num; ++i)
				{
					uint8_t* entry = connData + CONN_ENTRY_STRIDE * i;
					uint32_t d16 = *reinterpret_cast<uint32_t*>(entry + CONN_ENTRY_NETID_DWORD0);
					uint32_t d24 = *reinterpret_cast<uint32_t*>(entry + CONN_ENTRY_NETID_DWORD2);

					bool matched = (d16 != 0 && d16 == netIdDword0) ||
					               (d24 != 0xFFFFFFFFu && d24 == netIdDword2);
					if (!matched)
						continue;

					UCrItemDataBase* connItem = *reinterpret_cast<UCrItemDataBase**>(entry + CONN_ENTRY_ITEM);
					if (connItem && UKismetSystemLibrary::IsValid(connItem))
						outInfo.sendingItemName = UKismetTextLibrary::Conv_TextToString(connItem->ItemName).ToString();

					float startSendTime = *reinterpret_cast<float*>(entry + CONN_ENTRY_START_SEND_TIME);
					if (startSendTime > 0.f && outInfo.sendingTime > 0.f)
					{
						double serverTime = gameStateBase->GetServerWorldTimeSeconds();
						float elapsed = static_cast<float>(serverTime) - startSendTime;
						outInfo.sendProgress = elapsed / outInfo.sendingTime;
					}
					break;
				}
			}
		}

		return true;
	}
}
