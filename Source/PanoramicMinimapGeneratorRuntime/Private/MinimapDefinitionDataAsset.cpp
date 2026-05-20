#include "MinimapDefinitionDataAsset.h"

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
