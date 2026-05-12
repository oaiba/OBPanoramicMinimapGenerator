# OB Panoramic Minimap Generator

Editor-first Unreal Engine plugin for capturing large top-down minimap textures, authoring minimap overlay data, and exporting a runtime-ready `Texture2D + DataAsset` pipeline for game UI.

The plugin is designed for Game Designers who need to:

- Capture minimap images from a selected world region.
- Preview and validate the generated image.
- Add vector overlay data such as markers, zones, paths, and freehand/polyline strokes.
- Export a `UMinimapDefinitionDataAsset` that game/runtime code can read directly.

Current project target: Unreal Engine 5.7.

## Modules

- `PanoramicMinimapGeneratorEditor`
  - Editor window, capture workflow, image saving, texture import, overlay authoring, and DataAsset export.
- `PanoramicMinimapGeneratorRuntime`
  - Runtime data types, `UMinimapDefinitionDataAsset`, overlay structs, and Blueprint helper functions.

## Features

### Capture

- Capture bounds from selected actor(s).
- Manual capture bounds input.
- Single capture for standard resolutions.
- Tiled capture for large outputs.
- Resolution presets:
  - `512` Mobile
  - `2048` Standard
  - `4096` High
  - `8192` Ultra
- Orthographic or perspective capture.
- Camera height, rotation, and FOV controls.
- Transparent or solid-color background.
- Dynamic shadow toggle.
- Optional quality overrides.
- Show-only and hidden actor lists.
- Hide actors by class or tag.
- PNG export.
- Optional import as `Texture2D`.

### Runtime DataAsset Export

The plugin can export a `UMinimapDefinitionDataAsset` containing:

- Base minimap texture.
- World bounds used for the capture.
- Output pixel size.
- Map rotation metadata.
- Vector overlay layers.

Generated asset naming:

- Texture: `T_<FileName>`
- DataAsset: `DA_<FileName>`

If the same package/name already exists, the plugin updates the existing asset.

### Overlay Authoring

The Overlay Editor supports first-pass vector authoring from selected actors:

- Marker
- Zone
- Path
- Freehand/polyline stroke

Each overlay element supports:

- Label
- Category
- Color
- Opacity from color alpha
- Line width
- Runtime visibility flag in data
- Filter tags in runtime struct

Current implementation note: overlay authoring is selection-based. It stores real runtime vector data, but it is not yet a full interactive pan/zoom drawing canvas inside the minimap preview.

### Runtime Blueprint Helpers

`UMinimapBlueprintLibrary` provides:

- `WorldLocationToMapUV`
- `WorldLocationToMapPixel`
- `MapUVToWorldLocation`
- `GetOverlayElementsByCategory`
- `GetOverlayElementsByTag`

These helpers are available from runtime/game code and do not require the editor module.

## Quick Start

1. Enable the plugin in the project.
2. Restart the editor if prompted.
3. Open the tool from:

   `Tools > Panoramic Tools > Panoramic Minimap Generator`

4. Select one or more actors that cover the intended minimap area.
5. Click `Get Bounds from Selected Actor`.
6. Choose output resolution and output paths.
7. Keep `Export Runtime Minimap DataAsset` enabled if the minimap will be used by game UI.
8. Optionally add overlay elements from selected actors in `Overlay Editor`.
9. Click `Start Capture Process`.
10. Save the generated texture and DataAsset in the Content Browser if needed.

## Detailed Usage

### 1. Capture Region

Use this section to define the world-space bounds of the minimap.

Options:

- Enter min/max bounds manually.
- Select actor(s) in the level and click `Get Bounds from Selected Actor`.

The tool validates that min X/Y are lower than max X/Y before capture starts.

### 2. Tiling Settings

Enable tiled capture for high-resolution outputs or large world regions.

Fields:

- `Use Tiled Capture`: captures the image in smaller tiles and stitches the result.
- `Tile Resolution`: render target size for each tile.
- `Tile Overlap`: overlap area used to reduce seams between tiles.

Validation rule:

- `Tile Overlap` must be lower than `Tile Resolution`.

Recommended defaults:

- `Tile Resolution`: `2048`
- `Tile Overlap`: `64` to `256` for most captures
- Higher overlap can help hide seams but increases capture cost.

### 3. Camera Settings

Controls the capture camera.

Fields:

- `Camera Height`: world Z height for capture.
- `Camera Rotation`: capture orientation.
- `Use Orthographic Projection`: recommended for minimaps.
- `Camera FOV`: used when perspective mode is enabled.
- `Image Rotation`: convenience control for orthographic yaw rotation.

For most minimaps, use orthographic projection.

### 4. Quality Settings

Fields:

- `Capture Dynamic Shadows`
- `Override Editor Scalability Settings`
- Capture source and post-process overrides when quality override is enabled.

Use quality override when the generated minimap should be independent from the current editor viewport scalability settings.

### 5. Actor Filtering

Filtering controls which actors appear in the capture.

Options:

- `Show Only Actors`: only render selected actors.
- `Hidden Actors`: exclude selected actors.
- `Hide Actors of Class`: exclude all actors of a class.
- `Hide Actors with Tag`: exclude actors with a tag.

When filters are used, the capture component switches to a show-only capture list built by the tool.

### 6. Overlay Editor

The Overlay Editor creates runtime vector data from selected actors.

Common fields:

- `Label`: display or runtime label.
- `Category`: logical grouping, for example `POI`, `Quest`, `Danger`, `Extraction`.
- `Line Width`: stroke width for paths, zones, and freehand data.
- `Color`: style color and alpha.

Overlay actions:

- `Add Marker`
  - Adds marker point(s) at selected actor location(s).
- `Add Zone`
  - Creates a rectangular polygon from selected actor bounds.
- `Add Path`
  - Creates a path from selected actor locations.
  - Requires at least two selected actors.
- `Add Freehand`
  - Creates a freehand/polyline stroke from selected actor locations.
  - Requires at least two selected actors.
- `Remove Selected`
  - Removes selected overlay entries from the list.
- `Clear Overlay`
  - Clears all overlay data in the current editor session.

The overlay data is exported into `UMinimapDefinitionDataAsset` when capture/export runs.

### 7. Output Settings

Fields:

- `Output Width` / `Output Height`
- `Output Path`
- `File Name`
- `Auto-Generate Filename with Timestamp`
- `Import as Texture Asset`
- `Asset Path`
- `Export Runtime Minimap DataAsset`
- `DataAsset Path`
- `Background Mode`
- `Background Color`

Path notes:

- Disk PNG output uses `Output Path`.
- Texture and DataAsset package paths should be under `/Game`, for example:

  `/Game/Minimaps/`

When DataAsset export is enabled, the tool also imports/updates the base minimap texture because the DataAsset stores a texture reference.

## Runtime Integration

Use `UMinimapDefinitionDataAsset` as the handoff asset between Designer-authored minimap data and runtime UI.

Important fields:

- `BaseMapTexture`
  - Texture displayed by the minimap UI.
- `WorldBounds`
  - World-space bounds used for world-to-map conversion.
- `OutputSize`
  - Pixel dimensions of the generated minimap.
- `MapRotationDegrees`
  - Rotation metadata used by helper conversion functions.
- `OverlayLayers`
  - Vector overlay data.

### Blueprint Usage

Typical runtime UI flow:

1. Assign a `UMinimapDefinitionDataAsset` to the minimap widget.
2. Display `BaseMapTexture`.
3. Convert the player location:

   `WorldLocationToMapUV(MinimapDefinition, PlayerWorldLocation)`

4. Place the player icon using the returned UV.
5. Query overlays:

   `GetOverlayElementsByCategory(MinimapDefinition, "POI")`

6. Convert each overlay point with `WorldLocationToMapUV` or `WorldLocationToMapPixel`.

### C++ Example

```cpp
const FVector2D PlayerUV =
	UMinimapBlueprintLibrary::WorldLocationToMapUV(MinimapDefinition, PlayerLocation);

TArray<FMinimapOverlayElement> POIElements;
UMinimapBlueprintLibrary::GetOverlayElementsByCategory(
	MinimapDefinition,
	TEXT("POI"),
	POIElements);
```

## Troubleshooting

### Capture region is invalid

Ensure min bounds are lower than max bounds on X and Y.

Recommended fix:

- Select actor(s) that cover the level area.
- Click `Get Bounds from Selected Actor`.

### Tiled capture does not start

Check tiling values:

- `Tile Resolution` must be greater than zero.
- `Tile Overlap` must be greater than or equal to zero.
- `Tile Overlap` must be lower than `Tile Resolution`.

### Texture or DataAsset is not created

Check package paths:

- Use `/Game/...` paths for asset import/export.
- Example: `/Game/Minimaps/`

Also verify that the PNG was saved successfully to the disk output path.

### Preview image does not load

The capture may still have succeeded even if preview loading fails.

Check:

- The output PNG exists.
- The output image is not corrupt.
- The editor log for PNG decode errors.

### Runtime marker appears in the wrong place

Check:

- `WorldBounds` in the DataAsset matches the intended capture area.
- Camera/image rotation settings.
- The same DataAsset generated from the capture is used by runtime UI.
- The UI uses `WorldLocationToMapUV` or `WorldLocationToMapPixel` instead of custom mapping.

## Developer Notes

Builds verified during implementation:

```bash
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" OBExtractionEditor Mac Development -Project="/Users/phambaoai/UEProject/OBExtraction/OBExtraction.uproject" -NoHotReloadFromIDE
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" OBExtraction Mac Development -Project="/Users/phambaoai/UEProject/OBExtraction/OBExtraction.uproject" -NoHotReloadFromIDE
```

Known external warning:

- The project currently reports `StructUtils` deprecation warnings from `ExtractionCoreGame`. This is outside this plugin.

## Known Limitations

- Overlay authoring is currently based on selected actors, not direct drawing on the preview canvas.
- There is no runtime minimap widget included yet.
- Icon picker UI is not implemented yet, though runtime overlay style supports icon references.
- Overlay layer management is currently minimal.
- No JSON import/export for overlay data yet.

## Suggested Next Steps

- Add a true interactive overlay canvas with pan, zoom, click-to-place marker, polygon drawing, and path editing.
- Add layer management UI.
- Add icon picker and marker presets.
- Add a sample runtime UMG minimap widget.
- Add DataAsset edit/reopen workflow so Designers can refine overlays without recapturing the texture.
- Add automated tests for world-to-map conversion and overlay serialization.
