#pragma once

#include "AssetTypeActions_Base.h"

class FAssetTypeActions_MinimapDefinition : public FAssetTypeActions_Base
{
public:
	explicit FAssetTypeActions_MinimapDefinition(EAssetTypeCategories::Type InAssetCategory);

	//~ Begin IAssetTypeActions interface
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, const EAssetTypeActivationOpenedMethod OpenedMethod, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	//~ End IAssetTypeActions interface

private:
	EAssetTypeCategories::Type AssetCategory;
};
