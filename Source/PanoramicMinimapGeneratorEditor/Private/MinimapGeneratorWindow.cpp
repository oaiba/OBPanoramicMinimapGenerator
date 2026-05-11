// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorWindow.h"
#include "PanoramicMinimapGeneratorEditor.h"

#include "DesktopPlatformModule.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Editor.h"
#include "Selection.h"
#include "ImageUtils.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "SMinimapGeneratorWindow"

void SMinimapGeneratorWindow::Construct(const FArguments& InArgs)
{
	// --- INITIAL DATA SETUP ---
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Constructing SMinimapGeneratorWindow."));
	Manager = TStrongObjectPtr<UMinimapGeneratorManager>(NewObject<UMinimapGeneratorManager>());
	Manager->OnProgress.AddSP(this, &SMinimapGeneratorWindow::OnCaptureProgress);
	Manager->OnCaptureComplete.AddSP(this, &SMinimapGeneratorWindow::HandleCaptureCompleted);

	const FString DefaultPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	const FMargin LabelPadding(5, 5, 10, 5);
	CurrentBackgroundMode = EMinimapBackgroundMode::SolidColor;
	SelectedBackgroundColor = FLinearColor::Black;
	if (const UEnum* CaptureSourceEnum = StaticEnum<ESceneCaptureSource>())
	{
		for (int32 i = 0; i < CaptureSourceEnum->NumEnums() - 1; ++i)
		{
			CaptureSourceOptions.Add(
				MakeShared<FString>(CaptureSourceEnum->GetDisplayNameTextByIndex(i).ToString()));
		}
		CurrentCaptureSource = CaptureSourceOptions[4]; // Default to SCS_FinalColorHDR
	}

	for (int32 i = 5; i <= 14; ++i) // 2^5=32, 2^14=16384
	{
		ResolutionOptions.Add(MakeShared<int32>(1 << i));
	}

	// Set default resolution to 4096 (2^12).
	CurrentOutputWidth = ResolutionOptions[7];
	CurrentOutputHeight = ResolutionOptions[7];

	// === NEW LAYOUT STRUCTURE START ===
	ChildSlot
	[
		SNew(SVerticalBox)

		// Slot 1: Hosts the SSplitter and takes most of the available space.
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)

			// --- LEFT PANE (SETTINGS AREA) ---
			+ SSplitter::Slot()
			.Value(0.4f) // 40% ratio
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(10)
				[
					SNew(SVerticalBox)

					// --- SECTION: REGION (using SExpandableArea) ---
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(false)
						.HeaderContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RegionHeader", "1. Capture Region"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
						]
						.BodyContent()
						[
							SNew(SGridPanel).FillColumn(1, 1.0f)
							+ SGridPanel::Slot(0, 0).HAlign(HAlign_Right).Padding(LabelPadding)
							[
								SNew(STextBlock).Text(LOCTEXT("MinBoundsLabel", "Min Bounds"))
							]
							+ SGridPanel::Slot(1, 0)
							[
								SNew(SWrapBox).UseAllottedSize(true)
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(BoundsMinX, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(BoundsMinY, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(BoundsMinZ, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
								]
							]
							+ SGridPanel::Slot(0, 1).HAlign(HAlign_Right).Padding(LabelPadding)
							[
								SNew(STextBlock).Text(LOCTEXT("MaxBoundsLabel", "Max Bounds"))
							]
							+ SGridPanel::Slot(1, 1)
							[
								SNew(SWrapBox).UseAllottedSize(true)
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(BoundsMaxX, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(BoundsMaxY, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(BoundsMaxZ, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
								]
							]
							+ SGridPanel::Slot(1, 2).HAlign(HAlign_Right).Padding(5)
							[
								SNew(SButton)
								.Text(LOCTEXT("GetBoundsButton", "Get Bounds from Selected Actor"))
								.OnClicked(this, &SMinimapGeneratorWindow::OnGetBoundsFromSelectionClicked)
							]
						]
					]

					// --- SECTION: TILING (using SExpandableArea) ---
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.HeaderContent()
						[
							SNew(STextBlock).Text(LOCTEXT("TilingHeader", "2. Tiling Settings")).Font(
								FAppStyle::GetFontStyle("BoldFont"))
						]
						.BodyContent()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SAssignNew(UseTilingCheckbox, SCheckBox).IsChecked(ECheckBoxState::Unchecked)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("UseTilingLabel", "Use Tiled Capture (for high resolutions)"))
									.Font(FAppStyle::GetFontStyle("BoldFont"))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 0, 5)
							[
								SNew(SVerticalBox)
								.Visibility(this, &SMinimapGeneratorWindow::GetTilingSettingsVisibility)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("TileResLabel", "Tile Resolution"))
									]
									+ SHorizontalBox::Slot().FillWidth(0.6f)
									[
										SAssignNew(TileResolution, SSpinBox<int32>).MinValue(256).MaxValue(8192).Value(
											2048)
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("TileOverlapLabel", "Tile Overlap (px)"))
										.ToolTipText(LOCTEXT("TileOverlapTooltip",
										                     "Maximum value to hide grid lines where tiles overlap"))
									]
									+ SHorizontalBox::Slot().FillWidth(0.6f)
									[
										SAssignNew(TileOverlap, SSpinBox<int32>).MinValue(0).MaxValue(1024).Value(1024)
									]
								]
							]
						]
					]

					// --- SECTION: CAMERA (using SExpandableArea) ---
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.HeaderContent()
						[
							SNew(STextBlock).Text(LOCTEXT("CameraHeader", "3. Camera Settings")).Font(
								FAppStyle::GetFontStyle("BoldFont"))
						]
						.BodyContent()
						[
							SNew(SGridPanel).FillColumn(1, 1.0f)
							+ SGridPanel::Slot(0, 0).HAlign(HAlign_Right).Padding(LabelPadding)
							[
								SNew(STextBlock).Text(LOCTEXT("CameraHeightLabel", "Camera Height"))
							]
							+ SGridPanel::Slot(1, 0)
							[
								SAssignNew(CameraHeight, SSpinBox<float>).MinValue(100.f).MaxValue(999999.f).Value(
									50000.f)
							]
							+ SGridPanel::Slot(0, 1).HAlign(HAlign_Right).Padding(LabelPadding)
							[
								SNew(STextBlock).Text(LOCTEXT("CameraRotationLabel", "Camera Rotation (R, P, Y)"))
							]
							+ SGridPanel::Slot(1, 1)
							[
								SNew(SWrapBox).UseAllottedSize(true)
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(RotationRollSpinBox, SSpinBox<float>)
									.MinValue(-360.f)
									.MaxValue(360.f)
									.Value(-90.f)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(RotationPitchSpinBox, SSpinBox<float>)
									.MinValue(-360.f)
									.MaxValue(360.f)
									.Value(-90.f)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SAssignNew(RotationYawSpinBox, SSpinBox<float>)
									.MinValue(-360.f)
									.MaxValue(360.f)
									.Value(0.f)
								]
							]
							+ SGridPanel::Slot(1, 2)
							.Padding(2, 5, 2, 5)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RotationWarning",
								              "Warning: Non-square output. Adjust Pitch by +/- 90° if capture is misaligned."))
								.ColorAndOpacity(FAppStyle::GetSlateColor("Colors.Warning"))
								.Visibility(
									this, &SMinimapGeneratorWindow::GetRotationWarningVisibility)
								.AutoWrapText(true)
							]
							+ SGridPanel::Slot(1, 3)
							[
								SAssignNew(IsOrthographicCheckbox, SCheckBox)
								.IsChecked(ECheckBoxState::Checked)
								.OnCheckStateChanged(this, &SMinimapGeneratorWindow::OnProjectionTypeChanged)
								[
									SNew(STextBlock).Text(LOCTEXT("IsOrthoLabel", "Use Orthographic Projection"))
								]
							]
							+ SGridPanel::Slot(0, 4).HAlign(HAlign_Right).Padding(LabelPadding)
							[
								SNew(STextBlock).Text(LOCTEXT("CameraFOVLabel", "Camera FOV (Perspective only)"))
							]
							+ SGridPanel::Slot(1, 4)
							[
								SAssignNew(CameraFOV, SSpinBox<float>)
								.MinValue(10.f)
								.MaxValue(170.f)
								.Value(90.f)
								.IsEnabled(this, &SMinimapGeneratorWindow::IsPerspectiveMode)
							]
						]
					]

					// --- SECTION: QUALITY (using SExpandableArea) ---
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.HeaderContent()
						[
							SNew(STextBlock).Text(LOCTEXT("QualityHeader", "4. Quality Settings")).Font(
								FAppStyle::GetFontStyle("BoldFont"))
						]
						.BodyContent()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(15, 5)
							[
								SAssignNew(CaptureDynamicShadowsCheckbox, SCheckBox)
								.IsChecked(ECheckBoxState::Checked)
								[
									SNew(STextBlock).Text(LOCTEXT("DynamicShadowsLabel", "Capture Dynamic Shadows"))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(15, 5)
							[
								SAssignNew(OverrideQualityCheckbox, SCheckBox).IsChecked(ECheckBoxState::Unchecked)
								[
									SNew(STextBlock).Text(
										LOCTEXT("OverrideQualityLabel", "Override Editor Scalability Settings"))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(20, 5, 5, 5)
							[
								SNew(SVerticalBox).Visibility(
									this, &SMinimapGeneratorWindow::GetOverrideSettingsVisibility)
								// ... (AO, SSR, CaptureSource override widgets remain unchanged)
							]
						]
					]

					// --- SECTION: FILTERING (using SExpandableArea) ---
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.HeaderContent()
						[
							SNew(STextBlock).Text(LOCTEXT("FilteringHeader", "5. Actor Filtering")).Font(
								FAppStyle::GetFontStyle("BoldFont"))
						]
						.BodyContent()
						[
							SNew(SVerticalBox)

							// --- Show Only List ---
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(STextBlock).Text(LOCTEXT("ShowOnlyHeader", "Show Only Actors"))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox)
								.HeightOverride(150.0f)
								[
									SAssignNew(ShowOnlyActorsListView, SListView<TSharedPtr<FString>>)
									.ListItemsSource(&ShowOnlyActorNames)
									.SelectionMode(ESelectionMode::Multi)
									.OnGenerateRow_Lambda(
										[](const TSharedPtr<FString>& InItem,
										   const TSharedRef<STableViewBase>& OwnerTable)
										{
											return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
												[
													SNew(STextBlock).Text(FText::FromString(*InItem))
												];
										})
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("AddToShowOnly", "Add Selected")).OnClicked(
										this, &SMinimapGeneratorWindow::OnAddSelectedToShowOnlyList)
								]
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("RemoveFromShowOnly", "Remove Selected")).OnClicked(
										this, &SMinimapGeneratorWindow::OnRemoveSelectedFromShowOnlyList)
								]

								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("ClearShowOnly", "Clear List")).OnClicked(
										this, &SMinimapGeneratorWindow::OnClearShowOnlyList)
								]
							]

							// --- Hidden List ---
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 15, 0, 5)
							[
								SNew(STextBlock).Text(LOCTEXT("HiddenHeader", "Hidden Actors"))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox)
								.HeightOverride(150.0f)
								[
									SAssignNew(HiddenActorsListView, SListView<TSharedPtr<FString>>)
									.ListItemsSource(&HiddenActorNames)
									.SelectionMode(ESelectionMode::Multi)
									.OnGenerateRow_Lambda(
										[](const TSharedPtr<FString>& InItem,
										   const TSharedRef<STableViewBase>& OwnerTable)
										{
											return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
												[
													SNew(STextBlock).Text(FText::FromString(*InItem))
												];
										})
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("AddToHidden", "Add Selected")).OnClicked(
										this, &SMinimapGeneratorWindow::OnAddSelectedToHiddenList)
								]
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("RemoveFromHidden", "Remove Selected")).OnClicked(
										this, &SMinimapGeneratorWindow::OnRemoveSelectedFromHiddenList)
								]
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("ClearHidden", "Clear List")).OnClicked(
										this, &SMinimapGeneratorWindow::OnClearHiddenList)
								]
							]

							// --- Filter by Class and Tag ---
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 15, 0, 5)
							[
								SNew(SGridPanel).FillColumn(1, 1.0f)
								+ SGridPanel::Slot(0, 0).HAlign(HAlign_Right).Padding(LabelPadding)
								[
									SNew(STextBlock).Text(LOCTEXT("ClassFilterLabel", "Hide Actors of Class"))
								]
								+ SGridPanel::Slot(1, 0)
								[
									SAssignNew(ActorClassFilterBox, SClassPropertyEntryBox)
									.MetaClass(AActor::StaticClass())
									.SelectedClass(this, &SMinimapGeneratorWindow::GetSelectedActorClass)
									.OnSetClass(this, &SMinimapGeneratorWindow::OnActorClassChanged)
								]
								+ SGridPanel::Slot(0, 1).HAlign(HAlign_Right).Padding(LabelPadding)
								[
									SNew(STextBlock).Text(LOCTEXT("TagFilterLabel", "Hide Actors with Tag"))
								]
								+ SGridPanel::Slot(1, 1)
								[
									SAssignNew(ActorTagFilterTextBox, SEditableTextBox)
								]
							]
						]
					]
				]
			]
			// --- RIGHT PANE (PREVIEW AND OUTPUT) ---
			+ SSplitter::Slot()
			.Value(0.6f) // 60% ratio
			[
				SNew(SVerticalBox)

				// --- IMAGE PREVIEW AREA ---
				+ SVerticalBox::Slot()
				  .FillHeight(1.0f) // Let the preview area take most of the space.
				  .Padding(5)
				[
					SNew(SBorder)
					.Padding(FMargin(3))
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(SVerticalBox)
						// + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10)
						// [
						// 	SNew(STextBlock)
						// 	.Text(LOCTEXT("PreviewHeader", "PREVIEW"))
						// 	.Font(FAppStyle::GetFontStyle("BoldFont"))
						// ]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(ImageContainer, SBox)
							[
								SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
								[
									SAssignNew(FinalImageView, SImage)
								]
							]
						]
					]
				]

				// --- OUTPUT SETTINGS AREA (using SExpandableArea) ---
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock).Text(LOCTEXT("OutputHeaderRight", "Output Settings")).Font(
							FAppStyle::GetFontStyle("BoldFont"))
					]
					.BodyContent()
					[
						SNew(SGridPanel).FillColumn(1, 1.0f)
						+ SGridPanel::Slot(0, 1).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock).Text(LOCTEXT("OutputWidthLabel", "Output Width"))
						]
						+ SGridPanel::Slot(1, 1)
						[
							SAssignNew(OutputWidthComboBox, SComboBox<TSharedPtr<int32>>)
							.OptionsSource(&ResolutionOptions)
							.OnSelectionChanged(this, &SMinimapGeneratorWindow::OnOutputWidthChanged)
							.OnGenerateWidget_Lambda([](const TSharedPtr<int32>& InOption)
							{
								return SNew(STextBlock).Text(FText::AsNumber(*InOption));
							})
							[
								SNew(STextBlock).Text_Lambda([this] { return FText::AsNumber(*CurrentOutputWidth); })
							]
						]
						+ SGridPanel::Slot(0, 2).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock).Text(LOCTEXT("OutputHeightLabel", "Output Height"))
						]
						+ SGridPanel::Slot(1, 2)
						[
							SAssignNew(OutputHeightComboBox, SComboBox<TSharedPtr<int32>>)
							.OptionsSource(&ResolutionOptions)
							.OnSelectionChanged(this, &SMinimapGeneratorWindow::OnOutputHeightChanged)
							.OnGenerateWidget_Lambda([](const TSharedPtr<int32>& InOption)
							{
								return SNew(STextBlock).Text(FText::AsNumber(*InOption));
							})
							[
								SNew(STextBlock).Text_Lambda([this] { return FText::AsNumber(*CurrentOutputHeight); })
							]
						]
						+ SGridPanel::Slot(0, 3).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock).Text(LOCTEXT("OutputPathLabel", "Output Path"))
						]
						+ SGridPanel::Slot(1, 3)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SAssignNew(OutputPath, SEditableTextBox).Text(FText::FromString(DefaultPath))
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).Text(LOCTEXT("BrowseButton", "...")).OnClicked(
									this, &SMinimapGeneratorWindow::OnBrowseButtonClicked)
							]
						]
						+ SGridPanel::Slot(0, 4).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock).Text(LOCTEXT("FileNameLabel", "File Name"))
						]
						+ SGridPanel::Slot(1, 4)
						[
							SAssignNew(FileName, SEditableTextBox).Text(LOCTEXT("DefaultFileName", "Minimap_Result"))
						]
						+ SGridPanel::Slot(1, 5)
						[
							SAssignNew(AutoFilenameCheckbox, SCheckBox).IsChecked(ECheckBoxState::Checked)
							[
								SNew(STextBlock).Text(LOCTEXT("AutoFilenameLabel",
								                              "Auto-Generate Filename with Timestamp"))
							]
						]
						+ SGridPanel::Slot(1, 6) // Additional slot for checkbox
						[
							SAssignNew(ImportAsAssetCheckbox, SCheckBox).IsChecked(ECheckBoxState::Unchecked)
							[
								SNew(STextBlock).Text(LOCTEXT("ImportAsAssetLabel", "Import as Texture Asset"))
							]
						]
						+ SGridPanel::Slot(0, 7).HAlign(HAlign_Right).Padding(LabelPadding)
						// Additional slot for path label
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AssetPathLabel", "Asset Path"))
							.Visibility(this, &SMinimapGeneratorWindow::GetAssetPathVisibility)
						]
						+ SGridPanel::Slot(1, 7) // Additional slot for path text box
						[
							SAssignNew(AssetPathTextBox, SEditableTextBox)
							.Text(FText::FromString(TEXT("/Game/Minimaps/")))
							.Visibility(this, &SMinimapGeneratorWindow::GetAssetPathVisibility)
						]
						+ SGridPanel::Slot(0, 8).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock).Text(LOCTEXT("BackgroundModeLabel", "Background Mode"))
						]
						+ SGridPanel::Slot(1, 8)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
							[
								SNew(SCheckBox)
								.Style(FAppStyle::Get(), "RadioButton")
								.IsChecked(this, &SMinimapGeneratorWindow::IsBackgroundModeChecked,
								           EMinimapBackgroundMode::Transparent)
								.OnCheckStateChanged(this, &SMinimapGeneratorWindow::OnBackgroundModeChanged,
								                     EMinimapBackgroundMode::Transparent)
								[
									SNew(STextBlock).Text(LOCTEXT("TransparentRadio", "Transparent"))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SCheckBox)
								.Style(FAppStyle::Get(), "RadioButton")
								.IsChecked(this, &SMinimapGeneratorWindow::IsBackgroundModeChecked,
								           EMinimapBackgroundMode::SolidColor)
								.OnCheckStateChanged(this, &SMinimapGeneratorWindow::OnBackgroundModeChanged,
								                     EMinimapBackgroundMode::SolidColor)
								[
									SNew(STextBlock).Text(LOCTEXT("SolidColorRadio", "Solid Color"))
								]
							]
						]
						+ SGridPanel::Slot(1, 9).Padding(0, 5, 0, 5)
						[
							SNew(SColorBlock)
							.Color(this, &SMinimapGeneratorWindow::GetSelectedBackgroundColor)
							.OnMouseButtonDown(this, &SMinimapGeneratorWindow::OnBackgroundColorBlockMouseButtonDown)
							.Size(FVector2D(100.f, 20.f))
							.Visibility(this, &SMinimapGeneratorWindow::GetBackgroundColorPickerVisibility)
						]
					]
				]
			]
		]
		// Slot 2: Hosts buttons and the progress bar, anchored at the bottom.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
			[
				SAssignNew(StartButton, SButton)
				.Text(LOCTEXT("StartCaptureButton", "Start Capture Process"))
				.HAlign(HAlign_Center)
				.OnClicked(this, &SMinimapGeneratorWindow::OnStartCaptureClicked)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
			[
				SAssignNew(ProgressBar, SProgressBar).Percent(0.0f).Visibility(EVisibility::Hidden)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
			[
				SAssignNew(StatusText, STextBlock).Text(FText::GetEmpty()).Visibility(EVisibility::Hidden)
			]
		]
	];
}

void SMinimapGeneratorWindow::HandleCaptureCompleted(bool bSuccess, const FString& FinalImagePath)
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Capture completed. Success=%s, Path=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *FinalImagePath);
	StartButton->SetEnabled(true);
	// Use a short timer to hide progress UI after completion.
	// This lets users briefly see the final status ("Done!" or "Failed!").
	GEditor->GetTimerManager()->SetTimer(
		TimerHandle_HideProgress,
		[this]()
		{
			ProgressBar->SetVisibility(EVisibility::Hidden);
			StatusText->SetVisibility(EVisibility::Hidden);
		},
		2.0f, // Hide after 2 seconds
		false
	);

	if (bSuccess && !FinalImagePath.IsEmpty() && IFileManager::Get().FileExists(*FinalImagePath))
	{
		if (UTexture2D* LoadedTexture = FImageUtils::ImportFileAsTexture2D(FinalImagePath))
		{
			// 1) Create an FDeferredCleanupSlateBrush and store it in a member for lifetime management.
			//    CreateBrush returns TSharedRef, and we store it as TSharedPtr.
			FinalImageBrushSource = FDeferredCleanupSlateBrush::CreateBrush(
				LoadedTexture,
				FVector2D(LoadedTexture->GetSizeX(), LoadedTexture->GetSizeY())
			);

			// 2) Get the inner FSlateBrush* and assign it to SImage.
			FinalImageView->SetImage(FinalImageBrushSource->GetSlateBrush());
			ImageContainer->SetVisibility(EVisibility::Visible);

			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Successfully loaded and displayed image from %s"), *FinalImagePath);
		}
		else
		{
			UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to load texture from file %s"), *FinalImagePath);
			ImageContainer->SetVisibility(EVisibility::Collapsed);
		}
	}
	else
	{
		ImageContainer->SetVisibility(EVisibility::Collapsed);
	}
}

ECheckBoxState SMinimapGeneratorWindow::IsBackgroundModeChecked(EMinimapBackgroundMode Mode) const
{
	return CurrentBackgroundMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FLinearColor SMinimapGeneratorWindow::GetSelectedBackgroundColor() const
{
	return SelectedBackgroundColor;
}

void SMinimapGeneratorWindow::OnBackgroundColorChanged(const FLinearColor NewColor)
{
	SelectedBackgroundColor = NewColor;
}

FReply SMinimapGeneratorWindow::OnBackgroundColorBlockMouseButtonDown(const FGeometry& MyGeometry,
                                                                      const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FColorPickerArgs PickerArgs;
		PickerArgs.bUseAlpha = true; // Allow selecting alpha/transparency.
		PickerArgs.DisplayGamma = TAttribute<float>(2.2f);
		PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
			this, &SMinimapGeneratorWindow::OnBackgroundColorChanged);
		PickerArgs.InitialColor = SelectedBackgroundColor;
		PickerArgs.ParentWidget = AsShared();

		OpenColorPicker(PickerArgs);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SMinimapGeneratorWindow::OnBackgroundModeChanged(ECheckBoxState NewState, EMinimapBackgroundMode Mode)
{
	if (NewState == ECheckBoxState::Checked)
	{
		CurrentBackgroundMode = Mode;
	}
}

EVisibility SMinimapGeneratorWindow::GetBackgroundColorPickerVisibility() const
{
	return CurrentBackgroundMode == EMinimapBackgroundMode::SolidColor ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SMinimapGeneratorWindow::GetRotationWarningVisibility() const
{
	if (CurrentOutputWidth.IsValid() && CurrentOutputHeight.IsValid())
	{
		return (*CurrentOutputWidth != *CurrentOutputHeight) ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

EVisibility SMinimapGeneratorWindow::GetOverrideSettingsVisibility() const
{
	return OverrideQualityCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SMinimapGeneratorWindow::OnCaptureSourceChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		CurrentCaptureSource = NewSelection;
	}
}

void SMinimapGeneratorWindow::OnOutputWidthChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid()) CurrentOutputWidth = NewSelection;
}

void SMinimapGeneratorWindow::OnOutputHeightChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid()) CurrentOutputHeight = NewSelection;
}

EVisibility SMinimapGeneratorWindow::GetAssetPathVisibility() const
{
	return ImportAsAssetCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SMinimapGeneratorWindow::GetTilingSettingsVisibility() const
{
	return UseTilingCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SMinimapGeneratorWindow::OnStartCaptureClicked()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Start Capture button clicked."));

	StartButton->SetEnabled(false);

	// Collect values from all UI widgets.
	Settings.CaptureBounds = FBox(
		FVector(BoundsMinX->GetValue(), BoundsMinY->GetValue(), BoundsMinZ->GetValue()),
		FVector(BoundsMaxX->GetValue(), BoundsMaxY->GetValue(), BoundsMaxZ->GetValue())
	);
	Settings.OutputWidth = *CurrentOutputWidth;
	Settings.OutputHeight = *CurrentOutputHeight;
	Settings.OutputPath = OutputPath->GetText().ToString();
	Settings.FileName = FileName->GetText().ToString();
	Settings.BackgroundMode = CurrentBackgroundMode;
	if (CurrentBackgroundMode == EMinimapBackgroundMode::SolidColor)
	{
		Settings.BackgroundColor = SelectedBackgroundColor;
	}
	Settings.bUseAutoFilename = AutoFilenameCheckbox->IsChecked();
	Settings.bImportAsTextureAsset = ImportAsAssetCheckbox->IsChecked();
	if (Settings.bImportAsTextureAsset)
	{
		Settings.AssetPath = AssetPathTextBox->GetText().ToString();
	}
	Settings.bUseTiling = UseTilingCheckbox->IsChecked();
	Settings.TileResolution = TileResolution->GetValue();
	Settings.TileOverlap = TileOverlap->GetValue();
	Settings.CameraHeight = CameraHeight->GetValue();
	// Note FRotator constructor argument order: (Pitch, Yaw, Roll).
	Settings.CameraRotation = FRotator(
		RotationPitchSpinBox->GetValue(),
		RotationYawSpinBox->GetValue(),
		RotationRollSpinBox->GetValue()
	);
	Settings.bIsOrthographic = IsOrthographicCheckbox->IsChecked();
	Settings.CameraFOV = CameraFOV->GetValue();
	Settings.bCaptureDynamicShadows = CaptureDynamicShadowsCheckbox->IsChecked();
	Settings.bOverrideWithHighQualitySettings = OverrideQualityCheckbox->IsChecked();
	if (Settings.bOverrideWithHighQualitySettings)
	{
		if (const UEnum* EnumPtr = StaticEnum<ESceneCaptureSource>(); EnumPtr && CurrentCaptureSource.IsValid())
		{
			if (const int64 EnumValue = EnumPtr->GetValueByNameString(*CurrentCaptureSource); EnumValue != INDEX_NONE)
			{
				Settings.CaptureSource = static_cast<ESceneCaptureSource>(EnumValue);
			}
		}

		Settings.AmbientOcclusionIntensity = AOIntensitySpinBox->GetValue();
		Settings.AmbientOcclusionQuality = AOQualitySpinBox->GetValue();
		Settings.ScreenSpaceReflectionIntensity = SSRIntensitySpinBox->GetValue();
		Settings.ScreenSpaceReflectionQuality = SSRQualitySpinBox->GetValue();
	}
	Settings.ActorTagFilter = FName(*ActorTagFilterTextBox->GetText().ToString());
	UE_LOG(OBPanoramicMinimapGenerator, Log,
		TEXT("Capture settings: Output=%dx%d, Tiling=%s, TileRes=%d, TileOverlap=%d, ImportAsset=%s, OutputPath=%s, FileName=%s"),
		Settings.OutputWidth, Settings.OutputHeight,
		Settings.bUseTiling ? TEXT("true") : TEXT("false"),
		Settings.TileResolution, Settings.TileOverlap,
		Settings.bImportAsTextureAsset ? TEXT("true") : TEXT("false"),
		*Settings.OutputPath, *Settings.FileName);

	ProgressBar->SetVisibility(EVisibility::Visible);
	StatusText->SetVisibility(EVisibility::Visible);
	OnCaptureProgress(LOCTEXT("StartingProcess", "Starting..."), 0.f, 0, 0);

	Manager->StartCaptureProcess(Settings);
	// Manager->StartSingleCaptureForValidation(Settings);

	return FReply::Handled();
}

// START FILTER LOGIC

// In MinimapGeneratorWindow.cpp

FReply SMinimapGeneratorWindow::OnAddSelectedToShowOnlyList()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor*> ActorsToAdd;
	SelectedActors->GetSelectedObjects<AActor>(ActorsToAdd);

	if (ActorsToAdd.Num() > 0)
	{
		// Use TSet to avoid O(N^2) duplicate checks.
		TSet<FString> ExistingActorLabels;
		for (const TSharedPtr<FString>& NamePtr : ShowOnlyActorNames)
		{
			if (NamePtr.IsValid())
			{
				ExistingActorLabels.Add(*NamePtr);
			}
		}

		bool bListChanged = false;
		for (const AActor* Actor : ActorsToAdd)
		{
			if (Actor)
			{
				// TSet::Contains() is very fast (average O(1)).
				if (const FString ActorLabel = Actor->GetActorLabel(); !ExistingActorLabels.Contains(ActorLabel))
				{
					ExistingActorLabels.Add(ActorLabel);
					ShowOnlyActorNames.Add(MakeShared<FString>(ActorLabel));
					const FSoftObjectPath ActorPath(Actor);
					Settings.ShowOnlyActors.Add(TSoftObjectPtr<AActor>(ActorPath));
					bListChanged = true;
				}
			}
		}

		if (bListChanged)
		{
			ShowOnlyActorsListView->RequestListRefresh();
			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Added %d selected actor(s) to ShowOnly list."),
				ActorsToAdd.Num());
		}
	}
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnAddSelectedToHiddenList()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor*> ActorsToAdd;
	SelectedActors->GetSelectedObjects<AActor>(ActorsToAdd);

	if (ActorsToAdd.Num() > 0)
	{
		// Same optimization: use TSet for efficient lookups.
		TSet<FString> ExistingActorLabels;
		for (const TSharedPtr<FString>& NamePtr : HiddenActorNames)
		{
			if (NamePtr.IsValid())
			{
				ExistingActorLabels.Add(*NamePtr);
			}
		}

		bool bListChanged = false;
		for (const AActor* Actor : ActorsToAdd)
		{
			if (Actor)
			{
				if (const FString ActorLabel = Actor->GetActorLabel(); !ExistingActorLabels.Contains(ActorLabel))
				{
					ExistingActorLabels.Add(ActorLabel);
					HiddenActorNames.Add(MakeShared<FString>(ActorLabel));
					const FSoftObjectPath ActorPath(Actor);
					Settings.HiddenActors.Add(TSoftObjectPtr<AActor>(ActorPath));
					bListChanged = true;
				}
			}
		}

		if (bListChanged)
		{
			HiddenActorsListView->RequestListRefresh();
			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Added %d selected actor(s) to Hidden list."),
				ActorsToAdd.Num());
		}
	}
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnRemoveSelectedFromShowOnlyList()
{
	// Get the currently selected items (TSharedPtr<FString>).
	if (TArray<TSharedPtr<FString>> SelectedItems = ShowOnlyActorsListView->GetSelectedItems(); SelectedItems.Num() > 0)
	{
		// Build a set of actor names to remove for fast lookup.
		TSet<FString> NamesToRemove;
		for (const TSharedPtr<FString>& Item : SelectedItems)
		{
			if (Item.IsValid())
			{
				NamesToRemove.Add(*Item);
			}
		}

		// Remove from UI list (ShowOnlyActorNames).
		ShowOnlyActorNames.RemoveAll([&NamesToRemove](const TSharedPtr<FString>& Item)
		{
			return Item.IsValid() && NamesToRemove.Contains(*Item);
		});

		// Remove from backend list (Settings.ShowOnlyActors).
		Settings.ShowOnlyActors.RemoveAll([&NamesToRemove](const TSoftObjectPtr<AActor>& ActorPtr)
		{
			if (const AActor* Actor = ActorPtr.Get())
			{
				return NamesToRemove.Contains(Actor->GetActorLabel());
			}
			return true; // Also remove invalid pointers if present.
		});

		// Request ListView refresh.
		ShowOnlyActorsListView->RequestListRefresh();
		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Removed %d actor(s) from ShowOnly list."), SelectedItems.Num());
	}

	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnRemoveSelectedFromHiddenList()
{
	if (TArray<TSharedPtr<FString>> SelectedItems = HiddenActorsListView->GetSelectedItems(); SelectedItems.Num() > 0)
	{
		TSet<FString> NamesToRemove;
		for (const TSharedPtr<FString>& Item : SelectedItems)
		{
			if (Item.IsValid())
			{
				NamesToRemove.Add(*Item);
			}
		}

		HiddenActorNames.RemoveAll([&NamesToRemove](const TSharedPtr<FString>& Item)
		{
			return Item.IsValid() && NamesToRemove.Contains(*Item);
		});

		Settings.HiddenActors.RemoveAll([&NamesToRemove](const TSoftObjectPtr<AActor>& ActorPtr)
		{
			if (const AActor* Actor = ActorPtr.Get())
			{
				return NamesToRemove.Contains(Actor->GetActorLabel());
			}
			return true;
		});

		HiddenActorsListView->RequestListRefresh();
		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Removed %d actor(s) from Hidden list."), SelectedItems.Num());
	}

	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnClearShowOnlyList()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Clearing ShowOnly list."));
	Settings.ShowOnlyActors.Empty();
	ShowOnlyActorNames.Empty();
	ShowOnlyActorsListView->RequestListRefresh();
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnClearHiddenList()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Clearing Hidden list."));
	Settings.HiddenActors.Empty();
	HiddenActorNames.Empty();
	HiddenActorsListView->RequestListRefresh();
	return FReply::Handled();
}

const UClass* SMinimapGeneratorWindow::GetSelectedActorClass() const
{
	return Settings.ActorClassFilter;
}

void SMinimapGeneratorWindow::OnActorClassChanged(const UClass* NewClass)
{
	Settings.ActorClassFilter = const_cast<UClass*>(NewClass);
}

// END FILTER LOGIC

FReply SMinimapGeneratorWindow::OnBrowseButtonClicked()
{
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindBestParentWindowForDialogs(AsShared());

		const void* ParentWindowHandle = (ParentWindow.IsValid())
			                                 ? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
			                                 : nullptr;

		FString OutFolderName;
		const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
			ParentWindowHandle,
			LOCTEXT("BrowseDialogTitle", "Choose an output directory").ToString(),
			OutputPath->GetText().ToString(), OutFolderName);

		if (bFolderSelected)
		{
			OutputPath->SetText(FText::FromString(OutFolderName));
			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Output directory selected: %s"), *OutFolderName);
		}
	}
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnGetBoundsFromSelectionClicked()
{
	if (!GEditor)
	{
		return FReply::Handled();
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor*> Actors;
	SelectedActors->GetSelectedObjects<AActor>(Actors);

	if (Actors.Num() > 0)
	{
		// UPGRADED BOUNDS LOGIC START
		// 1) Start with an invalid FBox accumulator.
		FBox CombinedBounds(ForceInit);

		// 2) Iterate over all selected actors.
		for (AActor* SelectedActor : Actors)
		{
			if (SelectedActor)
			{
				// FIX: Use GetComponentsBoundingBox correctly.
				// First 'true' includes non-collision visual components.
				// Second 'true' includes child actor components.
				const FBox ActorBounds = SelectedActor->GetComponentsBoundingBox(true, true);

				// Merge this actor bounds into the combined bounds.
				CombinedBounds += ActorBounds;
			}
		}

		// 3) Validate combined bounds (e.g. actors may have no components).
		if (CombinedBounds.IsValid)
		{
			// Update the SSpinBox values in the UI.
			BoundsMinX->SetValue(CombinedBounds.Min.X);
			BoundsMinY->SetValue(CombinedBounds.Min.Y);
			BoundsMinZ->SetValue(CombinedBounds.Min.Z);

			BoundsMaxX->SetValue(CombinedBounds.Max.X);
			BoundsMaxY->SetValue(CombinedBounds.Max.Y);
			BoundsMaxZ->SetValue(CombinedBounds.Max.Z);

			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Updated bounds from %d selected actor(s)."), Actors.Num());
		}
		else
		{
			UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Selected actor(s) do not have valid bounds."));
		}
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("No actors selected to get bounds from."));
	}

	return FReply::Handled();
}

bool SMinimapGeneratorWindow::IsPerspectiveMode() const
{
	return IsOrthographicCheckbox.IsValid() ? !IsOrthographicCheckbox->IsChecked() : false;
}

void SMinimapGeneratorWindow::OnProjectionTypeChanged(ECheckBoxState NewState)
{
	// No extra logic is needed here because FOV IsEnabled is bound to IsPerspectiveMode().
	// Slate updates the UI automatically when checkbox state changes.
}

void SMinimapGeneratorWindow::OnCaptureProgress(const FText& Status, float Percentage, int32 CurrentTile,
                                                int32 TotalTiles)
{
	// This function is called by manager delegates.
	ProgressBar->SetPercent(Percentage);
	StatusText->SetText(Status);
	UE_LOG(OBPanoramicMinimapGenerator, Verbose, TEXT("Capture progress: %s (%.2f%%), Tile %d/%d"),
		*Status.ToString(), Percentage * 100.0f, CurrentTile, TotalTiles);

	// Re-enable the button when the process finishes (success or failure).
	if (Percentage >= 1.0f)
	{
		StartButton->SetEnabled(true);
	}
}

#undef LOCTEXT_NAMESPACE
