#include "ui_manager.h"
#include "plugin_helpers.h"
#include "Engine_classes.hpp"
#include "Engine_structs.hpp"
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

		LOG_DEBUG("WailaUIManager::Initialize: reading MaxDistance from config");
		m_maxDistance = self->config->ReadFloat(self, "Raycasting", "MaxDistance", 256.f);
		LOG_DEBUG("WailaUIManager::Initialize: MaxDistance = %.1f", m_maxDistance);

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

			if (m_self->hooks->UI && m_widgetHandle)
			{
				m_self->hooks->UI->UnregisterWidget(m_widgetHandle);
				m_widgetHandle = nullptr;
				m_widgetVisible = false;
			}
		}

		m_self = nullptr;
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
		}

		// Store the extracted info and debug ray under lock for the render thread
		{
			std::lock_guard<std::mutex> lock(m_infoMutex);
			m_pendingInfo        = info;
			m_pendingStorageInfo = storageInfo;
			m_debugRay.start     = hit.rayStart;
			m_debugRay.end       = hit.rayEnd;
			m_debugRay.hit       = bHit;
			m_debugRay.valid     = true;
		}

		bool shouldBeVisible = info.IsValid() || storageInfo.IsValid();

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

			// Take a snapshot of pending info for both detector types
			Waila::CrafterInfo  renderInfo;
			Waila::StorageInfo  renderStorageInfo;
			{
				std::lock_guard<std::mutex> lock(m_infoMutex);
				renderInfo        = m_pendingInfo;
				renderStorageInfo = m_pendingStorageInfo;
			}

			// Don't render if disabled (world ended) or no valid data
			if (!m_self || (!renderInfo.IsValid() && !renderStorageInfo.IsValid()))
			{
				return;
			}

			imgui->TextColored(0.2f, 1.0f, 0.2f, 1.0f, "What Am I Looking At");
			imgui->Separator();

			char buf[512];

			if (renderInfo.IsValid())
			{
				memset(buf, 0, sizeof(buf));
				snprintf(buf, sizeof(buf) - 1, "Name:     %s", renderInfo.crafterClass.empty() ? "N/A" : renderInfo.crafterClass.c_str());
				buf[sizeof(buf) - 1] = '\0';
				imgui->Text(buf);

				if (renderInfo.buildingDesc.empty())
					imgui->Text("Desc:     N/A");
				else
					RenderWrappedDesc(imgui, renderInfo.buildingDesc);

				memset(buf, 0, sizeof(buf));
				snprintf(buf, sizeof(buf) - 1, "Speed:    %.2fx", renderInfo.craftingSpeed);
				buf[sizeof(buf) - 1] = '\0';
				imgui->Text(buf);

				imgui->Separator();

				memset(buf, 0, sizeof(buf));
				snprintf(buf, sizeof(buf) - 1, "Crafting: %s", renderInfo.currentRecipeDisplayName.empty() ? renderInfo.currentRecipe.c_str() : renderInfo.currentRecipeDisplayName.c_str());
				buf[sizeof(buf) - 1] = '\0';
				imgui->Text(buf);

				if (renderInfo.recipeBuildTime > 0.f)
				{
					float ipm = (60.f / renderInfo.recipeBuildTime) * renderInfo.recipeOutputCount;
					char ipmBuf[32];
					if (ipm == floorf(ipm))
						snprintf(ipmBuf, sizeof(ipmBuf), "%d/pm", static_cast<int>(ipm));
					else
						snprintf(ipmBuf, sizeof(ipmBuf), "%.1f/pm", ipm);

					float bt = renderInfo.recipeBuildTime;
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
				snprintf(buf, sizeof(buf) - 1, "Progress: %.1f%%", renderInfo.craftingProgress * 100.f);
				buf[sizeof(buf) - 1] = '\0';
				imgui->Text(buf);
			}
			else if (renderStorageInfo.IsValid())
			{
				memset(buf, 0, sizeof(buf));
				snprintf(buf, sizeof(buf) - 1, "Name:     %s", renderStorageInfo.buildingName.c_str());
				buf[sizeof(buf) - 1] = '\0';
				imgui->Text(buf);

				if (!renderStorageInfo.buildingDesc.empty())
					RenderWrappedDesc(imgui, renderStorageInfo.buildingDesc);

				imgui->Separator();

				memset(buf, 0, sizeof(buf));
				snprintf(buf, sizeof(buf) - 1, "Capacity: %d slots", renderStorageInfo.maxCapacity);
				buf[sizeof(buf) - 1] = '\0';
				imgui->Text(buf);
			}
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
