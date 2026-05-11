#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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

    TSharedPtr<FUICommandList> PluginCommands;
};
