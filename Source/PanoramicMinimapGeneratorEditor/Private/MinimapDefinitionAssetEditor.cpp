#include "MinimapDefinitionAssetEditor.h"

#include "MinimapDefinitionDataAsset.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FMinimapDefinitionAssetEditor"

const FName FMinimapDefinitionAssetEditor::AppIdentifier(TEXT("MinimapDefinitionEditor"));
const FName FMinimapDefinitionAssetEditor::DetailsTabId(TEXT("MinimapDefinitionEditor_Details"));

void FMinimapDefinitionAssetEditor::InitMinimapDefinitionAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UMinimapDefinitionDataAsset* InAsset)
{
	EditingAsset = InAsset;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bShowOptions = true;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(InAsset);

	const TSharedRef<FTabManager::FLayout> StandaloneLayout = FTabManager::NewLayout(TEXT("Standalone_MinimapDefinitionEditor_Layout_v1"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(DetailsTabId, ETabState::OpenedTab)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, AppIdentifier, StandaloneLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InAsset);
	RegenerateMenusAndToolbars();
}

FMinimapDefinitionAssetEditor::~FMinimapDefinitionAssetEditor()
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(nullptr);
		DetailsView.Reset();
	}
	EditingAsset.Reset();
}

void FMinimapDefinitionAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MinimapDefinitionEditor", "Minimap Definition"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FMinimapDefinitionAssetEditor::SpawnDetailsTab))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FMinimapDefinitionAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
}

FName FMinimapDefinitionAssetEditor::GetToolkitFName() const
{
	return AppIdentifier;
}

FText FMinimapDefinitionAssetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Minimap Definition");
}

FText FMinimapDefinitionAssetEditor::GetToolkitName() const
{
	if (const UMinimapDefinitionDataAsset* Asset = EditingAsset.Get())
	{
		return FText::FromString(Asset->GetName());
	}
	return GetBaseToolkitName();
}

FText FMinimapDefinitionAssetEditor::GetToolkitToolTipText() const
{
	if (const UMinimapDefinitionDataAsset* Asset = EditingAsset.Get())
	{
		return FText::FromString(Asset->GetPathName());
	}
	return GetBaseToolkitName();
}

FString FMinimapDefinitionAssetEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("Minimap");
}

FLinearColor FMinimapDefinitionAssetEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.1f, 0.35f, 0.9f, 0.5f);
}

FName FMinimapDefinitionAssetEditor::GetEditingAssetTypeName() const
{
	return UMinimapDefinitionDataAsset::StaticClass()->GetFName();
}

TSharedRef<SDockTab> FMinimapDefinitionAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args) const
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			DetailsView.IsValid()
				? DetailsView.ToSharedRef()
				: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("MissingDetailsView", "Details view is not available.")))
		];
}

#undef LOCTEXT_NAMESPACE
