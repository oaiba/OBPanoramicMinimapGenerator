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
