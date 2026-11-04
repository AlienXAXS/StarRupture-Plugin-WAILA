// Console tests for Waila/rail_network.h: a few hand-built layouts, the rails
// around a captured supermagnet platform (fixtures\supermagnet-platform.txt),
// the walk to the machines attached to a rail, then the one-row-per-item sums
// of a rail card and its From/To lines.
// Prints each failure and returns non-zero when any check fails.

#include "rail_network.h"
#include "package_rates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Waila::RailNetwork;

namespace
{
	int g_failures = 0;

	void Check(bool ok, const std::string& what)
	{
		if (!ok)
		{
			++g_failures;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	std::set<size_t> SourcesOf(const Scene& scene, size_t rail)
	{
		const Sources found = Graph(scene).Find(rail, 1024, 256);
		return { found.crafters.begin(), found.crafters.end() };
	}

	std::set<size_t> ConsumersOf(const Scene& scene, size_t rail)
	{
		const Graph graph(scene);
		const Consumers found = graph.FindConsumers(graph.Find(rail, 1024, 256), 1024, 256);
		return { found.crafters.begin(), found.crafters.end() };
	}

	size_t AddRail(Scene& scene, Vec3 start, Vec3 end)
	{
		scene.rails.push_back({ start, end });
		return scene.rails.size() - 1;
	}

	// A 2 m cube standing on the floor of its bounds.
	size_t AddBuilding(Scene& scene, Vec3 origin, bool crafter)
	{
		scene.buildings.push_back({ origin, { 100, 100, 100 }, origin.z - 100, crafter });
		return scene.buildings.size() - 1;
	}

	void TestSocketNames()
	{
		int32_t lane = 99;
		Check(SocketLane("RailSocket", lane) && lane == kAnyLane, "RailSocket joins every socket");
		Check(SocketLane("RailSocket3", lane) && lane == kAnyLane, "RailSocket3 joins every socket");
		Check(SocketLane("Rail_0B", lane) && lane == 0, "Rail_0B is lane 0");
		Check(SocketLane("rail_12a", lane) && lane == 12, "rail_12a is lane 12");
		Check(SocketLane("Rail_Socket", lane) && lane == kAnyLane, "Rail_Socket has no lane");
		Check(!SocketLane("PowerSocket", lane), "PowerSocket holds no rail");
	}

	// Two crafters feed two parallel tracks of one lane building. Each track's
	// far rail shows only its own crafter.
	void TestLanesStaySeparate()
	{
		Scene scene;
		const size_t a = AddBuilding(scene, { 0, 0, 0 }, true);
		const size_t b = AddBuilding(scene, { 0, 1000, 0 }, true);
		scene.AddPathPoint({ { "RailSocket", { 500, 0, 0 } },   { "Rail_0B", { 608, 0, 0 } },
		                     { "Rail_1A", { 500, 20, 0 } },     { "Rail_1B", { 608, 20, 0 } } });
		AddRail(scene, { 100, 0, 0 }, { 500, 0, 0 });
		AddRail(scene, { 0, 900, 0 }, { 500, 20, 0 });
		const size_t outA = AddRail(scene, { 608, 0, 0 }, { 2000, 0, 0 });
		const size_t outB = AddRail(scene, { 608, 20, 0 }, { 2000, 20, 0 });

		Check(SourcesOf(scene, outA) == std::set<size_t>{ a }, "lane 0 shows only its crafter");
		Check(SourcesOf(scene, outB) == std::set<size_t>{ b }, "lane 1 shows only its crafter");
	}

	// A junction joins all its sockets; a crafter's input rail does not show the
	// crafter it feeds; storage passes its inputs through.
	void TestJunctionStorageAndInputs()
	{
		Scene scene;
		const size_t a       = AddBuilding(scene, { 0, 0, 0 }, true);
		const size_t b       = AddBuilding(scene, { 0, 2000, 0 }, true);
		AddBuilding(scene, { 3000, 1000, 0 }, false);   // storage
		const size_t user    = AddBuilding(scene, { 6000, 1000, 0 }, true);
		scene.AddPathPoint({ { "RailSocket0", { 1500, 946, 0 } }, { "RailSocket1", { 1500, 1054, 0 } },
		                     { "RailSocket2", { 1554, 1000, 0 } } });
		AddRail(scene, { 100, 0, 0 }, { 1500, 946, 0 });
		AddRail(scene, { 0, 1900, 0 }, { 1500, 1054, 0 });
		const size_t toStorage = AddRail(scene, { 1554, 1000, 0 }, { 2900, 1000, 0 });
		const size_t toUser    = AddRail(scene, { 3100, 1000, 0 }, { 5900, 1000, 0 });
		const size_t fromUser  = AddRail(scene, { 6100, 1000, 0 }, { 9000, 1000, 0 });

		Check(SourcesOf(scene, toStorage) == std::set<size_t>{ a, b }, "junction joins both inputs");
		Check(SourcesOf(scene, toUser) == std::set<size_t>{ a, b }, "storage passes its inputs through");
		Check(SourcesOf(scene, fromUser) == std::set<size_t>{ user }, "an output rail shows only its own crafter");
	}

	// A crafter feeds a line; a branch off the first point ends at crafter m and
	// one into storage; a branch off the second point ends at crafter n; the line
	// ends at crafter z. A stretch shows the crafters branching off before it,
	// never storage and never the crafter its own end feeds.
	void TestConsumersBranchOffTheLine()
	{
		Scene scene;
		const size_t src     = AddBuilding(scene, { 0, 0, 0 }, true);
		const size_t m       = AddBuilding(scene, { 1500, 5100, 0 }, true);
		const size_t storage = AddBuilding(scene, { 1500, -5100, 0 }, false);
		const size_t n       = AddBuilding(scene, { 3000, 5100, 0 }, true);
		const size_t z       = AddBuilding(scene, { 5100, 0, 0 }, true);
		(void)src; (void)storage; (void)z;
		scene.AddPathPoint({ { "RailSocket0", { 1500, 0, 0 } }, { "RailSocket1", { 1554, 0, 0 } },
		                     { "RailSocket2", { 1500, 80, 0 } }, { "RailSocket3", { 1500, -80, 0 } } });
		scene.AddPathPoint({ { "RailSocket0", { 3000, 0, 0 } }, { "RailSocket1", { 3054, 0, 0 } },
		                     { "RailSocket2", { 3000, 80, 0 } } });
		const size_t first  = AddRail(scene, { 100, 0, 0 }, { 1500, 0, 0 });
		const size_t main   = AddRail(scene, { 1554, 0, 0 }, { 3000, 0, 0 });
		const size_t last   = AddRail(scene, { 3054, 0, 0 }, { 5000, 0, 0 });
		AddRail(scene, { 1500, 80, 0 }, { 1500, 5000, 0 });
		AddRail(scene, { 1500, -80, 0 }, { 1500, -5000, 0 });
		AddRail(scene, { 3000, 80, 0 }, { 3000, 5000, 0 });

		Check(ConsumersOf(scene, first).empty(), "the stretch out of the crafter has no branch before it");
		Check(ConsumersOf(scene, main) == std::set<size_t>{ m }, "after the first branch: crafter m, not the storage");
		Check(ConsumersOf(scene, last) == std::set<size_t>{ m, n }, "after the second: m and n, not the crafter at the end");
	}

	// Two rails from one point into the same crafter: neither counts the crafter
	// its own end feeds.
	void TestConsumersLeaveOutOwnEnd()
	{
		Scene scene;
		AddBuilding(scene, { 0, 0, 0 }, true);
		const size_t z = AddBuilding(scene, { 3000, 5100, 0 }, true);
		(void)z;
		scene.AddPathPoint({ { "RailSocket0", { 1500, 0, 0 } }, { "RailSocket1", { 1554, 0, 0 } },
		                     { "RailSocket2", { 1500, 80, 0 } } });
		AddRail(scene, { 100, 0, 0 }, { 1500, 0, 0 });
		const size_t a = AddRail(scene, { 1554, 0, 0 }, { 3000, 5000, 0 });
		const size_t b = AddRail(scene, { 1500, 80, 0 }, { 3000, 5000, 0 });

		Check(ConsumersOf(scene, a).empty(), "a rail does not count the crafter it feeds");
		Check(ConsumersOf(scene, b).empty(), "nor does the rail beside it into the same crafter");
	}

	// Ends of a path point the game has not spawned join within kJoinTolerance.
	void TestLooseEnds()
	{
		Scene scene;
		const size_t a = AddBuilding(scene, { 0, 0, 0 }, true);
		AddRail(scene, { 100, 0, 0 }, { 1000, 0, 0 });
		const size_t near = AddRail(scene, { 1110, 0, 0 }, { 3000, 0, 0 });
		const size_t far  = AddRail(scene, { 1000, 130, 0 }, { 3000, 500, 0 });

		Check(SourcesOf(scene, near) == std::set<size_t>{ a }, "loose ends 110 apart join");
		Check(SourcesOf(scene, far).empty(), "loose ends 130 apart do not");
	}

	// A junction the game has not spawned sits inside a drill's 20 m tall
	// bounds, fed by a smelter. The drill, the smelter and the rail ends are as
	// captured; the smelter's rail into the junction is one rail here.
	void TestJunctionInsideDrill()
	{
		Scene scene;
		scene.buildings.push_back({ { -226142.594, -54458.769, 3350.953 }, { 733.513, 445.099, 980.614 }, 3035.253, true });
		scene.buildings.push_back({ { -227473.150, -54400.280, 3659.540 }, { 304.462, 198.482, 269.806 }, 3390.0, true });
		const size_t drill = 0, smelter = 1;

		const size_t drillOut = AddRail(scene, { -226843.355, -54458.769, 3345.253 }, { -226814.850, -53800.000, 3553.930 });
		AddRail(scene, { -227760, -54400, 3650 }, { -226400, -54854, 4116 });
		const size_t outA = AddRail(scene, { -226346, -54800, 4116 }, { -224854, -54800, 4116 });
		const size_t outB = AddRail(scene, { -226454, -54800, 4116 }, { -226500, -55003.953, 4210.188 });

		Check(SourcesOf(scene, drillOut) == std::set<size_t>{ drill }, "the drill's output shows the drill");
		Check(SourcesOf(scene, outA) == std::set<size_t>{ smelter }, "a junction inside the drill's bounds passes the smelter on");
		Check(SourcesOf(scene, outB) == std::set<size_t>{ smelter }, "both of its outputs show the smelter");
	}

	// Three parallel tracks cross a lane building and only the middle one is
	// fed. The socket and rail end positions at the lane are from a captured
	// Lane_3; the crafter and the far ends of its rails are made up. Each track
	// keeps to itself, whether the lane has loaded or its rail ends are loose.
	Scene LaneScene(bool loaded, size_t& crafter, std::vector<size_t>& out)
	{
		Scene scene;
		crafter = AddBuilding(scene, { -223654, -53200, 4950 }, true);
		if (loaded)
		{
			scene.AddPathPoint({ { "RailSocket", { -223254, -53200, 6146 } }, { "Rail_0B", { -223146, -53200, 6146 } },
			                     { "Rail_1A",    { -223254, -53220, 6146 } }, { "Rail_1B", { -223146, -53220, 6146 } },
			                     { "Rail_2A",    { -223254, -53180, 6146 } }, { "Rail_2B", { -223146, -53180, 6146 } } });
		}
		AddRail(scene, { -223854, -53200, 5726 }, { -223254, -53180, 6146 });
		AddRail(scene, { -223654, -53200, 5050 }, { -223254, -53200, 6146 });
		AddRail(scene, { -223454, -53200, 5586 }, { -223254, -53220, 6146 });
		out = { AddRail(scene, { -223146, -53180, 6146 }, { -223146, -53180, 6566 }),
		        AddRail(scene, { -223146, -53200, 6146 }, { -223146, -53200, 6566 }),
		        AddRail(scene, { -223146, -53220, 6146 }, { -223146, -53220, 6566 }) };
		return scene;
	}

	void TestLaneTracksStayApart()
	{
		for (bool loaded : { true, false })
		{
			size_t              crafter = 0;
			std::vector<size_t> out;
			const Scene         scene = LaneScene(loaded, crafter, out);
			const std::string   what  = loaded ? "a loaded lane" : "a lane that has not loaded";

			Check(SourcesOf(scene, out[1]) == std::set<size_t>{ crafter }, what + " carries its fed track");
			Check(SourcesOf(scene, out[0]).empty() && SourcesOf(scene, out[2]).empty(), what + " keeps its other tracks apart");
		}
	}

	struct Fixture
	{
		Scene                              scene;
		std::unordered_map<long, size_t>   rail;       // fixture id -> scene index
		std::unordered_map<size_t, long>   buildingId; // scene index -> fixture id
		std::unordered_map<size_t, std::string> item;
	};

	bool Load(const std::string& path, Fixture& out)
	{
		std::ifstream in(path);
		if (!in)
			return false;

		std::string line;
		while (std::getline(in, line))
		{
			std::istringstream s(line);
			std::string kind, cls, item;
			long id = 0;
			if (!(s >> kind) || kind[0] == '#' || !(s >> id))
				continue;

			if (kind == "rail")
			{
				Rail r;
				s >> r.start.x >> r.start.y >> r.start.z >> r.end.x >> r.end.y >> r.end.z;
				out.rail[id] = out.scene.rails.size();
				out.scene.rails.push_back(r);
			}
			else if (kind == "crafter" || kind == "storage")
			{
				Building b;
				s >> cls;
				if (kind == "crafter")
					s >> item;
				s >> b.origin.x >> b.origin.y >> b.origin.z >> b.extent.x >> b.extent.y >> b.extent.z >> b.floorZ;
				b.crafter = kind == "crafter";
				out.buildingId[out.scene.buildings.size()] = id;
				out.item[out.scene.buildings.size()] = item;
				out.scene.buildings.push_back(b);
			}
			else if (kind == "point")
			{
				s >> cls;
				std::vector<NamedSocket> sockets;
				NamedSocket socket;
				while (s >> socket.name >> socket.at.x >> socket.at.y >> socket.at.z)
					sockets.push_back(socket);
				out.scene.AddPathPoint(sockets);
			}
		}
		return true;
	}

	void TestPlatform(const std::string& dir)
	{
		Fixture f;
		if (!Load(dir + "fixtures\\supermagnet-platform.txt", f))
		{
			Check(false, "fixture loads");
			return;
		}

		const Graph graph(f.scene);
		auto ids = [&](size_t rail)
		{
			std::set<long> out;
			for (size_t c : graph.Find(rail, 1024, 256).crafters)
				out.insert(f.buildingId[c]);
			return out;
		};

		size_t outputs = 0, inputs = 0, largest = 0;
		for (size_t r = 0; r < f.scene.rails.size(); ++r)
		{
			const Sources found = graph.Find(r, 1024, 256);
			largest = std::max(largest, found.rails);

			const Port& start = graph.StartOf(r);
			if (start.kind == Port::Kind::Building && f.scene.buildings[start.index].crafter)
				++outputs;

			// A rail into a crafter never shows that crafter.
			const Vec3& end = f.scene.rails[r].end;
			for (size_t b = 0; b < f.scene.buildings.size(); ++b)
			{
				const Building& box = f.scene.buildings[b];
				if (!box.crafter
				 || std::abs(end.x - box.origin.x) > box.extent.x + kBuildingMargin
				 || std::abs(end.y - box.origin.y) > box.extent.y + kBuildingMargin
				 || std::abs(end.z - box.origin.z) > box.extent.z + kBuildingMargin)
					continue;
				++inputs;
				Check(std::find(found.crafters.begin(), found.crafters.end(), b) == found.crafters.end(),
				      "rail " + std::to_string(r) + " does not show the crafter it feeds");
			}
		}

		// The rails leaving a crafter, each carrying that crafter's output and
		// nothing else: six furnaces, two WolframPlate crafters and the drill.
		const std::pair<long, long> leaving[] = {
			{ 142428, 140907 }, { 140591, 140611 }, { 113035, 92073 }, { 301508, 301218 }, { 301553, 301533 },
			{ 302914, 302806 }, { 316068, 349173 }, { 316141, 349650 }, { 303440, 349745 } };
		for (const auto& [rail, crafter] : leaving)
			Check(ids(f.rail.at(rail)) == std::set<long>{ crafter }, "rail " + std::to_string(rail) + " shows only the crafter it leaves");
		Check(outputs == std::size(leaving), "the platform has " + std::to_string(std::size(leaving))
		      + " crafter output rails (found " + std::to_string(outputs) + ")");
		Check(inputs >= 10, "the platform has crafter input rails");
		Check(largest < 64, "no rail walks the whole platform (largest walk " + std::to_string(largest) + ")");

		// The merger into the platform's storage carries all six furnaces.
		Check(ids(f.rail.at(142511)) == std::set<long>{ 92073, 140611, 140907, 301218, 301533, 302806 },
		      "the rail into storage shows the six furnaces");

		// Out of storage, carrying what the six furnaces fill it with.
		Check(ids(f.rail.at(153565)) == std::set<long>{ 92073, 140611, 140907, 301218, 301533, 302806 },
		      "the rail out of storage shows the six furnaces");

		// Out of a merger the game has not spawned, joined by loose ends.
		Check(ids(f.rail.at(302060)) == std::set<long>{ 301218, 302806 },
		      "the unspawned merger's output shows its two furnaces");

		// Out of a junction the game has not spawned, inside the drill's bounds,
		// and on to the WolframPlate crafters. The smelter feeding it had not
		// loaded, so these show nothing rather than the drill.
		for (long rail : { 303344L, 303513L, 304120L, 314858L })
			Check(ids(f.rail.at(rail)).empty(), "rail " + std::to_string(rail) + " does not show the drill");
	}

	const ItemFlow* FlowOf(const std::vector<ItemFlow>& flows, const std::string& item)
	{
		for (const ItemFlow& f : flows)
			if (f.item == item)
				return &f;
		return nullptr;
	}

	bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

	// A maker running: it puts out its full rate now.
	FlowPart Maker(uint64_t building, const std::string& item, float full)
	{
		FlowPart part{ building, item, true, full };
		part.now = full;
		return part;
	}

	FlowPart Stalled(FlowPart part)
	{
		part.now = 0.0f;
		return part;
	}

	FlowPart Starved(FlowPart part)
	{
		part.starved = true;
		return part;
	}

	FlowPart Taker(uint64_t building, const std::string& item, float full)
	{
		return { building, item, false, full };
	}

	FlowPart Requesting(FlowPart part, bool requesting)
	{
		part.now = requesting ? part.full : 0.0f;
		return part;
	}

	// Three fabricators make wolfram plate, one found twice; a furnace makes
	// chemicals. Five furnaces and a press take plates, two of them found
	// twice; two furnaces take chemicals. One row per item, each machine
	// counted once, the rates summed.
	void TestFlowsOneRowPerItem()
	{
		const std::vector<FlowPart> parts = {
			Maker(1, "Plate", 30), Maker(2, "Plate", 30), Maker(3, "Plate", 30),
			Requesting(Taker(10, "Plate", 10), true), Requesting(Taker(11, "Plate", 10), false),
			Requesting(Taker(12, "Plate", 10), true),
			Requesting(Taker(10, "Acid", 10), true),   // the furnace's other input, not made here
			Maker(3, "Plate", 30), Requesting(Taker(10, "Plate", 10), true), Requesting(Taker(11, "Plate", 10), false),
			Requesting(Taker(13, "Plate", 10), false), Requesting(Taker(14, "Plate", 10), false),
			Requesting(Taker(15, "Plate", 20), true),
			Maker(4, "Chemicals", 20),
			Requesting(Taker(10, "Chemicals", 5), false), Requesting(Taker(16, "Chemicals", 5), true),
		};

		const std::vector<ItemFlow> flows = SumFlows(parts, 480.0f);
		Check(flows.size() == 2, "one row per made item (got " + std::to_string(flows.size()) + ")");
		Check(!FlowOf(flows, "Acid"), "an item nothing attached makes gets no row");

		const ItemFlow* plate = FlowOf(flows, "Plate");
		Check(plate && plate->sources == 3 && plate->consumers == 6, "plate: 3 sources, 6 consumers, duplicates counted once");
		Check(plate && Near(plate->max, 90), "plate max: 3 x 30");
		Check(plate && Near(plate->min, 90), "plate min: all three can make 30, the takers do not come off");
		Check(plate && Near(plate->now, 40), "plate now: 90 made, 10 + 10 + 20 requesting: the smaller, 40");

		const ItemFlow* chem = FlowOf(flows, "Chemicals");
		Check(chem && chem->sources == 1 && chem->consumers == 2, "chemicals: 1 source, 2 consumers");
		Check(chem && Near(chem->max, 20) && Near(chem->min, 20) && Near(chem->now, 5), "chemicals: 20, 20, 5");
		Check(flows.front().item == "Plate", "the biggest flow comes first");
	}

	// The rail caps all three; an unread rate leaves what it feeds unknown. max
	// and min are about the makers only, now is what moves.
	void TestFlowsLimits()
	{
		std::vector<ItemFlow> flows = SumFlows({ Maker(1, "A", 600), Requesting(Taker(2, "A", 900), false) }, 480.0f);
		Check(Near(flows[0].max, 480) && Near(flows[0].min, 480) && Near(flows[0].now, 0), "capped at the rail; nobody requesting: nothing moves");

		flows = SumFlows({ Maker(1, "A", 600), Requesting(Taker(2, "A", 60), true) }, 480.0f);
		Check(Near(flows[0].min, 480) && Near(flows[0].now, 60), "the takers do not lower min; what moves is what they request");

		flows = SumFlows({ Maker(1, "A", 60), Maker(2, "A", -1) }, 480.0f);
		Check(flows[0].max < 0 && flows[0].min < 0 && flows[0].now < 0, "an unread source makes the row unknown");

		flows = SumFlows({ Maker(1, "A", 60), Taker(2, "A", 30) }, 480.0f);
		Check(Near(flows[0].min, 60) && flows[0].now < 0, "an unread request leaves now unknown, not min");

		flows = SumFlows({ Maker(1, "A", 60), Requesting(Taker(2, "A", 100), true) }, -1.0f);
		Check(Near(flows[0].max, 60) && Near(flows[0].min, 60) && Near(flows[0].now, 60), "no capacity: uncapped");
	}

	// max and min are the makers capped by the rail, whatever the takers want;
	// now is what moves. The cases are from the game.
	void TestFlowsCapAndMin()
	{
		// Six furnaces make 2880 a minute, six takers want 240 in all.
		std::vector<FlowPart> parts;
		for (uint64_t i = 1; i <= 6; ++i)
			parts.push_back(Maker(i, "A", 480));
		for (uint64_t i = 11; i <= 16; ++i)
			parts.push_back(Taker(i, "A", 40));
		std::vector<ItemFlow> flows = SumFlows(parts, 480.0f);
		Check(Near(flows[0].max, 480) && Near(flows[0].min, 480), "480 of 2880 made: max and min 480, the takers do not count");

		// Three fabricators make 540 (3 x 180), one of them starved: it counts 0 in min.
		parts.clear();
		for (uint64_t i = 1; i <= 3; ++i)
			parts.push_back(i == 3 ? Starved(Stalled(Maker(i, "A", 180))) : Maker(i, "A", 180));
		flows = SumFlows(parts, 480.0f);
		Check(Near(flows[0].max, 480) && Near(flows[0].min, 360), "480 of 540 made, one starved: min 360");

		// A starved one under the cap: the others count in full.
		flows = SumFlows({ Maker(1, "A", 100), Starved(Stalled(Maker(2, "A", 100))), Stalled(Maker(3, "A", 100)) }, 480.0f);
		Check(Near(flows[0].max, 300) && Near(flows[0].min, 200),
			"an output-full maker has its inputs, so it counts in min; a starved one does not");

		// Twenty furnaces make 600 (one running, 30 now), six takers want 60 and request it now.
		parts.clear();
		for (uint64_t i = 1; i <= 20; ++i)
			parts.push_back(i == 1 ? Maker(i, "A", 30) : Stalled(Maker(i, "A", 30)));
		for (uint64_t i = 11; i <= 16; ++i)
			parts.push_back(Requesting(Taker(100 + i, "A", 10), true));
		flows = SumFlows(parts, 480.0f);
		Check(Near(flows[0].max, 480) && Near(flows[0].min, 480) && Near(flows[0].now, 30),
			"480 of 600 made, 60 requested, one furnace running: now 30");

		// Everything running: now is the smaller of what is made (capped) and what is requested.
		flows = SumFlows({ Maker(1, "A", 900), Requesting(Taker(2, "A", 100), true) }, 480.0f);
		Check(Near(flows[0].now, 100), "now: 900 capped to 480, 100 requested: 100");
		flows = SumFlows({ Maker(1, "A", 900), Requesting(Taker(2, "A", 700), true) }, 480.0f);
		Check(Near(flows[0].now, 480), "now: 700 requested of a rail that carries 480: 480");
	}

	// A Cargo Receiver or Sender has no rate to read: it is counted and named
	// but adds to no total. A receiver counts 0 in min and brings an unknown
	// extra: "+?" after max and now with makers, "?" for them without.
	void TestFlowsUnrated()
	{
		FlowPart receiver{ 90, "A", true };
		receiver.unrated = true;
		FlowPart sender{ 91, "A", false };
		sender.unrated = true;
		std::vector<ItemFlow> flows = SumFlows({ Maker(1, "A", 100), receiver, Requesting(Taker(2, "A", 40), true), sender }, 480.0f);
		Check(flows[0].sources == 2 && flows[0].consumers == 2, "the receiver and the sender are counted");
		Check(Near(flows[0].max, 100) && Near(flows[0].min, 100) && Near(flows[0].now, 40), "but add to no total: 100, 100, 40");
		Check(flows[0].plusUnknown, "with a receiver bringing more: the makers' part and +?");
		Check(flows[0].judged && !flows[0].short_, "and the row stays judged");

		flows = SumFlows({ receiver, Requesting(Taker(2, "A", 40), true) }, 480.0f);
		Check(flows.size() == 1 && flows[0].judged, "an item only a receiver brings is a row");
		Check(flows[0].max < 0 && Near(flows[0].min, 0) && flows[0].now < 0 && !flows[0].plusUnknown,
			"max and now are unknown, min counts the receiver as 0");

		// Takers wanting more than the rail carries are short even when a receiver
		// adds an unknown amount; below that they are not.
		flows = SumFlows({ Maker(1, "A", 100), receiver, Requesting(Taker(2, "A", 600), true) }, 480.0f);
		Check(flows[0].short_ && Near(flows[0].shortBy, 120), "600 requested of a rail that carries 480, a receiver or not: short by 120");
		flows = SumFlows({ Maker(1, "A", 100), receiver, Requesting(Taker(2, "A", 200), true) }, 480.0f);
		Check(!flows[0].short_, "200 requested, a receiver may bring the rest: not short");
	}

	FlowPart InStorage(FlowPart part, const std::string& name);

	// An item only storage holds, or a receiver brings, has no rate to judge by:
	// it is short only when consumers request it and neither stock nor a receiver
	// is there. An item some building makes keeps the rule that max must cover
	// what the consumers request.
	void TestFlowsStorageDot()
	{
		auto depot = [](float stock)
		{
			FlowPart part = InStorage({ 7, "A", true }, "Storage Depot v.2");
			part.stock = stock;
			return part;
		};
		FlowPart receiver{ 90, "A", true };
		receiver.unrated = true;

		std::vector<ItemFlow> flows = SumFlows({ depot(16000), Requesting(Taker(2, "A", 60), true) }, 480.0f);
		Check(flows[0].judged && !flows[0].short_, "stock on hand, consumers requesting: not short");
		Check(Near(flows[0].max, 0) && Near(flows[0].now, 0), "the totals stay 0 for an item only storage holds");

		flows = SumFlows({ depot(0), Requesting(Taker(2, "A", 60), true) }, 480.0f);
		Check(flows[0].judged && flows[0].short_, "no stock, consumers requesting: short");

		flows = SumFlows({ depot(0), Requesting(Taker(2, "A", 60), false) }, 480.0f);
		Check(flows[0].judged && !flows[0].short_, "no stock but nobody requesting: not short");

		flows = SumFlows({ depot(0), receiver, Requesting(Taker(2, "A", 60), true) }, 480.0f);
		Check(flows[0].judged && !flows[0].short_, "a receiver brings it: not short");

		FlowPart downstream = depot(0);
		downstream.source = false;
		flows = SumFlows({ depot(5), downstream, Requesting(Taker(2, "A", 60), true) }, 480.0f);
		Check(!flows[0].short_, "only the storage upstream counts as stock");

		flows = SumFlows({ Maker(1, "A", 30), depot(16000), Requesting(Taker(2, "A", 60), true) }, 480.0f);
		Check(flows[0].short_, "a maker feeds it: 30 made, 60 requested is still short, stock or not");
	}

	// A stalled maker (output full, or short of input) puts nothing on the rail
	// now: max and min still count it, now does not.
	void TestFlowsStalledSource()
	{
		std::vector<ItemFlow> flows = SumFlows({ Stalled(Maker(1, "A", 240)), Stalled(Maker(2, "A", 240)),
			Requesting(Taker(3, "A", 60), false) }, 480.0f);
		Check(Near(flows[0].max, 480) && Near(flows[0].min, 480), "stalled makers still count in max and min");
		Check(Near(flows[0].now, 0), "makers all stalled: nothing now");

		flows = SumFlows({ Maker(1, "A", 240), Stalled(Maker(2, "A", 240)), Requesting(Taker(3, "A", 60), true) }, 480.0f);
		Check(Near(flows[0].now, 60), "one of two running: 240 made, 60 requested: 60");
	}

	// short_: the consumers requesting right now want more than max, before the
	// floor at 0; a rail nobody requests from, or one that covers them, is not.
	void TestFlowsShort()
	{
		std::vector<ItemFlow> flows = SumFlows({ Maker(1, "A", 480), Requesting(Taker(2, "A", 300), true),
			Requesting(Taker(3, "A", 300), true), Requesting(Taker(4, "A", 300), false) }, 480.0f);
		Check(flows[0].short_ && Near(flows[0].now, 480), "600 requested of 480: short, and the 480 the rail carries is what moves");
		Check(Near(flows[0].shortBy, 120), "short by the takers requesting now (600) less max (480), not the one that is not");

		flows = SumFlows({ Maker(1, "A", 480), Requesting(Taker(2, "A", 300), true), Requesting(Taker(3, "A", 300), false) }, 480.0f);
		Check(!flows[0].short_ && Near(flows[0].shortBy, 0), "300 requested of 480: not short, short by nothing");

		flows = SumFlows({ Maker(1, "A", 900), Requesting(Taker(2, "A", 600), true) }, 480.0f);
		Check(flows[0].short_ && Near(flows[0].shortBy, 120), "600 requested of a rail that carries 480: short by 120");

		flows = SumFlows({ Maker(1, "A", 60), Taker(2, "A", 30) }, 480.0f);
		Check(!flows[0].short_, "an unread request leaves it not short");
	}

	FlowPart Named(FlowPart part, const std::string& name, float now)
	{
		part.name = name;
		part.now  = now;
		return part;
	}

	FlowPart InStorage(FlowPart part, const std::string& name)
	{
		part.name    = name;
		part.storage = true;
		return part;
	}

	// The From/To lines come from the same parts as the totals: buildings of a
	// name merge, each building once, sources first, the biggest kind first at
	// each end, and items not on the rail get no line.
	void TestEndLines()
	{
		const std::vector<FlowPart> parts = {
			Named(Taker(10, "Plate", 10), "Furnace", 0),
			Named(Maker(1, "Plate", 120), "Fabricator", 120),
			Named(Maker(2, "Plate", 120), "Fabricator", 0),
			Named(Maker(2, "Plate", 120), "Fabricator", 0),   // found twice
			Named(Maker(3, "Plate", 120), "Fabricator", -1),  // state unread
			Named(Taker(11, "Plate", 90), "Mega Press", 90),
			Named(Taker(12, "Plate", 10), "Furnace v.2", 0),
			Named(Taker(13, "Plate", 10), "Furnace v.2", 0),
			Named(Taker(10, "Acid", 10), "Furnace", 0),       // not made here
		};
		const std::vector<EndRow> rows = GroupEnds(parts, { "Plate" });
		Check(rows.size() == 4, "fabricators, then furnace v.2, furnace, press (got " + std::to_string(rows.size()) + ")");
		if (rows.size() != 4)
			return;
		Check(rows[0].source && rows[0].name == "Fabricator" && rows[0].count == 3, "3x Fabricator first, counted once each");
		Check(!rows[1].source && rows[1].name == "Furnace v.2" && rows[1].count == 2, "the biggest kind of taker first");
		Check(!rows[2].source && rows[2].name == "Furnace" && rows[2].count == 1, "kinds with different names stay apart");
		Check(!rows[3].source && rows[3].name == "Mega Press" && rows[3].count == 1, "press last, in the order seen");

		Check(EndsLine({ rows[0] }) == "3x Fabricator", "a kind reads \"3x Fabricator\"");
		Check(EndsLine({ rows[1], rows[2] }) == "2x Furnace v.2, Furnace", "a single building has no count");
		std::vector<EndRow> many = { rows[0], rows[1], rows[2], rows[3] };
		Check(EndsLine(many) == "3x Fabricator, 2x Furnace v.2, Furnace, +1 more", "three kinds at most, then +N more");
		Check(EndsLine({}).empty(), "no building, no line");
	}

	// A kind with starved and running makers is two groups, so the card can
	// colour only the starved ones; the spans give each group's text.
	void TestStarvedGroups()
	{
		const std::vector<FlowPart> parts = {
			Named(Maker(1, "Plate", 120), "Furnace v.2", 120),
			Named(Starved(Maker(2, "Plate", 120)), "Furnace v.2", 0),
			Named(Maker(3, "Plate", 120), "Furnace v.2", 120),
			Named(Taker(10, "Plate", 10), "Press", 10),
		};
		const std::vector<EndRow> rows = GroupEnds(parts, { "Plate" });
		Check(rows.size() == 3, "running furnaces, starved furnace, press (got " + std::to_string(rows.size()) + ")");
		if (rows.size() != 3)
			return;
		Check(rows[0].count == 2 && !rows[0].starved, "the two running furnaces are one group");
		Check(rows[1].count == 1 && rows[1].starved && rows[1].name == "Furnace v.2", "the starved one is a group of its own");

		std::vector<EndSpan> spans;
		const std::string line = EndsLine({ rows[0], rows[1] }, 3, &spans);
		Check(line == "2x Furnace v.2, Furnace v.2", "both groups are named");
		Check(spans.size() == 2 && !spans[0].starved && spans[1].starved, "only the second group is starved");
		if (spans.size() == 2)
			Check(line.substr(spans[0].begin, spans[0].end - spans[0].begin) == "2x Furnace v.2"
				&& line.substr(spans[1].begin, spans[1].end - spans[1].begin) == "Furnace v.2",
				"the spans cover each group's count and name, not the comma");

		const std::vector<EndRow> all = GroupEnds({ Starved(Named(Maker(1, "Plate", 120), "Furnace v.2", 0)),
			Starved(Named(Maker(2, "Plate", 120), "Furnace v.2", 0)) }, { "Plate" });
		Check(all.size() == 1 && all[0].count == 2 && all[0].starved, "all starved: one starved group");
	}

	// Storage holds an item and neither makes nor takes it: it is listed at an
	// end only when no building is, a kind reached through storage says so, and
	// the totals count it as a source that makes nothing.
	void TestStorageEnds()
	{
		FlowPart viaDepot = Named(Maker(1, "Plate", 30), "Assembler", 30);
		viaDepot.via = "Storage Depot v.2";
		FlowPart viaDepot2 = viaDepot;
		viaDepot2.building = 2;
		const std::vector<FlowPart> parts = {
			viaDepot, viaDepot2,
			InStorage({ 7, "Plate", true }, "Storage Depot v.2"),                // holds plates, makers feed it
			InStorage({ 7, "Ore", true }, "Storage Depot v.2"),                  // holds ore, nothing makes it
			Named(Taker(10, "Plate", 10), "Furnace", 0),
			InStorage({ 8, "Plate", false }, "Storage Depot"),                   // downstream, a furnace takes plates
			InStorage({ 8, "Ore", false }, "Storage Depot"),                     // downstream, nothing takes ore
		};
		const std::vector<EndRow> rows = GroupEnds(parts, { "Plate", "Ore" });
		Check(rows.size() == 4, "plate from, plate to, ore from, ore to (got " + std::to_string(rows.size()) + ")");
		if (rows.size() != 4)
			return;
		Check(rows[0].item == "Plate" && rows[0].source && rows[0].name == "Assembler" && rows[0].count == 2
		      && rows[0].via == "Storage Depot v.2", "2x Assembler via the depot; the depot itself is not a source of plate");
		Check(EndsLine({ rows[0] }) == "2x Assembler via Storage Depot v.2", "the line says via");
		Check(rows[1].item == "Ore" && rows[1].source && rows[1].name == "Storage Depot v.2", "an item only storage holds: from the depot");
		Check(rows[2].item == "Plate" && !rows[2].source && rows[2].name == "Furnace", "plate goes to the furnace, not the depot after it");
		Check(rows[3].item == "Ore" && !rows[3].source && rows[3].name == "Storage Depot", "no taker for ore: it goes to the depot");

		const std::vector<ItemFlow> flows = SumFlows({
			InStorage({ 7, "Ore", true }, "Storage Depot v.2"),
			Named(Taker(10, "Ore", 10), "Furnace", 10),
			InStorage({ 8, "Ore", false }, "Storage Depot"),
			Maker(1, "Plate", 30), InStorage({ 7, "Plate", true }, "Storage Depot v.2"),
		}, 480.0f);
		Check(flows.size() == 2, "an item only storage holds is a row (got " + std::to_string(flows.size()) + ")");
		const ItemFlow* ore = FlowOf(flows, "Ore");
		Check(ore && ore->sources == 0 && ore->consumers == 1, "storage is not counted as a source or a consumer");
		Check(ore && Near(ore->max, 0) && Near(ore->min, 0) && Near(ore->now, 0), "storage-only item: totals from production only, all 0");
		const ItemFlow* plate = FlowOf(flows, "Plate");
		Check(plate && Near(plate->max, 30) && Near(plate->min, 30), "stock does not add to what makers put out");
		Check(flows.front().item == "Plate", "items with production come first");
	}

	// A fake network for FindAttached: node id to its kind and links.
	struct FakeNet
	{
		std::unordered_map<uint64_t, NodeView> nodes;
		int reads = 0;

		void Node(uint64_t id, NodeKind kind) { nodes[id].kind = kind; }
		// A one-way rail from a to b (both ways when twoWay).
		void Rail(uint64_t a, uint64_t b, bool twoWay = false)
		{
			nodes[a].links.push_back({ b, twoWay, true });
			nodes[b].links.push_back({ a, true, twoWay });
		}
		// A one-way rail named `rail`, at a socket pair (a lane's track) at
		// either end, 0 for a socket without one.
		void Rail(uint64_t a, uint64_t b, uint64_t rail, uint64_t trackAtA, uint64_t trackAtB)
		{
			nodes[a].links.push_back({ b, false, true, rail, trackAtA });
			nodes[b].links.push_back({ a, true, false, rail, trackAtB });
		}
		bool operator()(uint64_t id, NodeView& out)
		{
			++reads;
			auto found = nodes.find(id);
			if (found == nodes.end())
				return false;
			out = found->second;
			return true;
		}
	};

	std::set<uint64_t> Set(const std::vector<uint64_t>& v) { return { v.begin(), v.end() }; }

	// A long line: two makers M1, M2 join at pole 10, then poles 11 to 15 to a
	// storage S, then pole 16 to consumer C. A branch off pole 11 feeds
	// consumer B; machine X sends into pole 12 from the side; pole 13 has a
	// rail back out to consumer Y. The aimed rail runs 14 to 15. One far node
	// (99) cannot be read, as if it were gone. Only C is downstream of the
	// rail: B and Y take off the line before it.
	void TestAttachedLongLine()
	{
		const uint64_t M1 = 1, M2 = 2, X = 3, B = 4, C = 5, Y = 6, S = 7;
		FakeNet net;
		for (uint64_t m : { M1, M2, X, B, C, Y })
			net.Node(m, NodeKind::Machine);
		net.Node(S, NodeKind::Storage);
		for (uint64_t p = 10; p <= 16; ++p)
			net.Node(p, NodeKind::PassThrough);
		net.Rail(M1, 10); net.Rail(M2, 10);
		net.Rail(10, 11); net.Rail(11, 12); net.Rail(12, 13); net.Rail(13, 14); net.Rail(14, 15);
		net.Rail(15, S); net.Rail(S, 16); net.Rail(16, C);
		net.Rail(11, B);
		net.Rail(X, 12);
		net.Rail(13, Y);
		net.Rail(99, 13);
		net.nodes.erase(99);

		const Attached found = FindAttached({ 14 }, { 15 }, net, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ M1, M2, X }, "every maker up the line, through every segment");
		Check(Set(found.downstream) == std::set<uint64_t>{ C }, "only the consumers after the rail's end, past storage");
		Check(!found.truncated, "a small network is not truncated");
		Check(found.downstreamStorage == std::vector<uint64_t>{ S }, "the storage after the rail is walked through");

		const Attached capped = FindAttached({ 14 }, { 15 }, net, 4, 256);
		Check(capped.truncated, "the node cap sets truncated");
	}

	// A production line on one rail spine: M1 feeds poles 11 to 15, and M2
	// takes off pole 12 and puts its output back on at pole 13: it feeds the
	// rail that starts at 13, and does not take from it. The same for a storage
	// hub S that machines 1 and 2 fill and 2 takes from.
	void TestAttachedSpine()
	{
		FakeNet net;
		net.Node(1, NodeKind::Machine);
		net.Node(2, NodeKind::Machine);
		for (uint64_t pole = 11; pole <= 15; ++pole)
			net.Node(pole, NodeKind::PassThrough);
		net.Rail(1, 11);
		for (uint64_t pole = 11; pole < 15; ++pole)
			net.Rail(pole, pole + 1);
		net.Rail(12, 2);
		net.Rail(2, 13);
		Attached found = FindAttached({ 13 }, { 14 }, net, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ 1, 2 }, "both makers on a spine are upstream");
		Check(found.downstream.empty(), "a maker taking off the spine before the rail does not take from it");

		FakeNet hub;
		hub.Node(1, NodeKind::Machine);
		hub.Node(2, NodeKind::Machine);
		hub.Node(30, NodeKind::Storage);
		hub.Node(20, NodeKind::PassThrough);
		hub.Rail(1, 30);
		hub.Rail(2, 30);
		hub.Rail(30, 2);
		hub.Rail(30, 20);
		found = FindAttached({ 30 }, { 20 }, hub, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ 1, 2 }, "both machines filling a hub are upstream");
		Check(found.downstream.empty(), "the rail out of the hub leads to no consumer");
		found = FindAttached({ 30 }, { 2 }, hub, 1024, 256);
		Check(Set(found.downstream) == std::set<uint64_t>{ 2 }, "the machine at the rail's end is a consumer");
	}

	// A drone lane L is one node with a socket pair per track. Maker M1 feeds
	// track 1 through storage S, maker M2 feeds track 2; each track leaves L to
	// its own consumer. Aimed at the rail leaving track 1, only that track's
	// machines are attached, and the storage it passes is listed.
	void TestAttachedLaneTracks()
	{
		const uint64_t M1 = 1, M2 = 2, C1 = 3, C2 = 4, S = 5, L = 10;
		FakeNet net;
		for (uint64_t m : { M1, M2, C1, C2 })
			net.Node(m, NodeKind::Machine);
		net.Node(S, NodeKind::Storage);
		net.Node(L, NodeKind::PassThrough);
		net.Rail(M1, S, 100, 0, 0);
		net.Rail(S, L, 101, 0, 1);
		net.Rail(M2, L, 102, 0, 2);
		net.Rail(L, C1, 103, 1, 0);
		net.Rail(L, C2, 104, 2, 0);

		Attached found = FindAttached({ L }, { C1 }, net, 1024, 256, 103);
		Check(Set(found.upstream) == std::set<uint64_t>{ M1 }, "a lane track has its own maker only");
		Check(Set(found.downstream) == std::set<uint64_t>{ C1 }, "...and its own consumer");
		Check(found.upstreamStorage == std::vector<uint64_t>{ S }, "the storage up the track is listed");
		Check(found.downstreamStorage.empty(), "...and no storage after the rail");

		found = FindAttached({ L }, { C1 }, net, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ M1, M2 }, "with no rail named, every track meets");
	}

	// A depot D fills from three assemblers A1..A3 through pole 10 and from
	// maker F directly; the aimed rail leaves D through pole 11 to a press P
	// and past a second depot E to a smelter T. A third assembler G feeds
	// the line after the rail through a pole 12 into E. The makers behind the
	// depot are found, the ones through it say so, the depot is neither maker
	// nor taker, and the stock is read from both depots.
	void TestAttachedThroughStorage()
	{
		const uint64_t A1 = 1, A2 = 2, A3 = 3, F = 4, P = 5, T = 6, G = 7, D = 20, E = 21;
		FakeNet net;
		for (uint64_t m : { A1, A2, A3, F, P, T, G })
			net.Node(m, NodeKind::Machine);
		net.Node(D, NodeKind::Storage);
		net.Node(E, NodeKind::Storage);
		for (uint64_t p : { uint64_t(10), uint64_t(11), uint64_t(12), uint64_t(13) })
			net.Node(p, NodeKind::PassThrough);
		net.Rail(A1, 10); net.Rail(A2, 10); net.Rail(A3, 10);
		net.Rail(10, D);
		net.Rail(F, D);
		net.Rail(D, 11);          // the aimed rail: D to pole 11
		net.Rail(11, P);
		net.Rail(11, 13);
		net.Rail(13, E);
		net.Rail(E, T);
		net.Rail(G, 12); net.Rail(12, E);

		const Attached found = FindAttached({ D }, { 11 }, net, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ A1, A2, A3, F }, "the makers behind the depot are found");
		Check(found.upstreamStorage == std::vector<uint64_t>{ D }, "the depot is walked through, not a machine");
		Check(found.upstreamVia.count(A1) && found.upstreamVia.at(A1) == D && found.upstreamVia.at(A3) == D
		      && found.upstreamVia.count(F) && found.upstreamVia.at(F) == D, "every maker was reached through the depot");
		Check(Set(found.downstream) == std::set<uint64_t>{ P, T }, "the takers after the rail, past the second depot");
		Check(found.downstreamStorage == std::vector<uint64_t>{ E }, "the second depot is walked through too");
		Check(!Set(found.downstream).count(G) && !Set(found.downstream).count(A1), "machines feeding the second depot are not takers");

		// Depots in a chain are passed through both ways.
		FakeNet chain;
		chain.Node(1, NodeKind::Machine); chain.Node(2, NodeKind::Machine);
		chain.Node(30, NodeKind::Storage); chain.Node(31, NodeKind::Storage);
		chain.Node(40, NodeKind::PassThrough);
		chain.Rail(1, 30); chain.Rail(30, 31); chain.Rail(31, 40); chain.Rail(40, 2);
		const Attached passed = FindAttached({ 31 }, { 40 }, chain, 1024, 256);
		Check(Set(passed.upstream) == std::set<uint64_t>{ 1 } && passed.upstreamStorage.size() == 2, "a chain of depots is passed through");
		Check(Set(passed.downstream) == std::set<uint64_t>{ 2 }, "...and the taker after the rail is found");
	}

	void TestAttachedEdges()
	{
		FakeNet net;
		net.Node(1, NodeKind::Machine);
		net.Node(2, NodeKind::Machine);
		net.Node(10, NodeKind::PassThrough);
		net.Rail(1, 10);
		net.Rail(10, 2);
		Attached found = FindAttached({ 1 }, { 10 }, net, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ 1 }, "a rail out of a machine has that machine upstream");
		Check(Set(found.downstream) == std::set<uint64_t>{ 2 }, "...and what its end feeds downstream");

		FakeNet two;
		two.Node(1, NodeKind::Machine);
		two.Node(2, NodeKind::Machine);
		two.Node(10, NodeKind::PassThrough);
		two.Node(11, NodeKind::PassThrough);
		two.Rail(1, 10);
		two.Rail(10, 11, true);
		two.Rail(11, 10);   // a loop back
		two.Rail(11, 2);
		found = FindAttached({ 10, 11 }, { 11, 10 }, two, 1024, 256);
		Check(Set(found.upstream) == std::set<uint64_t>{ 1 }, "a two-way rail walks back from both ends");
		Check(Set(found.downstream) == std::set<uint64_t>{ 2 }, "a loop is walked once");
	}

	// Cargo connection rates: amount x 60 / gap, the replicator's interval until
	// two sends are seen, none for a connection never seen sending.
	void TestPackageRates()
	{
		using namespace Waila::PackageRates;
		const uint64_t sender = 5, receiver = 6;
		Link link;
		link.sender   = sender;
		link.receiver = receiver;
		link.item     = "Wolfram";
		link.amount   = 10;

		Tracker tracker;
		Check(!tracker.Of(receiver, true, "Wolfram").known, "no connections seen: no rate");

		// Listed but never sent: no rate.
		link.start = 0.0f;
		tracker.Observe({ link }, 100.0, 5.0f);
		Rate rate = tracker.Of(receiver, true, "Wolfram");
		Check(rate.links == 1 && !rate.known, "never seen sending: counted, no rate");

		// First send: the interval stands in for the gap.
		link.start = 100.0f;
		tracker.Observe({ link }, 100.5, 5.0f);
		rate = tracker.Of(receiver, true, "Wolfram");
		Check(rate.known && Near(rate.full, 120.0f) && rate.measured == 0, "one send: amount x 60 / interval");

		// Second send: the measured gap takes over.
		link.start = 108.0f;
		tracker.Observe({ link }, 108.5, 5.0f);
		rate = tracker.Of(receiver, true, "Wolfram");
		Check(rate.known && Near(rate.full, 75.0f) && Near(rate.now, 75.0f) && rate.measured == 1, "two sends: the measured gap");

		// The same start seen again is not a new send.
		tracker.Observe({ link }, 109.0, 5.0f);
		Check(Near(tracker.Of(receiver, true, "Wolfram").full, 75.0f), "an unchanged start adds no send");

		// No new send for long: idle, so full stays and now drops to 0.
		tracker.Observe({ link }, 140.0, 5.0f);
		rate = tracker.Of(receiver, true, "Wolfram");
		Check(Near(rate.full, 75.0f) && Near(rate.now, 0.0f), "stopped sending: now is 0, full stays");

		// Sender side, and a second connection sums.
		Link other = link;
		other.receiver = 7;
		other.start    = 0.0f;
		other.amount   = 5;
		tracker.Observe({ link, other }, 140.0, 5.0f);
		rate = tracker.Of(sender, false, "Wolfram");
		Check(rate.links == 2 && !rate.known, "a sender with a connection never seen sending has no rate yet");
		other.start = 143.0f;
		tracker.Observe({ link, other }, 143.5, 5.0f);
		rate = tracker.Of(sender, false, "Wolfram");
		Check(rate.known && Near(rate.full, 75.0f + 60.0f), "a sender's rate is the sum of its connections");
		Check(tracker.ItemsOf(sender, false) == std::vector<std::string>{ "Wolfram" }, "items of a sender");
		Check(tracker.ItemsOf(receiver, false).empty(), "a receiver has no outgoing items");

		// A connection no longer listed is dropped.
		tracker.Observe({ other }, 144.0, 5.0f);
		Check(!tracker.Of(receiver, true, "Wolfram").links, "a dropped connection is forgotten");

		// The replicator reads the start as -1 between two sends (every next send
		// came out with gap = start + 1, 0.14 items a minute).
		// The last real start stays, so the next send is timed from it, and the
		// connection does not read as stopped while it waits.
		Tracker held;
		link.start = 200.0f;
		held.Observe({ link }, 200.5, 5.0f);
		link.start = 205.0f;
		held.Observe({ link }, 205.5, 5.0f);
		link.start = -1.0f;
		held.Observe({ link }, 207.0, 5.0f);
		rate = held.Of(receiver, true, "Wolfram");
		Check(Near(rate.full, 120.0f) && Near(rate.now, 120.0f), "a start read as -1 keeps the rate and does not idle it");
		link.start = 210.0f;
		held.Observe({ link }, 210.5, 5.0f);
		rate = held.Of(receiver, true, "Wolfram");
		Check(rate.known && Near(rate.full, 120.0f) && rate.measured == 1, "the send after a -1 read is timed from the last real start");

		tracker.Clear();
		Check(!tracker.Of(7, true, "Wolfram").links && !tracker.Of(sender, false, "Wolfram").links, "cleared");
	}
}

int main()
{
	std::string dir = __FILE__;
	dir.erase(dir.find_last_of("\\/") + 1);

	TestSocketNames();
	TestLanesStaySeparate();
	TestJunctionStorageAndInputs();
	TestLooseEnds();
	TestConsumersBranchOffTheLine();
	TestConsumersLeaveOutOwnEnd();
	TestJunctionInsideDrill();
	TestLaneTracksStayApart();
	TestPlatform(dir);
	TestFlowsOneRowPerItem();
	TestFlowsLimits();
	TestFlowsStalledSource();
	TestFlowsCapAndMin();
	TestFlowsUnrated();
	TestFlowsStorageDot();
	TestFlowsShort();
	TestStarvedGroups();
	TestEndLines();
	TestAttachedLongLine();
	TestAttachedEdges();
	TestAttachedSpine();
	TestAttachedLaneTracks();
	TestAttachedThroughStorage();
	TestStorageEnds();
	TestPackageRates();

	if (g_failures == 0)
		std::printf("All rail network tests passed.\n");
	return g_failures == 0 ? 0 : 1;
}
