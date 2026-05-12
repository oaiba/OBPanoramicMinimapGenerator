#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MinimapDefinitionDataAsset.h"
#include "MinimapBlueprintLibrary.generated.h"

UCLASS()
class PANORAMICMINIMAPGENERATORRUNTIME_API UMinimapBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Minimap")
	static FVector2D WorldLocationToMapUV(const UMinimapDefinitionDataAsset* MinimapDefinition, FVector WorldLocation, bool bClampToBounds = true);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	static FVector2D WorldLocationToMapPixel(const UMinimapDefinitionDataAsset* MinimapDefinition, FVector WorldLocation, bool bClampToBounds = true);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	static FVector MapUVToWorldLocation(const UMinimapDefinitionDataAsset* MinimapDefinition, FVector2D MapUV, float WorldZ = 0.0f, bool bClampToBounds = true);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	static void GetOverlayElementsByCategory(const UMinimapDefinitionDataAsset* MinimapDefinition, FName Category, TArray<FMinimapOverlayElement>& OutElements);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	static void GetOverlayElementsByTag(const UMinimapDefinitionDataAsset* MinimapDefinition, FName Tag, TArray<FMinimapOverlayElement>& OutElements);
};
