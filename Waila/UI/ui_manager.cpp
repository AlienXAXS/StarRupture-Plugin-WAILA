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

	void WailaUIManager::OnRenderLockWidget(IModLoaderImGui* imgui)
	{
		if (s_instance && imgui)
		{
			s_instance->RenderLockWidget(imgui);
		}
	}

	void WailaUIManager::OnRenderToastWidget(IModLoaderImGui* imgui)
	{
		if (s_instance && imgui)
		{
			s_instance->RenderToastWidget(imgui);
		}
	}

	void WailaUIManager::OnCopyKey(EModKey, EModKeyEvent)
	{
		if (s_instance) s_instance->m_copyPending = true;
	}

	void WailaUIManager::OnPasteKey(EModKey, EModKeyEvent)
	{
		if (s_instance) s_instance->m_pastePending = true;
	}

	void WailaUIManager::OnLockKey(EModKey, EModKeyEvent)
	{
		if (!s_instance) return;

		// Deactivation needs no engine calls — handle immediately
		if (s_instance->m_lockActive)
		{
			s_instance->m_lockActive = false;
			s_instance->m_lockRecipe.Clear();
			s_instance->m_pastedActors.clear();
			s_instance->SetLockWidgetVisible(false);
			LOG_INFO("WAILA Lock: recipe lock cleared");
			return;
		}

		s_instance->m_lockPending = true;
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
		LOG_DEBUG("WailaUIManager::Initialize: MaxDistance = %.1f, ActionDistance = %.1f, ShowActionToast = %d, Scale = %.2f",
			m_maxDistance, m_actionRaycastDistance, m_showActionToast, m_scale);

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

			// Register persistent HUD widget
			if (m_self->hooks->UI)
			{
				Waila::UI::Icons::Init();
				RegisterMainWidget();

				// Lock-mode banner widget (always visible while lock is active)
				m_lockWidgetDesc.name     = "WAILA Lock";
				m_lockWidgetDesc.renderFn = &WailaUIManager::OnRenderLockWidget;

				m_lockWidgetHandle = m_self->hooks->UI->RegisterWidget(&m_lockWidgetDesc);
				if (m_lockWidgetHandle)
				{
					m_self->hooks->UI->SetWidgetVisible(m_lockWidgetHandle, false);
					m_lockWidgetVisible = false;
				}

				// Toast notification widget — centered slightly below screen middle
				m_toastWindowHints.width    = 0.f;
				m_toastWindowHints.height   = 0.f;
				m_toastWindowHints.pos_x    = 960.f;  // updated each frame via GetDisplaySize
				m_toastWindowHints.pos_y    = 620.f;
				m_toastWindowHints.pivot_x  = 0.5f;
				m_toastWindowHints.pivot_y  = 0.5f;
				m_toastWindowHints.size_cond = 0;
				m_toastWindowHints.pos_cond  = 0;  // Always re-apply position
				m_toastWindowHints.extra_window_flags = PluginWindowFlags_NoTitleBar    |
				                                        PluginWindowFlags_NoResize      |
				                                        PluginWindowFlags_NoMove        |
				                                        PluginWindowFlags_NoSavedSettings |
				                                        PluginWindowFlags_NoMouseInputs;

				m_toastWidgetDesc.name        = "WAILA Toast";
				m_toastWidgetDesc.renderFn    = &WailaUIManager::OnRenderToastWidget;
				m_toastWidgetDesc.windowHints = &m_toastWindowHints;

				m_toastWidgetHandle = m_self->hooks->UI->RegisterWidget(&m_toastWidgetDesc);
				if (m_toastWidgetHandle)
				{
					m_self->hooks->UI->SetWidgetVisible(m_toastWidgetHandle, false);
				}
			}

			// Listen for live config changes
			if (m_self->hooks->UI)
			{
				m_self->hooks->UI->RegisterOnConfigChanged(m_self, &OnConfigChanged);
			}

			// Register hotkeys
			if (m_self->hooks->Input)
			{
				auto copyKey  = WailaPluginConfig::Config::GetCopyKey();
				auto pasteKey = WailaPluginConfig::Config::GetPasteKey();
				auto lockKey  = WailaPluginConfig::Config::GetLockKey();
				m_self->hooks->Input->RegisterKeybindByName(copyKey.c_str(),  EModKeyEvent::Pressed, &OnCopyKey);
				m_self->hooks->Input->RegisterKeybindByName(pasteKey.c_str(), EModKeyEvent::Pressed, &OnPasteKey);
				m_self->hooks->Input->RegisterKeybindByName(lockKey.c_str(),  EModKeyEvent::Pressed, &OnLockKey);
				LOG_INFO("WAILA: hotkeys registered (copy=%s paste=%s lock=%s)",
					copyKey.c_str(), pasteKey.c_str(), lockKey.c_str());
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

				if (m_lockWidgetHandle)
				{
					m_self->hooks->UI->UnregisterWidget(m_lockWidgetHandle);
					m_lockWidgetHandle  = nullptr;
					m_lockWidgetVisible = false;
				}
				if (m_toastWidgetHandle)
				{
					m_self->hooks->UI->UnregisterWidget(m_toastWidgetHandle);
					m_toastWidgetHandle    = nullptr;
					m_toastTimeRemaining   = 0.f;
				}
			}

			if (m_self->hooks->Input)
			{
				auto copyKey  = WailaPluginConfig::Config::GetCopyKey();
				auto pasteKey = WailaPluginConfig::Config::GetPasteKey();
				auto lockKey  = WailaPluginConfig::Config::GetLockKey();
				m_self->hooks->Input->UnregisterKeybindByName(copyKey.c_str(),  EModKeyEvent::Pressed, &OnCopyKey);
				m_self->hooks->Input->UnregisterKeybindByName(pasteKey.c_str(), EModKeyEvent::Pressed, &OnPasteKey);
				m_self->hooks->Input->UnregisterKeybindByName(lockKey.c_str(),  EModKeyEvent::Pressed, &OnLockKey);
			}

			if (m_self->hooks->UI)
			{
				m_self->hooks->UI->UnregisterOnConfigChanged(m_self, &OnConfigChanged);
			}
		}

		// Clear clipboard and lock state
		m_clipboard.Clear();
		m_lockRecipe.Clear();
		m_lockActive = false;
		m_pastedActors.clear();

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
		m_actionRaycastDistance = WailaPluginConfig::Config::GetActionDistance();
		m_showActionToast       = WailaPluginConfig::Config::GetShowActionToast();
		m_scale                 = WailaPluginConfig::Config::GetScale();
		m_opacity               = WailaPluginConfig::Config::GetOpacity();
		m_lockOverlay           = WailaPluginConfig::Config::GetLockOverlay();
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
		int flags = PluginWindowFlags_NoTitleBar
		          | PluginWindowFlags_NoResize
		          | PluginWindowFlags_NoScrollbar
		          | PluginWindowFlags_NoBackground;

		// Unlocked leaves ImGui's move handling on, so the card can be dragged by
		// its body whenever the ModLoader UI (F2) has a cursor up. The render
		// callback reserves the content region with an ID-less Dummy, which keeps
		// every pixel of the card a drag handle rather than a widget.
		if (m_lockOverlay)
			flags |= PluginWindowFlags_NoMove | PluginWindowFlags_NoMouseInputs;

		m_widgetHints.extra_window_flags = flags;
	}

	void WailaUIManager::RegisterMainWidget()
	{
		if (!m_self || !m_self->hooks || !m_self->hooks->UI || m_widgetHandle)
			return;

		// Seeded with the smallest card so the first frame is never over-sized;
		// Tick replaces this the moment there is something to show.
		UpdateWindowHints(300.f * m_scale, 110.f * m_scale);
		m_appliedFlags = m_widgetHints.extra_window_flags;

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
		m_appliedFlags  = -1;
	}

	void WailaUIManager::SetLockWidgetVisible(bool visible)
	{
		if (!m_self || !m_self->hooks || !m_self->hooks->UI || !m_lockWidgetHandle) return;
		if (visible != m_lockWidgetVisible)
		{
			m_self->hooks->UI->SetWidgetVisible(m_lockWidgetHandle, visible);
			m_lockWidgetVisible = visible;
		}
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
		if      (strcmp(key, "Max Distance")    == 0) s_instance->m_maxDistance           = strtof(newValue, nullptr);
		else if (strcmp(key, "Action Distance") == 0) s_instance->m_actionRaycastDistance = strtof(newValue, nullptr);
		else if (strcmp(key, "Scale")           == 0) s_instance->m_scale                 = strtof(newValue, nullptr);
		else if (strcmp(key, "Opacity")         == 0) s_instance->m_opacity               = strtof(newValue, nullptr);
		else if (strcmp(key, "Show Action Toast") == 0) s_instance->m_showActionToast = ParseConfigBool(newValue);
		else if (strcmp(key, "Lock Overlay")      == 0) s_instance->m_lockOverlay     = ParseConfigBool(newValue);
		else if (strcmp(key, "Render Building Descriptions") == 0)
		{
			s_instance->m_showDescriptions = ParseConfigBool(newValue);
			Card::SetShowDescriptions(s_instance->m_showDescriptions);
		}
		else return;   // not a setting that changes how the card looks

		s_instance->ClampSettings();

		// Size and colour changes land on their own; the lock flag is baked into
		// the window at creation, so that one needs the widget rebuilt.
		s_instance->UpdateWindowHints(s_instance->m_widgetHints.width, s_instance->m_widgetHints.height);
		if (s_instance->m_widgetHints.extra_window_flags != s_instance->m_appliedFlags)
		{
			s_instance->UnregisterMainWidget();
			s_instance->RegisterMainWidget();
		}
	}

	void WailaUIManager::ShowToast(const std::string& message)
	{
		LOG_TRACE("WAILA Toast: ShowToast called msg='%s'", message.c_str());
		LOG_TRACE("WAILA Toast: m_self=%p hooks=%p UI=%p handle=%p",
			m_self,
			m_self ? m_self->hooks : nullptr,
			(m_self && m_self->hooks) ? m_self->hooks->UI : nullptr,
			m_toastWidgetHandle);

		if (!m_self || !m_self->hooks || !m_self->hooks->UI || !m_toastWidgetHandle)
		{
			LOG_TRACE("WAILA Toast: ShowToast aborted — null pointer");
			return;
		}

		m_toastMessage       = message;
		m_toastTimeRemaining = 1.5f;
		LOG_TRACE("WAILA Toast: calling SetWidgetVisible(true)");
		m_self->hooks->UI->SetWidgetVisible(m_toastWidgetHandle, true);
		LOG_TRACE("WAILA Toast: SetWidgetVisible returned");
	}

	void WailaUIManager::RenderToastWidget(IModLoaderImGui* imgui)
	{
		LOG_TRACE("WAILA Toast: RenderToastWidget called timeRemaining=%.2f msg='%s'",
			m_toastTimeRemaining, m_toastMessage.c_str());

		if (m_toastTimeRemaining <= 0.f || m_toastMessage.empty()) return;

		float dispW = 1920.f, dispH = 1080.f;
		imgui->GetDisplaySize(&dispW, &dispH);

		// Measure text and size window to fit with padding
		float textW = 0.f, textH = 0.f;
		imgui->CalcTextSize(m_toastMessage.c_str(), &textW, &textH, false, -1.f);
		constexpr float kPadX = 16.f;
		constexpr float kPadY = 12.f;
		m_toastWindowHints.width  = textW + kPadX * 2.f;
		m_toastWindowHints.height = textH + kPadY * 2.f;
		m_toastWindowHints.pos_x  = dispW * 0.5f;
		m_toastWindowHints.pos_y  = dispH * 0.58f;

		LOG_TRACE("WAILA Toast: calling TextColored");
		imgui->TextColored(1.0f, 1.0f, 0.3f, 1.0f, m_toastMessage.c_str());
		LOG_TRACE("WAILA Toast: RenderToastWidget complete");
	}

	void WailaUIManager::ApplyRecipeToCrafter(SDK::AActor* actor, const Waila::RecipeClipboard& clip)
	{
		LOG_TRACE("WAILA ApplyRecipe: actor=%p recipeData=%p recipe='%s'",
			actor, clip.recipeData, clip.recipeDisplayName.c_str());

		if (!actor || !clip.recipeData)
		{
			LOG_WARN("WAILA ApplyRecipe: null actor or recipeData");
			return;
		}

		if (strstr(actor->GetName().c_str(), "BP_MechanicalDrill_C") != nullptr)
		{
			LOG_INFO("WAILA ApplyRecipe: skipping excluded actor '%s'", actor->GetName().c_str());
			return;
		}

		auto fnCtor = Waila::Functions::GetMassActorHelperCtor();
		auto fnAdd  = Waila::Functions::GetAddItemToCraft();
		LOG_TRACE("WAILA ApplyRecipe: fnCtor=%p fnAdd=%p", fnCtor, fnAdd);
		if (!fnCtor || !fnAdd)
		{
			LOG_WARN("WAILA ApplyRecipe: function pointers not resolved — check pattern scan");
			return;
		}

		SDK::UWorld* world = SDK::UWorld::GetWorld();
		LOG_TRACE("WAILA ApplyRecipe: world=%p", world);
		if (!world) return;

		SDK::APlayerController* pc = SDK::UGameplayStatics::GetPlayerController(world, 0);
		SDK::ACrPlayerControllerBase* crPc = static_cast<SDK::ACrPlayerControllerBase*>(pc);
		LOG_TRACE("WAILA ApplyRecipe: pc=%p crPc=%p", pc, crPc);
		if (!crPc) return;

		LOG_TRACE("WAILA ApplyRecipe: calling fnCtor");
		SDK::FCrMassActorReplicationHelper helper{};
		fnCtor(&helper, actor);

		LOG_TRACE("WAILA ApplyRecipe: calling fnAdd");
		fnAdd(crPc, helper, clip.recipeData, 1);
		LOG_INFO("WAILA ApplyRecipe: applied '%s' to %s",
			clip.recipeDisplayName.c_str(), actor->GetName().c_str());
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

		// Store the card and debug ray under lock for the render thread and hotkey callbacks
		{
			std::lock_guard<std::mutex> lock(m_infoMutex);
			m_pendingCard        = std::move(card);
			m_debugRay.start     = hit.rayStart;
			m_debugRay.end       = hit.rayEnd;
			m_debugRay.hit       = bHit;
			m_debugRay.valid     = true;
			// Cache for hotkey callbacks
			m_lastHitActor  = info.IsValid() ? info.actorPtr  : nullptr;
			m_lastRecipeData = info.IsValid() ? info.recipeDataPtr : nullptr;
		}

		// Process deferred key actions (input thread set flags, game thread executes)
		if (m_copyPending.exchange(false))
		{
			LOG_TRACE("WAILA Copy: action triggered, raycasting at distance %.1f", m_actionRaycastDistance);
			Waila::Core::RaycastHit ah;
			bool bActionHit = m_raycaster.PerformRaycast(m_actionRaycastDistance, ah);
			LOG_TRACE("WAILA Copy: raycast hit=%d actor=%p", bActionHit, bActionHit ? ah.actor : nullptr);

			Waila::CrafterInfo snap;
			if (bActionHit && ah.actor)
			{
				bool isCrafter = Waila::CrafterDetector::IsCrafter(ah.actor);
				LOG_TRACE("WAILA Copy: IsCrafter=%d", isCrafter);
				if (isCrafter && strstr(ah.actor->GetName().c_str(), "BP_MechanicalDrill_C") == nullptr)
					Waila::CrafterDetector::GetCrafterInfo(ah.actor, snap);
				else if (isCrafter)
					LOG_INFO("WAILA Copy: skipping excluded actor '%s'", ah.actor->GetName().c_str());
			}

			LOG_TRACE("WAILA Copy: snap.IsValid=%d recipe='%s' recipeDataPtr=%p",
				snap.IsValid(), snap.currentRecipe.c_str(), snap.recipeDataPtr);

			if (snap.IsValid() && snap.currentRecipe != "(idle)" && snap.recipeDataPtr)
			{
				m_clipboard.buildingClass     = snap.crafterClass;
				m_clipboard.recipeDisplayName = snap.currentRecipeDisplayName;
				m_clipboard.recipeData        = snap.recipeDataPtr;
				LOG_INFO("WAILA Copy: copied '%s' from %s", snap.currentRecipeDisplayName.c_str(), snap.crafterClass.c_str());
				if (m_showActionToast)
					ShowToast(snap.currentRecipeDisplayName + " Copied From " + snap.crafterClass + "!");
			}
			else
			{
				LOG_INFO("WAILA Copy: nothing to copy (no crafter in view or no recipe set)");
			}
		}

		if (m_pastePending.exchange(false))
		{
			LOG_TRACE("WAILA Paste: action triggered, clipboard.IsValid=%d buildingClass='%s' recipe='%s' recipeData=%p",
				m_clipboard.IsValid(), m_clipboard.buildingClass.c_str(),
				m_clipboard.recipeDisplayName.c_str(), m_clipboard.recipeData);

			if (!m_clipboard.IsValid())
			{
				LOG_INFO("WAILA Paste: clipboard is empty");
			}
			else
			{
				LOG_TRACE("WAILA Paste: raycasting at distance %.1f", m_actionRaycastDistance);
				Waila::Core::RaycastHit ah;
				bool bActionHit = m_raycaster.PerformRaycast(m_actionRaycastDistance, ah);
				LOG_TRACE("WAILA Paste: raycast hit=%d actor=%p", bActionHit, bActionHit ? ah.actor : nullptr);

				Waila::CrafterInfo snap;
				if (bActionHit && ah.actor)
				{
					bool isCrafter = Waila::CrafterDetector::IsCrafter(ah.actor);
					LOG_TRACE("WAILA Paste: IsCrafter=%d", isCrafter);
					if (isCrafter)
						Waila::CrafterDetector::GetCrafterInfo(ah.actor, snap);
				}

				LOG_TRACE("WAILA Paste: snap.IsValid=%d actorPtr=%p crafterClass='%s'",
					snap.IsValid(), snap.actorPtr, snap.crafterClass.c_str());

				if (!snap.IsValid() || !snap.actorPtr)
				{
					LOG_INFO("WAILA Paste: not looking at a crafter");
				}
				else
				{
					bool classMatch = (snap.crafterClass == m_clipboard.buildingClass);
					LOG_TRACE("WAILA Paste: classMatch=%d targetLen=%d clipLen=%d",
						classMatch, (int)snap.crafterClass.size(), (int)m_clipboard.buildingClass.size());

					if (!classMatch)
					{
						LOG_INFO("WAILA Paste: building type mismatch (target='%s' clipboard='%s')",
							snap.crafterClass.c_str(), m_clipboard.buildingClass.c_str());
					}
					else
					{
						LOG_INFO("WAILA Paste: applying '%s' to %s",
							m_clipboard.recipeDisplayName.c_str(), snap.crafterClass.c_str());
						ApplyRecipeToCrafter(snap.actorPtr, m_clipboard);
						if (m_showActionToast)
							ShowToast(m_clipboard.recipeDisplayName + " Pasted To " + snap.crafterClass + "!");
					}
				}
			}
		}

		if (m_lockPending.exchange(false))
		{
			LOG_TRACE("WAILA Lock: action triggered, raycasting at distance %.1f", m_actionRaycastDistance);
			Waila::Core::RaycastHit ah;
			bool bActionHit = m_raycaster.PerformRaycast(m_actionRaycastDistance, ah);
			LOG_TRACE("WAILA Lock: raycast hit=%d actor=%p", bActionHit, bActionHit ? ah.actor : nullptr);

			Waila::CrafterInfo snap;
			if (bActionHit && ah.actor)
			{
				bool isCrafter = Waila::CrafterDetector::IsCrafter(ah.actor);
				LOG_TRACE("WAILA Lock: IsCrafter=%d", isCrafter);
				if (isCrafter)
					Waila::CrafterDetector::GetCrafterInfo(ah.actor, snap);
			}

			LOG_TRACE("WAILA Lock: snap.IsValid=%d recipe='%s' recipeDataPtr=%p",
				snap.IsValid(), snap.currentRecipe.c_str(), snap.recipeDataPtr);

			if (snap.IsValid() && snap.currentRecipe != "(idle)" && snap.recipeDataPtr)
			{
				m_lockRecipe.buildingClass     = snap.crafterClass;
				m_lockRecipe.recipeDisplayName = snap.currentRecipeDisplayName;
				m_lockRecipe.recipeData        = snap.recipeDataPtr;
				m_lockActive                   = true;
				m_pastedActors.clear();
				SetLockWidgetVisible(true);
				if (m_showActionToast)
					ShowToast(snap.currentRecipeDisplayName + " Locked From " + snap.crafterClass + "!");
				LOG_INFO("WAILA Lock: locked '%s' for %s", snap.currentRecipeDisplayName.c_str(), snap.crafterClass.c_str());
			}
			else
			{
				LOG_INFO("WAILA Lock: not looking at a crafter with a recipe set");
			}
		}

		// Lock-mode autopaste: find any new idle crafter of the locked type not yet pasted
		if (m_lockActive)
		{
			SDK::UWorld* world = SDK::UWorld::GetWorld();
			if (world)
			{
				SDK::TArray<SDK::AActor*> allActors;
				SDK::UGameplayStatics::GetAllActorsOfClass(
					world, SDK::ACrCrafter::StaticClass(), &allActors);

				for (int32_t i = 0; i < allActors.Num(); ++i)
				{
					SDK::AActor* a = allActors[i];
					if (!a || !SDK::UKismetSystemLibrary::IsValid(a)) continue;
					if (m_pastedActors.count(static_cast<void*>(a))) continue;

					SDK::ACrCrafter*          cr = static_cast<SDK::ACrCrafter*>(a);
					SDK::ACrBuildingActorBase* b  = static_cast<SDK::ACrBuildingActorBase*>(a);
					SDK::UCrBuildingData* bd = (b->PlacementData &&
						b->PlacementData->IsA(SDK::UCrBuildingData::StaticClass()))
						? static_cast<SDK::UCrBuildingData*>(b->PlacementData)
						: nullptr;

					if (!bd) continue;
					std::string cls = SDK::UKismetTextLibrary::Conv_TextToString(bd->BuildingName).ToString();
					if (cls != m_lockRecipe.buildingClass) continue;

					// Only paste onto idle buildings (no recipe queued yet)
					if (!cr->CraftComponent || cr->CraftComponent->ItemsToCraft.Num() > 0)
					{
						// Already has a recipe — mark as seen so we don't retry it
						m_pastedActors.insert(static_cast<void*>(a));
						continue;
					}

					ApplyRecipeToCrafter(a, m_lockRecipe);
					m_pastedActors.insert(static_cast<void*>(a));
				}
			}
		}

		// Toast countdown
		if (m_toastTimeRemaining > 0.f)
		{
			m_toastTimeRemaining -= deltaSeconds;
			if (m_toastTimeRemaining <= 0.f)
			{
				m_toastTimeRemaining = 0.f;
				if (m_self->hooks && m_self->hooks->UI && m_toastWidgetHandle)
					m_self->hooks->UI->SetWidgetVisible(m_toastWidgetHandle, false);
			}
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

	void WailaUIManager::RenderLockWidget(IModLoaderImGui* imgui)
	{
		if (!m_lockActive) return;

		char buf[256];
		imgui->TextColored(1.0f, 0.85f, 0.0f, 1.0f, "[ RECIPE LOCK ACTIVE ]");
		imgui->Separator();

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Building: %s", m_lockRecipe.buildingClass.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Recipe:   %s", m_lockRecipe.recipeDisplayName.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);
	}
}
