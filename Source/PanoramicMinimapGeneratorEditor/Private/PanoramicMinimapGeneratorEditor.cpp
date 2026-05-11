#include "PanoramicMinimapGeneratorEditor.h"
#include "PanoramicMinimapGeneratorCommands.h"
#include "MinimapGeneratorWindow.h"
#include "LevelEditor.h" // Required include for Level Editor menu extension.
#include "Widgets/Docking/SDockTab.h"
#include "Logging/LogMacros.h"

static const FName PanoramicMinimapGeneratorTabName("PanoramicMinimapGenerator");

#define LOCTEXT_NAMESPACE "FPanoramicMinimapGeneratorEditorModule"

DEFINE_LOG_CATEGORY(OBPanoramicMinimapGenerator);
void FPanoramicMinimapGeneratorEditorModule::StartupModule()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("PanoramicMinimapGeneratorEditor module startup."));
    // Register tab spawner first so it is available when invoked.
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PanoramicMinimapGeneratorTabName, FOnSpawnTab::CreateRaw(this, &FPanoramicMinimapGeneratorEditorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FPanoramicMinimapGeneratorTabTitle", "Panoramic Minimap"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

    // Register callback to run RegisterMenus() when ToolMenus is ready.
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPanoramicMinimapGeneratorEditorModule::RegisterMenus));
}

void FPanoramicMinimapGeneratorEditorModule::ShutdownModule()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("PanoramicMinimapGeneratorEditor module shutdown."));
    // Unregister callback to avoid shutdown-time issues.
    UToolMenus::UnRegisterStartupCallback(this);

    // Unregister tab spawner.
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanoramicMinimapGeneratorTabName);
}

void FPanoramicMinimapGeneratorEditorModule::RegisterMenus()
{
    // Find the "Tools" menu in Level Editor.
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    if (!Menu)
    {
        UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("[PanoramicMinimapGenerator] Failed to find 'LevelEditor.MainMenu.Tools' menu."));
        return;
    }

    // Add a new section to the "Tools" menu.
    FToolMenuSection& Section = Menu->AddSection(
        "PanoramicTools", // Section internal name.
        LOCTEXT("PanoramicToolsSection", "Panoramic Tools") // Section display label.
    );

    // Add our menu entry into the new section.
    Section.AddMenuEntry(
        FName("OpenPanoramicMinimapGenerator"), // Menu entry internal name.
        LOCTEXT("PanoramicMinimapGeneratorLabel", "Panoramic Minimap Generator"),
        LOCTEXT("PanoramicMinimapGeneratorTooltip", "Opens the Panoramic Minimap Generator window."),
        FSlateIcon(), // Icon can be added later.
        FUIAction(FExecuteAction::CreateRaw(this, &FPanoramicMinimapGeneratorEditorModule::PluginButtonClicked))
    );

    UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("[PanoramicMinimapGenerator] Menu registered successfully using UToolMenus."));
}

void FPanoramicMinimapGeneratorEditorModule::PluginButtonClicked()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Panoramic Minimap Generator menu entry clicked."));
	FGlobalTabmanager::Get()->TryInvokeTab(PanoramicMinimapGeneratorTabName);
}

TSharedRef<SDockTab> FPanoramicMinimapGeneratorEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Spawning Panoramic Minimap Generator tab."));
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SMinimapGeneratorWindow)
		];
}

IMPLEMENT_MODULE(FPanoramicMinimapGeneratorEditorModule, PanoramicMinimapGeneratorEditor)

#undef LOCTEXT_NAMESPACE
