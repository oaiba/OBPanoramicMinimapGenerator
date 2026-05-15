#include "PanoramicMinimapGeneratorEditor.h"
#include "AssetTypeActions_MinimapDefinition.h"
#include "PanoramicMinimapGeneratorCommands.h"
#include "MinimapGeneratorWindow.h"
#include "MinimapDefinitionDataAsset.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "LevelEditor.h" // Required include for Level Editor menu extension.
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Logging/LogMacros.h"
#include "Misc/CoreDelegates.h"

static const FName PanoramicMinimapGeneratorTabName("PanoramicMinimapGenerator");

#define LOCTEXT_NAMESPACE "FPanoramicMinimapGeneratorEditorModule"

DEFINE_LOG_CATEGORY(OBPanoramicMinimapGenerator);
void FPanoramicMinimapGeneratorEditorModule::StartupModule()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("PanoramicMinimapGeneratorEditor module startup."));
	RegisterAssetTypeActions();
	EditorPreExitDelegateHandle = FEditorDelegates::OnEditorPreExit.AddRaw(this, &FPanoramicMinimapGeneratorEditorModule::CloseAssetEditorsBeforeEditorExit);
	EnginePreExitDelegateHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FPanoramicMinimapGeneratorEditorModule::CloseAssetEditorsBeforeEditorExit);

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
	if (EditorPreExitDelegateHandle.IsValid())
	{
		FEditorDelegates::OnEditorPreExit.Remove(EditorPreExitDelegateHandle);
		EditorPreExitDelegateHandle.Reset();
	}

	if (EnginePreExitDelegateHandle.IsValid())
	{
		FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitDelegateHandle);
		EnginePreExitDelegateHandle.Reset();
	}

	CloseOpenMinimapDefinitionEditors();

	if (const TSharedPtr<SDockTab> ExistingTab = FGlobalTabmanager::Get()->FindExistingLiveTab(PanoramicMinimapGeneratorTabName))
	{
		ExistingTab->RequestCloseTab();
	}

    // Unregister callback to avoid shutdown-time issues.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("ToolMenus")))
	{
		UToolMenus::UnRegisterStartupCallback(this);
	}

    // Unregister tab spawner.
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanoramicMinimapGeneratorTabName);
	UnregisterAssetTypeActions();
}

void FPanoramicMinimapGeneratorEditorModule::RegisterAssetTypeActions()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
	{
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	MinimapAssetCategory = AssetTools.RegisterAdvancedAssetCategory(TEXT("OBMinimap"), LOCTEXT("OBMinimapAssetCategory", "OB Minimap"));

	const TSharedRef<IAssetTypeActions> MinimapDefinitionActions = MakeShared<FAssetTypeActions_MinimapDefinition>(MinimapAssetCategory);
	AssetTools.RegisterAssetTypeActions(MinimapDefinitionActions);
	RegisteredAssetTypeActions.Add(MinimapDefinitionActions);
}

void FPanoramicMinimapGeneratorEditorModule::UnregisterAssetTypeActions()
{
	if (RegisteredAssetTypeActions.Num() == 0 || !FAssetToolsModule::IsModuleLoaded())
	{
		RegisteredAssetTypeActions.Empty();
		return;
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	for (const TSharedRef<IAssetTypeActions>& AssetTypeActions : RegisteredAssetTypeActions)
	{
		AssetTools.UnregisterAssetTypeActions(AssetTypeActions);
	}
	RegisteredAssetTypeActions.Empty();
}

void FPanoramicMinimapGeneratorEditorModule::CloseOpenMinimapDefinitionEditors()
{
	if (!GEditor)
	{
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (UObject* EditedAsset : EditedAssets)
	{
		if (EditedAsset && EditedAsset->IsA<UMinimapDefinitionDataAsset>())
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(EditedAsset);
		}
	}
}

void FPanoramicMinimapGeneratorEditorModule::CloseAssetEditorsBeforeEditorExit()
{
	if (bClosingAssetEditorsForExit || !GEditor)
	{
		return;
	}

	TGuardValue<bool> GuardClosingAssetEditors(bClosingAssetEditorsForExit, true);

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	const int32 OpenEditorCount = AssetEditorSubsystem->GetAllOpenEditors().Num();
	if (OpenEditorCount == 0)
	{
		return;
	}

	UE_LOG(
		OBPanoramicMinimapGenerator,
		Log,
		TEXT("[PanoramicMinimapGenerator] Closing %d asset editor(s) before editor exit to avoid late Slate/SimpleAssetEditor teardown."),
		OpenEditorCount);
	AssetEditorSubsystem->SaveOpenAssetEditors(true);
	AssetEditorSubsystem->SetAutoRestoreAndDisableSavingOverride(true);
	AssetEditorSubsystem->CloseAllAssetEditors();
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
