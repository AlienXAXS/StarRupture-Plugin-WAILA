#pragma once

#include "plugin_interface.h"
#include "ui_theme.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Waila
{
	struct CrafterInfo;
	struct StorageInfo;
	struct PowerInfo;
	struct CoolerActiveInfo;
	struct CoolerPassiveInfo;
	struct CargoSenderInfo;
	struct CargoReceiverInfo;
}

namespace Waila::UI
{
	// An icon with a number under or beside it — an ingredient, a product, or a
	// stack sitting in a container.
	struct FlowSlot
	{
		PluginTextureHandle icon = nullptr;
		std::string         label;
		Health              tone = Health::Idle;
		bool                dim  = false;   // input the machine hasn't got
	};

	// A figure with a short caption, used by buildings that have no recipe to draw.
	struct StatCell
	{
		std::string value;
		std::string label;
		Health      tone = Health::Idle;
	};

	// Everything the card needs for one frame, with every engine pointer already
	// resolved to a texture handle or a string.
	//
	// This is the hand-off between threads: built on the game thread where the
	// actor is alive, then copied under the manager's mutex and read by the
	// render callback. It deliberately holds no UObject pointers.
	struct CardModel
	{
		bool   valid  = false;
		Accent accent = Accent::Crafter;

		PluginTextureHandle buildingIcon = nullptr;

		std::string title;        // building name
		std::string chip;         // left chip — speed multiplier and the like
		std::string statusText;   // right chip
		Health      status = Health::Idle;

		float progress      = 0.0f;
		bool  progressValid = false;

		// Flow body — inputs, the building in its ring, then the product.
		bool                  hasFlow   = false;
		std::vector<FlowSlot> inputs;
		bool                  hasOutput = false;
		FlowSlot              output;
		std::string           outputName;  // drawn under the product's icon
		std::string           rateValue;   // "24"
		std::string           rateUnit;    // "/min"
		std::string           rateSub;     // "5.0s cycle"

		// Stat body — for buildings with nothing to craft.
		std::vector<FlowSlot> stock;      // every entry has an icon
		int32_t               stockMore = 0;   // kinds held that didn't fit the grid
		std::vector<StatCell> stats;

		std::string footLeft;
		std::string footRight;
		std::string description;   // only populated when enabled in config

		void Clear() { *this = CardModel{}; }
	};

	// Builders. Game thread only — they resolve icons through the cache.
	namespace Card
	{
		// Mirrors the config flag. Set by the UI manager -- reading the ini from
		// here would lag a live toggle by one edit.
		void SetShowDescriptions(bool show);

		void FromCrafter      (const Waila::CrafterInfo&       info, CardModel& out);
		void FromStorage      (const Waila::StorageInfo&       info, CardModel& out);
		void FromPower        (const Waila::PowerInfo&         info, CardModel& out);
		void FromCoolerActive (const Waila::CoolerActiveInfo&  info, CardModel& out);
		void FromCoolerPassive(const Waila::CoolerPassiveInfo& info, CardModel& out);
		void FromCargoSender  (const Waila::CargoSenderInfo&   info, CardModel& out);
		void FromCargoReceiver(const Waila::CargoReceiverInfo& info, CardModel& out);
	}
}
