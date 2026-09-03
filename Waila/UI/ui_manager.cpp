#include "ui_manager.h"
#include "plugin_helpers.h"
#include "plugin_config.h"
#include "waila_functions.h"
#include "UI/icon_cache.h"
#include "UI/ui_draw.h"
#include "UI/ui_theme.h"
#include "UI/waila_card.h"
#include "Engine_classes.hpp"
#include "Engine_structs.hpp"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "ChimeraMassCommon_structs.hpp"
#include <cstdio>
#include <string>

namespace Waila::UI
{
	WailaUIManager* WailaUIManager::s_instance = nullptr;

	// ---------------------------------------------------------------------------
	// Static callbacks (C function pointers — access state via s_instance)
	// ---------------------------------------------------------------------------

	void WailaUIManager::OnTick(float deltaSeconds)
	{
		if (s_instance)
		{
			s_instance->Tick(deltaSeconds);
		}
	}

	void WailaUIManager::OnRenderWidget(IModLoaderImGui* imgui)
	{
		if (s_instance && imgui)
		{
			s_instance->RenderWidget(imgui);
		}
	}

	// ---------------------------------------------------------------------------
	// Lifecycle
	// ---------------------------------------------------------------------------

	void WailaUIManager::Initialize(IPluginSelf* self)
	{
		LOG_DEBUG("WailaUIManager::Initialize: this=%p self=%p", this, self);
		s_instance = this;
		LOG_DEBUG("WailaUIManager::Initialize: s_instance set");

		if (!self)
		{
			LOG_ERROR("WailaUIManager::Initialize: self is null");
			return;
		}

		LOG_DEBUG("WailaUIManager::Initialize: self->config = %p", self->config);
		if (!self->config)
		{
			LOG_ERROR("WailaUIManager::Initialize: self->config is null, using default MaxDistance");
			return;
		}

		LOG_DEBUG("WailaUIManager::Initialize: reading settings from config");
		ApplySettings();
		LOG_DEBUG("WailaUIManager::Initialize: MaxDistance = %.1f, Scale = %.2f",
			m_maxDistance, m_scale);

		LOG_DEBUG("WailaUIManager::Initialize: complete");
	}

	void WailaUIManager::Shutdown()
	{
		LOG_DEBUG("WailaUIManager::Shutdown begin");

		Disable();
		s_instance = nullptr;

		LOG_DEBUG("WailaUIManager::Shutdown complete");
	}

	void WailaUIManager::Enable(IPluginSelf* self)
	{
		LOG_INFO("WailaUIManager: Enabling Waila UI Manager...");
		if (m_self || !self)
		{
			return;
		}

		m_self = self;

		// Register engine tick for raycasting
		if (m_self->hooks)
		{
			if (m_self->hooks->Engine)
			{
				m_self->hooks->Engine->RegisterOnTick(&WailaUIManager::OnTick);
			}

			// Register persistent HUD widget and listen for live config changes
			if (m_self->hooks->UI)
			{
				Waila::UI::Icons::Init();
				RegisterMainWidget();

				m_self->hooks->UI->RegisterOnConfigChanged(m_self, &OnConfigChanged);
			}
		}
	}

	void WailaUIManager::Disable()
	{
		if (!m_self)
		{
			return;
		}

		LOG_DEBUG("WailaUIManager::Disable begin");

		if (m_self->hooks)
		{
			if (m_self->hooks->Engine)
			{
				m_self->hooks->Engine->UnregisterOnTick(&WailaUIManager::OnTick);
			}

			if (m_self->hooks->UI)
			{
				UnregisterMainWidget();

				// The published model holds texture handles Icons is about to free,
				// so it has to go first.
				{
					std::lock_guard<std::mutex> lock(m_infoMutex);
					m_pendingCard.Clear();
				}
				Waila::UI::Icons::Shutdown();

				m_self->hooks->UI->UnregisterOnConfigChanged(m_self, &OnConfigChanged);
			}
		}

		m_self = nullptr;
	}

	// ---------------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------------

	void WailaUIManager::ClampSettings()
	{
		if (m_scale < 0.5f) m_scale = 0.5f;
		if (m_scale > 3.0f) m_scale = 3.0f;
		if (m_opacity < 0.1f) m_opacity = 0.1f;
		if (m_opacity > 1.0f) m_opacity = 1.0f;
	}

	// Full read from the ini. Only for startup — live edits come through
	// OnConfigChanged, which is given the new value directly.
	void WailaUIManager::ApplySettings()
	{
		m_maxDistance           = WailaPluginConfig::Config::GetMaxDistance();
		m_scale                 = WailaPluginConfig::Config::GetScale();
		m_opacity               = WailaPluginConfig::Config::GetOpacity();
		m_showDescriptions      = WailaPluginConfig::Config::ShouldRenderDescriptions();

		ClampSettings();
		Card::SetShowDescriptions(m_showDescriptions);
	}

	void WailaUIManager::UpdateWindowHints(float width, float height)
	{
		m_widgetHints.width   = width;
		m_widgetHints.height  = height;
		m_widgetHints.pos_x   = -1.f;   // let the player place it
		m_widgetHints.pos_y   = -1.f;
		m_widgetHints.pivot_x = 0.f;
		m_widgetHints.pivot_y = 0.f;
		m_widgetHints.size_cond = 0;    // Always — a scale change lands immediately
		m_widgetHints.pos_cond  = 1;    // FirstUseEver

		// NoBackground matters: the card is drawn by hand, so ImGui's own window
		// frame would sit around it as a second, larger box.
		//
		// No NoSavedSettings, and pos_x/pos_y stay negative so the ModLoader never
		// calls SetNextWindowPos: between them, wherever the player drags the card
		// is what ImGui writes to its ini and restores next launch.
		//
		// ImGui's move handling stays on, so the card drags by its body whenever the
		// ModLoader UI (F2) has a cursor up. The render callback reserves the content
		// region with an ID-less Dummy, which keeps every pixel of the card a drag
		// handle rather than a widget.
		m_widgetHints.extra_window_flags = PluginWindowFlags_NoTitleBar
		                                 | PluginWindowFlags_NoResize
		                                 | PluginWindowFlags_NoScrollbar
		                                 | PluginWindowFlags_NoBackground;
	}

	void WailaUIManager::RegisterMainWidget()
	{
		if (!m_self || !m_self->hooks || !m_self->hooks->UI || m_widgetHandle)
			return;

		// Seeded with the smallest card so the first frame is never over-sized;
		// Tick replaces this the moment there is something to show.
		UpdateWindowHints(300.f * m_scale, 110.f * m_scale);

		m_widgetDesc.name        = "WAILA";
		m_widgetDesc.renderFn    = &WailaUIManager::OnRenderWidget;
		m_widgetDesc.windowHints = &m_widgetHints;

		m_widgetHandle = m_self->hooks->UI->RegisterWidget(&m_widgetDesc);
		if (!m_widgetHandle)
		{
			LOG_ERROR("WailaUIManager: failed to register the WAILA card widget");
			return;
		}

		m_self->hooks->UI->SetWidgetVisible(m_widgetHandle, false);
		m_widgetVisible = false;
	}

	void WailaUIManager::UnregisterMainWidget()
	{
		if (!m_self || !m_self->hooks || !m_self->hooks->UI || !m_widgetHandle)
			return;

		m_self->hooks->UI->UnregisterWidget(m_widgetHandle);
		m_widgetHandle  = nullptr;
		m_widgetVisible = false;
	}

	namespace
	{
		bool ParseConfigBool(const char* value)
		{
			return value && (_stricmp(value, "true") == 0 ||
			                 _stricmp(value, "yes")  == 0 ||
			                 strcmp(value, "1") == 0);
		}
	}

	void WailaUIManager::OnConfigChanged(const char* section, const char* key, const char* newValue)
	{
		if (!s_instance || !section || !key || !newValue) return;
		if (strcmp(section, "WAILA") != 0) return;

		// The ModLoader fires this the moment the value changes in memory and only
		// writes the ini when the widget is released, so re-reading the config here
		// would hand back the *previous* setting — the edit would appear to take
		// two changes to land. Take the value from the callback instead.
		if      (strcmp(key, "Max Distance") == 0) s_instance->m_maxDistance = strtof(newValue, nullptr);
		else if (strcmp(key, "Scale")        == 0) s_instance->m_scale       = strtof(newValue, nullptr);
		else if (strcmp(key, "Opacity")      == 0) s_instance->m_opacity     = strtof(newValue, nullptr);
		else if (strcmp(key, "Render Building Descriptions") == 0)
		{
			s_instance->m_showDescriptions = ParseConfigBool(newValue);
			Card::SetShowDescriptions(s_instance->m_showDescriptions);
		}
		else return;   // not a setting that changes how the card looks

		s_instance->ClampSettings();

		// Size and colour changes land on their own — the next Tick re-measures the
		// card and pushes the new hints, and the window flags never vary.
		s_instance->UpdateWindowHints(s_instance->m_widgetHints.width, s_instance->m_widgetHints.height);
	}

	// ---------------------------------------------------------------------------
	// Per-frame tick: raycast then update widget visibility
	// ---------------------------------------------------------------------------

	void WailaUIManager::Tick(float deltaSeconds)
	{
		if (!m_self)
		{
			return;
		}


		Waila::Core::RaycastHit hit;
		Waila::CrafterInfo  info;
		Waila::StorageInfo  storageInfo;
		Waila::PowerInfo    powerInfo;
		Waila::CoolerActiveInfo  coolerActiveInfo;
		Waila::CoolerPassiveInfo coolerPassiveInfo;
		Waila::CargoSenderInfo   cargoSenderInfo;
		Waila::CargoReceiverInfo cargoReceiverInfo;

		// Perform raycast and extract info immediately (while actor pointer is valid)
		// PerformRaycast always fills hit.rayStart/rayEnd even on a miss, so we can visualise both cases.
		bool bHit = m_raycaster.PerformRaycast(m_maxDistance, hit);
		if (bHit && hit.actor)
		{
			if (Waila::CrafterDetector::IsCrafter(hit.actor))
			{
				Waila::CrafterDetector::GetCrafterInfo(hit.actor, info);
			}
			else if (Waila::StorageDetector::IsStorage(hit.actor))
			{
				Waila::StorageDetector::GetStorageInfo(hit.actor, storageInfo);
			}
			else if (Waila::PowerDetector::IsGenerator(hit.actor))
			{
				Waila::PowerDetector::GetPowerInfo(hit.actor, powerInfo);
			}
			else if (Waila::CoolerActiveDetector::IsCoolerActive(hit.actor))
			{
				Waila::CoolerActiveDetector::GetCoolerActiveInfo(hit.actor, coolerActiveInfo);
			}
			else if (Waila::CoolerPassiveDetector::IsCoolerPassive(hit.actor))
			{
				Waila::CoolerPassiveDetector::GetCoolerPassiveInfo(hit.actor, coolerPassiveInfo);
			}
			else if (Waila::CargoSenderDetector::IsSender(hit.actor))
			{
				Waila::CargoSenderDetector::GetSenderInfo(hit.actor, cargoSenderInfo);
			}
			else if (Waila::CargoReceiverDetector::IsReceiver(hit.actor))
			{
				Waila::CargoReceiverDetector::GetReceiverInfo(hit.actor, cargoReceiverInfo);
			}
		}

		// Retry any icons the GPU wasn't ready for on an earlier frame.
		Waila::UI::Icons::Tick();

		// Fold whatever was detected into the card model. This is the only place
		// engine pointers are read for display — everything downstream of here is
		// strings and texture handles, so the render thread can never chase an
		// actor that has since been destroyed.
		Waila::UI::CardModel card;
		if (info.IsValid())                   Waila::UI::Card::FromCrafter(info, card);
		else if (storageInfo.IsValid())       Waila::UI::Card::FromStorage(storageInfo, card);
		else if (powerInfo.IsValid())         Waila::UI::Card::FromPower(powerInfo, card);
		else if (coolerActiveInfo.IsValid())  Waila::UI::Card::FromCoolerActive(coolerActiveInfo, card);
		else if (coolerPassiveInfo.IsValid()) Waila::UI::Card::FromCoolerPassive(coolerPassiveInfo, card);
		else if (cargoSenderInfo.IsValid())   Waila::UI::Card::FromCargoSender(cargoSenderInfo, card);
		else if (cargoReceiverInfo.IsValid()) Waila::UI::Card::FromCargoReceiver(cargoReceiverInfo, card);

		// Size the window to exactly what the card will paint, at the current scale.
		const bool hasCard = card.valid;
		if (hasCard)
		{
			float cardW = 0.f, cardH = 0.f;
			Waila::UI::Card::Measure(card, m_scale, cardW, cardH);
			UpdateWindowHints(cardW, cardH);
		}

		// Store the card and debug ray under lock for the render thread
		{
			std::lock_guard<std::mutex> lock(m_infoMutex);
			m_pendingCard        = std::move(card);
			m_debugRay.start     = hit.rayStart;
			m_debugRay.end       = hit.rayEnd;
			m_debugRay.hit       = bHit;
			m_debugRay.valid     = true;
		}

		if (m_self->hooks && m_self->hooks->UI && m_widgetHandle)
		{
			if (hasCard != m_widgetVisible)
			{
				m_self->hooks->UI->SetWidgetVisible(m_widgetHandle, hasCard);
				m_widgetVisible = hasCard;
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Widget render — called by mod loader when widget is visible
	// ---------------------------------------------------------------------------

	void WailaUIManager::RenderWidget(IModLoaderImGui* imgui)
	{
		try
		{
			if (!imgui || !m_self)
				return;

			Waila::UI::CardModel card;
			{
				std::lock_guard<std::mutex> lock(m_infoMutex);
				card = m_pendingCard;
			}

			if (!card.valid)
				return;

			// Draw over the whole window rect rather than the padded content
			// region -- with NoBackground the padding is just dead transparent
			// margin, and the card should fill what the size hint asked for.
			float wx = 0.f, wy = 0.f, ww = 0.f, wh = 0.f;
			imgui->GetWindowPos(&wx, &wy);
			imgui->GetWindowSize(&ww, &wh);
			if (ww <= 1.f || wh <= 1.f)
				return;

			// Still reserve the content region so ImGui keeps the window sized and
			// leaves the body draggable when the overlay is unlocked.
			float aw = 0.f, ah = 0.f;
			imgui->GetContentRegionAvail(&aw, &ah);
			if (aw > 0.f && ah > 0.f)
				imgui->Dummy(aw, ah);

			const Waila::UI::Palette pal = Waila::UI::Theme::Build(card.accent, m_opacity);
			const Waila::UI::Rect    rect{ wx, wy, wx + ww, wy + wh };

			Waila::UI::Card::Draw(imgui, imgui->GetWindowDrawList(), card, pal, m_scale, rect);
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("WailaUIManager::RenderWidget: caught exception: %s", e.what());
		}
		catch (...)
		{
			LOG_ERROR("WailaUIManager::RenderWidget: caught unknown exception");
		}
	}
}
