# StarRupture-Plugin-WAILA

**W**hat **A**m **I** **L**ooking **A**t? — a real-time HUD overlay plugin for the [StarRupture ModLoader](https://github.com/AlienXAXS) targeting the Chimera (UE5) game. Inspired by the classic Minecraft WAILA mod, it displays live information about factory buildings and machines directly in the game HUD as the player looks at them.

## Features

- Raycast-based detection — identifies the building the player's crosshair is aimed at
- Per-building info panels rendered via ImGui, including:
  - **Crafters** — current recipe, crafting progress, speed multiplier, missing items / output-full flags
  - **Storage** — storage contents and capacity
  - **Power** — power consumption/production state
  - **Active Coolers** — cooling load and status
  - **Passive Coolers** — passive heat-sink info
  - **Cargo Senders** — send interval, send progress, current item being shipped
  - **Cargo Receivers** — incoming cargo state
- **Recipe Clipboard** — copy a recipe from any crafter (`C` by default) and paste it onto another of the same type (`V` by default)
- **Lock Mode** — lock a recipe (`L` by default) to automatically paste it onto every newly-looked-at compatible crafter; a persistent HUD banner shows when lock mode is active
- **Toast notifications** — brief on-screen confirmation shown after copy, paste, and lock actions (configurable)
- Configurable max detection distance, separate action raycast distance, and optional building descriptions
- All hotkeys are remappable via `Waila.ini`
- Thread-safe: game tick and ImGui render thread are fully decoupled via mutex

## Requirements

- Visual Studio 2022 (MSVC v143, C++20)
- [StarRupture-Game-SDK](https://github.com/AlienXAXS/StarRupture-Game-SDK)
- [StarRupture-Plugin-SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK)
- StarRupture with ModLoader installed

## Building

Open `StarRupture-Plugin-Waila.sln` in Visual Studio 2022 and build, or use MSBuild from the repository root:

```bat
msbuild StarRupture-Plugin-Waila.sln /p:Configuration="Client Debug" /p:Platform=x64
```

Output is placed at `build/<Configuration>/Plugins/Waila.dll`.

### Configurations

| Configuration | Description |
|---|---|
| `Client Debug` | Client mod build with debug symbols |
| `Client Release` | Optimised client mod build |
| `Server Debug` | Server-side build with debug symbols |
| `Server Release` | Optimised server-side build |
| `Local SDK Client Debug/Release` | Client build using a local SDK checkout |
| `Local SDK Server Debug/Release` | Server build using a local SDK checkout |

SDK checkout paths can be adjusted in [`Shared.props`](Shared.props) to match your local directory layout.

## Installation

1. Build the DLL (see above).
2. Copy `Waila.dll` into the game's `Plugins/` directory alongside the other StarRupture mod DLLs.
3. Launch the game — the ModLoader will load the plugin automatically.

On first run, `Waila.ini` is generated in `<game_dir>/Plugins/config/` with default values.

## Configuration

`Waila.ini` is auto-created on first run. Available options:

| Section | Key | Default | Description |
|---|---|---|---|
| `General` | `Enabled` | `true` | Enable or disable the plugin entirely |
| `WAILA` | `Max Distance` | `650.0` | Maximum raycast range for HUD display in Unreal units (0 – 2500) |
| `WAILA` | `Action Distance` | `3000.0` | Maximum raycast range for copy/paste/lock actions in Unreal units (650 – 10000) |
| `WAILA` | `Show Action Toast` | `true` | Show a brief toast notification after copy, paste, or lock actions |
| `WAILA` | `Render Building Descriptions` | `true` | Show/hide the building description line in the HUD panel |
| `Hotkeys` | `Copy Recipe Key` | `C` | Key to copy the recipe from the looked-at crafter |
| `Hotkeys` | `Paste Recipe Key` | `V` | Key to paste the clipboard recipe onto the looked-at crafter |
| `Hotkeys` | `Lock Recipe Key` | `L` | Key to toggle lock mode (auto-paste onto newly-looked-at crafters) |

## Architecture

```
Engine tick → WailaUIManager::Tick()
  → WailaRaycastSystem::PerformRaycast()   [Core/raycaster.h]
  → *Detector::Is*(actor)                   [detector classes]
  → *Detector::Get*Info(actor, info)
  → mutex-locked update of pending info structs
  → ImGui render callback reads info and draws HUD panel
```

### Key Files

| File | Role |
|---|---|
| [`plugin.cpp`](Waila/plugin.cpp) | `GetPluginInfo` / `PluginInit` / `PluginShutdown` C exports |
| [`UI/ui_manager.h`](Waila/UI/ui_manager.h) | Singleton owning tick, ImGui widget, and thread-safe info structs |
| [`Core/raycaster.h`](Waila/Core/raycaster.h) | Line trace from player eye; returns `RaycastHit` |
| [`Core/recipe_clipboard.h`](Waila/Core/recipe_clipboard.h) | `RecipeClipboard` struct holding copied recipe data for paste/lock |
| [`crafter_detector.h`](Waila/crafter_detector.h) | Detects and reads `ACrCrafter` actors |
| [`storage_detector.h`](Waila/storage_detector.h) | Detects and reads storage buildings |
| [`power_detector.h`](Waila/power_detector.h) | Detects and reads power buildings |
| [`cooler_active_detector.h`](Waila/cooler_active_detector.h) | Detects active cooler machines |
| [`cooler_passive_detector.h`](Waila/cooler_passive_detector.h) | Detects passive cooler machines |
| [`cargo_sender_detector.h`](Waila/cargo_sender_detector.h) | Detects cargo sender machines |
| [`cargo_receiver_detector.h`](Waila/cargo_receiver_detector.h) | Detects cargo receiver machines |
| [`waila_functions.h`](Waila/waila_functions.h) | Pattern-scanned function pointers for internal game APIs |
| [`plugin_config.h`](Waila/plugin_config.h) | Schema-based `Waila.ini` config with typed accessors |

### Adding a New Detector

1. Create `<type>_detector.h` / `<type>_detector.cpp` implementing:
   ```cpp
   static bool Is<Type>(SDK::AActor* actor);
   static bool Get<Type>Info(SDK::AActor* actor, <Type>Info& out);
   ```
2. Add a `m_pending<Type>Info` field (guarded by `m_infoMutex`) in `WailaUIManager`.
3. Call the detector in `WailaUIManager::Tick()`.
4. Add a `Render<Type>Info()` branch in the ImGui render callback.

## License

This project is provided as-is for modding purposes. See the StarRupture ModLoader documentation for distribution terms.
