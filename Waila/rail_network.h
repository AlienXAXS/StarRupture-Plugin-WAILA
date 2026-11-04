#pragma once

// The drone rail topology, in plain C++ with no game types, so the same code
// runs in the plugin and in Tests\RailNetworkTests.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Waila::RailNetwork
{
	struct Vec3
	{
		double x = 0.0, y = 0.0, z = 0.0;
	};

	// The world positions of a rail's first and last spline point. Drones fly a
	// rail from its start to its end. INFERRED from two captured saves: every
	// crafter's rails start on one face of it (its output) and end on another
	// (its inputs), and every input of a merger is a rail end.
	struct Rail
	{
		Vec3 start;
		Vec3 end;
	};

	// One socket of a path point, as the game names it.
	struct NamedSocket
	{
		std::string name;
		Vec3        at;
	};

	// A rail socket of a path point (a pole, rail support, junction, merger or
	// lane). Sockets of one point with the same lane are connected.
	struct Socket
	{
		size_t  point;
		int32_t lane;
		Vec3    at;
	};

	// A building drones bring items to or take them from: a crafter, storage, a
	// drone station. `origin` and `extent` are its actor bounds, `floorZ` the
	// height of its actor location, which is where it stands.
	struct Building
	{
		Vec3   origin;
		Vec3   extent;
		double floorZ  = 0.0;
		bool   crafter = false;
	};

	constexpr int32_t kAnyLane = -1;

	// The lane of a rail socket, from its name. A lane building carries parallel
	// tracks that must not mix: its sockets are "Rail_<n>A" and "Rail_<n>B", and
	// its plain "RailSocket" is track 0. Every other path point joins all its
	// sockets (kAnyLane). Returns false for a socket that holds no rail.
	inline bool SocketLane(std::string_view name, int32_t& outLane)
	{
		auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
		auto startsWith = [&](std::string_view prefix)
		{
			if (name.size() < prefix.size())
				return false;
			for (size_t i = 0; i < prefix.size(); ++i)
			{
				if (lower(name[i]) != prefix[i])
					return false;
			}
			return true;
		};

		if (!startsWith("rail"))
			return false;

		outLane = kAnyLane;
		if (startsWith("rail_") && name.size() >= 7)
		{
			int32_t lane = 0;
			size_t  i    = 5;
			for (; i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])); ++i)
				lane = lane * 10 + (name[i] - '0');

			const char side = i < name.size() ? lower(name[i]) : '\0';
			if (i > 5 && i + 1 == name.size() && (side == 'a' || side == 'b'))
				outLane = lane;
		}
		return true;
	}

	struct Scene
	{
		std::vector<Rail>     rails;
		std::vector<Socket>   sockets;
		std::vector<Building> buildings;
		size_t                points = 0;

		// Adds one path point with all its sockets. Sockets that hold no rail are
		// skipped.
		void AddPathPoint(const std::vector<NamedSocket>& named)
		{
			const size_t first = sockets.size();
			bool hasLanes = false;
			for (const NamedSocket& s : named)
			{
				int32_t lane = kAnyLane;
				if (!SocketLane(s.name, lane))
					continue;
				hasLanes = hasLanes || lane != kAnyLane;
				sockets.push_back({ points, lane, s.at });
			}
			if (hasLanes)
			{
				for (size_t i = first; i < sockets.size(); ++i)
				{
					if (sockets[i].lane == kAnyLane)
						sockets[i].lane = 0;
				}
			}
			++points;
		}
	};

	struct Sources
	{
		std::vector<size_t> crafters;    // indices into Scene::buildings
		size_t              rails = 0;   // how many rails the walk reached, the start one included
		bool                truncated = false;
		std::vector<size_t> walked;      // those rails: the start one first, then each against the flow
	};

	// The crafters that take items off the line before a rail, see Graph::FindConsumers.
	struct Consumers
	{
		std::vector<size_t> crafters;    // indices into Scene::buildings
		bool                truncated = false;
	};

	// INFERRED, as is everything about which buildings a rail serves: the SDK
	// has no explicit building-to-rail link. ConnectedStartActor, ConnectedEndActor
	// and SocketActor were null on every rail of four captured saves, so the
	// topology comes from where each rail's spline ends sit.
	//
	// A rail end on a path point's rail socket belongs to that point: in those
	// captures every such end lay within 0.5 units of its socket, and each socket
	// held one rail. Supply buildings expose no rail sockets (their SocketsData
	// held none), so an end belongs to a building when it lies within
	// kBuildingMargin of its bounds and no higher than kMaxPortHeight above its
	// floor. Every such end sat 260 to 332 units above the building's floor, on
	// furnaces, crafters, smelters, a drill, storage and a turret. A drill's
	// bounds are 20 m tall, and the ends of a junction that had not loaded sat
	// inside them 1081 above the drill's floor.
	//
	// Path points the game has not spawned as actors leave their rail ends loose.
	// A loose rail start is fed by the loose rail ends within kJoinTolerance: the
	// sockets of a pole sit 30 units apart, those of a junction 76 to 108, and a
	// merger's outer input 117 from its output. The next gap measured was 130.
	//
	// Loose starts side by side, within kLaneWidth of each other, are the
	// parallel tracks of a lane building that has not loaded. Each takes only
	// its nearest loose end, the one straight across the lane, so the tracks
	// stay apart. A lane's tracks sit 20 apart and its outer pair 40; the next
	// spacing measured between loose starts was 48.
	constexpr double kSocketTolerance = 5.0;     // unreal units, 100 = 1 m
	constexpr double kBuildingMargin  = 50.0;
	constexpr double kMaxPortHeight   = 600.0;
	constexpr double kJoinTolerance   = 120.0;
	constexpr double kLaneWidth       = 44.0;

	namespace Detail
	{
		inline double DistSq(const Vec3& a, const Vec3& b)
		{
			const double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
			return x * x + y * y + z * z;
		}

		// Points bucketed in cubes of `cell` units, so the points near a position
		// are found by checking the 27 cubes around it. Queries reach at most one
		// cell out.
		class PointGrid
		{
		public:
			explicit PointGrid(double cell) : m_cell(cell) {}

			void Add(size_t id, const Vec3& at) { m_cells[CellOf(at)].push_back({ id, at }); }

			template <typename Fn>
			void ForNear(const Vec3& at, Fn&& fn) const
			{
				const Cell c = CellOf(at);
				for (int64_t dx = -1; dx <= 1; ++dx)
				for (int64_t dy = -1; dy <= 1; ++dy)
				for (int64_t dz = -1; dz <= 1; ++dz)
				{
					const auto it = m_cells.find({ c.x + dx, c.y + dy, c.z + dz });
					if (it == m_cells.end())
						continue;
					for (const Entry& e : it->second)
						fn(e.id, e.at);
				}
			}

		private:
			struct Cell
			{
				int64_t x, y, z;
				bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; }
			};

			struct CellHash
			{
				size_t operator()(const Cell& c) const
				{
					return static_cast<size_t>(c.x * 73856093LL ^ c.y * 19349663LL ^ c.z * 83492791LL);
				}
			};

			struct Entry
			{
				size_t id;
				Vec3   at;
			};

			Cell CellOf(const Vec3& p) const
			{
				return { static_cast<int64_t>(std::floor(p.x / m_cell)),
				         static_cast<int64_t>(std::floor(p.y / m_cell)),
				         static_cast<int64_t>(std::floor(p.z / m_cell)) };
			}

			double                                           m_cell;
			std::unordered_map<Cell, std::vector<Entry>, CellHash> m_cells;
		};
	}

	// What a rail end sits on.
	struct Port
	{
		enum class Kind : uint8_t { Loose, Socket, Building };

		Kind   kind  = Kind::Loose;
		size_t index = 0;   // into Scene::sockets or Scene::buildings
	};

	class Graph
	{
	public:
		explicit Graph(const Scene& scene)
			: m_scene(scene), m_looseEnds(kJoinTolerance)
		{
			Detail::PointGrid looseStarts(kJoinTolerance);
			Detail::PointGrid sockets(kJoinTolerance);
			for (size_t i = 0; i < scene.sockets.size(); ++i)
				sockets.Add(i, scene.sockets[i].at);

			m_start.resize(scene.rails.size());
			m_end.resize(scene.rails.size());
			m_endsAtBuilding.resize(scene.buildings.size());
			m_startsAtBuilding.resize(scene.buildings.size());

			for (size_t r = 0; r < scene.rails.size(); ++r)
			{
				m_start[r] = Place(scene.rails[r].start, sockets);
				switch (m_start[r].kind)
				{
				case Port::Kind::Socket:
				{
					const Socket& s = scene.sockets[m_start[r].index];
					m_startsAtPoint[{ s.point, s.lane }].push_back(r);
					break;
				}
				case Port::Kind::Building:
					m_startsAtBuilding[m_start[r].index].push_back(r);
					break;
				case Port::Kind::Loose:
					looseStarts.Add(r, scene.rails[r].start);
					break;
				}

				const Port end = Place(scene.rails[r].end, sockets);
				m_end[r] = end;
				switch (end.kind)
				{
				case Port::Kind::Socket:
				{
					const Socket& s = scene.sockets[end.index];
					m_endsAtPoint[{ s.point, s.lane }].push_back(r);
					break;
				}
				case Port::Kind::Building:
					m_endsAtBuilding[end.index].push_back(r);
					break;
				case Port::Kind::Loose:
					m_looseEnds.Add(r, scene.rails[r].end);
					break;
				}
			}

			// A loose start beside another one (not on top of it) is a lane track.
			m_laneTrack.resize(scene.rails.size(), false);
			for (size_t r = 0; r < scene.rails.size(); ++r)
			{
				if (m_start[r].kind != Port::Kind::Loose)
					continue;
				const Vec3& at = scene.rails[r].start;
				looseStarts.ForNear(at, [&](size_t other, const Vec3& start)
				{
					const double sq = Detail::DistSq(at, start);
					if (other != r && sq > 1.0 && sq <= kLaneWidth * kLaneWidth)
						m_laneTrack[r] = true;
				});
			}
		}

		const Port& StartOf(size_t rail) const { return m_start[rail]; }

		// The crafters whose output reaches `rail`, walking against the flow: from
		// the rail's start to the rails that end there, and on. A crafter at a
		// rail's start is a source and the walk stops there, so a factory's input
		// rails never show the factory they feed. Other supply buildings, such as
		// storage, pass their input rails through to their output rails.
		Sources Find(size_t rail, size_t maxRails, size_t maxSources) const
		{
			Sources out;
			if (rail >= m_scene.rails.size())
				return out;

			std::vector<bool>          seen(m_scene.rails.size(), false);
			std::vector<size_t>        queue{ rail };
			std::unordered_set<size_t> found;
			seen[rail] = true;

			auto visit = [&](size_t next)
			{
				if (seen[next])
					return true;
				if (queue.size() >= maxRails)
				{
					out.truncated = true;
					return false;
				}
				seen[next] = true;
				queue.push_back(next);
				return true;
			};

			for (size_t head = 0; head < queue.size() && !out.truncated; ++head)
			{
				const size_t current = queue[head];
				const Port&  start   = m_start[current];

				switch (start.kind)
				{
				case Port::Kind::Socket:
				{
					const Socket& s = m_scene.sockets[start.index];
					const auto it = m_endsAtPoint.find({ s.point, s.lane });
					if (it == m_endsAtPoint.end())
						break;
					for (size_t next : it->second)
					{
						if (!visit(next))
							break;
					}
					break;
				}
				case Port::Kind::Building:
				{
					if (m_scene.buildings[start.index].crafter)
					{
						if (found.count(start.index))
							break;
						if (out.crafters.size() >= maxSources)
						{
							out.truncated = true;
							break;
						}
						found.insert(start.index);
						out.crafters.push_back(start.index);
						break;
					}
					for (size_t next : m_endsAtBuilding[start.index])
					{
						if (!visit(next))
							break;
					}
					break;
				}
				case Port::Kind::Loose:
				{
					const Vec3&  at    = m_scene.rails[current].start;
					const double limit = kJoinTolerance * kJoinTolerance;
					if (m_laneTrack[current])
					{
						size_t nearest = m_scene.rails.size();
						double best    = limit;
						m_looseEnds.ForNear(at, [&](size_t next, const Vec3& end)
						{
							const double sq = Detail::DistSq(at, end);
							if (sq < best || (sq == best && next < nearest))
							{
								best    = sq;
								nearest = next;
							}
						});
						if (nearest < m_scene.rails.size())
							visit(nearest);
						break;
					}

					bool more = true;
					m_looseEnds.ForNear(at, [&](size_t next, const Vec3& end)
					{
						if (more && Detail::DistSq(at, end) <= limit)
							more = visit(next);
					});
					break;
				}
				}
			}

			out.rails = queue.size();
			out.walked = std::move(queue);
			return out;
		}

		// The crafters that take items off the line before the rail `upstream` was
		// found for. At every path point or storage the walk against the flow
		// passed, a rail that starts there too is a branch, and the crafters its
		// rails end at pull from the line ahead of this rail. Storage on a branch
		// is a sink and not counted, a crafter ends the branch, and a branch that
		// ends loose is not followed. A crafter a walked rail ends at is left out.
		// A rail that taps off a junction counts its sibling taps too: they share
		// the line. Rails the walk itself used are left out, so
		// a branch that finds its way back to the line adds nothing. A start that
		// sits loose has no known branches.
		Consumers FindConsumers(const Sources& upstream, size_t maxRails, size_t maxCrafters) const
		{
			Consumers out;
			std::vector<bool>          seen(m_scene.rails.size(), false);
			std::vector<size_t>        queue;
			std::unordered_set<size_t> found;
			for (size_t used : upstream.walked)
			{
				seen[used] = true;
				// The crafter a walked rail feeds is the stretch's own, not a branch.
				if (m_end[used].kind == Port::Kind::Building)
					found.insert(m_end[used].index);
			}

			auto branch = [&](const std::vector<size_t>& rails)
			{
				for (size_t next : rails)
				{
					if (seen[next])
						continue;
					if (queue.size() >= maxRails)
					{
						out.truncated = true;
						return;
					}
					seen[next] = true;
					queue.push_back(next);
				}
			};
			auto startingAt = [&](const Socket& s) -> const std::vector<size_t>*
			{
				const auto it = m_startsAtPoint.find({ s.point, s.lane });
				return it == m_startsAtPoint.end() ? nullptr : &it->second;
			};

			for (size_t used : upstream.walked)
			{
				const Port& start = m_start[used];
				if (start.kind == Port::Kind::Socket)
				{
					if (const auto* rails = startingAt(m_scene.sockets[start.index]))
						branch(*rails);
				}
				else if (start.kind == Port::Kind::Building && !m_scene.buildings[start.index].crafter)
				{
					branch(m_startsAtBuilding[start.index]);
				}
			}

			for (size_t head = 0; head < queue.size() && !out.truncated; ++head)
			{
				const Port& end = m_end[queue[head]];
				if (end.kind == Port::Kind::Socket)
				{
					if (const auto* rails = startingAt(m_scene.sockets[end.index]))
						branch(*rails);
				}
				else if (end.kind == Port::Kind::Building && m_scene.buildings[end.index].crafter
				         && !found.count(end.index))
				{
					if (out.crafters.size() >= maxCrafters)
					{
						out.truncated = true;
						break;
					}
					found.insert(end.index);
					out.crafters.push_back(end.index);
				}
			}
			return out;
		}

	private:
		Port Place(const Vec3& at, const Detail::PointGrid& sockets) const
		{
			Port   port;
			double best = kSocketTolerance * kSocketTolerance;
			sockets.ForNear(at, [&](size_t i, const Vec3& socket)
			{
				const double sq = Detail::DistSq(at, socket);
				if (sq <= best)
				{
					best       = sq;
					port.kind  = Port::Kind::Socket;
					port.index = i;
				}
			});
			if (port.kind == Port::Kind::Socket)
				return port;

			// The building whose grown bounds hold the end at port height, the
			// nearest centre winning when bounds overlap.
			for (size_t b = 0; b < m_scene.buildings.size(); ++b)
			{
				const Building& box = m_scene.buildings[b];
				if (std::abs(at.x - box.origin.x) > box.extent.x + kBuildingMargin
				 || std::abs(at.y - box.origin.y) > box.extent.y + kBuildingMargin
				 || std::abs(at.z - box.origin.z) > box.extent.z + kBuildingMargin
				 || at.z - box.floorZ > kMaxPortHeight)
					continue;

				const double sq = Detail::DistSq(at, box.origin);
				if (port.kind == Port::Kind::Loose || sq < best)
				{
					best       = sq;
					port.kind  = Port::Kind::Building;
					port.index = b;
				}
			}
			return port;
		}

		const Scene&                                          m_scene;
		std::vector<Port>                                     m_start;
		std::vector<Port>                                     m_end;
		std::map<std::pair<size_t, int32_t>, std::vector<size_t>> m_endsAtPoint;
		std::map<std::pair<size_t, int32_t>, std::vector<size_t>> m_startsAtPoint;
		std::vector<std::vector<size_t>>                      m_endsAtBuilding;
		std::vector<std::vector<size_t>>                      m_startsAtBuilding;
		Detail::PointGrid                                     m_looseEnds;
		std::vector<bool>                                     m_laneTrack;
	};

	// ---- The machines attached to a rail ---------------------------------------

	// A node of the rail network as the game's Mass data has it: a rail joins
	// two nodes, and a node lists the rails at its sockets.
	enum class NodeKind : uint8_t
	{
		PassThrough,   // a pole, support or junction: rails meet there
		Storage,       // storage or a station: items rest there, nothing is made
		Machine,       // a producer or consumer: the network ends there
	};

	// One rail at a node, by the node at its other end. A node with paired
	// sockets (each track of a drone lane) passes items only between the two
	// rails of a pair; the rails at sockets without a pair all meet.
	struct NodeLink
	{
		uint64_t other = 0;
		bool     in    = false;   // items come into the node on it
		bool     out   = false;   // items leave the node on it
		uint64_t rail  = 0;       // the rail itself, 0 when not known
		uint64_t track = 0;       // the socket pair it is on here, 0 for a socket without one
	};

	// What a machine node does with items, from its logistics type: a Producer
	// only puts items on the rails and a Consumer only takes them. A Cargo
	// Receiver is a Producer and a Cargo Sender a Consumer, neither with a
	// recipe.
	enum class Role : uint8_t
	{
		Other,
		Producer,
		Consumer,
	};

	struct NodeView
	{
		NodeKind              kind = NodeKind::PassThrough;
		Role                  role = Role::Other;
		std::vector<NodeLink> links;
	};

	struct Attached
	{
		std::vector<uint64_t> upstream;     // machines whose output can reach the rail
		std::vector<uint64_t> downstream;   // machines the rail's items can reach
		std::vector<uint64_t> upstreamStorage;     // storage walked through upstream: its stock can leave on the rail
		std::vector<uint64_t> downstreamStorage;   // storage walked through downstream
		// The storage an upstream machine's items reach the rail through: the
		// first one the walk passed on its way back to the machine. Missing for
		// a machine the walk reached without passing storage.
		std::unordered_map<uint64_t, uint64_t> upstreamVia;
		bool                  truncated = false;
	};

	// The machines attached to a rail: `from` are the nodes its items leave
	// (its start, and its end too when it runs both ways), `to` the nodes they
	// arrive at (its end, and its start too when it runs both ways). Upstream
	// walks back against the flow from `from`; downstream walks on from `to`
	// only, so the machines on branches before the rail are not counted.
	// Pass-through nodes and storage are walked through in both directions, so
	// the machines behind a depot are found; a machine ends a walk. At a node
	// with paired sockets the walk stays on the track it came by: `rail` names
	// the rail the walk starts from (0 follows every rail at the seed nodes).
	// `read(node, NodeView&)` returns false for a node it cannot read. At most
	// maxNodes nodes are read and maxMachines machines kept each way;
	// truncated is set when either bites.
	template <class Read>
	Attached FindAttached(const std::vector<uint64_t>& from, const std::vector<uint64_t>& to, Read&& read,
	                      size_t maxNodes, size_t maxMachines, uint64_t rail = 0)
	{
		Attached out;
		std::unordered_map<uint64_t, NodeView> views;
		auto view = [&](uint64_t node) -> const NodeView*
		{
			auto found = views.find(node);
			if (found != views.end())
				return &found->second;
			if (views.size() >= maxNodes)
			{
				out.truncated = true;
				return nullptr;
			}
			NodeView v;
			if (!read(node, v))
				return nullptr;
			return &views.emplace(node, std::move(v)).first->second;
		};

		// A node to walk from, and the rail the walk reached it by (0 for none).
		using Step = std::pair<uint64_t, uint64_t>;

		// A seed that is a machine is kept, every other seed expanded. Every
		// machine reached through a link is kept and not walked past.
		auto walk = [&](const std::vector<Step>& seeds, bool upstream, std::vector<uint64_t>& machines,
		                std::vector<uint64_t>& storage)
		{
			std::set<Step> reached;    // a node, and the rail it was reached by
			std::set<Step> expanded;   // a node, and the track it was walked from
			std::vector<Step> queue;
			std::unordered_map<uint64_t, uint64_t> via;   // a node, and the storage it was first reached through
			auto keep = [&](uint64_t machine)
			{
				if (std::find(machines.begin(), machines.end(), machine) != machines.end())
					return true;
				if (machines.size() >= maxMachines)
				{
					out.truncated = true;
					return false;
				}
				machines.push_back(machine);
				const auto through = via.find(machine);
				if (upstream && through != via.end() && through->second)
					out.upstreamVia.emplace(machine, through->second);
				return true;
			};
			auto expand = [&](const Step& at, const NodeView& v)
			{
				// The track of the rail the node was reached by. 0 (a socket
				// without a pair, no rail, or a machine) follows every rail.
				uint64_t track = 0;
				if (v.kind != NodeKind::Machine && at.second)
					for (const NodeLink& link : v.links)
						if (link.rail == at.second)
						{
							track = link.track;
							break;
						}
				if (!expanded.insert({ at.first, track }).second)
					return;
				if (v.kind == NodeKind::Storage && std::find(storage.begin(), storage.end(), at.first) == storage.end())
					storage.push_back(at.first);
				uint64_t passed = 0;
				if (v.kind == NodeKind::Storage)
					passed = at.first;
				else if (const auto through = via.find(at.first); through != via.end())
					passed = through->second;
				for (const NodeLink& link : v.links)
					if ((!track || link.track == track) && (upstream ? link.in : link.out) && link.other
						&& reached.insert({ link.other, link.rail }).second)
					{
						queue.push_back({ link.other, link.rail });
						via.emplace(link.other, passed);
					}
			};
			for (const Step& seed : seeds)
			{
				const NodeView* v = view(seed.first);
				if (!v)
					continue;
				if (v->kind == NodeKind::Machine)
				{
					if (!keep(seed.first))
						return;
					continue;
				}
				expand(seed, *v);
			}
			for (size_t i = 0; i < queue.size(); ++i)
			{
				const Step at = queue[i];
				const NodeView* v = view(at.first);
				if (!v)
					continue;
				if (v->kind == NodeKind::Machine)
				{
					if (!keep(at.first))
						return;
					continue;
				}
				expand(at, *v);
			}
		};

		std::vector<Step> back, ahead;
		for (uint64_t node : from)
			back.push_back({ node, rail });
		for (uint64_t node : to)
			ahead.push_back({ node, rail });
		walk(back, true, out.upstream, out.upstreamStorage);
		walk(ahead, false, out.downstream, out.downstreamStorage);
		return out;
	}

	// ---- One row per item -----------------------------------------------------

	// One machine's part in an item's flow along a rail: a source making it or
	// a consumer taking it. A machine found twice counts once.
	struct FlowPart
	{
		uint64_t    building  = 0;
		std::string item;               // the item's unique name
		bool        source    = false;  // false: it takes the item
		float       full      = -1.0f;  // items a minute running full out; negative when unread
		float       now       = -1.0f;  // right now: a source's output (0 while stalled), a consumer's intake
		                                // while it has a request open for the item (else 0); negative when unread
		std::string name;               // the building's name, which the From/To lines group by
		bool        storage = false;    // storage: it holds the item, it neither makes nor takes it
		std::string via;                // a source: the name of the storage its items reach the rail through
		bool        unrated = false;    // a source or consumer with no rate to read (a Cargo Receiver or Sender
		                                // not yet seen sending): it is counted and named but adds to no total
		float       stock   = 0.0f;     // storage: how many of the item it holds
		bool        starved = false;    // a source: missing an input of its own and not output full
	};

	// The totals of one item, items a minute; negative when unknown.
	struct ItemFlow
	{
		std::string item;
		int32_t     sources   = 0;      // distinct machines making it
		int32_t     consumers = 0;      // distinct machines taking it
		float       output    = 0.0f;   // the sources' output, uncapped; orders the rows
		float       max       = -1.0f;  // the sources' output at full speed, capped by the rail
		float       min       = -1.0f;  // what the sources can make with the inputs they get now (a starved one counts 0), capped
		float       now       = -1.0f;  // what moves: the sources' output right now (capped) or what the consumers requesting it want, the smaller
		bool        judged    = false;  // consumers take it and their requests are read, so short_ means something
		bool        short_    = false;  // the consumers requesting it right now want more than the rail can bring
		float       shortBy   = 0.0f;   // when short_: how many items a minute the consumers requesting it now want over what the rail can bring
		bool        plusUnknown = false; // makers and a Cargo Receiver bring it: max and now are the makers' part and "+?" more
	};

	// Sums the parts into one flow per item that has a source, biggest output
	// first. capacity is what the rail carries in items a minute (negative when
	// unread, which leaves the figures uncapped). max is what the sources make
	// at full speed, capped by the capacity; min is what they can make with the
	// inputs they get now, the same capped: a starved source counts 0, any
	// other its full rate; now is what moves, the smaller of what the sources
	// put out right now (capped, and 0 for a source that is stalled) and what
	// the consumers requesting right now want. min and max never depend on the
	// consumers. A source whose rate is unread leaves the item's figures
	// unknown; a consumer whose request is unread leaves now unknown. A Cargo
	// Receiver or Sender with a measured rate counts like any maker or taker. An
	// unrated part (one never seen sending) is counted but adds to no total: an
	// unrated receiver counts 0 in min, and in max and now it is an unknown
	// extra, so with makers they read as the makers' part with plusUnknown set,
	// and with no maker at all max and now are unknown (negative) while min is 0.
	//
	// short_ is set when the consumers requesting the item right now are not
	// served, and shortBy is by how much. It is only judged (and only means
	// something) when a consumer takes the item and every figure it needs is
	// read. For an item some building makes it means max is below what they
	// take; with a receiver bringing more it means they take more than the rail
	// carries. An item only storage holds or an unrated Cargo Receiver brings has
	// no rate to judge by: it is short when consumers request it and the storage
	// holds none of it and no receiver brings it.
	inline std::vector<ItemFlow> SumFlows(const std::vector<FlowPart>& parts, float capacity)
	{
		struct Sum
		{
			ItemFlow flow;
			bool     outputKnown = true;
			bool     nowKnown    = true;
			float    outputNow = 0.0f;   // the sources' output right now
			float    outputMin = 0.0f;   // what the sources can make with the inputs they get now
			float    takeNow  = 0.0f;
			bool     held     = false;   // storage holds it
			float    stock    = 0.0f;    // how much the storage upstream holds
			int32_t  makers   = 0;       // buildings making it, with or without a rate read
			bool     brought  = false;   // a source with no rate (a Cargo Receiver) brings it
			std::unordered_set<uint64_t> from, to;
		};

		std::vector<Sum> sums;
		std::unordered_map<std::string, size_t> index;
		for (const FlowPart& part : parts)
		{
			auto found = index.find(part.item);
			if (found == index.end())
			{
				found = index.emplace(part.item, sums.size()).first;
				sums.emplace_back();
				sums.back().flow.item = part.item;
			}
			Sum& sum = sums[found->second];

			if (part.storage)
			{
				sum.held = sum.held || part.source;
				if (part.source)
					sum.stock += (std::max)(0.0f, part.stock);
				continue;
			}

			if (!(part.source ? sum.from : sum.to).insert(part.building).second)
				continue;

			if (part.unrated)
			{
				sum.brought = sum.brought || part.source;
				continue;
			}

			if (part.source)
			{
				++sum.makers;
				if (part.full >= 0.0f)
				{
					sum.flow.output += part.full;
					sum.outputMin   += part.starved ? 0.0f : part.full;
				}
				else
					sum.outputKnown = false;
				if (part.now >= 0.0f)
					sum.outputNow += part.now;
				else
					sum.nowKnown = false;
				continue;
			}
			if (part.now >= 0.0f)
				sum.takeNow += part.now;
			else
				sum.nowKnown = false;
		}

		auto cap = [capacity](float v)
		{
			v = (std::max)(0.0f, v);
			return capacity >= 0.0f ? (std::min)(v, capacity) : v;
		};

		std::vector<ItemFlow> flows;
		for (Sum& sum : sums)
		{
			// An item nothing attached makes, brings or holds is not on the card.
			if (sum.from.empty() && !sum.held)
				continue;

			ItemFlow& flow = sum.flow;
			flow.sources   = static_cast<int32_t>(sum.from.size());
			flow.consumers = static_cast<int32_t>(sum.to.size());
			if (sum.outputKnown)
			{
				flow.max = cap(flow.output);
				flow.min = cap(sum.outputMin);
				if (sum.nowKnown)
				{
					flow.now    = (std::min)(cap(sum.outputNow), sum.takeNow);
					flow.judged = flow.consumers > 0;
					// What the rail can bring at most: the makers' max, or with a
					// receiver adding an unknown amount, only what it carries.
					const bool  unbounded = sum.brought && capacity < 0.0f;
					const float bound     = sum.brought ? (capacity >= 0.0f ? capacity : 0.0f) : flow.max;
					flow.short_ = flow.judged
						&& (sum.makers > 0 ? !unbounded && sum.takeNow > bound
						                   : sum.takeNow > 0.0f && !sum.brought && sum.stock <= 0.0f);
					if (flow.short_)
						flow.shortBy = sum.takeNow - bound;
				}
				// A receiver brings an unknown amount on top of the makers. With
				// none, max and now are unknown ("?"), and min counts it as 0.
				flow.plusUnknown = sum.makers > 0 && sum.brought;
				if (sum.makers == 0 && sum.brought)
				{
					flow.max = -1.0f;
					flow.now = -1.0f;
				}
			}
			flows.push_back(std::move(flow));
		}

		std::stable_sort(flows.begin(), flows.end(),
			[](const ItemFlow& a, const ItemFlow& b) { return a.output > b.output; });
		return flows;
	}

	// The buildings of one kind (one name, reached the same way) at one end of
	// the rail making or taking one item, from the same parts the totals are
	// summed from, each building once.
	struct EndRow
	{
		bool        source = false;
		std::string name;
		std::string item;
		std::string via;       // a source: the storage its items reach the rail through
		bool        starved = false;   // a source: these are the ones missing an input; a kind with both is two rows
		int32_t     count = 0;
	};

	// Sources first, the most buildings of an item's end first, otherwise in the
	// order a kind was first seen. Only items in `items` get a row: a consumer's
	// other ingredients are not on this rail. Storage is neither a maker nor a
	// taker: it is listed at an end only when no building makes (or takes) the
	// item there.
	inline std::vector<EndRow> GroupEnds(const std::vector<FlowPart>& parts, const std::unordered_set<std::string>& items)
	{
		std::unordered_set<std::string> made, taken;   // items some building makes, takes
		for (const FlowPart& part : parts)
		{
			if (!part.storage)
				(part.source ? made : taken).insert(part.item);
		}

		std::vector<EndRow> rows;
		std::unordered_set<std::string> seen;   // end, item and building
		for (const FlowPart& part : parts)
		{
			if (!items.count(part.item))
				continue;
			if (part.storage && (part.source ? made : taken).count(part.item))
				continue;
			if (!seen.insert(std::string(part.source ? "from|" : "to|") + part.item + "|" + std::to_string(part.building)).second)
				continue;

			const std::string& via = part.source ? part.via : std::string();
			auto row = std::find_if(rows.begin(), rows.end(), [&](const EndRow& r)
				{ return r.source == part.source && r.item == part.item && r.name == part.name && r.via == via
					&& r.starved == part.starved; });
			if (row == rows.end())
			{
				EndRow added;
				added.source = part.source;
				added.name   = part.name;
				added.item   = part.item;
				added.via    = via;
				added.starved = part.starved;
				rows.push_back(std::move(added));
				row = rows.end() - 1;
			}
			++row->count;
		}
		std::stable_sort(rows.begin(), rows.end(), [](const EndRow& a, const EndRow& b)
			{ return a.source != b.source ? a.source : a.count > b.count; });
		return rows;
	}

	// One end of an item as a line, names only: "2x Furnace, Furnace v.2", with
	// "via <storage>" after a kind reached through storage. At most `maxKinds`
	// kinds are named, then "+N more". `rows` are those of one item and end,
	// largest first (GroupEnds). Empty when there are none. `spans`, when given,
	// gets the byte range of each named kind's "2x Furnace" text (without its
	// "via"), with whether the kind is starved, so the card can colour the names.
	struct EndSpan
	{
		size_t begin   = 0;
		size_t end     = 0;
		bool   starved = false;
	};

	inline std::string EndsLine(const std::vector<EndRow>& rows, size_t maxKinds = 3, std::vector<EndSpan>* spans = nullptr)
	{
		std::string line;
		for (size_t i = 0; i < rows.size() && i < maxKinds; ++i)
		{
			if (i)
				line += ", ";
			const size_t begin = line.size();
			if (rows[i].count > 1)
				line += std::to_string(rows[i].count) + "x ";
			line += rows[i].name;
			if (spans)
				spans->push_back({ begin, line.size(), rows[i].starved });
			if (!rows[i].via.empty())
				line += " via " + rows[i].via;
		}
		if (rows.size() > maxKinds)
			line += ", +" + std::to_string(rows.size() - maxKinds) + " more";
		return line;
	}

}
