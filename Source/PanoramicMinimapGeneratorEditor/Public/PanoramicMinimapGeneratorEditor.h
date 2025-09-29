#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FPanoramicMinimapGeneratorEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void AddMenuExtension(FMenuBuilder& Builder);
    // HÀM MỚI để đăng ký menu
    void RegisterMenus();
    void PluginButtonClicked();
    TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

    TSharedPtr<FUICommandList> PluginCommands;
};
