#include "MinimapDefinitionDataAsset.h"

namespace
{
bool HasUsableXYBounds(const FBox& Bounds)
{
	const FVector BoundsSize = Bounds.GetSize();
	return Bounds.IsValid
		&& !FMath::IsNearlyZero(BoundsSize.X)
		&& !FMath::IsNearlyZero(BoundsSize.Y);
}

bool HasUsableOutputSize(const FIntPoint& OutputSize)
{
	return OutputSize.X > 0 && OutputSize.Y > 0;
}

bool HasUsableProjectionSize(const FVector2D& ProjectionWorldSize)
{
	return ProjectionWorldSize.X > 0.0f && ProjectionWorldSize.Y > 0.0f;
}

FVector2D CalculateProjectionWorldSize(const FBox& Bounds, const FIntPoint& OutputSize)
{
	const FVector BoundsSize = Bounds.GetSize();
	if (!HasUsableXYBounds(Bounds) || !HasUsableOutputSize(OutputSize))
	{
		return FVector2D(FMath::Abs(BoundsSize.X), FMath::Abs(BoundsSize.Y));
	}

	const float AspectRatio = static_cast<float>(OutputSize.X) / static_cast<float>(OutputSize.Y);
	if (AspectRatio >= 1.0f)
	{
		const float Width = FMath::Max(FMath::Abs(BoundsSize.X), FMath::Abs(BoundsSize.Y) * AspectRatio);
		return FVector2D(Width, Width / AspectRatio);
	}

	const float Height = FMath::Max(FMath::Abs(BoundsSize.Y), FMath::Abs(BoundsSize.X) / AspectRatio);
	return FVector2D(Height * AspectRatio, Height);
}

bool ResolveProjectionFrameFromMetadata(const FVector& StoredProjectionWorldCenter,
                                        const FVector2D& StoredProjectionWorldSize,
                                        const FBox& CaptureBounds,
                                        const FIntPoint& CaptureOutputSize,
                                        const FBox& WorldBounds,
                                        const FIntPoint& OutputSize,
                                        FVector& OutWorldCenter,
                                        FVector2D& OutWorldSize)
{
	if (HasUsableProjectionSize(StoredProjectionWorldSize))
	{
		OutWorldCenter = StoredProjectionWorldCenter;
		OutWorldSize = StoredProjectionWorldSize;
		return true;
	}

	if (HasUsableXYBounds(CaptureBounds) && HasUsableOutputSize(CaptureOutputSize))
	{
		OutWorldCenter = CaptureBounds.GetCenter();
		OutWorldSize = CalculateProjectionWorldSize(CaptureBounds, CaptureOutputSize);
		return HasUsableProjectionSize(OutWorldSize);
	}

	if (HasUsableXYBounds(WorldBounds))
	{
		OutWorldCenter = WorldBounds.GetCenter();
		OutWorldSize = CalculateProjectionWorldSize(WorldBounds, OutputSize);
		return HasUsableProjectionSize(OutWorldSize);
	}

	OutWorldCenter = FVector::ZeroVector;
	OutWorldSize = FVector2D::ZeroVector;
	return false;
}
}

const FMinimapTileRef* FMinimapTilePyramidLevel::FindTile(const int32 X, const int32 Y) const
{
	for (const FMinimapTileRef& Tile : Tiles)
	{
		if (Tile.Coord.X == X && Tile.Coord.Y == Y)
		{
			return &Tile;
		}
	}

	return nullptr;
}

bool UMinimapTileSetDataAsset::IsValidTileSet() const
{
	return WorldBounds.IsValid
		&& OutputSize.X > 0
		&& OutputSize.Y > 0
		&& PyramidLevels.Num() > 0;
}

int32 UMinimapTileSetDataAsset::GetMaxLOD() const
{
	int32 MaxLOD = INDEX_NONE;
	for (const FMinimapTilePyramidLevel& Level : PyramidLevels)
	{
		MaxLOD = FMath::Max(MaxLOD, Level.LOD);
	}

	return MaxLOD;
}

const FMinimapTilePyramidLevel* UMinimapTileSetDataAsset::GetPyramidLevel(const int32 LOD) const
{
	for (const FMinimapTilePyramidLevel& Level : PyramidLevels)
	{
		if (Level.LOD == LOD)
		{
			return &Level;
		}
	}

	return nullptr;
}

bool UMinimapTileSetDataAsset::ResolveProjectionFrame(FVector& OutWorldCenter, FVector2D& OutWorldSize) const
{
	return ResolveProjectionFrameFromMetadata(
		ProjectionWorldCenter,
		ProjectionWorldSize,
		CaptureBounds,
		CaptureOutputSize,
		WorldBounds,
		OutputSize,
		OutWorldCenter,
		OutWorldSize);
}

bool UMinimapDefinitionDataAsset::IsTiledDefinition() const
{
	if (TileSet.IsNull())
	{
		return false;
	}

	if (const UMinimapTileSetDataAsset* LoadedTileSet = TileSet.Get())
	{
		return LoadedTileSet->IsValidTileSet();
	}

	return true;
}

bool UMinimapDefinitionDataAsset::ResolveProjectionFrame(FVector& OutWorldCenter, FVector2D& OutWorldSize) const
{
	return ResolveProjectionFrameFromMetadata(
		ProjectionWorldCenter,
		ProjectionWorldSize,
		CaptureBounds,
		CaptureOutputSize,
		WorldBounds,
		OutputSize,
		OutWorldCenter,
		OutWorldSize);
}
