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

UCLASS(BlueprintType)
class PANORAMICMINIMAPGENERATORRUNTIME_API UMinimapDefinitionDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	TObjectPtr<UTexture2D> BaseMapTexture = nullptr;

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
};
