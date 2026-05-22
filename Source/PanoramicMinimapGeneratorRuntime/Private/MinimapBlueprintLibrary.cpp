#include "MinimapBlueprintLibrary.h"

namespace
{
bool IsDefinitionUsable(const UMinimapDefinitionDataAsset* MinimapDefinition)
{
	FVector ProjectionWorldCenter;
	FVector2D ProjectionWorldSize;
	return MinimapDefinition && MinimapDefinition->WorldBounds.IsValid
		&& MinimapDefinition->ResolveProjectionFrame(ProjectionWorldCenter, ProjectionWorldSize);
}

FVector ClampWorldLocationToBoundsXY(const FVector& WorldLocation, const FBox& Bounds)
{
	if (!Bounds.IsValid)
	{
		return WorldLocation;
	}

	return FVector(
		FMath::Clamp(WorldLocation.X, Bounds.Min.X, Bounds.Max.X),
		FMath::Clamp(WorldLocation.Y, Bounds.Min.Y, Bounds.Max.Y),
		WorldLocation.Z);
}

FVector2D ProjectWorldToPanoramicMapUV(const UMinimapDefinitionDataAsset* MinimapDefinition,
                                       const FVector& WorldLocation)
{
	FVector ProjectionWorldCenter;
	FVector2D ProjectionWorldSize;
	if (!MinimapDefinition->ResolveProjectionFrame(ProjectionWorldCenter, ProjectionWorldSize))
	{
		return FVector2D::ZeroVector;
	}

	const float Radians = FMath::DegreesToRadians(MinimapDefinition->MapRotationDegrees);
	const float CosAngle = FMath::Cos(Radians);
	const float SinAngle = FMath::Sin(Radians);
	const FVector WorldDelta = WorldLocation - ProjectionWorldCenter;
	const float LocalX = WorldDelta.X * CosAngle + WorldDelta.Y * SinAngle;
	const float LocalY = -WorldDelta.X * SinAngle + WorldDelta.Y * CosAngle;

	return FVector2D(
		0.5f + LocalX / ProjectionWorldSize.X,
		0.5f + LocalY / ProjectionWorldSize.Y);
}

FVector ProjectPanoramicMapUVToWorld(const UMinimapDefinitionDataAsset* MinimapDefinition,
                                     const FVector2D& MapUV, const float WorldZ)
{
	FVector ProjectionWorldCenter;
	FVector2D ProjectionWorldSize;
	if (!MinimapDefinition->ResolveProjectionFrame(ProjectionWorldCenter, ProjectionWorldSize))
	{
		return FVector::ZeroVector;
	}

	const float Radians = FMath::DegreesToRadians(MinimapDefinition->MapRotationDegrees);
	const float CosAngle = FMath::Cos(Radians);
	const float SinAngle = FMath::Sin(Radians);
	const float LocalX = (MapUV.X - 0.5f) * ProjectionWorldSize.X;
	const float LocalY = (MapUV.Y - 0.5f) * ProjectionWorldSize.Y;
	return FVector(
		ProjectionWorldCenter.X + LocalX * CosAngle - LocalY * SinAngle,
		ProjectionWorldCenter.Y + LocalX * SinAngle + LocalY * CosAngle,
		WorldZ);
}

void NormalizeUVRect(const FVector2D& InA, const FVector2D& InB, FVector2D& OutMin, FVector2D& OutMax)
{
	OutMin = FVector2D(FMath::Min(InA.X, InB.X), FMath::Min(InA.Y, InB.Y));
	OutMax = FVector2D(FMath::Max(InA.X, InB.X), FMath::Max(InA.Y, InB.Y));
}

bool UVRectContainsPoint(const FVector2D& RectMin, const FVector2D& RectMax, const FVector2D& Point)
{
	return Point.X >= RectMin.X && Point.X <= RectMax.X && Point.Y >= RectMin.Y && Point.Y <= RectMax.Y;
}

bool UVRectsIntersect(const FVector2D& AMin, const FVector2D& AMax, const FVector2D& BMin, const FVector2D& BMax)
{
	return AMin.X <= BMax.X && AMax.X >= BMin.X && AMin.Y <= BMax.Y && AMax.Y >= BMin.Y;
}

bool DoesTileUVMatchGridRect(const FMinimapTileRef& Tile, const FIntPoint& GridDimensions)
{
	if (GridDimensions.X <= 0 || GridDimensions.Y <= 0)
	{
		return true;
	}

	const FVector2D ExpectedMin(
		static_cast<float>(Tile.Coord.X) / static_cast<float>(GridDimensions.X),
		static_cast<float>(Tile.Coord.Y) / static_cast<float>(GridDimensions.Y));
	const FVector2D ExpectedMax(
		static_cast<float>(Tile.Coord.X + 1) / static_cast<float>(GridDimensions.X),
		static_cast<float>(Tile.Coord.Y + 1) / static_cast<float>(GridDimensions.Y));
	return Tile.UVMin.Equals(ExpectedMin, 0.001f) && Tile.UVMax.Equals(ExpectedMax, 0.001f);
}

void WarnIfTileUVDoesNotMatchGrid(const UMinimapTileSetDataAsset* TileSet, const FMinimapTileRef& Tile,
                                  const FIntPoint& GridDimensions)
{
	static TSet<FString> WarnedTiles;
	if (DoesTileUVMatchGridRect(Tile, GridDimensions))
	{
		return;
	}

	const FString TileKey = FString::Printf(TEXT("%s:%d:%d:%d"),
		TileSet ? *TileSet->GetPathName() : TEXT("None"), Tile.Coord.LOD, Tile.Coord.X, Tile.Coord.Y);
	if (WarnedTiles.Contains(TileKey))
	{
		return;
	}

	WarnedTiles.Add(TileKey);
	UE_LOG(LogTemp, Warning,
	       TEXT("Panoramic tile UV does not match coord-grid rect. TileSet='%s' LOD=%d X=%d Y=%d Grid=%dx%d UVMin=%s UVMax=%s"),
	       TileSet ? *TileSet->GetPathName() : TEXT("None"), Tile.Coord.LOD, Tile.Coord.X, Tile.Coord.Y,
	       GridDimensions.X, GridDimensions.Y, *Tile.UVMin.ToString(), *Tile.UVMax.ToString());
}
}

FVector2D UMinimapBlueprintLibrary::WorldLocationToMapUV(const UMinimapDefinitionDataAsset* MinimapDefinition, const FVector WorldLocation, const bool bClampToBounds)
{
	if (!IsDefinitionUsable(MinimapDefinition))
	{
		return FVector2D::ZeroVector;
	}

	const FVector ProjectedWorldLocation = (bClampToBounds || MinimapDefinition->bClampQueriesToBounds)
		                                       ? ClampWorldLocationToBoundsXY(WorldLocation, MinimapDefinition->WorldBounds)
		                                       : WorldLocation;
	FVector2D UV = ProjectWorldToPanoramicMapUV(MinimapDefinition, ProjectedWorldLocation);

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

	FVector WorldLocation = ProjectPanoramicMapUVToWorld(MinimapDefinition, MapUV, WorldZ);
	if (bClampToBounds || MinimapDefinition->bClampQueriesToBounds)
	{
		WorldLocation = ClampWorldLocationToBoundsXY(WorldLocation, MinimapDefinition->WorldBounds);
	}
	return WorldLocation;
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

	for (const FMinimapTileRef& Tile : Level->Tiles)
	{
		FVector2D TileUVMin;
		FVector2D TileUVMax;
		NormalizeUVRect(Tile.UVMin, Tile.UVMax, TileUVMin, TileUVMax);
		if (!UVRectContainsPoint(TileUVMin, TileUVMax, MapUV))
		{
			continue;
		}

		WarnIfTileUVDoesNotMatchGrid(TileSet, Tile, Level->GridDimensions);
		const FVector2D TileUVExtent = TileUVMax - TileUVMin;
		OutCoord = Tile.Coord;
		OutTileUV = FVector2D(
			TileUVExtent.X > KINDA_SMALL_NUMBER ? (MapUV.X - TileUVMin.X) / TileUVExtent.X : 0.0f,
			TileUVExtent.Y > KINDA_SMALL_NUMBER ? (MapUV.Y - TileUVMin.Y) / TileUVExtent.Y : 0.0f);
		return true;
	}

	return false;
}

bool UMinimapBlueprintLibrary::WorldLocationToTileCoord(const UMinimapDefinitionDataAsset* MinimapDefinition,
                                                        const FVector WorldLocation, const int32 LOD,
                                                        FMinimapTileCoord& OutCoord, FVector2D& OutTileUV,
                                                        const bool bClampToBounds)
{
	if (!MinimapDefinition || MinimapDefinition->TileSet.IsNull())
	{
		OutCoord = FMinimapTileCoord();
		OutTileUV = FVector2D::ZeroVector;
		return false;
	}

	const UMinimapTileSetDataAsset* TileSet = MinimapDefinition->TileSet.LoadSynchronous();
	if (!TileSet)
	{
		OutCoord = FMinimapTileCoord();
		OutTileUV = FVector2D::ZeroVector;
		return false;
	}

	const FVector2D MapUV = WorldLocationToMapUV(MinimapDefinition, WorldLocation, bClampToBounds);
	return MapUVToTileCoord(TileSet, MapUV, LOD, OutCoord, OutTileUV, bClampToBounds);
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

	FVector2D QueryUVMin;
	FVector2D QueryUVMax;
	NormalizeUVRect(UVMin, UVMax, QueryUVMin, QueryUVMax);
	UVMin = QueryUVMin;
	UVMax = QueryUVMax;

	if (bClampToBounds || TileSet->bClampQueriesToBounds)
	{
		UVMin.X = FMath::Clamp(UVMin.X, 0.0f, 1.0f);
		UVMin.Y = FMath::Clamp(UVMin.Y, 0.0f, 1.0f);
		UVMax.X = FMath::Clamp(UVMax.X, 0.0f, 1.0f);
		UVMax.Y = FMath::Clamp(UVMax.Y, 0.0f, 1.0f);
	}

	for (const FMinimapTileRef& Tile : Level->Tiles)
	{
		FVector2D TileUVMin;
		FVector2D TileUVMax;
		NormalizeUVRect(Tile.UVMin, Tile.UVMax, TileUVMin, TileUVMax);
		if (UVRectsIntersect(UVMin, UVMax, TileUVMin, TileUVMax))
		{
			WarnIfTileUVDoesNotMatchGrid(TileSet, Tile, Level->GridDimensions);
			OutTiles.Add(Tile);
		}
	}

	OutTiles.Sort([](const FMinimapTileRef& A, const FMinimapTileRef& B)
	{
		if (A.Coord.LOD != B.Coord.LOD)
		{
			return A.Coord.LOD < B.Coord.LOD;
		}
		if (A.Coord.Y != B.Coord.Y)
		{
			return A.Coord.Y < B.Coord.Y;
		}
		return A.Coord.X < B.Coord.X;
	});
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
		if (Level.TilePixelSize.X <= 0 || Level.TilePixelSize.Y <= 0
			|| Level.WorldTileSize.X <= 0.0f || Level.WorldTileSize.Y <= 0.0f)
		{
			continue;
		}

		const float LevelWorldUnitsPerPixel = FMath::Max(
			Level.WorldTileSize.X / static_cast<float>(Level.TilePixelSize.X),
			Level.WorldTileSize.Y / static_cast<float>(Level.TilePixelSize.Y));
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
