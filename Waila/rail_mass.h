#pragma once

#include "rail_network.h"
#include "package_rates.h"

#include <cstdint>
#include <vector>

struct IPluginSelf;
struct IPluginHookScanner;

namespace SDK { class AActor; class UAuItemDataBase; class UAuItemRecipeData; class UCrBuildingData; }

// Reads the game's drone logistics from its Mass simulation, which holds every
// rail and building of the save whether or not its actor is loaded: a rail
// names the two nodes it joins, a node lists the rails at its sockets, and a
// machine's crafting fragment and requests say what it makes and wants.
//
// Every member of every Mass fragment and every struct stride comes from the
// loader by name (ResolveField). Keys are declared as six features: "rail
// sources" (Waila::Features::RailSources), "rail capacity", "rail
// buildings", "rail tracks", "rail stock" and "rail requests". The last five
// list the same core keys, so a name the game no longer has turns off only
// what it feeds: the sources, the max, the names of the buildings at the
// rail's ends, keeping a lane's tracks apart, the stock lines, or the
// buildings named behind a request.
namespace Waila { struct StoredItemEntry; }

namespace Waila::RailMass
{
	// A Mass entity, as the engine's FMassEntityHandle packs it: Index in the
	// low half, SerialNumber in the high half. 0 is no entity.
	using Entity = uint64_t;

	// Declares the features with every key the reader needs, and
	// `alsoNeeds`, the keys of the parts the sources build on (the building and
	// item names they show). Call first in OnPluginLoadHooks, with the other
	// features.
	bool Declare(IPluginSelf* self, IPluginHookScanner* scanner, const std::vector<const char*>& alsoNeeds);

	// Asks the loader for those keys. Call from OnPluginLoadHooks, after
	// Fields::Resolve and the declaration. A miss turns the feature off and
	// leaves the rest of WAILA running.
	void Resolve(IPluginSelf* self, IPluginHookScanner* scanner);

	// True when the sources feature is on, everything resolved and no read has
	// faulted. The capacity and the building names have their own gates on top of it.
	bool Available();
	bool CapacityAvailable();

	// The rail's, or a building's, own Mass entity. 0 when it has none yet.
	Entity EntityOf(SDK::AActor* actor);

	// What a crafter's Mass fragment says. isCrafter is false for storage and
	// stations, which have no crafting fragment.
	struct CrafterState
	{
		bool                    isCrafter = false;
		SDK::UAuItemRecipeData* recipe    = nullptr;   // valid only in the tick it was read
		float                   speed     = 0.0f;
		bool                    stalled   = false;     // missing items or output full
		bool                    starved   = false;     // missing items and not output full, as the machine card's "Starved"
	};

	// Game thread only. False when the reader is unavailable or a read faulted;
	// a fault turns the reader off for the rest of the session.
	//
	// The two nodes a rail joins, from its spline connection: items go from
	// start to end, and both ways when twoWay. False when either is unknown.
	bool ReadEnds(Entity rail, Entity& start, Entity& end, bool& twoWay);
	// What a node is and the rails at its sockets, each with its far end.
	bool ReadNode(Entity node, RailNetwork::NodeView& out);
	// The open requests of a building right now: the item each one wants and,
	// when the deliveries are read, whether it ships items out (Store) or brings
	// them in (Acquire) and where each assigned delivery picks up and brings to
	// (0 when not known). The item pointers are valid only in the tick they were read.
	struct Delivery
	{
		Entity source      = 0;
		Entity destination = 0;
	};

	struct RequestView
	{
		SDK::UAuItemDataBase* wanted = nullptr;
		bool                  store  = false;
		std::vector<Delivery> deliveries;
	};

	bool ReadRequests(Entity building, std::vector<RequestView>& out);
	bool ReadCrafter(Entity building, CrafterState& out);

	// The building a Mass entity is, from its building parameters (the entity
	// keeps them whether or not its actor is loaded). The pointer is valid only
	// in the tick it was read. False, with `out` null, when: the reader or the
	// building names are off (a key missing on this build, or an earlier read
	// faulted); the building parameters' struct is not found; the entity is gone
	// or not built; it has no building parameters; or they hold no building data.
	bool ReadBuilding(Entity building, SDK::UCrBuildingData*& out);

	// The most items a minute the rail's lane lets through, from the lane's own
	// speed and spacing. A drone carries one item, so this holds for any item.
	// Same rules as above; false leaves perMin as it was.
	bool ReadCapacity(Entity rail, float& perMin);

	// What a storage holds, per item, from both containers of its inventory
	// fragment: its own card lists the one it keeps items in. The item pointers are valid only
	// in the tick they were read. Same rules as above, with the stock's own
	// gate; false leaves `out` empty.
	bool ReadStored(Entity storage, std::vector<StoredItemEntry>& out);

	// The cargo connections the package transport replicator lists, each with
	// the Mass entity of its sender and receiver (0 when one does not resolve),
	// the replicator's send interval in seconds (0 when unread) and the server
	// time now. Own gate (the package-rates feature) and own fault flag. False,
	// with `out` empty, when the gate is off or a read faulted.
	bool ReadPackageLinks(std::vector<PackageRates::Link>& out, float& interval, double& serverNow);
}
