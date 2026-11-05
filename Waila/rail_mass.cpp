#include "rail_mass.h"
#include "waila_fields.h"
#include "plugin_helpers.h"

#include "Basic.hpp"
#include "CoreUObject_classes.hpp"
#include "Engine_classes.hpp"
#include "Chimera_classes.hpp"
#include "AuItems_classes.hpp"
#include "AuCrafting_classes.hpp"
#include "storage_detector.h"
#include "Chimera_structs.hpp"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace SDK;

// Engine layout this file keeps compiled, because the loader has nothing to
// say about it and the engine does not move it between builds:
//   - FMassEntityHandle passed by value: two int32 (Index, SerialNumber), one
//     register. Its size is still asked of the loader (kHandleSize).
//   - TArray: data pointer, then Num as int32 (RawArray).
//   - TSharedPtr: the object pointer is its first word (the entity manager).
//   - A const shared fragment: FConstSharedStruct's first word points at an
//     FStructSharedMemory, whose first word is the struct type; the data
//     follows, aligned to the type's MinAlignment.
//   - The calling convention and argument order of the Mass functions.
// Everything else (every fragment member, every struct size and array stride)
// is asked of the loader by name.

namespace Waila::RailMass
{
	namespace
	{
		struct Handle { int32_t index; int32_t serial; };

		struct RawArray { const uint8_t* data; int32_t num; int32_t max; };

		using GetSubsystemFn = void* (__fastcall*)(const UWorld*);
		using GetFragmentFn  = void* (__fastcall*)(const void* manager, Handle entity, const UScriptStruct* type);
		using EntityCheckFn  = bool  (__fastcall*)(const void* manager, Handle entity);
		using GetHandleFn    = Handle* (__fastcall*)(void* persistentId, Handle* out, const UObject* worldContext);
		using GetSharedFn    = const void* (__fastcall*)(const void* manager, Handle entity, const UScriptStruct* type);

		// The Mass functions, by the names the loader's map would use.
		constexpr const char* kKeyGetSubsystem = "UWorld::GetSubsystem<UMassEntitySubsystem>";
		constexpr const char* kKeyGetFragment  = "FMassEntityManager::InternalGetFragmentDataPtr";
		constexpr const char* kKeyIsValid      = "FMassEntityManager::IsEntityValid";
		constexpr const char* kKeyIsBuilt      = "FMassEntityManager::IsEntityBuilt";
		constexpr const char* kKeyGetHandle    = "FCrMassPersistentEntityID::GetHandle";
		constexpr const char* kKeyGetShared    = "FMassEntityManager::InternalGetConstSharedFragmentPtr";

		// Call sites and function starts in CL-127004, each unique there. The
		// call-site patterns follow the E8 at the offset below to the function.
		constexpr const char* kPatGetSubsystem =
			"49 8B CD E8 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B C8 "
			"E8 ?? ?? ?? ?? 48 8B D8 EB ?? 33 DB 48 83 7B 38 00 75 ?? "
			"4C 8D 0D ?? ?? ?? ?? 41 B8 ?? ?? ?? ??";
		constexpr int32_t kAtGetSubsystem = 0x10;

		constexpr const char* kPatGetFragment =
			"E8 ?? ?? ?? ?? 4C 8B C0 48 8B D3 48 8B CE E8 ?? ?? ?? ?? "
			"48 85 C0 0F 84 ?? ?? ?? ?? 0F 10 48 20 66 41 0F 2F 4D 30 "
			"0F 10 40 30 0F 11 4D EF F2 0F 11 45 FF";
		constexpr int32_t kAtGetFragment = 0x0E;

		constexpr const char* kPatGetShared =
			"E8 ?? ?? ?? ?? 4C 8B C0 48 8B D3 48 8B CE E8 ?? ?? ?? ?? 48 85 C0 74 ?? "
			"48 8B 10 48 85 D2 74 ?? 48 8B 02 48 0F BF 48 5C 48 8D 41 07 48 F7 D9 48 03 C2 48 23 C1 74 ?? "
			"F3 0F 10 00 48 8B 4D 6F F3 0F 58 01 F3 0F 11 01";
		constexpr int32_t kAtGetShared = 0x0E;

		// IsEntityBuilt is the function start the pattern matches; IsEntityValid
		// is the call 21 bytes in.
		constexpr const char* kPatIsBuilt =
			"48 89 5C 24 18 48 89 54 24 10 57 48 83 EC 40 48 8B DA 48 8B F9 "
			"E8 ?? ?? ?? ?? 84 C0 75 4B 48 8D 0D ?? ?? ?? ?? 85 DB 48 8D 05 ?? ?? ?? ?? "
			"41 B8 45 07 00 00 48 0F 44 C1 4C 8D 0D ?? ?? ?? ?? 48 89 44 24 30 48 8D 15 ?? ?? ?? ??";
		constexpr int32_t kAtIsValid = 21;

		// The function start. It returns the cached handle when both its halves
		// are set; otherwise it looks the ID up in the world's persistent ID
		// subsystem and caches the result in the ID it was called on.
		constexpr const char* kPatGetHandle =
			"48 89 5C 24 08 57 48 83 EC 30 83 39 FF 49 8B C0 48 8B DA 48 8B F9 74 ?? 48 85 C0 75 ??";

		GetSubsystemFn g_getSubsystem = nullptr;
		GetFragmentFn  g_getFragment  = nullptr;
		EntityCheckFn  g_isValid      = nullptr;
		EntityCheckFn  g_isBuilt      = nullptr;
		GetHandleFn    g_getHandle    = nullptr;
		GetSharedFn    g_getShared    = nullptr;

		// A struct's size on the running build, asked by its name. The row's
		// size is filled in by Resolve; 0 means it did not resolve.
		using SizeRow = PluginFieldSpec;
#define RAIL_SIZE_ROW(Name) PluginFieldSpec{ Name, Name, nullptr, PLUGIN_FIELD_FLAG_NONE, 0, -1 }

		SizeRow kHandleSize      = RAIL_SIZE_ROW("FMassEntityHandle");
		SizeRow kPersistentSize  = RAIL_SIZE_ROW("FCrMassPersistentEntityID");
		SizeRow kSimpleItemSize  = RAIL_SIZE_ROW("FAuSimpleItem");
		SizeRow kRequestSize     = RAIL_SIZE_ROW("FCrLogisticsRequestData");
		SizeRow kSocketSize      = RAIL_SIZE_ROW("FCrLogisticsSocketRuntimeData");

		SizeRow* const kSizeRows[] = {
			&kHandleSize, &kPersistentSize, &kSimpleItemSize, &kRequestSize, &kSocketSize,
		};

		// The stock read's own: a storage container's size.
		SizeRow kContainerSize   = RAIL_SIZE_ROW("FCrItemsStorageContainer");

		// A member: its size is a size row's, or fixed for a pointer, float,
		// bool or TArray header.
		struct Member
		{
			PluginFieldSpec  spec;
			const SizeRow*   type;    // the member's struct, or null
			uint32_t         fixed;   // otherwise its size
			int32_t Offset() const { return spec.offset; }
		};
#define RAIL_MEMBER(Class, Name, Type, Fixed) \
		Member{ PluginFieldSpec{ #Class "::" #Name, #Class, #Name, PLUGIN_FIELD_FLAG_NONE, 0, -1 }, Type, Fixed }

		constexpr uint32_t kPtr    = sizeof(void*);
		constexpr uint32_t kArray  = sizeof(TArray<uint8_t>);
		constexpr uint32_t kFloat  = sizeof(float);
		constexpr uint32_t kBool   = sizeof(bool);
		constexpr uint32_t kByte   = sizeof(uint8_t);

		// An actor's Mass component, and the entity the component made.
		Member mMassComponent  = RAIL_MEMBER(ACrBuildingActorBase, MassActorComponent, nullptr, kPtr);
		Member mEntityHandle   = RAIL_MEMBER(UCrMassActorComponent, EntityHandle, &kHandleSize, 0);

		// A rail's two end nodes, and the rails at a node's sockets. Every rail
		// names the nodes it joins (SaveGame data); the game fills a node's
		// sockets with the rails at them when it sets each rail up at load.
		Member mStartEntity    = RAIL_MEMBER(FAuSplineConnectionFragment, StartEntity, &kPersistentSize, 0);
		Member mEndEntity      = RAIL_MEMBER(FAuSplineConnectionFragment, EndEntity, &kPersistentSize, 0);
		Member mOneWay         = RAIL_MEMBER(FCrLogisticsLineParameters, bOneWay, nullptr, kBool);
		Member mSockets        = RAIL_MEMBER(FCrLogisticsSocketsFragment, Sockets, nullptr, kArray);
		Member mSocketType     = RAIL_MEMBER(FCrLogisticsSocketRuntimeData, ConnectionType, nullptr, kByte);
		Member mSocketRail     = RAIL_MEMBER(FCrLogisticsSocketRuntimeData, ConnectionEntity, &kHandleSize, 0);
		Member mLogisticsType  = RAIL_MEMBER(FCrLogisticsParameters, LogisticsType, nullptr, kByte);

		// A building's open requests, and the item each one wants.
		Member mRequests       = RAIL_MEMBER(FCrLogisticsRequestContainerFragment, Requests, nullptr, kArray);
		Member mPending        = RAIL_MEMBER(FCrLogisticsRequestContainerFragment, PendingRequests, nullptr, kArray);
		Member mRequestData    = RAIL_MEMBER(FCrLogisticsRequestFragment, Data, &kRequestSize, 0);
		Member mWantedItem     = RAIL_MEMBER(FCrLogisticsRequestData, WantedItem, &kSimpleItemSize, 0);
		Member mItemData       = RAIL_MEMBER(FAuSimpleItem, ItemDataBase, nullptr, kPtr);

		// A crafter's recipe and state.
		Member mSelectedRecipe = RAIL_MEMBER(FCrCraftingFragment, SelectedRecipe, nullptr, kPtr);
		Member mCurrentRecipe  = RAIL_MEMBER(FCrCraftingFragment, CurrentRecipe, nullptr, kPtr);
		Member mCraftingSpeed  = RAIL_MEMBER(FCrCraftingFragment, CraftingSpeed, nullptr, kFloat);
		Member mMissingItems   = RAIL_MEMBER(FCrCraftingFragment, bIsMissingItems, nullptr, kBool);
		Member mOutputFull     = RAIL_MEMBER(FCrCraftingFragment, bOutputFull, nullptr, kBool);

		// The building name read, a feature of its own: a building entity's building
		// parameters (a const shared fragment) hold the placement data, which names
		// the building whether or not its actor is loaded. Its miss turns off the
		// names and nothing else: the rows then read "Unknown building".
		Member mPlacement      = RAIL_MEMBER(FCrBuildingParameters, PlacementData, nullptr, kPtr);

		Member* const kMembers[] = {
			&mMassComponent, &mEntityHandle, &mStartEntity, &mEndEntity, &mOneWay, &mSockets, &mSocketType,
			&mSocketRail, &mLogisticsType, &mRequests, &mPending, &mRequestData, &mWantedItem, &mItemData,
			&mSelectedRecipe, &mCurrentRecipe, &mCraftingSpeed, &mMissingItems, &mOutputFull,
		};

		// The rail capacity read, a feature of its own: a lane's speed and
		// spacing, whether a route starts on it, and the drone settings' gap for
		// joining it. Its miss turns off the max figure and nothing else.
		Member mLineMoveSpeed  = RAIL_MEMBER(FCrLogisticsLineParameters, MoveSpeed, nullptr, kFloat);
		Member mLineSlotLength = RAIL_MEMBER(FCrLogisticsLineParameters, SlotLength, nullptr, kFloat);
		Member mIsStartLine    = RAIL_MEMBER(FCrLogisticsLineFragment, bIsStartLine, nullptr, kBool);
		Member mHalfSlotLength = RAIL_MEMBER(UDronesDeveloperSettings, HalfRailSlotLengthDefault, nullptr, kFloat);

		Member* const kBuildingMembers[] = { &mPlacement };

		Member* const kCapacityMembers[] = { &mLineMoveSpeed, &mLineSlotLength, &mIsStartLine, &mHalfSlotLength };

		// The socket pairs, a feature of its own: the game gives both sockets of
		// a pair (each track of a drone lane) one invisible connector, and drones
		// cross the node from one socket of a pair to the other only. Its miss
		// lets a lane's tracks meet on the card and turns off nothing else.
		Member mSocketPair     = RAIL_MEMBER(FCrLogisticsSocketRuntimeData, SocketPairInvisibleConnector, &kPersistentSize, 0);

		// The stock read, a feature of its own: a storage entity's inventory
		// fragment holds what it stores in one of its two containers. Its miss
		// turns off the stock lines and nothing else.
		Member mInputInventory = RAIL_MEMBER(FCrInventoryFragment, InputInventoryContainer, &kContainerSize, 0);
		Member mMainInventory  = RAIL_MEMBER(FCrInventoryFragment, MainInventoryContainer, &kContainerSize, 0);

		// The delivery read, a feature of its own: each open request lists the
		// deliveries assigned to it, and each one names the building it picks the
		// item up at (ItemSource) and brings it to (ItemDestination). Its miss
		// turns off the source and destination names and nothing else.
		SizeRow kRuntimeSize     = RAIL_SIZE_ROW("FCrLogisticsRequestRuntimeData");
		Member mRequestType      = RAIL_MEMBER(FCrLogisticsRequestData, Type, nullptr, kByte);
		Member mRuntimeData      = RAIL_MEMBER(FCrLogisticsRequestData, RuntimeData, nullptr, kArray);
		Member mItemSource       = RAIL_MEMBER(FCrLogisticsRequestRuntimeData, ItemSource, &kPersistentSize, 0);
		Member mItemDestination  = RAIL_MEMBER(FCrLogisticsRequestRuntimeData, ItemDestination, &kPersistentSize, 0);

		Member* const kTrackMembers[] = { &mSocketPair };

		Member* const kRequestMembers[] = { &mRequestType, &mRuntimeData, &mItemSource, &mItemDestination };

		Member* const kStockMembers[] = { &mInputInventory, &mMainInventory };

		// The fragments' reflection objects, found by name on first use.
		const UScriptStruct* g_lineStruct      = nullptr;
		const UScriptStruct* g_paramsStruct    = nullptr;
		const UScriptStruct* g_requestStruct   = nullptr;
		const UScriptStruct* g_craftingStruct  = nullptr;
		const UScriptStruct* g_buildingStruct  = nullptr;
		const UScriptStruct* g_splineStruct    = nullptr;
		const UScriptStruct* g_socketsStruct   = nullptr;
		const UScriptStruct* g_logisticsStruct = nullptr;
		const UScriptStruct* g_containerStruct = nullptr;
		const UScriptStruct* g_inventoryStruct = nullptr;

		PluginMap::Feature g_feature{ "rail-sources", "Where a drone rail's items come from and go to", {}, {} };
		PluginMap::Feature g_capacityFeature{ "rail-capacity", "How many items a minute a drone rail carries", {}, {} };
		PluginMap::Feature g_buildingFeature{ "rail-buildings", "Which building a rail's far ends are", {}, {} };
		PluginMap::Feature g_trackFeature{ "rail-tracks", "Keeping a drone lane's tracks apart", {}, {} };
		PluginMap::Feature g_stockFeature{ "rail-stock", "What the storage on a rail's network holds", {}, {} };
		PluginMap::Feature g_requestsFeature{ "rail-requests", "Where a request's items are picked up and delivered", {}, {} };
		bool g_resolved         = false;
		bool g_capacityResolved = false;
		bool g_buildingResolved = false;
		bool g_trackResolved    = false;
		bool g_stockResolved    = false;
		bool g_requestsResolved = false;
		bool g_faulted          = false;
		bool g_capacityFaulted  = false;
		bool g_buildingFaulted  = false;
		bool g_stockFaulted     = false;
		bool g_packageFaulted   = false;

		// Bounds on what one array can hold, so a corrupt count cannot run away.
		constexpr int32_t kMaxArray  = 4096;
		constexpr size_t  kMaxWanted = 64;
		constexpr int32_t kMaxDeliveries = 16;

		// The room HandleOf has for a persistent id on a copy; an id the loader
		// sizes larger is not copied.
		constexpr uint32_t kMaxIdCopy = 32;

		bool ResolveSize(IPluginHookScanner* scanner, const IPluginSelf* self, SizeRow& row)
		{
			PluginFieldRequest request = PLUGIN_FIELD_REQUEST_INIT;
			request.key       = row.key;
			request.className = row.className;
			request.builtCL   = PLUGIN_GAME_BUILT_CL;
			PluginFieldResult result{};
			if (!scanner->ResolveField(self, &request, &result))
				return false;
			row.size         = result.size;
			row.resolvedSize = result.size;
			row.offset       = 0;
			return true;
		}

		// Values of the game's enums, read by name from the running game
		// (ECrMassLogisticsType and ECrLogisticsRequestType), set by Resolve.
		struct LogisticsTypes
		{
			uint8_t storage = 0, both = 0, producer = 0, consumer = 0, lineNode = 0, lineConnection = 0;
		};
		LogisticsTypes g_types;
		uint8_t        g_requestStore = 0;

		// One entry's value, or false with a log line naming what is missing.
		bool ResolveEnum(const char* enumName, const char* entry, uint8_t& out)
		{
			int64_t value = 0;
			switch (PluginMap::EnumValue(enumName, entry, value))
			{
			case PluginMap::EnumLookup::Found:
				if (value >= 0 && value <= 0xFF)
				{
					out = static_cast<uint8_t>(value);
					return true;
				}
				LOG_WARN("RailMass: %s::%s is %lld, which does not fit a byte", enumName, entry, static_cast<long long>(value));
				return false;
			case PluginMap::EnumLookup::EntryMissing:
				LOG_WARN("RailMass: enum %s has no entry %s on this build", enumName, entry);
				return false;
			default:
				LOG_WARN("RailMass: enum %s is not loaded on this build", enumName);
				return false;
			}
		}

		uintptr_t ResolveFunction(IPluginHookScanner* scanner, const IPluginSelf* self, const char* key,
			const char* pattern, int32_t followAt)
		{
			return PluginMap::Function(scanner, self, key, pattern, PLUGIN_SCAN_FUNCTION_START,
				followAt >= 0 ? PLUGIN_SCAN_FLAG_FOLLOW_REL32 : 0u, 0, followAt >= 0 ? followAt : 0);
		}

		Handle ToHandle(Entity e)
		{
			Handle h;
			std::memcpy(&h, &e, sizeof(h));
			return h;
		}

		Entity ToEntity(Handle h)
		{
			Entity e;
			std::memcpy(&e, &h, sizeof(e));
			return e;
		}

		template <class T>
		T ReadAt(const uint8_t* base, int32_t offset)
		{
			T value{};
			std::memcpy(&value, base + offset, sizeof(T));
			return value;
		}

		// A persistent id's entity, resolved the way the game resolves it. Its
		// cached handle is transient and stays empty until the game asks for that
		// copy of the id, so most ids in the Mass data have none. The lookup runs
		// on a copy, so the game's data is left as it was. 0 = no entity.
		Entity HandleOf(const uint8_t* persistentId, const UWorld* world)
		{
			uint8_t copy[kMaxIdCopy]{};
			if (!world || kPersistentSize.size == 0 || kPersistentSize.size > sizeof(copy))
				return 0;
			std::memcpy(copy, persistentId, kPersistentSize.size);
			Handle h{};
			g_getHandle(copy, &h, world);
			return h.index > 0 ? ToEntity(h) : 0;
		}

		RawArray ArrayAt(const uint8_t* base, int32_t offset)
		{
			RawArray a = ReadAt<RawArray>(base, offset);
			if (!a.data || a.num < 0 || a.num > kMaxArray)
				return RawArray{ nullptr, 0, 0 };
			return a;
		}

		const UScriptStruct* FindStruct(const char* name)
		{
			return UObject::FindObjectFast<UScriptStruct>(name);
		}

		const UScriptStruct* Found(const UScriptStruct*& cache, const char* name)
		{
			if (!cache)
				cache = FindStruct(name);
			return cache;
		}

		const void* Manager(const UWorld* world)
		{
			if (!world)
				return nullptr;
			const uint8_t* subsystem = static_cast<const uint8_t*>(g_getSubsystem(world));
			return PluginMap::MassEntityManagerOf(subsystem);
		}

		// A built, current entity's fragment, or null.
		const uint8_t* Fragment(const void* manager, Entity entity, const UScriptStruct* type)
		{
			if (!entity || !type)
				return nullptr;
			const Handle h = ToHandle(entity);
			if (!g_isValid(manager, h) || !g_isBuilt(manager, h))
				return nullptr;
			return static_cast<const uint8_t*>(g_getFragment(manager, h, type));
		}

		UAuItemDataBase* ItemOf(const uint8_t* simpleItem)
		{
			auto* item = ReadAt<UAuItemDataBase*>(simpleItem, mItemData.Offset());
			return item && UKismetSystemLibrary::IsValid(item) ? item : nullptr;
		}

		// The socket pair a socket is on, as a key: its invisible connector's
		// persistent ID, which the game sets on both sockets of a pair at load
		// (from the socket's SocketPairIndex). 0 when the socket has no pair: the
		// ID is then INDEX_NONE. The handle cached after the ID can differ per
		// copy, so it is left out.
		uint64_t TrackOf(const uint8_t* socket)
		{
			const int32_t id = ReadAt<int32_t>(socket + mSocketPair.Offset(), 0);
			return id == -1 ? 0 : static_cast<uint64_t>(static_cast<uint32_t>(id)) + 1;
		}

		const uint8_t* SharedFragment(const void* manager, Entity entity, const UScriptStruct* type);
		void ReadCrafterImpl(Entity building, CrafterState& out);

		// A building of the type that makes and takes is storage when it has no
		// recipe to run and keeps an inventory: items rest there, and the rails
		// out of it carry what the rails into it brought (INFERRED from the
		// depots seen in the game, which were type 2 with no recipe).
		bool HoldsStockOnly(const void* manager, Entity building)
		{
			CrafterState crafter;
			ReadCrafterImpl(building, crafter);
			return !crafter.recipe && Fragment(manager, building, Found(g_inventoryStruct, "CrInventoryFragment")) != nullptr;
		}

		void ReadEndsImpl(const void* manager, const UWorld* world, Entity rail, Entity& start, Entity& end, bool& twoWay)
		{
			const uint8_t* spline = Fragment(manager, rail, Found(g_splineStruct, "AuSplineConnectionFragment"));
			if (!spline)
				return;
			start = HandleOf(spline + mStartEntity.Offset(), world);
			end   = HandleOf(spline + mEndEntity.Offset(), world);
			const uint8_t* params = SharedFragment(manager, rail, Found(g_paramsStruct, "CrLogisticsLineParameters"));
			twoWay = params && !ReadAt<bool>(params, mOneWay.Offset());
		}

		// The rails at the node's sockets. Which way each runs comes from the
		// rail's own ends, whatever the socket's type says.
		void ReadNodeImpl(Entity node, RailNetwork::NodeView& out, bool& found)
		{
			const UWorld* world = PluginSdkRuntime::GetWorld();
			const void* manager = Manager(world);
			const uint8_t* logistics = manager ? SharedFragment(manager, node, Found(g_logisticsStruct, "CrLogisticsParameters")) : nullptr;
			if (!logistics)
				return;
			found = true;
			const uint8_t type = ReadAt<uint8_t>(logistics, mLogisticsType.Offset());
			using RailNetwork::NodeKind;
			const LogisticsTypes& t = g_types;
			out.kind = type == t.lineNode || type == t.lineConnection ? NodeKind::PassThrough
			         : type == t.storage || (type == t.both && HoldsStockOnly(manager, node)) ? NodeKind::Storage
			         : NodeKind::Machine;
			out.role = type == t.producer ? RailNetwork::Role::Producer
			         : type == t.consumer ? RailNetwork::Role::Consumer
			         : RailNetwork::Role::Other;

			const uint8_t* sockets = Fragment(manager, node, Found(g_socketsStruct, "CrLogisticsSocketsFragment"));
			if (!sockets)
				return;
			const RawArray list = ArrayAt(sockets, mSockets.Offset());
			const bool tracks = g_trackResolved && g_trackFeature.On();
			for (int32_t i = 0; i < list.num; ++i)
			{
				const uint8_t* socket = PluginMap::ElementAt(list.data, list.num, i, kSocketSize);
				if (!socket)
					break;
				const Entity rail = ReadAt<Entity>(socket, mSocketRail.Offset());
				if (!rail || ReadAt<uint8_t>(socket, mSocketType.Offset()) == 0)
					continue;
				Entity start = 0, end = 0;
				bool twoWay = false;
				ReadEndsImpl(manager, world, rail, start, end, twoWay);
				RailNetwork::NodeLink link;
				link.other = start == node ? end : end == node ? start : 0;
				link.in    = end == node || (twoWay && start == node);
				link.out   = start == node || (twoWay && end == node);
				link.rail  = rail;
				link.track = tracks ? TrackOf(socket) : 0;
				if (link.other && link.other != node)
					out.links.push_back(link);
			}
		}

		// A request's item, and with the requests feature its type and the
		// deliveries assigned to it.
		void ReadRequestData(const uint8_t* data, const UWorld* world, std::vector<RequestView>& out)
		{
			UAuItemDataBase* item = ItemOf(data + mWantedItem.Offset());
			if (!item)
				return;
			RequestView view;
			view.wanted = item;
			if (g_requestsResolved && g_requestsFeature.On())
			{
				view.store = ReadAt<uint8_t>(data, mRequestType.Offset()) == g_requestStore;
				const RawArray runs = ArrayAt(data, mRuntimeData.Offset());
				for (int32_t i = 0; i < runs.num && i < kMaxDeliveries; ++i)
				{
					const uint8_t* run = PluginMap::ElementAt(runs.data, runs.num, i, kRuntimeSize);
					if (!run)
						break;
					Delivery delivery;
					delivery.source      = HandleOf(run + mItemSource.Offset(), world);
					delivery.destination = HandleOf(run + mItemDestination.Offset(), world);
					if (delivery.source || delivery.destination)
						view.deliveries.push_back(delivery);
				}
			}
			out.push_back(std::move(view));
		}

		// Pending requests hold their data in place; the others are entities
		// of their own.
		void ReadRequestsImpl(Entity building, std::vector<RequestView>& out)
		{
			const UWorld* world = PluginSdkRuntime::GetWorld();
			const void* manager = Manager(world);
			const uint8_t* container = manager ? Fragment(manager, building, Found(g_containerStruct, "CrLogisticsRequestContainerFragment")) : nullptr;
			if (!container)
				return;

			const RawArray pending = ArrayAt(container, mPending.Offset());
			for (int32_t i = 0; i < pending.num && out.size() < kMaxWanted; ++i)
			{
				const uint8_t* data = PluginMap::ElementAt(pending.data, pending.num, i, kRequestSize);
				if (!data)
					break;
				ReadRequestData(data, world, out);
			}

			const UScriptStruct* requestType = Found(g_requestStruct, "CrLogisticsRequestFragment");
			const RawArray requests = ArrayAt(container, mRequests.Offset());
			for (int32_t i = 0; i < requests.num && out.size() < kMaxWanted; ++i)
			{
				const uint8_t* id = PluginMap::ElementAt(requests.data, requests.num, i, kPersistentSize);
				if (!id)
					break;
				const Entity entity = HandleOf(id, world);
				const uint8_t* request = Fragment(manager, entity, requestType);
				if (request)
					ReadRequestData(request + mRequestData.Offset(), world, out);
			}
		}

		void ReadCrafterImpl(Entity building, CrafterState& out)
		{
			const void* manager = Manager(PluginSdkRuntime::GetWorld());
			const uint8_t* fragment = manager ? Fragment(manager, building, Found(g_craftingStruct, "CrCraftingFragment")) : nullptr;
			if (!fragment)
				return;

			out.isCrafter = true;
			auto* recipe = ReadAt<UAuItemRecipeData*>(fragment, mSelectedRecipe.Offset());
			if (!recipe)
				recipe = ReadAt<UAuItemRecipeData*>(fragment, mCurrentRecipe.Offset());
			out.recipe  = recipe && UKismetSystemLibrary::IsValid(recipe) ? recipe : nullptr;
			out.speed   = ReadAt<float>(fragment, mCraftingSpeed.Offset());
			const bool missing    = ReadAt<bool>(fragment, mMissingItems.Offset());
			const bool outputFull = ReadAt<bool>(fragment, mOutputFull.Offset());
			out.stalled = missing || outputFull;
			out.starved = missing && !outputFull;
		}

		// A built, current entity's const shared fragment, or null. The address
		// is worked out the way the game's GetConstSharedFragmentDataPtr does.
		const uint8_t* SharedFragment(const void* manager, Entity entity, const UScriptStruct* type)
		{
			if (!entity || !type)
				return nullptr;
			const Handle h = ToHandle(entity);
			if (!g_isValid(manager, h) || !g_isBuilt(manager, h))
				return nullptr;
			const auto* shared = static_cast<const uint8_t* const*>(g_getShared(manager, h, type));
			const uint8_t* memory = shared ? *shared : nullptr;
			const auto* memoryType = memory ? ReadAt<const UStruct*>(memory, 0) : nullptr;
			if (!memoryType || memoryType->MinAlignment <= 0)
				return nullptr;
			const uintptr_t align = static_cast<uintptr_t>(memoryType->MinAlignment);
			return reinterpret_cast<const uint8_t*>(
				(reinterpret_cast<uintptr_t>(memory) + sizeof(void*) - 1 + align) & ~(align - 1));
		}

		// The lane's throughput, the way UCrLogisticsLineSimulateMovementProcessor
		// moves drones along it. Each tick a drone moves MoveSpeed x dt and stops
		// SlotLength behind the drone ahead. A waiting drone joins once the space
		// left at the start (the lane's length, less the last drone's distance and
		// half a SlotLength, or nothing on a start lane) reaches the settings'
		// HalfRailSlotLengthDefault. A drone arriving while none wait may join
		// sooner, but once drones queue, which is when the rail is full, they join
		// the second way. So a full rail runs drones no closer than the larger of
		// the two gaps. The request subsystem loads a drone with at most one item
		// (UpdateRequests clamps the carried count to 1), whatever the item.
		void ReadCapacityImpl(Entity rail, float& perMin)
		{
			if (!Found(g_lineStruct, "CrLogisticsLineFragment") || !Found(g_paramsStruct, "CrLogisticsLineParameters"))
				return;
			const void* manager = Manager(PluginSdkRuntime::GetWorld());
			const uint8_t* line   = manager ? Fragment(manager, rail, g_lineStruct) : nullptr;
			const uint8_t* params = line ? SharedFragment(manager, rail, g_paramsStruct) : nullptr;
			const auto* settings  = reinterpret_cast<const uint8_t*>(UDronesDeveloperSettings::GetDefaultObj());
			if (!params || !settings)
				return;

			const float speed     = ReadAt<float>(params, mLineMoveSpeed.Offset());
			const float slot      = ReadAt<float>(params, mLineSlotLength.Offset());
			const float joinSpace = ReadAt<float>(settings, mHalfSlotLength.Offset());
			const bool  startLine = ReadAt<bool>(line, mIsStartLine.Offset());

			const float joinGap = (startLine ? 0.0f : 0.5f * slot) + joinSpace;
			const float gap     = slot > joinGap ? slot : joinGap;
			if (speed > 0.0f && gap > 0.0f && std::isfinite(speed / gap))
				perMin = speed / gap * 60.0f;
		}

		// The building a built entity is: the placement data in its building
		// parameters, when that is building data.
		void ReadBuildingImpl(Entity building, UCrBuildingData*& out)
		{
			if (!Found(g_buildingStruct, "CrBuildingParameters"))
				return;
			const void* manager = Manager(PluginSdkRuntime::GetWorld());
			const uint8_t* params = manager ? SharedFragment(manager, building, g_buildingStruct) : nullptr;
			if (!params)
				return;

			auto* placement = ReadAt<UObject*>(params, mPlacement.Offset());
			if (placement && UKismetSystemLibrary::IsValid(placement) && placement->IsA(UCrBuildingData::StaticClass()))
				out = static_cast<UCrBuildingData*>(placement);
		}

		// What a storage holds: both containers of its inventory fragment, one
		// row per item. UCrBuildingItemStorageComponent::
		// GetStoredItemsContainerInternal hands the storage's card one of them,
		// picked by the component's InventoryType, a per-building setting the
		// Mass data does not carry; the two together are what it holds,
		// whichever it is.
		void ReadStoredImpl(Entity storage, std::vector<StoredItemEntry>& out, bool& found)
		{
			const void* manager = Manager(PluginSdkRuntime::GetWorld());
			const uint8_t* inventory = manager ? Fragment(manager, storage, Found(g_inventoryStruct, "CrInventoryFragment")) : nullptr;
			if (!inventory)
				return;
			found = true;
			int32_t usedSlots = 0;
			std::vector<StoredItemEntry> main;
			ReadStoredItems(reinterpret_cast<const SDK::FCrItemsStorageContainer*>(inventory + mInputInventory.Offset()), out, usedSlots);
			ReadStoredItems(reinterpret_cast<const SDK::FCrItemsStorageContainer*>(inventory + mMainInventory.Offset()), main, usedSlots);
			for (StoredItemEntry& entry : main)
			{
				const auto same = std::find_if(out.begin(), out.end(),
					[&](const StoredItemEntry& held) { return held.uniqueName == entry.uniqueName; });
				if (same != out.end())
					same->count += entry.count;
				else
					out.push_back(std::move(entry));
			}
		}

		// The cargo connections the package transport replicator lists: the item
		// and package size of each, when its latest send started, and the Mass
		// entity at each end. The replicator names a building by its persistent
		// id, which is resolved here like any other. `interval` is its send
		// interval and `serverNow` the server time, for the rates.
		void ReadPackageLinksImpl(std::vector<PackageRates::Link>& out, float& interval, double& serverNow)
		{
			const UWorld* world = PluginSdkRuntime::GetWorld();
			AGameStateBase* gameState = world ? UGameplayStatics::GetGameState(world) : nullptr;
			if (!gameState || !gameState->IsA(ACrGameStateBase::StaticClass()))
				return;
			auto* replicator = Fields::ValueOr<ACrPackageTransportReplicator*>(
				Fields::PackageTransportReplicator, static_cast<ACrGameStateBase*>(gameState), nullptr);
			if (!replicator || !UKismetSystemLibrary::IsValid(replicator))
				return;

			interval  = Fields::ValueOr<float>(Fields::SendingTime, replicator);
			serverNow = gameState->GetServerWorldTimeSeconds();

			// The persistent id sits at the helper's Entity member, which must lie
			// inside the helper as the loader sizes it.
			const PluginFieldSpec& entityField = Fields::HelperEntity;
			if (entityField.offset < 0 || kPersistentSize.size == 0
				|| static_cast<uint32_t>(entityField.offset) + kPersistentSize.size > Fields::HelperSize.size)
				return;
			auto resolve = [&](const void* helper)
			{
				return HandleOf(static_cast<const uint8_t*>(helper) + entityField.offset, world);
			};

			const auto* container = Fields::ConnectionsContainer.In(replicator);
			const Fields::StructArray connections(Fields::ConnectionsData.In(container), Fields::ConnectionDataSize);
			for (int32_t i = 0; i < connections.Num() && i < kMaxArray; ++i)
			{
				const uint8_t* entry = connections.At(i);
				auto* item = Fields::ValueOr<UCrItemDataBase*>(Fields::ConnectionItem, entry, nullptr);
				if (!item || !UKismetSystemLibrary::IsValid(item))
					continue;
				PackageRates::Link link;
				link.sender   = resolve(Fields::ConnectionSender.In(entry));
				link.receiver = resolve(Fields::ConnectionReceiver.In(entry));
				link.item     = Fields::UniqueNameOf(item);
				link.amount   = Fields::ValueOr<int32_t>(Fields::ConnectionAmount, entry);
				link.start    = static_cast<float>(Fields::StartSendTimeOf(entry));
				out.push_back(std::move(link));
			}
		}

		// A bad pointer in the game's data must not take the game down.
		// A fault turns off the reads that share `faulted`; the capacity and the
		// building reads keep their own, so one of them failing leaves the rest.
		template <class Fn>
		bool Guarded(Fn&& fn, bool& faulted = g_faulted)
		{
			struct Call
			{
				static bool Run(void* ctx, void (*thunk)(void*), DWORD* code)
				{
					__try
					{
						thunk(ctx);
						return true;
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						*code = GetExceptionCode();
						return false;
					}
				}
			};
			DWORD code = 0;
			const bool ok = Call::Run(&fn, [](void* ctx) { (*static_cast<Fn*>(ctx))(); }, &code);
			if (!ok)
			{
				faulted = true;
				LOG_WARN("RailMass: exception 0x%08lX while reading the Mass data; this read is off for the session", code);
			}
			return ok;
		}
	}

	bool Declare(IPluginSelf* self, IPluginHookScanner* scanner, const std::vector<const char*>& alsoNeeds)
	{
		// The keys every rail read starts from. The other features list them
		// too, so their own misses turn off only themselves while a miss here
		// turns off all of them.
		std::vector<const char*> core;
		for (const SizeRow* row : kSizeRows)
			core.push_back(row->key);
		for (const Member* member : kMembers)
			core.push_back(member->spec.key);
		core.push_back(PluginMap::MassEntityManagerField.key);
		core.push_back(kKeyGetSubsystem);
		core.push_back(kKeyGetFragment);
		core.push_back(kKeyIsValid);
		core.push_back(kKeyIsBuilt);
		core.push_back(kKeyGetHandle);
		core.push_back(kKeyGetShared);

		g_feature.keys = core;
		g_feature.keys.insert(g_feature.keys.end(), alsoNeeds.begin(), alsoNeeds.end());

		g_capacityFeature.keys = core;
		for (const Member* member : kCapacityMembers)
			g_capacityFeature.keys.push_back(member->spec.key);

		g_buildingFeature.keys = core;
		for (const Member* member : kBuildingMembers)
			g_buildingFeature.keys.push_back(member->spec.key);

		g_trackFeature.keys = core;
		for (const Member* member : kTrackMembers)
			g_trackFeature.keys.push_back(member->spec.key);

		g_stockFeature.keys = core;
		g_stockFeature.keys.push_back(kContainerSize.key);
		for (const Member* member : kStockMembers)
			g_stockFeature.keys.push_back(member->spec.key);

		g_requestsFeature.keys = core;
		g_requestsFeature.keys.push_back(kRuntimeSize.key);
		for (const Member* member : kRequestMembers)
			g_requestsFeature.keys.push_back(member->spec.key);

		return PluginMap::DeclareFeatures(scanner, self, g_feature, g_capacityFeature, g_buildingFeature,
			g_trackFeature, g_stockFeature, g_requestsFeature);
	}

	void Resolve(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		if (!self || !scanner)
			return;

		// A member, once the size it is checked against is known. A miss is
		// recorded by the loader and turns off the feature that lists it. The
		// struct sizes are resolved first, so a member whose struct did not
		// resolve is skipped here; that miss is already recorded.
		auto resolveMember = [&](Member& member)
		{
			const uint32_t size = member.type ? member.type->size : member.fixed;
			if (!size)
				return false;
			member.spec.size = size;
			return PluginMap::ResolveField(scanner, self, member.spec);
		};

		// Every key is asked even after a miss, so the log lists them all.
		bool all = true;
		for (SizeRow* row : kSizeRows)
			all = ResolveSize(scanner, self, *row) && all;
		if (kPersistentSize.size == 0 || kPersistentSize.size > kMaxIdCopy)
		{
			LOG_WARN("RailMass: %s is %u bytes on this build, more than the %u HandleOf copies",
				kPersistentSize.key, kPersistentSize.size, kMaxIdCopy);
			all = false;
		}

		all = ResolveEnum("ECrMassLogisticsType", "Storage", g_types.storage) && all;
		all = ResolveEnum("ECrMassLogisticsType", "ProducerConsumer", g_types.both) && all;
		all = ResolveEnum("ECrMassLogisticsType", "Producer", g_types.producer) && all;
		all = ResolveEnum("ECrMassLogisticsType", "Consumer", g_types.consumer) && all;
		all = ResolveEnum("ECrMassLogisticsType", "TransportLineNode", g_types.lineNode) && all;
		all = ResolveEnum("ECrMassLogisticsType", "TransportLineConnection", g_types.lineConnection) && all;
		all = ResolveEnum("ECrLogisticsRequestType", "Store", g_requestStore) && all;

		for (Member* member : kMembers)
			all = resolveMember(*member) && all;
		all = PluginMap::ResolveMassEntityManager(scanner, self) && all;

		bool capacity = true;
		for (Member* member : kCapacityMembers)
			capacity = resolveMember(*member) && capacity;
		bool building = true;
		for (Member* member : kBuildingMembers)
			building = resolveMember(*member) && building;
		bool tracks = true;
		for (Member* member : kTrackMembers)
			tracks = resolveMember(*member) && tracks;
		bool stock = ResolveSize(scanner, self, kContainerSize);
		for (Member* member : kStockMembers)
			stock = resolveMember(*member) && stock;
		bool requests = ResolveSize(scanner, self, kRuntimeSize);
		for (Member* member : kRequestMembers)
			requests = resolveMember(*member) && requests;

		const uintptr_t getSubsystem = ResolveFunction(scanner, self, kKeyGetSubsystem, kPatGetSubsystem, kAtGetSubsystem);
		const uintptr_t getFragment  = ResolveFunction(scanner, self, kKeyGetFragment, kPatGetFragment, kAtGetFragment);
		const uintptr_t isValid      = ResolveFunction(scanner, self, kKeyIsValid, kPatIsBuilt, kAtIsValid);
		const uintptr_t isBuilt      = ResolveFunction(scanner, self, kKeyIsBuilt, kPatIsBuilt, -1);
		const uintptr_t getHandle    = ResolveFunction(scanner, self, kKeyGetHandle, kPatGetHandle, -1);
		const uintptr_t getShared    = ResolveFunction(scanner, self, kKeyGetShared, kPatGetShared, kAtGetShared);
		g_getSubsystem = reinterpret_cast<GetSubsystemFn>(getSubsystem);
		g_getFragment  = reinterpret_cast<GetFragmentFn>(getFragment);
		g_isValid      = reinterpret_cast<EntityCheckFn>(isValid);
		g_isBuilt      = reinterpret_cast<EntityCheckFn>(isBuilt);
		g_getHandle    = reinterpret_cast<GetHandleFn>(getHandle);
		g_getShared    = reinterpret_cast<GetSharedFn>(getShared);

		g_resolved         = all && getSubsystem && getFragment && isValid && isBuilt && getHandle && getShared;
		g_capacityResolved = capacity;
		g_buildingResolved = building;
		g_trackResolved    = tracks;
		g_stockResolved    = stock;
		g_requestsResolved = requests;
		if (!g_resolved || !g_feature.On())
			LOG_WARN("RailMass: rail sources are off on this build; the rest of WAILA is unaffected");
		else if (!g_capacityResolved || !g_capacityFeature.On())
			LOG_WARN("RailMass: the rail capacity is off on this build; a rail shows no max, the rest of its card is unaffected");
		if (g_resolved && g_feature.On() && (!g_buildingResolved || !g_buildingFeature.On()))
			LOG_WARN("RailMass: building names are off on this build; a rail's ends read Unknown building, the rest of its card is unaffected");
		if (g_resolved && g_feature.On() && (!g_trackResolved || !g_trackFeature.On()))
			LOG_WARN("RailMass: lane tracks are not told apart on this build; a rail on a lane lists every track's machines, the rest of its card is unaffected");
		if (g_resolved && g_feature.On() && (!g_stockResolved || !g_stockFeature.On()))
			LOG_WARN("RailMass: storage stock is off on this build; a rail shows no stock lines, the rest of its card is unaffected");
		if (g_resolved && g_feature.On() && (!g_requestsResolved || !g_requestsFeature.On()))
			LOG_WARN("RailMass: request deliveries are off on this build; a rail does not name the buildings behind its requests, the rest of its card is unaffected");
	}

	bool Available()
	{
		return g_resolved && g_feature.On() && !g_faulted
			&& g_getSubsystem && g_getFragment && g_isValid && g_isBuilt && g_getHandle && g_getShared;
	}

	bool CapacityAvailable()
	{
		return Available() && !g_capacityFaulted && g_capacityResolved && g_capacityFeature.On();
	}

	bool BuildingAvailable()
	{
		return Available() && !g_buildingFaulted && g_buildingResolved && g_buildingFeature.On();
	}

	Entity EntityOf(AActor* actor)
	{
		if (!Available() || !actor || !UKismetSystemLibrary::IsValid(actor))
			return 0;
		const auto* component = ReadAt<const UObject*>(reinterpret_cast<const uint8_t*>(actor), mMassComponent.Offset());
		if (!component || !UKismetSystemLibrary::IsValid(component))
			return 0;
		const Entity e = ReadAt<Entity>(reinterpret_cast<const uint8_t*>(component), mEntityHandle.Offset());
		return ToHandle(e).index > 0 ? e : 0;
	}

	bool ReadEnds(Entity rail, Entity& start, Entity& end, bool& twoWay)
	{
		start = end = 0;
		twoWay = false;
		if (!Available())
			return false;
		return Guarded([&]
			{
				const UWorld* world = PluginSdkRuntime::GetWorld();
				if (const void* manager = Manager(world))
					ReadEndsImpl(manager, world, rail, start, end, twoWay);
			}) && start && end;
	}

	bool ReadNode(Entity node, RailNetwork::NodeView& out)
	{
		out = RailNetwork::NodeView{};
		if (!Available())
			return false;
		bool found = false;
		return Guarded([&] { ReadNodeImpl(node, out, found); }) && found;
	}

	bool ReadRequests(Entity building, std::vector<RequestView>& out)
	{
		out.clear();
		if (!Available())
			return false;
		return Guarded([&] { ReadRequestsImpl(building, out); });
	}

	bool ReadCrafter(Entity building, CrafterState& out)
	{
		if (!Available())
			return false;
		return Guarded([&] { ReadCrafterImpl(building, out); });
	}

	bool ReadBuilding(Entity building, UCrBuildingData*& out)
	{
		out = nullptr;
		if (!BuildingAvailable())
			return false;
		return Guarded([&] { ReadBuildingImpl(building, out); }, g_buildingFaulted) && out != nullptr;
	}

	bool ReadCapacity(Entity rail, float& perMin)
	{
		if (!CapacityAvailable())
			return false;
		return Guarded([&] { ReadCapacityImpl(rail, perMin); }, g_capacityFaulted);
	}

	bool ReadStored(Entity storage, std::vector<StoredItemEntry>& out)
	{
		out.clear();
		if (!Available() || g_stockFaulted || !g_stockResolved || !g_stockFeature.On())
			return false;
		bool found = false;
		const bool ok = Guarded([&] { ReadStoredImpl(storage, out, found); }, g_stockFaulted) && found;
		if (!ok)
			out.clear();
		return ok;
	}

	bool ReadPackageLinks(std::vector<PackageRates::Link>& out, float& interval, double& serverNow)
	{
		out.clear();
		interval  = 0.0f;
		serverNow = 0.0;
		if (!Available() || g_packageFaulted || !Features::ShowPackageRates())
			return false;
		const bool ok = Guarded([&] { ReadPackageLinksImpl(out, interval, serverNow); }, g_packageFaulted);
		if (!ok)
			out.clear();
		return ok;
	}
}
