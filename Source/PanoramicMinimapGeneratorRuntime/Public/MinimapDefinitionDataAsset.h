#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "MinimapDefinitionDataAsset.generated.h"

UENUM(BlueprintType)
enum class EMinimapOverlayElementType : uint8
{
	Marker UMETA(DisplayName = "Marker"),
	Zone UMETA(DisplayName = "Zone"),
	Path UMETA(DisplayName = "Path"),
	Freehand UMETA(DisplayName = "Freehand")
};

USTRUCT(BlueprintType)
struct PANORAMICMINIMAPGENERATORRUNTIME_API FMinimapOverlayStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	float LineWidth = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	TSoftObjectPtr<UTexture2D> Icon;
};

USTRUCT(BlueprintType)
struct PANORAMICMINIMAPGENERATORRUNTIME_API FMinimapOverlayElement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	EMinimapOverlayElementType Type = EMinimapOverlayElementType::Marker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FName Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	TArray<FName> FilterTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	TArray<FVector> WorldPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FMinimapOverlayStyle Style;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	bool bVisibleByDefault = true;
};

USTRUCT(BlueprintType)
struct PANORAMICMINIMAPGENERATORRUNTIME_API FMinimapOverlayLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FName LayerName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	bool bVisibleByDefault = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TArray<FMinimapOverlayElement> Elements;
};

USTRUCT(BlueprintType)
struct PANORAMICMINIMAPGENERATORRUNTIME_API FMinimapTileCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	int32 LOD = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	int32 Y = 0;
};

USTRUCT(BlueprintType)
struct PANORAMICMINIMAPGENERATORRUNTIME_API FMinimapTileRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FMinimapTileCoord Coord;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	TSoftObjectPtr<UTexture2D> Texture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FIntPoint ValidPixelMin = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FIntPoint ValidPixelMax = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FBox WorldBounds = FBox(ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FVector2D UVMin = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FVector2D UVMax = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct PANORAMICMINIMAPGENERATORRUNTIME_API FMinimapTilePyramidLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	int32 LOD = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FIntPoint GridDimensions = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FIntPoint TilePixelSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FVector2D WorldTileSize = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FIntPoint LogicalPixelSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	TArray<FMinimapTileRef> Tiles;

	const FMinimapTileRef* FindTile(int32 X, int32 Y) const;
};

UCLASS(BlueprintType)
class PANORAMICMINIMAPGENERATORRUNTIME_API UMinimapTileSetDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	FBox WorldBounds = FBox(ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	FIntPoint OutputSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	float MapRotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	bool bClampQueriesToBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	TArray<FMinimapTilePyramidLevel> PyramidLevels;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay")
	TArray<FMinimapOverlayLayer> OverlayLayers;

	UFUNCTION(BlueprintPure, Category = "Minimap|Tiles")
	bool IsValidTileSet() const;

	UFUNCTION(BlueprintPure, Category = "Minimap|Tiles")
	int32 GetMaxLOD() const;

	const FMinimapTilePyramidLevel* GetPyramidLevel(int32 LOD) const;
};

UCLASS(BlueprintType)
class PANORAMICMINIMAPGENERATORRUNTIME_API UMinimapDefinitionDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	TSoftObjectPtr<UTexture2D> BaseMapTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	TSoftObjectPtr<UMinimapTileSetDataAsset> TileSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	FBox WorldBounds = FBox(ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	FIntPoint OutputSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	float MapRotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	bool bClampQueriesToBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay")
	TArray<FMinimapOverlayLayer> OverlayLayers;

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsTiledDefinition() const;
};
