#include "ui_manager.h"
#include "plugin_helpers.h"
#include "plugin_config.h"
#include "waila_functions.h"
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

		LOG_DEBUG("WailaUIManager::Initialize: reading distances from config");
		m_maxDistance           = WailaPluginConfig::Config::GetMaxDistance();
		m_actionRaycastDistance = WailaPluginConfig::Config::GetActionDistance();
		m_showActionToast       = WailaPluginConfig::Config::GetShowActionToast();
		LOG_DEBUG("WailaUIManager::Initialize: MaxDistance = %.1f, ActionDistance = %.1f, ShowActionToast = %d",
			m_maxDistance, m_actionRaycastDistance, m_showActionToast);

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
				m_widgetDesc.name = "WAILA";
				m_widgetDesc.renderFn = &WailaUIManager::OnRenderWidget;

				m_widgetHandle = m_self->hooks->UI->RegisterWidget(&m_widgetDesc);

				if (m_widgetHandle)
				{
					m_self->hooks->UI->SetWidgetVisible(m_widgetHandle, false);
					m_widgetVisible = false;
				}

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
				m_self->hooks->UI->RegisterOnConfigChanged(&OnConfigChanged);
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
				if (m_widgetHandle)
				{
					m_self->hooks->UI->UnregisterWidget(m_widgetHandle);
					m_widgetHandle  = nullptr;
					m_widgetVisible = false;
				}
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
				m_self->hooks->UI->UnregisterOnConfigChanged(&OnConfigChanged);
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

	void WailaUIManager::SetLockWidgetVisible(bool visible)
	{
		if (!m_self || !m_self->hooks || !m_self->hooks->UI || !m_lockWidgetHandle) return;
		if (visible != m_lockWidgetVisible)
		{
			m_self->hooks->UI->SetWidgetVisible(m_lockWidgetHandle, visible);
			m_lockWidgetVisible = visible;
		}
	}

	void WailaUIManager::OnConfigChanged(const char* section, const char* /*key*/, const char* /*newValue*/)
	{
		if (!s_instance || strcmp(section, "WAILA") != 0) return;
		s_instance->m_maxDistance           = WailaPluginConfig::Config::GetMaxDistance();
		s_instance->m_actionRaycastDistance = WailaPluginConfig::Config::GetActionDistance();
		s_instance->m_showActionToast       = WailaPluginConfig::Config::GetShowActionToast();
		LOG_INFO("WAILA: config updated — MaxDistance=%.1f ActionDistance=%.1f",
			s_instance->m_maxDistance, s_instance->m_actionRaycastDistance);
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

		LOG_TRACE("WAILA Toast: calling GetDisplaySize");
		float dispW = 1920.f, dispH = 1080.f;
		imgui->GetDisplaySize(&dispW, &dispH);
		LOG_TRACE("WAILA Toast: display=%.0fx%.0f", dispW, dispH);

		m_toastWindowHints.pos_x = dispW * 0.5f;
		m_toastWindowHints.pos_y = dispH * 0.58f;

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

		// Store the extracted info and debug ray under lock for the render thread and hotkey callbacks
		{
			std::lock_guard<std::mutex> lock(m_infoMutex);
			m_pendingInfo        = info;
			m_pendingStorageInfo = storageInfo;
			m_pendingPowerInfo   = powerInfo;
			m_pendingCoolerActiveInfo  = coolerActiveInfo;
			m_pendingCoolerPassiveInfo = coolerPassiveInfo;
			m_pendingCargoSenderInfo   = cargoSenderInfo;
			m_pendingCargoReceiverInfo = cargoReceiverInfo;
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
			LOG_INFO("WAILA Copy: action triggered, raycasting at distance %.1f", m_actionRaycastDistance);
			Waila::Core::RaycastHit ah;
			bool bActionHit = m_raycaster.PerformRaycast(m_actionRaycastDistance, ah);
			LOG_INFO("WAILA Copy: raycast hit=%d actor=%p", bActionHit, bActionHit ? ah.actor : nullptr);

			Waila::CrafterInfo snap;
			if (bActionHit && ah.actor)
			{
				bool isCrafter = Waila::CrafterDetector::IsCrafter(ah.actor);
				LOG_INFO("WAILA Copy: IsCrafter=%d", isCrafter);
				if (isCrafter)
					Waila::CrafterDetector::GetCrafterInfo(ah.actor, snap);
			}

			LOG_INFO("WAILA Copy: snap.IsValid=%d recipe='%s' recipeDataPtr=%p",
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
			LOG_INFO("WAILA Paste: action triggered, clipboard.IsValid=%d buildingClass='%s' recipe='%s' recipeData=%p",
				m_clipboard.IsValid(), m_clipboard.buildingClass.c_str(),
				m_clipboard.recipeDisplayName.c_str(), m_clipboard.recipeData);

			if (!m_clipboard.IsValid())
			{
				LOG_INFO("WAILA Paste: clipboard is empty");
			}
			else
			{
				LOG_INFO("WAILA Paste: raycasting at distance %.1f", m_actionRaycastDistance);
				Waila::Core::RaycastHit ah;
				bool bActionHit = m_raycaster.PerformRaycast(m_actionRaycastDistance, ah);
				LOG_INFO("WAILA Paste: raycast hit=%d actor=%p", bActionHit, bActionHit ? ah.actor : nullptr);

				Waila::CrafterInfo snap;
				if (bActionHit && ah.actor)
				{
					bool isCrafter = Waila::CrafterDetector::IsCrafter(ah.actor);
					LOG_INFO("WAILA Paste: IsCrafter=%d", isCrafter);
					if (isCrafter)
						Waila::CrafterDetector::GetCrafterInfo(ah.actor, snap);
				}

				LOG_INFO("WAILA Paste: snap.IsValid=%d actorPtr=%p crafterClass='%s'",
					snap.IsValid(), snap.actorPtr, snap.crafterClass.c_str());

				if (!snap.IsValid() || !snap.actorPtr)
					LOG_INFO("WAILA Paste: not looking at a crafter");
				else
				{
					LOG_TRACE("WAILA Paste: class comparison target='%s'(%d) clipboard='%s'(%d) match=%d",
						snap.crafterClass.c_str(), (int)snap.crafterClass.size(),
						m_clipboard.buildingClass.c_str(), (int)m_clipboard.buildingClass.size(),
						snap.crafterClass == m_clipboard.buildingClass);

					if (snap.crafterClass != m_clipboard.buildingClass)
						LOG_INFO("WAILA Paste: building type mismatch (target='%s' clipboard='%s')",
							snap.crafterClass.c_str(), m_clipboard.buildingClass.c_str());
					else
					{
						LOG_INFO("WAILA Paste: applying '%s' to %s", m_clipboard.recipeDisplayName.c_str(), snap.crafterClass.c_str());
						ApplyRecipeToCrafter(snap.actorPtr, m_clipboard);
						if (m_showActionToast)
							ShowToast(m_clipboard.recipeDisplayName + " Pasted To " + snap.crafterClass + "!");
					}
				}
			}
		}

		if (m_lockPending.exchange(false))
		{
			LOG_INFO("WAILA Lock: action triggered, raycasting at distance %.1f", m_actionRaycastDistance);
			Waila::Core::RaycastHit ah;
			bool bActionHit = m_raycaster.PerformRaycast(m_actionRaycastDistance, ah);
			LOG_INFO("WAILA Lock: raycast hit=%d actor=%p", bActionHit, bActionHit ? ah.actor : nullptr);

			Waila::CrafterInfo snap;
			if (bActionHit && ah.actor)
			{
				bool isCrafter = Waila::CrafterDetector::IsCrafter(ah.actor);
				LOG_INFO("WAILA Lock: IsCrafter=%d", isCrafter);
				if (isCrafter)
					Waila::CrafterDetector::GetCrafterInfo(ah.actor, snap);
			}

			LOG_INFO("WAILA Lock: snap.IsValid=%d recipe='%s' recipeDataPtr=%p",
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

		bool shouldBeVisible = info.IsValid() || storageInfo.IsValid() || powerInfo.IsValid() || coolerActiveInfo.IsValid() || coolerPassiveInfo.IsValid() || cargoSenderInfo.IsValid() || cargoReceiverInfo.IsValid();

		if (m_self->hooks && m_self->hooks->UI && m_widgetHandle)
		{
			if (shouldBeVisible != m_widgetVisible)
			{
				m_self->hooks->UI->SetWidgetVisible(m_widgetHandle, shouldBeVisible);
				m_widgetVisible = shouldBeVisible;
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Widget render — called by mod loader when widget is visible
	// ---------------------------------------------------------------------------

	static void RenderWrappedDesc(IModLoaderImGui* imgui, const std::string& desc, int maxLineLen = 30)
	{
		auto shouldRenderDesc = WailaPluginConfig::Config::ShouldRenderDescriptions();
		if (!shouldRenderDesc)
		{
			return;
		}

		static const char* prefix = "Desc:     ";
		static const char* indent = "          ";

		char buf[512];
		const char* text = desc.c_str();
		size_t len = desc.size();
		size_t pos = 0;
		bool first = true;

		while (pos < len)
		{
			size_t end = pos + maxLineLen;
			if (end >= len)
			{
				end = len;
			}
			else
			{
				size_t space = end;
				while (space < len && text[space] != ' ')
					++space;
				if (space - pos <= (size_t)(maxLineLen * 2))
					end = space;
			}

			std::string chunk(text + pos, text + end);
			snprintf(buf, sizeof(buf) - 1, "%s%s", first ? prefix : indent, chunk.c_str());
			buf[sizeof(buf) - 1] = '\0';
			imgui->Text(buf);

			pos = end;
			if (pos < len && text[pos] == ' ')
				++pos;
			first = false;
		}
	}

	void WailaUIManager::RenderWidget(IModLoaderImGui* imgui)
	{
		try {

			if (!imgui)
			{
				return;
			}

			// Take a snapshot of pending info for all detector types
			Waila::CrafterInfo  renderInfo;
			Waila::StorageInfo  renderStorageInfo;
			Waila::PowerInfo    renderPowerInfo;
			Waila::CoolerActiveInfo  renderCoolerActiveInfo;
			Waila::CoolerPassiveInfo renderCoolerPassiveInfo;
			Waila::CargoSenderInfo   renderCargoSenderInfo;
			Waila::CargoReceiverInfo renderCargoReceiverInfo;
			{
				std::lock_guard<std::mutex> lock(m_infoMutex);
				renderInfo               = m_pendingInfo;
				renderStorageInfo        = m_pendingStorageInfo;
				renderPowerInfo          = m_pendingPowerInfo;
				renderCoolerActiveInfo   = m_pendingCoolerActiveInfo;
				renderCoolerPassiveInfo  = m_pendingCoolerPassiveInfo;
				renderCargoSenderInfo    = m_pendingCargoSenderInfo;
				renderCargoReceiverInfo  = m_pendingCargoReceiverInfo;
			}

			// Don't render if disabled (world ended) or no valid data
			if (!m_self || (!renderInfo.IsValid() && !renderStorageInfo.IsValid() && !renderPowerInfo.IsValid() && !renderCoolerActiveInfo.IsValid() && !renderCoolerPassiveInfo.IsValid() && !renderCargoSenderInfo.IsValid() && !renderCargoReceiverInfo.IsValid()))
			{
				return;
			}

			imgui->TextColored(0.2f, 1.0f, 0.2f, 1.0f, "What Am I Looking At?");
			imgui->Separator();

			char buf[512];

			if (renderInfo.IsValid())
				RenderCrafterInfo(imgui, renderInfo);
			else if (renderStorageInfo.IsValid())
				RenderStorageInfo(imgui, renderStorageInfo);
			else if (renderPowerInfo.IsValid())
				RenderPowerInfo(imgui, renderPowerInfo);
			else if (renderCoolerActiveInfo.IsValid())
				RenderCoolerActiveInfo(imgui, renderCoolerActiveInfo);
			else if (renderCoolerPassiveInfo.IsValid())
				RenderCoolerPassiveInfo(imgui, renderCoolerPassiveInfo);
			else if (renderCargoSenderInfo.IsValid())
				RenderCargoSenderInfo(imgui, renderCargoSenderInfo);
			else if (renderCargoReceiverInfo.IsValid())
				RenderCargoReceiverInfo(imgui, renderCargoReceiverInfo);
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

	void WailaUIManager::RenderCrafterInfo(IModLoaderImGui* imgui, const Waila::CrafterInfo& info)
	{
		char buf[512];

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Name:     %s", info.crafterClass.empty() ? "N/A" : info.crafterClass.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (info.buildingDesc.empty())
			imgui->Text("Desc:     N/A");
		else
			RenderWrappedDesc(imgui, info.buildingDesc);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Speed:    %.2fx", info.craftingSpeed);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		imgui->Separator();

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Crafting: %s%s",
			info.currentRecipeDisplayName.empty() ? info.currentRecipe.c_str() : info.currentRecipeDisplayName.c_str(),
			info.bOutputFull ? " (Output Full)" : "");
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (info.recipeBuildTime > 0.f)
		{
			float ipm = (60.f / info.recipeBuildTime) * info.recipeOutputCount;
			char ipmBuf[32];
			if (ipm == floorf(ipm))
				snprintf(ipmBuf, sizeof(ipmBuf), "%d/pm", static_cast<int>(ipm));
			else
				snprintf(ipmBuf, sizeof(ipmBuf), "%.1f/pm", ipm);

			float bt = info.recipeBuildTime;
			char timeBuf[16];
			if (bt == floorf(bt))
				snprintf(timeBuf, sizeof(timeBuf), "%ds", static_cast<int>(bt));
			else
				snprintf(timeBuf, sizeof(timeBuf), "%.1fs", bt);

			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf) - 1, "Interval: %s (%s)", timeBuf, ipmBuf);
			buf[sizeof(buf) - 1] = '\0';
			imgui->Text(buf);
		}

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Progress: %.1f%%", info.craftingProgress * 100.f);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);
	}

	void WailaUIManager::RenderStorageInfo(IModLoaderImGui* imgui, const Waila::StorageInfo& info)
	{
		char buf[512];

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Name:     %s", info.buildingName.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (!info.buildingDesc.empty())
			RenderWrappedDesc(imgui, info.buildingDesc);

		imgui->Separator();

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Capacity: %d slots", info.maxCapacity);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		for (const auto& item : info.storedItems)
		{
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf) - 1, "Storing %dx %s", item.count, item.displayName.c_str());
			buf[sizeof(buf) - 1] = '\0';
			imgui->Text(buf);
		}
	}

	void WailaUIManager::RenderPowerInfo(IModLoaderImGui* imgui, const Waila::PowerInfo& info)
	{
		char buf[512];

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Name:     %s", info.buildingName.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (!info.buildingDesc.empty())
			RenderWrappedDesc(imgui, info.buildingDesc);

		imgui->Separator();

		const char* statusStr = "Not Connected";
		if (info.gridConnectionStatus == 1)
			statusStr = "Connected";
		else if (info.gridConnectionStatus == 2)
			statusStr = "Connected (Off)";

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Grid:     %s", statusStr);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Output:   %.1f W", info.buildingPower);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		imgui->Separator();

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Grid Gen: %.1f W", info.gridAddPower);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Grid Use: %.1f W", info.gridRemovePower);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Grid Cap: %.1f W", info.gridTotalPower);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);
	}

	static void RenderCoolerShared(IModLoaderImGui* imgui, const std::string& buildingName, const std::string& buildingDesc,
		uint8_t state, int32_t connectedSockets, int32_t totalSockets)
	{
		char buf[512];

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Name:     %s", buildingName.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (!buildingDesc.empty())
			RenderWrappedDesc(imgui, buildingDesc);

		imgui->Separator();

		const char* stateStr = "Unknown";
		switch (state)
		{
		case 0: stateStr = "Idle";         break;
		case 1: stateStr = "Working";      break;
		case 2: stateStr = "No Fuel";      break;
		case 3: stateStr = "Too Hot/Cold"; break;
		}

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "State:    %s", stateStr);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Sockets:  %d / %d", connectedSockets, totalSockets);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);
	}

	void WailaUIManager::RenderCoolerActiveInfo(IModLoaderImGui* imgui, const Waila::CoolerActiveInfo& info)
	{
		RenderCoolerShared(imgui, info.buildingName, info.buildingDesc, info.state, info.connectedSockets, info.totalSockets);
	}

	void WailaUIManager::RenderCoolerPassiveInfo(IModLoaderImGui* imgui, const Waila::CoolerPassiveInfo& info)
	{
		RenderCoolerShared(imgui, info.buildingName, info.buildingDesc, info.state, info.connectedSockets, info.totalSockets);
	}

	void WailaUIManager::RenderCargoSenderInfo(IModLoaderImGui* imgui, const Waila::CargoSenderInfo& info)
	{
		char buf[512];

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Name:     %s", info.buildingName.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (!info.buildingDesc.empty())
			RenderWrappedDesc(imgui, info.buildingDesc);

		imgui->Separator();

		imgui->Text(info.canSend ? "Status:   Loading Drone / Idle" : "Status:   Sending...");

		memset(buf, 0, sizeof(buf));
		float progress = info.sendProgress;

		if (progress < 0.0f)
			progress = 0.0f;

		progress *= 100.0f;
		snprintf(buf, sizeof(buf), "Progress: %.0f%%", progress);
		imgui->Text(buf);

		if (!info.sendingItemName.empty())
		{
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf) - 1, "Sending:  %s", info.sendingItemName.c_str());
			buf[sizeof(buf) - 1] = '\0';
			imgui->Text(buf);
		}
	}

	void WailaUIManager::RenderCargoReceiverInfo(IModLoaderImGui* imgui, const Waila::CargoReceiverInfo& info)
	{
		char buf[512];

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Name:     %s", info.buildingName.c_str());
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		if (!info.buildingDesc.empty())
			RenderWrappedDesc(imgui, info.buildingDesc);

		imgui->Separator();

		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf) - 1, "Capacity: %d slots", info.maxCapacity);
		buf[sizeof(buf) - 1] = '\0';
		imgui->Text(buf);

		for (const auto& item : info.storedItems)
		{
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf) - 1, "Storing %dx %s", item.count, item.displayName.c_str());
			buf[sizeof(buf) - 1] = '\0';
			imgui->Text(buf);
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
