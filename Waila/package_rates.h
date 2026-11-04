#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// How fast the Cargo Receivers and Senders on a rail move items, from the
// connections the package transport replicator lists (sender, receiver, item,
// the amount of one package and the server time its send started).
//
// The replicator gives no rate, so it is measured: a connection that starts
// a send at server times t1 then t2 moves its package amount in t2 - t1, which
// is amount x 60 / (t2 - t1) items a minute. Until a connection has two
// sends seen, the replicator's own send interval (the "every N s" of a Cargo
// Sender's card) stands in for the gap; a connection never seen sending has
// no rate.
//
// Plain data, no engine types: nothing here holds an object pointer, and the
// Mass entity handle of a machine is only compared.
namespace Waila::PackageRates
{
	// One connection as the replicator lists it. sender and receiver are the
	// Mass entities of the two buildings (0 when one could not be resolved).
	struct Link
	{
		uint64_t    sender   = 0;
		uint64_t    receiver = 0;
		std::string item;               // the item's unique name
		int32_t     amount   = 0;       // items in one package
		float       start    = 0.0f;    // server time the latest send started; 0 or less when none has
	};

	// The rate of a machine's connections for one item, items a minute. known
	// is true when the machine has connections for it and every one has a
	// rate. full is the sum of those rates; now is the same without the
	// connections that have stopped sending.
	struct Rate
	{
		bool    known    = false;
		float   full     = 0.0f;
		float   now      = 0.0f;
		int32_t links    = 0;     // connections for the item
		int32_t measured = 0;     // of those, rates taken from two sends seen
	};

	// A connection is idle once no send has started for this many of its gaps,
	// plus the second the sampler may be late by.
	inline constexpr float kIdleGaps = 2.0f;

	class Tracker
	{
	public:
		// One connection's standing.
		struct Entry
		{
			bool  idle     = false;
			float perMin   = -1.0f;  // negative: no rate
			bool  measured = false;
		};

		// Takes the connections as read now, at server time `serverNow`.
		// `interval` is the replicator's send interval in seconds (0 or less
		// when it could not be read). A connection no longer listed is dropped.
		void Observe(const std::vector<Link>& links, double serverNow, float interval)
		{
			m_now      = serverNow;
			m_interval = interval;

			std::unordered_map<std::string, State> next;
			for (const Link& link : links)
			{
				const std::string key = KeyOf(link);
				State state;
				if (const auto old = m_states.find(key); old != m_states.end())
					state = old->second;

				if (link.start > 0.0f && link.start != state.link.start)
				{
					// A new send. The gap needs a send before it, and one that
					// came earlier: a clock reset gives a gap that is skipped.
					if (state.sends > 0 && link.start > state.link.start)
						state.gap = link.start - state.link.start;
					++state.sends;
				}
				// The replicator reads a connection's start as -1 at times between
				// two sends; the last real start stays, or the next send would be
				// timed from -1 (a gap of the whole server time, a rate near 0)
				// and the idle check would read the connection as stopped.
				const float lastStart = state.link.start;
				state.link = link;
				if (link.start <= 0.0f && lastStart > 0.0f)
					state.link.start = lastStart;
				next[key]  = std::move(state);
			}
			m_states = std::move(next);
		}

		// The rate of `machine`'s connections for `item`, as a receiver (the
		// connections that bring it items) or as a sender (the ones that take
		// them away).
		Rate Of(uint64_t machine, bool asReceiver, const std::string& item) const
		{
			Rate rate;
			if (!machine)
				return rate;
			bool all = true;
			for (const auto& [key, state] : m_states)
			{
				if (state.link.item != item || (asReceiver ? state.link.receiver : state.link.sender) != machine)
					continue;
				++rate.links;
				const Entry entry = Describe(state);
				if (entry.perMin < 0.0f)
				{
					all = false;
					continue;
				}
				rate.measured += entry.measured ? 1 : 0;
				rate.full     += entry.perMin;
				rate.now      += entry.idle ? 0.0f : entry.perMin;
			}
			rate.known = rate.links > 0 && all;
			return rate;
		}

		// The items `machine` has connections for, as a receiver or a sender.
		std::vector<std::string> ItemsOf(uint64_t machine, bool asReceiver) const
		{
			std::vector<std::string> items;
			if (!machine)
				return items;
			for (const auto& [key, state] : m_states)
			{
				if ((asReceiver ? state.link.receiver : state.link.sender) != machine)
					continue;
				if (std::find(items.begin(), items.end(), state.link.item) == items.end())
					items.push_back(state.link.item);
			}
			std::sort(items.begin(), items.end());
			return items;
		}

		void Clear()
		{
			m_states.clear();
			m_now      = 0.0;
			m_interval = 0.0f;
		}

	private:
		struct State
		{
			Link    link;
			float   gap   = 0.0f;
			int32_t sends = 0;
		};

		static std::string KeyOf(const Link& link)
		{
			return std::to_string(link.sender) + "|" + std::to_string(link.receiver) + "|" + link.item;
		}

		Entry Describe(const State& state) const
		{
			Entry entry;
			// Never seen sending: nothing says how often it will.
			const float gap = state.gap > 0.0f ? state.gap : state.sends > 0 ? m_interval : 0.0f;
			if (gap <= 0.0f)
				return entry;
			entry.measured = state.gap > 0.0f;
			entry.perMin   = (std::max)(0, state.link.amount) * 60.0f / gap;
			entry.idle     = static_cast<float>(m_now - state.link.start) > kIdleGaps * gap + 1.0f;
			return entry;
		}

		std::unordered_map<std::string, State> m_states;
		double m_now      = 0.0;
		float  m_interval = 0.0f;
	};
}
