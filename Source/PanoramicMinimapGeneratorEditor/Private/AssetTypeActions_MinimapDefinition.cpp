#include "AssetTypeActions_MinimapDefinition.h"

#include "MinimapDefinitionAssetEditor.h"
#include "MinimapDefinitionDataAsset.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_MinimapDefinition"

FAssetTypeActions_MinimapDefinition::FAssetTypeActions_MinimapDefinition(const EAssetTypeCategories::Type InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FAssetTypeActions_MinimapDefinition::GetName() const
{
	return LOCTEXT("MinimapDefinitionName", "Minimap Definition");
}

FColor FAssetTypeActions_MinimapDefinition::GetTypeColor() const
{
	return FColor(65, 140, 255);
}

UClass* FAssetTypeActions_MinimapDefinition::GetSupportedClass() const
{
	return UMinimapDefinitionDataAsset::StaticClass();
}

uint32 FAssetTypeActions_MinimapDefinition::GetCategories()
{
	return AssetCategory;
}

void FAssetTypeActions_MinimapDefinition::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	for (UObject* Object : InObjects)
	{
		if (UMinimapDefinitionDataAsset* MinimapDefinition = Cast<UMinimapDefinitionDataAsset>(Object))
		{
			const TSharedRef<FMinimapDefinitionAssetEditor> Editor = MakeShared<FMinimapDefinitionAssetEditor>();
			Editor->InitMinimapDefinitionAssetEditor(EToolkitMode::Standalone, EditWithinLevelEditor, MinimapDefinition);
		}
	}
}

void FAssetTypeActions_MinimapDefinition::OpenAssetEditor(const TArray<UObject*>& InObjects, const EAssetTypeActivationOpenedMethod OpenedMethod, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	if (OpenedMethod == EAssetTypeActivationOpenedMethod::Edit)
	{
		OpenAssetEditor(InObjects, EditWithinLevelEditor);
	}
}

#undef LOCTEXT_NAMESPACE
