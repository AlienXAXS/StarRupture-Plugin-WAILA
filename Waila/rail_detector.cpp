#include "rail_detector.h"
#include "rail_network.h"
#include "rail_mass.h"
#include "package_rates.h"
#include "storage_detector.h"
#include "crafter_detector.h"
#include "plugin_helpers.h"
#include "waila_functions.h"
#include "waila_fields.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuCrafting_classes.hpp"
#include "AuCrafting_structs.hpp"
#include "AuItems_classes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace SDK;

namespace Waila
{
	namespace
	{
		// Bounds for the network walk. Both stop a runaway walk and set
		// RailInfo::truncated when they bite.
		constexpr size_t kMaxRails   = 1024;
		constexpr size_t kMaxSources = 256;

		constexpr std::chrono::milliseconds kRefresh{ 1000 };

		// Everything the walk found for one rail, kept between refreshes: each
		// machine's part in the flow of an item (rail_network.h, SumFlows), the
		// totals, and the items' names. Item data is held as a weak pointer and
		// resolved on use. `rail` is only ever compared, never dereferenced: a
		// recycled address could at worst show the previous rail's numbers until
		// the next refresh.
		//
		// The walk is the game's own rail network from its Mass data, loaded or
		// not (FindAttached, rail_mass.h). When the Mass reader is off, the loaded
		// rail layout stands in: only actors the game has spawned are visible
		// to GetAllActorsOfClass, and player buildings are streamed in around the
		// camera (INFERRED), so that one can cut a long rail short.
		struct ItemInfo
		{
			FWeakObjectPtr item;
			std::string    displayName;
		};

		struct Cache
		{
			const AActor*                             rail = nullptr;
			std::chrono::steady_clock::time_point     stamp{};
			bool                                      fromMass  = false;
			bool                                      found     = false;   // the Mass walk could start from the rail
			bool                                      linked    = false;   // it found a building or storage linked to the rail
			bool                                      truncated = false;
			float                                     capacity  = -1.0f;   // items a minute; negative when unread
			bool                                      starvedFeeder = false;   // a maker upstream is short of an input
			std::vector<RailNetwork::FlowPart>        parts;
			std::vector<RailNetwork::ItemFlow>        flows;
			std::unordered_map<std::string, ItemInfo> items;

			// What the storage the walk passed through holds, per item, and the
			// name of each storage holding it.
			struct Stock
			{
				int64_t                  count = 0;
				std::vector<std::string> storages;
			};
			std::unordered_map<std::string, Stock> stock;
		};

		Cache g_cache;

		// The cargo connections' send rates, measured from the replicator's list
		// (package_rates.h) and sampled on the tick whether or not a rail is
		// aimed at, so a rate is there by the time one is.
		PackageRates::Tracker g_rates;
		float                 g_sampleClock  = 0.0f;
		constexpr float       kSampleSeconds = 1.0f;

		// A Cargo Receiver's or Sender's part takes its connections' rate; with
		// none for one of them yet it is counted and named but adds to no total.
		void TakeRate(RailNetwork::FlowPart& part, const PackageRates::Rate& rate)
		{
			part.unrated = !rate.known;
			if (!rate.known)
				return;
			part.full = rate.full;
			part.now  = rate.now;
			// A feeder whose connections have all stopped sending can make
			// nothing now, which is what a starved maker counts as.
			part.starved = part.source && rate.full > 0.0f && rate.now <= 0.0f;
		}

		// Items per minute of one recipe at a crafting speed: output count /
		// BuildTime x speed x 60. countOverride (when above 0) replaces the
		// recipe's own output count.
		bool RecipeRate(UAuItemRecipeData* recipe, int32_t countOverride, float speed, float& outFull)
		{
			return CountRate(recipe, countOverride > 0 ? countOverride : Fields::RecipeOutputCount(recipe), speed, outFull);
		}

		// What a crafter works on: the recipe it is making, or for an idle crafter
		// the recipe it is set to, the output count of its queue entry (0 when it
		// has none) and its crafting speed. CraftingSpeed is the live one from the
		// crafting fragment, falling back to the class default. CraftingMultiplier
		// is left out (INFERRED: it sets how many are requested, not how fast they
		// are made). False when there is no recipe.
		struct CrafterWork
		{
			UAuItemRecipeData* recipe      = nullptr;
			int32_t            outputCount = 0;
			float              speed       = 0.0f;
		};

		bool CrafterWorkOf(ACrCrafter* crafter, CrafterWork& out)
		{
			UCrCraftingComponent* comp = Fields::ValueOr<UCrCraftingComponent*>(Fields::CraftComponent, crafter, nullptr);
			if (!comp)
				return false;

			const FCrCraftingFragment* frag = nullptr;
			if (auto fnGetFrag = Waila::Functions::GetCraftingFragment())
				frag = fnGetFrag(comp);

			// ItemsToCraft is empty while a crafter waits for input (seen on every
			// crafter just after a save loads); the fragment still holds the recipe
			// the player picked (INFERRED).
			const Fields::QueuedCraft first = Fields::FirstQueuedCraft(comp);
			const bool queued = first.recipe != nullptr;

			UAuItemRecipeData* recipe = queued ? first.recipe
									: frag     ? static_cast<UAuItemRecipeData*>(Fields::ValueOr<UCrItemRecipeData*>(Fields::FragSelected, frag, nullptr))
												: nullptr;
			if (!recipe)
				return false;

			const FCrCraftingSettings* settings = Fields::CraftingSettings.In(crafter);
			out.recipe      = recipe;
			out.outputCount = queued ? first.outputCount : 0;
			out.speed       = frag ? Fields::ValueOr<float>(Fields::FragSpeed, frag)
			                       : Fields::ValueOr<float>(Fields::SettingsSpeed, settings);
			return true;
		}

		// Items per minute of one crafter: output count / BuildTime x CraftingSpeed
		// x 60. outItem is set whenever there is a recipe; the return value is
		// false when the rate itself can't be worked out.
		bool CrafterFullRate(ACrCrafter* crafter, UAuItemDataBase*& outItem, float& outFull)
		{
			outItem = nullptr;

			CrafterWork work;
			if (!CrafterWorkOf(crafter, work))
				return false;
			outItem = work.recipe->GetItemDataBase();
			if (!outItem)
				return false;
			return RecipeRate(work.recipe, work.outputCount, work.speed, outFull);
		}

		RailNetwork::Vec3 ToVec(const FVector& v) { return { v.X, v.Y, v.Z }; }

		// The buildings drones serve, fetched once per scan. The scene holds their
		// bounds and floor height at the same indices; the crafter pointers (null
		// for storage and the like) are only used inside that scan.
		std::vector<ACrCrafter*> GatherBuildings(UWorld* world, RailNetwork::Scene& scene)
		{
			TArray<AActor*> all{};
			UGameplayStatics::GetAllActorsOfClass(world, ACrDroneSupplySystemBuildingBase::StaticClass(), &all);

			std::vector<ACrCrafter*> crafters;
			for (int32_t i = 0; i < all.Num(); ++i)
			{
				AActor* building = all[i];
				if (!building || !UKismetSystemLibrary::IsValid(building))
					continue;

				ACrCrafter* crafter = building->IsA(ACrCrafter::StaticClass()) ? static_cast<ACrCrafter*>(building) : nullptr;

				FVector origin{}, extent{};
				building->GetActorBounds(false, &origin, &extent, false);
				scene.buildings.push_back({ ToVec(origin), ToVec(extent), building->K2_GetActorLocation().Z, crafter != nullptr });
				crafters.push_back(crafter);
			}
			return crafters;
		}

		// The rail sockets of every path point: poles, rail supports, junctions,
		// mergers and lanes.
		void GatherPathPoints(UWorld* world, RailNetwork::Scene& scene)
		{
			TArray<AActor*> all{};
			UGameplayStatics::GetAllActorsOfClass(world, ACrDronePathPoint::StaticClass(), &all);

			std::vector<RailNetwork::NamedSocket> named;
			for (int32_t i = 0; i < all.Num(); ++i)
			{
				auto* point = static_cast<ACrDronePathPoint*>(all[i]);
				if (!point || !UKismetSystemLibrary::IsValid(point))
					continue;

				named.clear();
				const Fields::StructArray sockets(Fields::SocketsData.In(point), Fields::SocketDataSize);
				for (int32_t s = 0; s < sockets.Num(); ++s)
				{
					const uint8_t* socket = sockets.At(s);
					const UStaticMeshSocket* mesh = Fields::ValueOr<UStaticMeshSocket*>(Fields::SocketRef, socket, nullptr);
					const FTransform* transform = Fields::SocketTransform.In(socket);
					if (mesh && transform)
						named.push_back({ mesh->SocketName.ToString(), ToVec(transform->Translation) });
				}
				scene.AddPathPoint(named);
			}
		}

		// World positions of a rail's first and last spline point. False when the
		// spline is missing.
		bool RailEnds(ACrDronePathPointConnection* rail, RailNetwork::Rail& out)
		{
			USplineComponent* spline = Fields::ValueOr<USplineComponent*>(Fields::Spline, rail, nullptr);
			if (!spline || !UKismetSystemLibrary::IsValid(spline))
				return false;

			const int32_t points = spline->GetNumberOfSplinePoints();
			if (points < 2)
				return false;

			out.start = ToVec(spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World));
			out.end   = ToVec(spline->GetLocationAtSplinePoint(points - 1, ESplineCoordinateSpace::World));
			return true;
		}

		// Every rail with a spline. The pointers are only compared, at the same
		// indices as the scene's rails.
		std::vector<const AActor*> GatherRails(UWorld* world, RailNetwork::Scene& scene)
		{
			TArray<AActor*> all{};
			UGameplayStatics::GetAllActorsOfClass(world, ACrDronePathPointConnection::StaticClass(), &all);

			std::vector<const AActor*> rails;
			for (int32_t i = 0; i < all.Num(); ++i)
			{
				auto* rail = static_cast<ACrDronePathPointConnection*>(all[i]);
				RailNetwork::Rail ends;
				if (!rail || !UKismetSystemLibrary::IsValid(rail) || !RailEnds(rail, ends))
					continue;

				scene.rails.push_back(ends);
				rails.push_back(rail);
			}
			return rails;
		}

		// The layout names a crafter by its address, only ever compared.
		uint64_t BuildingKey(ACrCrafter* crafter)
		{
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(crafter));
		}

		// Stands in for a building the game's data cannot name: the building names
		// are off on this build or faulted, the entity is gone or not built, it has
		// no building parameters, or they hold no building data (see ReadBuilding).
		const std::string kUnknownBuilding = "Unknown building";

		// A loaded building's name, from its actor.
		std::string NameOf(ACrCrafter* crafter)
		{
			std::string name, desc;
			if (Fields::ReadBuildingTexts(Fields::BuildingDataOf(crafter), name, desc) && !name.empty())
				return name;
			return kUnknownBuilding;
		}

		// What a recipe takes at a crafting speed, by item unique name: count /
		// BuildTime x speed x 60 a minute for each ingredient.
		struct Input
		{
			std::string item;
			float       perMin = 0.0f;
		};

		std::vector<Input> InputsOf(UAuItemRecipeData* recipe, float speed)
		{
			std::vector<Input> inputs;
			const Fields::StructArray needed(Fields::NeededResources.In(recipe), Fields::RecipeOrderSize);
			for (int32_t i = 0; i < needed.Num(); ++i)
			{
				const uint8_t* order = needed.At(i);
				UAuItemDataBase* item = Fields::ItemDefaultsOf(Fields::OrderItemClass(order));
				float rate = 0.0f;
				if (item && CountRate(recipe, Fields::ValueOr<int32_t>(Fields::OrderCount, order), speed, rate))
					inputs.push_back({ Fields::UniqueNameOf(item), rate });
			}
			return inputs;
		}

		void Remember(Cache& out, UAuItemDataBase* item, const std::string& unique)
		{
			if (!item || out.items.count(unique))
				return;
			ItemInfo info;
			info.item.ObjectIndex        = item->Index;
			info.item.ObjectSerialNumber = 0;
			info.displayName = Fields::DisplayNameOf(item);
			out.items.emplace(unique, std::move(info));
		}

		// The crafters the rail layout finds: the ones feeding the stretch, at
		// their output with every one running, and the ones taking items off the
		// line ahead of it. A crafter with no recipe has no item to count. Their
		// state is in the Mass data, which this fallback runs without, so no now
		// figure.
		void CollectNetwork(const std::vector<ACrCrafter*>& crafters, const RailNetwork::Sources& sources,
		                    const RailNetwork::Consumers& consumers, Cache& out)
		{
			for (size_t source : sources.crafters)
			{
				ACrCrafter* crafter = crafters[source];
				UAuItemDataBase* item = nullptr;
				float full = 0.0f;
				const bool readable = CrafterFullRate(crafter, item, full);
				if (!item)
					continue;

				const std::string unique = Fields::UniqueNameOf(item);
				Remember(out, item, unique);
				RailNetwork::FlowPart part{ BuildingKey(crafter), unique, true, readable ? full : -1.0f };
				part.name = NameOf(crafter);
				out.parts.push_back(std::move(part));
			}

			for (size_t consumer : consumers.crafters)
			{
				ACrCrafter* crafter = crafters[consumer];
				CrafterWork work;
				if (!crafter || !CrafterWorkOf(crafter, work))
					continue;
				for (const Input& in : InputsOf(work.recipe, work.speed))
				{
					RailNetwork::FlowPart taker{ BuildingKey(crafter), in.item, false, in.perMin };
					taker.name = NameOf(crafter);
					out.parts.push_back(std::move(taker));
				}
			}
		}

		// ---- From the game's Mass data (rail_mass.h) ---------------------------

		// The building's name from its Mass entity's building parameters, which
		// every entity keeps whether or not its actor is loaded.
		std::string BuildingNameOf(RailMass::Entity entity)
		{
			SDK::UCrBuildingData* data = nullptr;
			std::string name, desc;
			if (RailMass::ReadBuilding(entity, data) && Fields::ReadBuildingTexts(data, name, desc) && !name.empty())
				return name;
			return kUnknownBuilding;
		}

		// A storage's name for the stock line: "Storage Depot v.2", "2x Storage
		// Depot v.2", or a count when the names differ.
		std::string StorageLabel(const std::vector<std::string>& names)
		{
			if (names.empty())
				return std::string();
			const bool same = std::all_of(names.begin(), names.end(), [&](const std::string& n) { return n == names.front(); });
			if (!same)
				return std::to_string(names.size()) + " storages";
			return names.size() > 1 ? std::to_string(names.size()) + "x " + names.front() : names.front();
		}

		// What a taker downstream does with the rail's items: the ingredients its
		// recipe takes at its crafting speed, and its open requests.
		struct Taker
		{
			RailMass::Entity                  machine = 0;
			std::vector<Input>                inputs;
			std::vector<RailMass::RequestView> requests;
			bool                              requestsRead = false;
		};

		// The machines attached to the rail's network (FindAttached) and each
		// one's part in the sums. An upstream crafter makes its recipe's item at
		// its full rate (0 now while stalled). Storage upstream holds the items
		// in its stock, which can leave on the rail; the machines behind it are
		// found by the walk, which passes through it. A downstream machine takes
		// each item the rail carries that its recipe uses or its requests ask
		// for, now only while it has a request open for it. A filtered rail only
		// carries that item. When the walk finds no maker for an item a
		// downstream machine asks for, the source its request names stands in,
		// and when it finds no taker for an item a maker ships out, the
		// destination the maker's request names. False when the rail's ends
		// cannot be read.
		bool CollectAttached(RailMass::Entity rail, const std::string& filterUnique, Cache& out)
		{
			RailMass::Entity start = 0, end = 0;
			bool twoWay = false;
			if (!RailMass::ReadEnds(rail, start, end, twoWay))
				return false;
			std::vector<uint64_t> from{ start };
			std::vector<uint64_t> to{ end };
			if (twoWay)
			{
				from.push_back(end);
				to.push_back(start);
			}

			const RailNetwork::Attached attached = RailNetwork::FindAttached(from, to,
				[](uint64_t node, RailNetwork::NodeView& view) { return RailMass::ReadNode(node, view); },
				kMaxRails, kMaxSources, rail);
			out.truncated = attached.truncated;
			out.linked = !attached.upstream.empty() || !attached.downstream.empty()
				|| !attached.upstreamStorage.empty() || !attached.downstreamStorage.empty();

			std::unordered_map<uint64_t, std::string> names;
			auto nameOf = [&](uint64_t entity) -> const std::string&
			{
				const auto [at, added] = names.try_emplace(entity);
				if (added)
					at->second = BuildingNameOf(entity);
				return at->second;
			};
			auto carriesItem = [&](const std::string& unique) { return filterUnique.empty() || unique == filterUnique; };

			const std::unordered_set<uint64_t> upstreamStorage(attached.upstreamStorage.begin(), attached.upstreamStorage.end());
			std::unordered_set<uint64_t> upstreamSet(attached.upstream.begin(), attached.upstream.end());
			upstreamSet.insert(upstreamStorage.begin(), upstreamStorage.end());

			// The items on the rail: those a building makes or storage upstream holds.
			std::unordered_set<std::string> carried;
			for (const RailMass::Entity machine : attached.upstream)
			{
				RailMass::CrafterState crafter;
				if (!RailMass::ReadCrafter(machine, crafter) || !crafter.isCrafter || !crafter.recipe)
					continue;
				UAuItemDataBase* item = crafter.recipe->GetItemDataBase();
				if (!item)
					continue;
				const std::string unique = Fields::UniqueNameOf(item);
				if (!carriesItem(unique))
					continue;

				Remember(out, item, unique);
				carried.insert(unique);
				float full = -1.0f;
				if (!RecipeRate(crafter.recipe, 0, crafter.speed, full))
					full = -1.0f;
				out.starvedFeeder = out.starvedFeeder || crafter.starved;
				RailNetwork::FlowPart part{ machine, unique, true, full };
				part.now  = full >= 0.0f ? (crafter.stalled ? 0.0f : full) : -1.0f;
				part.starved = crafter.starved;
				part.name = nameOf(machine);
				const auto through = attached.upstreamVia.find(machine);
				if (through != attached.upstreamVia.end())
					part.via = nameOf(through->second);
				out.parts.push_back(std::move(part));
			}

			// The stock of every storage the walks passed through, counted once:
			// all of what the storage upstream holds, which can leave on the rail,
			// and of the storage downstream the items the rail carries. Storage
			// holding an item is listed at that end when no building is.
			std::unordered_set<uint64_t> counted;
			std::vector<uint64_t> readStorages;   // those whose stock was read, empty ones too
			std::vector<StoredItemEntry> held;
			auto count = [&](const std::vector<uint64_t>& storages, bool upstream)
			{
				for (const uint64_t storage : storages)
				{
					if (!counted.insert(storage).second || !RailMass::ReadStored(storage, held))
						continue;
					readStorages.push_back(storage);
					for (const StoredItemEntry& entry : held)
					{
						if (entry.uniqueName.empty() || !carriesItem(entry.uniqueName))
							continue;
						if (!upstream && !carried.count(entry.uniqueName))
							continue;
						Remember(out, entry.itemData, entry.uniqueName);
						Cache::Stock& stock = out.stock[entry.uniqueName];
						stock.count += entry.count;
						stock.storages.push_back(nameOf(storage));

						RailNetwork::FlowPart part{ storage, entry.uniqueName, upstream };
						part.name    = nameOf(storage);
						part.storage = true;
						part.stock   = static_cast<float>(entry.count);
						out.parts.push_back(std::move(part));
						if (upstream)
							carried.insert(entry.uniqueName);
					}
				}
			};
			count(attached.upstreamStorage, true);

			std::vector<Taker> takers;
			for (const RailMass::Entity machine : attached.downstream)
			{
				Taker taker;
				taker.machine = machine;
				RailMass::CrafterState crafter;
				if (RailMass::ReadCrafter(machine, crafter) && crafter.isCrafter && crafter.recipe)
					taker.inputs = InputsOf(crafter.recipe, crafter.speed);
				taker.requestsRead = RailMass::ReadRequests(machine, taker.requests);
				if (!taker.inputs.empty() || !taker.requests.empty())
					takers.push_back(std::move(taker));
			}

			// Cargo Receivers upstream put items on the rail at the rate their
			// connections send (package_rates.h), which counts like a maker's.
			// The items are those the replicator lists as sent to it. A receiver
			// it does not list is named and counted as a source of the items it
			// brings but adds to no total. Those are the items a request names it
			// the source of, else those its own inventory holds, else those held
			// by the storage it feeds (the one the walk passed on the way back to
			// it, or with no other source on the rail the storage downstream).
			const bool noOtherSource = carried.empty();
			// The item behind a request, for the name and icon of one the
			// replicator names; null when no taker asks for it.
			auto wantedItem = [&](const std::string& unique) -> UAuItemDataBase*
			{
				for (const Taker& taker : takers)
					for (const RailMass::RequestView& request : taker.requests)
						if (Fields::UniqueNameOf(request.wanted) == unique)
							return request.wanted;
				return nullptr;
			};
			for (const RailMass::Entity machine : attached.upstream)
			{
				RailMass::CrafterState crafter;
				RailNetwork::NodeView view;
				if ((RailMass::ReadCrafter(machine, crafter) && crafter.isCrafter && crafter.recipe)
					|| !RailMass::ReadNode(machine, view) || view.role != RailNetwork::Role::Producer)
					continue;

				std::vector<std::pair<std::string, UAuItemDataBase*>> brings;
				auto bring = [&](const std::string& unique, UAuItemDataBase* item)
				{
					if (unique.empty() || !carriesItem(unique)
						|| std::any_of(brings.begin(), brings.end(), [&](const auto& b) { return b.first == unique; }))
						return;
					brings.emplace_back(unique, item);
				};
				const std::vector<std::string> sent = g_rates.ItemsOf(machine, true);
				for (const std::string& unique : sent)
					bring(unique, wantedItem(unique));
				const auto through = attached.upstreamVia.find(machine);
				if (sent.empty())
				{
					for (const Taker& taker : takers)
						for (const RailMass::RequestView& request : taker.requests)
							for (const RailMass::Delivery& delivery : request.deliveries)
								if (delivery.source == machine)
									bring(Fields::UniqueNameOf(request.wanted), request.wanted);
					if (RailMass::ReadStored(machine, held))
						for (const StoredItemEntry& entry : held)
							bring(entry.uniqueName, entry.itemData);
					if (brings.empty() && (through != attached.upstreamVia.end() || noOtherSource))
					{
						const std::vector<uint64_t> feeds = through != attached.upstreamVia.end()
							? std::vector<uint64_t>{ through->second } : attached.downstreamStorage;
						for (const uint64_t storage : feeds)
							if (RailMass::ReadStored(storage, held))
								for (const StoredItemEntry& entry : held)
									if (entry.count > 0)
										bring(entry.uniqueName, entry.itemData);
					}
				}

				for (const auto& [unique, item] : brings)
				{
					Remember(out, item, unique);
					carried.insert(unique);
					RailNetwork::FlowPart part{ machine, unique, true };
					part.name = nameOf(machine);
					TakeRate(part, g_rates.Of(machine, true, unique));
					out.parts.push_back(std::move(part));
				}
			}

			// An item a taker asks for from a source on the way back to the rail
			// is on the rail even when no maker was found for it.
			for (const Taker& taker : takers)
			{
				for (const RailMass::RequestView& request : taker.requests)
				{
					const std::string unique = Fields::UniqueNameOf(request.wanted);
					if (!carriesItem(unique) || carried.count(unique))
						continue;
					for (const RailMass::Delivery& delivery : request.deliveries)
					{
						if (!upstreamSet.count(delivery.source))
							continue;
						Remember(out, request.wanted, unique);
						carried.insert(unique);
						RailNetwork::FlowPart part{ delivery.source, unique, true };
						part.name    = nameOf(delivery.source);
						part.storage = upstreamStorage.count(delivery.source) != 0;
						if (!part.storage)
							TakeRate(part, g_rates.Of(delivery.source, true, unique));
						out.parts.push_back(std::move(part));
						break;
					}
				}
			}

			std::unordered_set<std::string> taken;
			for (const Taker& taker : takers)
			{
				const std::string& name = nameOf(taker.machine);
				auto requesting = [&](const std::string& item)
				{
					return std::any_of(taker.requests.begin(), taker.requests.end(),
						[&](const RailMass::RequestView& r) { return Fields::UniqueNameOf(r.wanted) == item; });
				};
				// An item only a request names has no recipe rate: a Cargo Sender
				// takes it at its connections' rate, any other taker is counted and
				// named but adds to no total.
				auto add = [&](const std::string& item, float perMin)
				{
					RailNetwork::FlowPart part{ taker.machine, item, false, perMin };
					float nowPerMin = perMin;
					part.unrated = perMin < 0.0f;
					if (part.unrated)
					{
						const PackageRates::Rate rate = g_rates.Of(taker.machine, false, item);
						TakeRate(part, rate);
						if (rate.known)
							nowPerMin = rate.now;
					}
					part.now     = !taker.requestsRead ? -1.0f : requesting(item) ? nowPerMin : 0.0f;
					part.name    = name;
					out.parts.push_back(std::move(part));
					taken.insert(item);
				};
				std::unordered_set<std::string> added;
				for (const Input& in : taker.inputs)
				{
					if (carried.count(in.item) && added.insert(in.item).second)
						add(in.item, in.perMin);
				}
				for (const RailMass::RequestView& request : taker.requests)
				{
					const std::string unique = Fields::UniqueNameOf(request.wanted);
					if (carried.count(unique) && added.insert(unique).second)
						add(unique, -1.0f);
				}
			}

			// Cargo Senders downstream take what the rail carries at the rate of
			// their connections. One the replicator does not list, with no request
			// naming an item on the rail, takes them all at no known rate.
			for (const RailMass::Entity machine : attached.downstream)
			{
				RailMass::CrafterState crafter;
				RailNetwork::NodeView view;
				if ((RailMass::ReadCrafter(machine, crafter) && crafter.isCrafter && crafter.recipe)
					|| !RailMass::ReadNode(machine, view) || view.role != RailNetwork::Role::Consumer
					|| std::any_of(out.parts.begin(), out.parts.end(),
						[&](const RailNetwork::FlowPart& p) { return !p.source && p.building == machine; }))
					continue;
				const std::vector<std::string> sends = g_rates.ItemsOf(machine, false);
				for (const std::string& item : carried)
				{
					if (!sends.empty() && std::find(sends.begin(), sends.end(), item) == sends.end())
						continue;
					RailNetwork::FlowPart part{ machine, item, false };
					part.name = nameOf(machine);
					TakeRate(part, g_rates.Of(machine, false, item));
					out.parts.push_back(std::move(part));
					taken.insert(item);
				}
			}
			count(attached.downstreamStorage, false);

			// Storage the walk passed that holds none of an item says so: the
			// stock line stays whenever the walk passes storage.
			if (!readStorages.empty())
				for (const std::string& item : carried)
					if (!out.stock.count(item))
					{
						Cache::Stock& none = out.stock[item];
						for (const uint64_t storage : readStorages)
							none.storages.push_back(nameOf(storage));
					}

			// An item no building takes: where the requests of the makers behind
			// the rail ship it to.
			if (std::any_of(carried.begin(), carried.end(), [&](const std::string& item) { return !taken.count(item); }))
			{
				std::unordered_map<uint64_t, bool> isStorage;
				std::vector<RailMass::RequestView> requests;
				for (const RailMass::Entity machine : attached.upstream)
				{
					if (!RailMass::ReadRequests(machine, requests))
						continue;
					for (const RailMass::RequestView& request : requests)
					{
						const std::string unique = Fields::UniqueNameOf(request.wanted);
						if (!request.store || !carried.count(unique) || taken.count(unique))
							continue;
						for (const RailMass::Delivery& delivery : request.deliveries)
						{
							if (!delivery.destination)
								continue;
							auto kind = isStorage.find(delivery.destination);
							if (kind == isStorage.end())
							{
								RailNetwork::NodeView view;
								kind = isStorage.emplace(delivery.destination,
									RailMass::ReadNode(delivery.destination, view) && view.kind == RailNetwork::NodeKind::Storage).first;
							}
							RailNetwork::FlowPart part{ delivery.destination, unique, false };
							part.name    = nameOf(delivery.destination);
							part.storage = kind->second;
							out.parts.push_back(std::move(part));
						}
					}
				}
			}
			return true;
		}

		// An item's name and icon.
		void DescribeItem(const std::string& unique, std::string& display, UAuItemDataBase*& data)
		{
			const auto info = g_cache.items.find(unique);
			if (info == g_cache.items.end())
			{
				// An item only the replicator names has no data object to read.
				display = unique;
				return;
			}
			display = info->second.displayName;
			// A dead item just loses its icon.
			UObject* obj = info->second.item.Get();
			if (obj && UKismetSystemLibrary::IsValid(obj) && obj->IsA(UAuItemDataBase::StaticClass()))
				data = static_cast<UAuItemDataBase*>(obj);
		}
	}

	bool RailDetector::IsRail(AActor* actor)
	{
		if (!actor || !UKismetSystemLibrary::IsValid(actor))
			return false;

		return actor->IsA(ACrDronePathPointConnection::StaticClass());
	}

	bool RailDetector::GetRailInfo(AActor* actor, RailInfo& outInfo)
	{
		if (!IsRail(actor))
			return false;

		ACrDronePathPointConnection* rail = static_cast<ACrDronePathPointConnection*>(actor);

		UCrBuildingData* buildingData = Fields::BuildingDataOf(rail);

		outInfo.buildingData = buildingData;
		if (!Fields::ReadBuildingTexts(buildingData, outInfo.buildingName, outInfo.buildingDesc))
		{
			outInfo.buildingName = actor->Class ? actor->Class->GetName() : "Drone Rail";
		}

		// A null filter means the rail carries anything (INFERRED).
		const UAuItemDataBase* filter = rail->GetFilter();
		std::string filterUnique;
		if (filter && UKismetSystemLibrary::IsValid(filter))
		{
			outInfo.filterName = Fields::DisplayNameOf(filter);
			filterUnique       = Fields::UniqueNameOf(filter);
		}

		// Rails are only compared by address here, never dereferenced. With actor
		// pooling an address can come back as a different rail, which shows the old
		// rail's numbers for at most one refresh.
		const auto now = std::chrono::steady_clock::now();
		const bool fromMass = Features::ShowRailSources() && Features::ShowCrafters();
		if (g_cache.rail != actor || g_cache.fromMass != fromMass || now - g_cache.stamp >= kRefresh)
		{
			g_cache = Cache{};
			g_cache.rail     = actor;
			g_cache.stamp    = now;
			g_cache.fromMass = fromMass;

			if (fromMass)
			{
				const RailMass::Entity entity = RailMass::EntityOf(actor);
				g_cache.found = entity && CollectAttached(entity, filterUnique, g_cache);
				if (g_cache.found && Features::ShowRailCapacity())
					RailMass::ReadCapacity(entity, g_cache.capacity);
			}

			if (!fromMass && Features::ShowDroneRails())
			{
				UWorld* world = PluginSdkRuntime::GetWorld();
				RailNetwork::Scene scene;
				const std::vector<const AActor*> rails    = world ? GatherRails(world, scene) : std::vector<const AActor*>{};
				const std::vector<ACrCrafter*>   crafters = world ? GatherBuildings(world, scene) : std::vector<ACrCrafter*>{};
				if (world)
					GatherPathPoints(world, scene);

				const auto self = std::find(rails.begin(), rails.end(), actor);
				if (self != rails.end())
				{
					const RailNetwork::Graph   graph(scene);
					const RailNetwork::Sources sources = graph.Find(
						static_cast<size_t>(self - rails.begin()), kMaxRails, kMaxSources);
					const RailNetwork::Consumers consumers = graph.FindConsumers(sources, kMaxRails, kMaxSources);
					g_cache.truncated = sources.truncated || consumers.truncated;
					CollectNetwork(crafters, sources, consumers, g_cache);
				}
				if (!filterUnique.empty())
					g_cache.parts.erase(std::remove_if(g_cache.parts.begin(), g_cache.parts.end(),
						[&](const RailNetwork::FlowPart& p) { return p.item != filterUnique; }), g_cache.parts.end());
			}
			g_cache.flows = RailNetwork::SumFlows(g_cache.parts, g_cache.capacity);
		}

		// One row per item on the rail: its totals, the kinds of building making
		// and taking it, and its stock.
		std::unordered_set<std::string> onRail;
		for (const RailNetwork::ItemFlow& flow : g_cache.flows)
			onRail.insert(flow.item);
		const std::vector<RailNetwork::EndRow> ends = fromMass ? RailNetwork::GroupEnds(g_cache.parts, onRail)
		                                                       : std::vector<RailNetwork::EndRow>{};
		bool judged = false, feedShort = false;
		for (const RailNetwork::ItemFlow& flow : g_cache.flows)
		{
			judged    = judged || flow.judged;
			feedShort = feedShort || flow.short_;
			RailRateEntry entry;
			entry.perMinMax = flow.max;
			entry.perMinMin = flow.min;
			entry.perMinNow = flow.now;
			entry.plusUnknown = flow.plusUnknown;
			DescribeItem(flow.item, entry.displayName, entry.itemData);

			std::vector<RailNetwork::EndRow> fromRows, toRows;
			for (const RailNetwork::EndRow& end : ends)
			{
				if (end.item != flow.item)
					continue;
				(end.source ? fromRows : toRows).push_back(end);
				(end.source ? outInfo.fromCount : outInfo.destCount) += end.count;
			}
			std::vector<RailNetwork::EndSpan> fromSpans, toSpans;
			entry.from = RailNetwork::EndsLine(fromRows, 3, &fromSpans);
			entry.to   = RailNetwork::EndsLine(toRows, 3, &toSpans);
			for (const RailNetwork::EndSpan& span : fromSpans)
				if (span.starved)
					entry.fromStarved.push_back({ span.begin, span.end });
			if (flow.short_)
			{
				entry.perMinShort = flow.shortBy;
				for (const RailNetwork::EndSpan& span : toSpans)
					entry.toShort.push_back({ span.begin, span.end });
			}

			const auto held = g_cache.stock.find(flow.item);
			if (held != g_cache.stock.end())
			{
				entry.stored   = held->second.count;
				entry.storedIn = StorageLabel(held->second.storages);
			}
			outInfo.rates.push_back(std::move(entry));
			outInfo.sourceCount += flow.sources;
		}
		outInfo.truncated = g_cache.truncated;
		outInfo.fromMass  = fromMass;
		outInfo.capacity  = g_cache.capacity;
		outInfo.feed      = g_cache.starvedFeeder ? RailInfo::Feed::Starved
		                  : feedShort             ? RailInfo::Feed::Short
		                  : judged                ? RailInfo::Feed::Fed : RailInfo::Feed::Unjudged;

		if (outInfo.rates.empty() && fromMass)
			outInfo.note = !g_cache.found ? "Rail data not available"
			             : !g_cache.linked ? "Nothing connected"
			             : "Nothing upstream makes or stores items for this rail";

		return true;
	}

	void RailDetector::SampleRates(float deltaSeconds)
	{
		g_sampleClock += deltaSeconds;
		if (g_sampleClock < kSampleSeconds)
			return;
		g_sampleClock = 0.0f;

		std::vector<PackageRates::Link> links;
		float  interval  = 0.0f;
		double serverNow = 0.0;
		if (RailMass::ReadPackageLinks(links, interval, serverNow))
			g_rates.Observe(links, serverNow, interval);
	}

	void RailDetector::Reset()
	{
		g_cache = Cache{};
		g_rates.Clear();
		g_sampleClock = 0.0f;
	}
}
