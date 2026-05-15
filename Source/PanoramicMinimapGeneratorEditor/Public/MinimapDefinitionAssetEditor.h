#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class UMinimapDefinitionDataAsset;

class FMinimapDefinitionAssetEditor : public FAssetEditorToolkit
{
public:
	void InitMinimapDefinitionAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UMinimapDefinitionDataAsset* InAsset);
	virtual ~FMinimapDefinitionAssetEditor() override;

	//~ Begin FAssetEditorToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual bool IsPrimaryEditor() const override { return true; }
	virtual FName GetEditingAssetTypeName() const override;
	//~ End FAssetEditorToolkit interface

private:
	TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args) const;

private:
	TWeakObjectPtr<UMinimapDefinitionDataAsset> EditingAsset;
	TSharedPtr<IDetailsView> DetailsView;

	static const FName AppIdentifier;
	static const FName DetailsTabId;
};
