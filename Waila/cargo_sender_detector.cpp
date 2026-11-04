#include "cargo_sender_detector.h"
#include "waila_functions.h"
#include "waila_fields.h"
#include "plugin_helpers.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuItems_classes.hpp"
#include "BP_PackageSender_classes.hpp"
#include "Engine_classes.hpp"

using namespace SDK;

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

		UCrBuildingData* buildingData = Fields::BuildingDataOf(building);

		outInfo.buildingData = buildingData;

		if (!Fields::ReadBuildingTexts(buildingData, outInfo.buildingName, outInfo.buildingDesc))
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Package Sender";
			outInfo.buildingDesc = "";
		}

		// CanSend from blueprint class
		if (actor->IsA(ABP_PackageSender_C::StaticClass()))
		{
			if (const bool* canSend = Fields::SenderCanSend.In(Fields::ObjectProperties(), static_cast<ABP_PackageSender_C*>(actor)))
				outInfo.canSend = *canSend;
		}

		// Get ACrPackageTransportReplicator via GameState
		UWorld* world = PluginSdkRuntime::GetWorld();
		if (!world)
		{
			LOG_WARN("CargoSenderDetector: GetWorld() returned null");
			return true;
		}

		AGameStateBase* gameStateBase = UGameplayStatics::GetGameState(world);
		if (!gameStateBase || !gameStateBase->IsA(ACrGameStateBase::StaticClass()))
			return true;

		ACrGameStateBase* gameState = static_cast<ACrGameStateBase*>(gameStateBase);
		ACrPackageTransportReplicator* replicator = Fields::ValueOr<ACrPackageTransportReplicator*>(Fields::PackageTransportReplicator, gameState, nullptr);
		if (!replicator || !UKismetSystemLibrary::IsValid(replicator))
			return true;	

		// The two TArrays live in containers inside the replicator: the member of the
		// replicator, then the member of its container's type.
		outInfo.sendingTime = Fields::ValueOr<float>(Fields::SendingTime, replicator);

		const auto* senderContainer = Fields::SenderReceiversContainer.In(replicator);
		const Fields::StructArray allData(Fields::AllSenderReceiversData.In(senderContainer), Fields::SenderReceiverSize);

		// Match by world-space proximity: find the sender entry closest to this building
		FVector actorLoc = actor->K2_GetActorLocation();
		const uint8_t* bestMatch = nullptr;
		double bestDistSq = 1e30;

		for (int32_t i = 0; i < allData.Num(); ++i)
		{
			const uint8_t* data = allData.At(i);
			const auto* location = Fields::SenderLocation.In(data);
			if (!location || !Fields::ValueOr<bool>(Fields::SenderIsSender, data))
				continue;

			double dx = location->X - actorLoc.X;
			double dy = location->Y - actorLoc.Y;
			double dz = location->Z - actorLoc.Z;
			double distSq = dx * dx + dy * dy + dz * dz;
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				bestMatch  = data;
			}
		}

		if (bestMatch)
		{
			// Primary: ItemType on the sender data entry (may be null)
			UCrItemDataBase* matchItem = Fields::ValueOr<UCrItemDataBase*>(Fields::SenderItemType, bestMatch, nullptr);
			if (matchItem && UKismetSystemLibrary::IsValid(matchItem))
			{
				outInfo.sendingItem     = matchItem;
				outInfo.sendingItemName = Fields::DisplayNameOf(matchItem);
			}
			// Replicate GetSenderItem + GetStartSendServerTime inline in one pass.
			// Both scan ConnectionsContainer.ConnectionsData by the same sender match: the
			// helper's NetID and the first dword of its Entity. Item and start time are
			// members of the connection entry.
			{
				const auto* connContainer = Fields::ConnectionsContainer.In(replicator);
				const Fields::StructArray connections(Fields::ConnectionsData.In(connContainer), Fields::ConnectionDataSize);

				// The sender's NetID and Entity dwords, to match against each connection's sender.
				const uint8_t* helper = reinterpret_cast<const uint8_t*>(Fields::SenderEntity.In(bestMatch));
				const PluginFieldSpec& netIdField = Fields::HelperNetID;
				const PluginFieldSpec& entityField = Fields::HelperEntity;
				// Both dwords are read at a resolved offset inside the helper struct, bounded
				// by the size the loader reports for it on the running build.
				constexpr int32_t kDword = sizeof(uint32_t);
				const int32_t helperSize = static_cast<int32_t>(Fields::HelperSize.size);
				if (!helper || connections.Num() == 0 || netIdField.offset < 0 || entityField.offset < 0 ||
					netIdField.offset + kDword > helperSize || entityField.offset + kDword > helperSize)
					return true;
				uint32_t netIdDword0 = *reinterpret_cast<const uint32_t*>(helper + netIdField.offset);
				uint32_t netIdDword2 = *reinterpret_cast<const uint32_t*>(helper + entityField.offset);

				for (int32_t i = 0; i < connections.Num(); ++i)
				{
					const uint8_t* entry = connections.At(i);
					const auto* sender = Fields::ConnectionSender.In(entry);
					if (!sender)
						break;
					const uint8_t* senderBytes = reinterpret_cast<const uint8_t*>(sender);
					uint32_t d16 = *reinterpret_cast<const uint32_t*>(senderBytes + netIdField.offset);
					uint32_t d24 = *reinterpret_cast<const uint32_t*>(senderBytes + entityField.offset);

					bool matched = (d16 != 0 && d16 == netIdDword0) ||
					               (d24 != 0xFFFFFFFFu && d24 == netIdDword2);
					if (!matched)
						continue;

					UCrItemDataBase* connItem = Fields::ValueOr<UCrItemDataBase*>(Fields::ConnectionItem, entry, nullptr);
					if (connItem && UKismetSystemLibrary::IsValid(connItem))
					{
						outInfo.sendingItem     = connItem;
						outInfo.sendingItemName = Fields::DisplayNameOf(connItem);
					}

					float startSendTime = Fields::ValueOr<float>(Fields::ConnectionStartSendTime, entry);
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
