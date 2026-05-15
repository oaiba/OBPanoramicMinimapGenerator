#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IAssetTypeActions.h"

DECLARE_LOG_CATEGORY_EXTERN(OBPanoramicMinimapGenerator, Log, All);

class FPanoramicMinimapGeneratorEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void AddMenuExtension(FMenuBuilder& Builder);
    void RegisterMenus();
    void PluginButtonClicked();
    TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);
    void RegisterAssetTypeActions();
    void UnregisterAssetTypeActions();
    void CloseOpenMinimapDefinitionEditors();
    void CloseAssetEditorsBeforeEditorExit();

    TSharedPtr<FUICommandList> PluginCommands;
    TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
    FDelegateHandle EditorPreExitDelegateHandle;
    FDelegateHandle EnginePreExitDelegateHandle;
    EAssetTypeCategories::Type MinimapAssetCategory = EAssetTypeCategories::Misc;
    bool bClosingAssetEditorsForExit = false;
};
