#include "MinimapBlueprintLibrary.h"

namespace
{
bool IsDefinitionUsable(const UMinimapDefinitionDataAsset* MinimapDefinition)
{
	return MinimapDefinition && MinimapDefinition->WorldBounds.IsValid && MinimapDefinition->WorldBounds.GetSize().X != 0.0f && MinimapDefinition->WorldBounds.GetSize().Y != 0.0f;
}

FVector2D ApplyRotation(FVector2D UV, float RotationDegrees)
{
	if (FMath::IsNearlyZero(RotationDegrees))
	{
		return UV;
	}

	const FVector2D Center(0.5f, 0.5f);
	const float Radians = FMath::DegreesToRadians(RotationDegrees);
	const float CosAngle = FMath::Cos(Radians);
	const float SinAngle = FMath::Sin(Radians);
	const FVector2D Offset = UV - Center;

	return Center + FVector2D(
		Offset.X * CosAngle - Offset.Y * SinAngle,
		Offset.X * SinAngle + Offset.Y * CosAngle);
}
}

FVector2D UMinimapBlueprintLibrary::WorldLocationToMapUV(const UMinimapDefinitionDataAsset* MinimapDefinition, const FVector WorldLocation, const bool bClampToBounds)
{
	if (!IsDefinitionUsable(MinimapDefinition))
	{
		return FVector2D::ZeroVector;
	}

	const FVector BoundsMin = MinimapDefinition->WorldBounds.Min;
	const FVector BoundsSize = MinimapDefinition->WorldBounds.GetSize();

	FVector2D UV(
		(WorldLocation.X - BoundsMin.X) / BoundsSize.X,
		1.0f - ((WorldLocation.Y - BoundsMin.Y) / BoundsSize.Y));

	UV = ApplyRotation(UV, MinimapDefinition->MapRotationDegrees);

	if (bClampToBounds || MinimapDefinition->bClampQueriesToBounds)
	{
		UV.X = FMath::Clamp(UV.X, 0.0f, 1.0f);
		UV.Y = FMath::Clamp(UV.Y, 0.0f, 1.0f);
	}

	return UV;
}

FVector2D UMinimapBlueprintLibrary::WorldLocationToMapPixel(const UMinimapDefinitionDataAsset* MinimapDefinition, const FVector WorldLocation, const bool bClampToBounds)
{
	if (!MinimapDefinition)
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D UV = WorldLocationToMapUV(MinimapDefinition, WorldLocation, bClampToBounds);
	return FVector2D(UV.X * MinimapDefinition->OutputSize.X, UV.Y * MinimapDefinition->OutputSize.Y);
}

FVector UMinimapBlueprintLibrary::MapUVToWorldLocation(const UMinimapDefinitionDataAsset* MinimapDefinition, FVector2D MapUV, const float WorldZ, const bool bClampToBounds)
{
	if (!IsDefinitionUsable(MinimapDefinition))
	{
		return FVector::ZeroVector;
	}

	if (bClampToBounds || MinimapDefinition->bClampQueriesToBounds)
	{
		MapUV.X = FMath::Clamp(MapUV.X, 0.0f, 1.0f);
		MapUV.Y = FMath::Clamp(MapUV.Y, 0.0f, 1.0f);
	}

	MapUV = ApplyRotation(MapUV, -MinimapDefinition->MapRotationDegrees);

	const FVector BoundsMin = MinimapDefinition->WorldBounds.Min;
	const FVector BoundsSize = MinimapDefinition->WorldBounds.GetSize();
	return FVector(
		BoundsMin.X + MapUV.X * BoundsSize.X,
		BoundsMin.Y + (1.0f - MapUV.Y) * BoundsSize.Y,
		WorldZ);
}

bool UMinimapBlueprintLibrary::MapUVToTileCoord(const UMinimapTileSetDataAsset* TileSet, FVector2D MapUV,
                                                const int32 LOD, FMinimapTileCoord& OutCoord, FVector2D& OutTileUV,
                                                const bool bClampToBounds)
{
	OutCoord = FMinimapTileCoord();
	OutCoord.LOD = LOD;
	OutTileUV = FVector2D::ZeroVector;

	if (!TileSet || !TileSet->IsValidTileSet())
	{
		return false;
	}

	const FMinimapTilePyramidLevel* Level = TileSet->GetPyramidLevel(LOD);
	if (!Level || Level->GridDimensions.X <= 0 || Level->GridDimensions.Y <= 0)
	{
		return false;
	}

	if (bClampToBounds || TileSet->bClampQueriesToBounds)
	{
		MapUV.X = FMath::Clamp(MapUV.X, 0.0f, 1.0f);
		MapUV.Y = FMath::Clamp(MapUV.Y, 0.0f, 1.0f);
	}
	else if (MapUV.X < 0.0f || MapUV.X > 1.0f || MapUV.Y < 0.0f || MapUV.Y > 1.0f)
	{
		return false;
	}

	const FVector2D ScaledUV(
		MapUV.X * static_cast<float>(Level->GridDimensions.X),
		MapUV.Y * static_cast<float>(Level->GridDimensions.Y));

	OutCoord.X = FMath::Clamp(FMath::FloorToInt(ScaledUV.X), 0, Level->GridDimensions.X - 1);
	OutCoord.Y = FMath::Clamp(FMath::FloorToInt(ScaledUV.Y), 0, Level->GridDimensions.Y - 1);
	OutTileUV = FVector2D(ScaledUV.X - OutCoord.X, ScaledUV.Y - OutCoord.Y);

	return Level->FindTile(OutCoord.X, OutCoord.Y) != nullptr;
}

bool UMinimapBlueprintLibrary::WorldLocationToTileCoord(const UMinimapDefinitionDataAsset* MinimapDefinition,
                                                        const FVector WorldLocation, const int32 LOD,
                                                        FMinimapTileCoord& OutCoord, FVector2D& OutTileUV,
                                                        const bool bClampToBounds)
{
	if (!MinimapDefinition || !MinimapDefinition->TileSet)
	{
		OutCoord = FMinimapTileCoord();
		OutTileUV = FVector2D::ZeroVector;
		return false;
	}

	const FVector2D MapUV = WorldLocationToMapUV(MinimapDefinition, WorldLocation, bClampToBounds);
	return MapUVToTileCoord(MinimapDefinition->TileSet, MapUV, LOD, OutCoord, OutTileUV, bClampToBounds);
}

void UMinimapBlueprintLibrary::GetTilesIntersectingUVRect(const UMinimapTileSetDataAsset* TileSet, FVector2D UVMin,
                                                          FVector2D UVMax, const int32 LOD,
                                                          TArray<FMinimapTileRef>& OutTiles,
                                                          const bool bClampToBounds)
{
	OutTiles.Reset();

	if (!TileSet || !TileSet->IsValidTileSet())
	{
		return;
	}

	const FMinimapTilePyramidLevel* Level = TileSet->GetPyramidLevel(LOD);
	if (!Level || Level->GridDimensions.X <= 0 || Level->GridDimensions.Y <= 0)
	{
		return;
	}

	if (UVMin.X > UVMax.X)
	{
		Swap(UVMin.X, UVMax.X);
	}
	if (UVMin.Y > UVMax.Y)
	{
		Swap(UVMin.Y, UVMax.Y);
	}

	if (bClampToBounds || TileSet->bClampQueriesToBounds)
	{
		UVMin.X = FMath::Clamp(UVMin.X, 0.0f, 1.0f);
		UVMin.Y = FMath::Clamp(UVMin.Y, 0.0f, 1.0f);
		UVMax.X = FMath::Clamp(UVMax.X, 0.0f, 1.0f);
		UVMax.Y = FMath::Clamp(UVMax.Y, 0.0f, 1.0f);
	}

	const int32 MinX = FMath::Clamp(FMath::FloorToInt(UVMin.X * Level->GridDimensions.X), 0, Level->GridDimensions.X - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt(UVMin.Y * Level->GridDimensions.Y), 0, Level->GridDimensions.Y - 1);
	const int32 MaxX = FMath::Clamp(FMath::FloorToInt(UVMax.X * Level->GridDimensions.X), 0, Level->GridDimensions.X - 1);
	const int32 MaxY = FMath::Clamp(FMath::FloorToInt(UVMax.Y * Level->GridDimensions.Y), 0, Level->GridDimensions.Y - 1);

	for (const FMinimapTileRef& Tile : Level->Tiles)
	{
		if (Tile.Coord.X >= MinX && Tile.Coord.X <= MaxX && Tile.Coord.Y >= MinY && Tile.Coord.Y <= MaxY)
		{
			OutTiles.Add(Tile);
		}
	}
}

int32 UMinimapBlueprintLibrary::ChooseTileLODForWorldUnitsPerPixel(const UMinimapTileSetDataAsset* TileSet,
                                                                   const float RequestedWorldUnitsPerPixel)
{
	if (!TileSet || !TileSet->IsValidTileSet())
	{
		return INDEX_NONE;
	}

	int32 BestLOD = TileSet->GetMaxLOD();
	float BestError = TNumericLimits<float>::Max();
	for (const FMinimapTilePyramidLevel& Level : TileSet->PyramidLevels)
	{
		if (Level.TilePixelSize.X <= 0 || Level.WorldTileSize.X <= 0.0f)
		{
			continue;
		}

		const float LevelWorldUnitsPerPixel = Level.WorldTileSize.X / static_cast<float>(Level.TilePixelSize.X);
		const float Error = FMath::Abs(LevelWorldUnitsPerPixel - RequestedWorldUnitsPerPixel);
		if (Error < BestError)
		{
			BestError = Error;
			BestLOD = Level.LOD;
		}
	}

	return BestLOD;
}

void UMinimapBlueprintLibrary::GetOverlayElementsByCategory(const UMinimapDefinitionDataAsset* MinimapDefinition, const FName Category, TArray<FMinimapOverlayElement>& OutElements)
{
	OutElements.Reset();
	if (!MinimapDefinition)
	{
		return;
	}

	for (const FMinimapOverlayLayer& Layer : MinimapDefinition->OverlayLayers)
	{
		for (const FMinimapOverlayElement& Element : Layer.Elements)
		{
			if (Element.Category == Category)
			{
				OutElements.Add(Element);
			}
		}
	}
}

void UMinimapBlueprintLibrary::GetOverlayElementsByTag(const UMinimapDefinitionDataAsset* MinimapDefinition, const FName Tag, TArray<FMinimapOverlayElement>& OutElements)
{
	OutElements.Reset();
	if (!MinimapDefinition)
	{
		return;
	}

	for (const FMinimapOverlayLayer& Layer : MinimapDefinition->OverlayLayers)
	{
		for (const FMinimapOverlayElement& Element : Layer.Elements)
		{
			if (Element.FilterTags.Contains(Tag))
			{
				OutElements.Add(Element);
			}
		}
	}
}
