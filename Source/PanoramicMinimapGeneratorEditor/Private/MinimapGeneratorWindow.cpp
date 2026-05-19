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
#include "Async/Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "SMinimapGeneratorWindow"

namespace
{
constexpr int64 MaxPreviewPixels = 4096LL * 4096LL;
}

void SMinimapGeneratorWindow::Construct(const FArguments& InArgs)
{
	// --- INITIAL DATA SETUP ---
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Constructing SMinimapGeneratorWindow."));
	Manager = TStrongObjectPtr<UMinimapGeneratorManager>(NewObject<UMinimapGeneratorManager>());
	Manager->OnProgress.AddSP(this, &SMinimapGeneratorWindow::OnCaptureProgress);
	Manager->OnCaptureComplete.AddSP(this, &SMinimapGeneratorWindow::HandleCaptureCompleted);
	OverlayLayers.AddDefaulted();

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
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
								[
									SAssignNew(ExportTileSetCheckbox, SCheckBox).IsChecked(ECheckBoxState::Checked)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("ExportTileSetLabel", "Export Tile Set + LOD instead of stitched texture"))
									]
								]
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
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SVerticalBox)
									.Visibility(this, &SMinimapGeneratorWindow::GetTileSetSettingsVisibility)
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
										[
											SNew(STextBlock).Text(LOCTEXT("TileWorldSizeLabel", "World Tile Size"))
										]
										+ SHorizontalBox::Slot().FillWidth(0.6f)
										[
											SAssignNew(TileSetWorldTileSize, SSpinBox<float>)
											.MinValue(100.0f)
											.MaxValue(100000.0f)
											.Value(1000.0f)
										]
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
										[
											SNew(STextBlock).Text(LOCTEXT("TileSetMaxLODLabel", "Max LOD"))
										]
										+ SHorizontalBox::Slot().FillWidth(0.6f)
										[
											SAssignNew(TileSetMaxLOD, SSpinBox<int32>)
											.MinValue(0)
											.MaxValue(8)
											.Value(3)
										]
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
										[
											SNew(STextBlock).Text(LOCTEXT("TileOverviewResLabel", "LOD0 Resolution"))
										]
										+ SHorizontalBox::Slot().FillWidth(0.6f)
										[
											SAssignNew(TileSetOverviewResolution, SSpinBox<int32>)
											.MinValue(256)
											.MaxValue(4096)
											.Value(1024)
										]
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
							+ SGridPanel::Slot(0, 5).HAlign(HAlign_Right).Padding(LabelPadding)
							[
								SNew(STextBlock).Text(LOCTEXT("ImageRotationLabel", "Image Rotation (Orthographic)"))
								.Visibility(this, &SMinimapGeneratorWindow::GetImageRotationVisibility)
							]
							+ SGridPanel::Slot(1, 5)
							[
								SNew(SVerticalBox)
								.Visibility(this, &SMinimapGeneratorWindow::GetImageRotationVisibility)
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
								[
									SAssignNew(ImageRotationSpinBox, SSpinBox<float>)
									.MinValue(-360.f)
									.MaxValue(360.f)
									.Value(0.f)
									.OnValueChanged(this, &SMinimapGeneratorWindow::OnImageRotationChanged)
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("Rot0", "0°"))
										.OnClicked(this, &SMinimapGeneratorWindow::OnRotationPresetClicked, 0.f)
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("Rot90", "90°"))
										.OnClicked(this, &SMinimapGeneratorWindow::OnRotationPresetClicked, 90.f)
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("Rot180", "180°"))
										.OnClicked(this, &SMinimapGeneratorWindow::OnRotationPresetClicked, 180.f)
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("Rot270", "270°"))
										.OnClicked(this, &SMinimapGeneratorWindow::OnRotationPresetClicked, 270.f)
									]
								]
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

					// --- SECTION: OVERLAY AUTHORING ---
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(false)
						.HeaderContent()
						[
							SNew(STextBlock).Text(LOCTEXT("OverlayHeader", "6. Overlay Editor")).Font(
								FAppStyle::GetFontStyle("BoldFont"))
						]
						.BodyContent()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SGridPanel).FillColumn(1, 1.0f)
								+ SGridPanel::Slot(0, 0).HAlign(HAlign_Right).Padding(LabelPadding)
								[
									SNew(STextBlock).Text(LOCTEXT("OverlayLabelLabel", "Label"))
								]
								+ SGridPanel::Slot(1, 0)
								[
									SAssignNew(OverlayLabelTextBox, SEditableTextBox)
									.Text(LOCTEXT("OverlayDefaultLabel", "POI"))
								]
								+ SGridPanel::Slot(0, 1).HAlign(HAlign_Right).Padding(LabelPadding)
								[
									SNew(STextBlock).Text(LOCTEXT("OverlayCategoryLabel", "Category"))
								]
								+ SGridPanel::Slot(1, 1)
								[
									SAssignNew(OverlayCategoryTextBox, SEditableTextBox)
									.Text(LOCTEXT("OverlayDefaultCategory", "Default"))
								]
								+ SGridPanel::Slot(0, 2).HAlign(HAlign_Right).Padding(LabelPadding)
								[
									SNew(STextBlock).Text(LOCTEXT("OverlayLineWidthLabel", "Line Width"))
								]
								+ SGridPanel::Slot(1, 2)
								[
									SAssignNew(OverlayLineWidthSpinBox, SSpinBox<float>)
									.MinValue(1.0f)
									.MaxValue(32.0f)
									.Value(3.0f)
								]
								+ SGridPanel::Slot(0, 3).HAlign(HAlign_Right).Padding(LabelPadding)
								[
									SNew(STextBlock).Text(LOCTEXT("OverlayColorLabel", "Color"))
								]
								+ SGridPanel::Slot(1, 3).Padding(0, 4)
								[
									SNew(SColorBlock)
									.Color(this, &SMinimapGeneratorWindow::GetSelectedOverlayColor)
									.OnMouseButtonDown(this, &SMinimapGeneratorWindow::OnOverlayColorBlockMouseButtonDown)
									.Size(FVector2D(100.f, 20.f))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SWrapBox).UseAllottedSize(true)
								+ SWrapBox::Slot().Padding(2)
								[
									SNew(SButton).Text(LOCTEXT("AddOverlayMarker", "Add Marker")).OnClicked(this, &SMinimapGeneratorWindow::OnAddMarkerFromSelectionClicked)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SNew(SButton).Text(LOCTEXT("AddOverlayZone", "Add Zone")).OnClicked(this, &SMinimapGeneratorWindow::OnAddZoneFromSelectionClicked)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SNew(SButton).Text(LOCTEXT("AddOverlayPath", "Add Path")).OnClicked(this, &SMinimapGeneratorWindow::OnAddPathFromSelectionClicked)
								]
								+ SWrapBox::Slot().Padding(2)
								[
									SNew(SButton).Text(LOCTEXT("AddOverlayFreehand", "Add Freehand")).OnClicked(this, &SMinimapGeneratorWindow::OnAddFreehandFromSelectionClicked)
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox)
								.HeightOverride(150.0f)
								[
									SAssignNew(OverlayElementsListView, SListView<TSharedPtr<FString>>)
									.ListItemsSource(&OverlayElementNames)
									.SelectionMode(ESelectionMode::Multi)
									.OnGenerateRow_Lambda(
										[](const TSharedPtr<FString>& InItem,
										   const TSharedRef<STableViewBase>& OwnerTable)
										{
											return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
												[
													SNew(STextBlock).Text(FText::FromString(InItem.IsValid() ? *InItem : FString()))
												];
										})
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("RemoveOverlay", "Remove Selected")).OnClicked(this, &SMinimapGeneratorWindow::OnRemoveSelectedOverlayClicked)
								]
								+ SHorizontalBox::Slot()
								[
									SNew(SButton).Text(LOCTEXT("ClearOverlay", "Clear Overlay")).OnClicked(this, &SMinimapGeneratorWindow::OnClearOverlayClicked)
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
								SAssignNew(PreviewSwitcher, SWidgetSwitcher)
								.WidgetIndex(0)
								+ SWidgetSwitcher::Slot()
								[
									SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
									[
										SAssignNew(FinalImageView, SImage)
									]
								]
								+ SWidgetSwitcher::Slot()
								[
									SNew(SScrollBox)
									.Orientation(Orient_Vertical)
									+ SScrollBox::Slot()
									[
										SNew(SScrollBox)
										.Orientation(Orient_Horizontal)
										+ SScrollBox::Slot()
										[
											SNew(SBox)
											.HAlign(HAlign_Center)
											.VAlign(VAlign_Center)
											.WidthOverride(this, &SMinimapGeneratorWindow::GetPreviewZoomedWidth)
											.HeightOverride(this, &SMinimapGeneratorWindow::GetPreviewZoomedHeight)
											[
												SAssignNew(ZoomedImageView, SImage)
											]
										]
									]
								]
							]
						]
					]
				]
				
				// --- PREVIEW INFO & ACTIONS (Phase 5 + Zoom) ---
				+ SVerticalBox::Slot().AutoHeight().Padding(5)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SAssignNew(ImageInfoText, STextBlock)
						.Text(FText::GetEmpty())
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0).VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("ZoomOutBtn", "-"))
						.OnClicked(this, &SMinimapGeneratorWindow::OnZoomOutClicked)
						.ToolTipText(LOCTEXT("ZoomOutTooltip", "Zoom Out"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SMinimapGeneratorWindow::GetZoomText)
						.MinDesiredWidth(40.0f)
						.Justification(ETextJustify::Center)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0).VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("ZoomInBtn", "+"))
						.OnClicked(this, &SMinimapGeneratorWindow::OnZoomInClicked)
						.ToolTipText(LOCTEXT("ZoomInTooltip", "Zoom In"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0).VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("Zoom100Btn", "100%"))
						.OnClicked(this, &SMinimapGeneratorWindow::OnZoom100Clicked)
						.ToolTipText(LOCTEXT("Zoom100Tooltip", "Actual Size"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0).VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("ZoomFitBtn", "Fit"))
						.OnClicked(this, &SMinimapGeneratorWindow::OnZoomFitClicked)
						.ToolTipText(LOCTEXT("ZoomFitTooltip", "Fit to Viewport"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(OpenFolderButton, SButton)
						.Text(LOCTEXT("OpenFolderBtn", "Open Output Folder"))
						.Visibility(EVisibility::Collapsed)
						.OnClicked(this, &SMinimapGeneratorWindow::OnOpenFolderClicked)
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
						// --- PRESETS ---
						+ SGridPanel::Slot(1, 0).Padding(0, 0, 0, 10)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
							[
								SNew(SButton).Text(LOCTEXT("Preset512", "512 (Mobile)")).OnClicked(this, &SMinimapGeneratorWindow::OnResolutionPresetClicked, 512)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
							[
								SNew(SButton).Text(LOCTEXT("Preset2048", "2048 (Standard)")).OnClicked(this, &SMinimapGeneratorWindow::OnResolutionPresetClicked, 2048)
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
							[
								SNew(SButton).Text(LOCTEXT("Preset4096", "4096 (High)")).OnClicked(this, &SMinimapGeneratorWindow::OnResolutionPresetClicked, 4096)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).Text(LOCTEXT("Preset8192", "8192 (Ultra)")).OnClicked(this, &SMinimapGeneratorWindow::OnResolutionPresetClicked, 8192)
							]
						]
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
						+ SGridPanel::Slot(1, 8)
						[
							SAssignNew(ExportDefinitionAssetCheckbox, SCheckBox).IsChecked(ECheckBoxState::Checked)
							[
								SNew(STextBlock).Text(LOCTEXT("ExportDefinitionAssetLabel", "Export Runtime Minimap DataAsset"))
							]
						]
						+ SGridPanel::Slot(0, 9).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DefinitionAssetPathLabel", "DataAsset Path"))
							.Visibility(this, &SMinimapGeneratorWindow::GetDefinitionAssetPathVisibility)
						]
						+ SGridPanel::Slot(1, 9)
						[
							SAssignNew(DefinitionAssetPathTextBox, SEditableTextBox)
							.Text(FText::FromString(TEXT("/Game/Minimaps/")))
							.Visibility(this, &SMinimapGeneratorWindow::GetDefinitionAssetPathVisibility)
						]
						+ SGridPanel::Slot(0, 10).HAlign(HAlign_Right).Padding(LabelPadding)
						[
							SNew(STextBlock).Text(LOCTEXT("BackgroundModeLabel", "Background Mode"))
						]
						+ SGridPanel::Slot(1, 10)
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
						+ SGridPanel::Slot(1, 11).Padding(0, 5, 0, 5)
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
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center)
				[
					SAssignNew(StartButton, SButton)
					.Text(LOCTEXT("StartCaptureButton", "Start Capture Process"))
					.OnClicked(this, &SMinimapGeneratorWindow::OnStartCaptureClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0)
				[
					SAssignNew(CancelButton, SButton)
					.Text(LOCTEXT("CancelCaptureButton", "Cancel"))
					.Visibility(EVisibility::Collapsed)
					.OnClicked(this, &SMinimapGeneratorWindow::OnCancelCaptureClicked)
				]
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

	// Load settings from config
	LoadSettings();
}

SMinimapGeneratorWindow::~SMinimapGeneratorWindow()
{
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(TimerHandle_HideProgress);
	}

	UnbindManagerDelegates();
	if (Manager.IsValid())
	{
		Manager->ShutdownCapture(false);
		Manager.Reset();
	}

	ReleasePreviewResources();
}

void SMinimapGeneratorWindow::UnbindManagerDelegates()
{
	if (Manager.IsValid())
	{
		Manager->OnProgress.RemoveAll(this);
		Manager->OnCaptureComplete.RemoveAll(this);
	}
}

void SMinimapGeneratorWindow::ReleasePreviewResources()
{
	if (FinalImageView.IsValid())
	{
		FinalImageView->SetImage(nullptr);
	}
	if (ZoomedImageView.IsValid())
	{
		ZoomedImageView->SetImage(nullptr);
	}

	FinalImageBrushSource.Reset();
	LastSavedImagePath.Reset();
}

void SMinimapGeneratorWindow::HandleCaptureCompleted(bool bSuccess, const FString& FinalImagePath)
{
	if (IsEngineExitRequested() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Capture completed. Success=%s, Path=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *FinalImagePath);
	StartButton->SetEnabled(true);
	CancelButton->SetVisibility(EVisibility::Collapsed);

	// Use a short timer to hide progress UI after completion.
	// This lets users briefly see the final status ("Done!" or "Failed!").
	TWeakPtr<SMinimapGeneratorWindow> WeakThis = SharedThis(this);
	GEditor->GetTimerManager()->SetTimer(
		TimerHandle_HideProgress,
		[WeakThis]()
		{
			if (IsEngineExitRequested() || !FSlateApplication::IsInitialized())
			{
				return;
			}

			if (const TSharedPtr<SMinimapGeneratorWindow> StrongThis = WeakThis.Pin())
			{
				if (StrongThis->ProgressBar.IsValid())
				{
					StrongThis->ProgressBar->SetVisibility(EVisibility::Hidden);
				}
				if (StrongThis->StatusText.IsValid())
				{
					StrongThis->StatusText->SetVisibility(EVisibility::Hidden);
				}
			}
		},
		2.0f, // Hide after 2 seconds
		false
	);

	if (bSuccess && !FinalImagePath.IsEmpty() && IFileManager::Get().FileExists(*FinalImagePath))
	{
		LastSavedImagePath = FinalImagePath;
		OpenFolderButton->SetVisibility(EVisibility::Visible);

		// Get file size
		const int64 FileSize = IFileManager::Get().FileSize(*FinalImagePath);
		const FString FileSizeStr = FString::Printf(TEXT("%.2f MB"), FileSize / (1024.0f * 1024.0f));
		ImageInfoText->SetText(FText::Format(LOCTEXT("ImageInfo", "{0}x{1} • {2} • PNG"),
			*CurrentOutputWidth, *CurrentOutputHeight, FText::FromString(FileSizeStr)));

		const int64 PreviewPixels = static_cast<int64>(*CurrentOutputWidth) * *CurrentOutputHeight;
		if (PreviewPixels > MaxPreviewPixels)
		{
			ReleasePreviewResources();
			OpenFolderButton->SetVisibility(EVisibility::Visible);
			ImageContainer->SetVisibility(EVisibility::Collapsed);
			ImageInfoText->SetText(FText::Format(LOCTEXT("ImageInfoPreviewSkipped", "{0}x{1} • {2} • PNG • preview skipped"),
				*CurrentOutputWidth, *CurrentOutputHeight, FText::FromString(FileSizeStr)));
			CollectGarbage(RF_NoFlags);
			return;
		}

		// Load and decompress the PNG on a background thread to avoid freezing the editor.
		// A 4096x4096 PNG can take 2-5 seconds to decompress — doing this on the game thread
		// would make the editor unresponsive.
		Async(EAsyncExecution::ThreadPool, [ImagePath = FinalImagePath]()
		{
			if (IsEngineExitRequested())
			{
				return TArray<uint8>();
			}

			TArray<uint8> PngData;
			if (!FFileHelper::LoadFileToArray(PngData, *ImagePath))
			{
				return TArray<uint8>();
			}

			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(PngData.GetData(), PngData.Num()))
			{
				return TArray<uint8>();
			}

			TArray<uint8> RawBGRA;
			ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawBGRA);
			return RawBGRA;
		})
		.Then([WeakThis, ImagePath = FinalImagePath, PreviewWidth = *CurrentOutputWidth, PreviewHeight = *CurrentOutputHeight](TFuture<TArray<uint8>> Future)
		{
			// Dispatch texture creation back to game thread (UObject creation must happen there).
			AsyncTask(ENamedThreads::GameThread, [WeakThis, ImagePath, PreviewWidth, PreviewHeight, RawData = Future.Get()]()
			{
				if (IsEngineExitRequested() || !FSlateApplication::IsInitialized())
				{
					return;
				}

				TSharedPtr<SMinimapGeneratorWindow> StrongThis = WeakThis.Pin();
				if (!StrongThis.IsValid() || RawData.Num() == 0)
				{
					if (StrongThis.IsValid())
					{
						UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to load or decompress preview image: %s"), *ImagePath);
						StrongThis->ImageContainer->SetVisibility(EVisibility::Collapsed);
					}
					return;
				}

				if (RawData.Num() != PreviewWidth * PreviewHeight * 4)
				{
					UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Preview image dimensions do not match decoded data: %s"), *ImagePath);
					StrongThis->ImageContainer->SetVisibility(EVisibility::Collapsed);
					return;
				}

				UTexture2D* LoadedTexture = UTexture2D::CreateTransient(PreviewWidth, PreviewHeight, PF_B8G8R8A8);
				if (!LoadedTexture)
				{
					UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to create transient texture for preview."));
					StrongThis->ImageContainer->SetVisibility(EVisibility::Collapsed);
					return;
				}

				void* MipData = LoadedTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
				FMemory::Memcpy(MipData, RawData.GetData(), RawData.Num());
				LoadedTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
				LoadedTexture->UpdateResource();

				StrongThis->FinalImageBrushSource = FDeferredCleanupSlateBrush::CreateBrush(
					LoadedTexture,
					FVector2D(LoadedTexture->GetSizeX(), LoadedTexture->GetSizeY())
				);
				StrongThis->FinalImageView->SetImage(StrongThis->FinalImageBrushSource->GetSlateBrush());
				StrongThis->ZoomedImageView->SetImage(StrongThis->FinalImageBrushSource->GetSlateBrush());

				// Reset zoom to "Fit" when a new capture finishes
				StrongThis->PreviewZoomFactor = 0.0f;
				StrongThis->PreviewSwitcher->SetActiveWidgetIndex(0);

				StrongThis->ImageContainer->SetVisibility(EVisibility::Visible);

				UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Successfully loaded and displayed preview image from %s (async)."), *ImagePath);
			});
		});
	}
	else
	{
		ImageContainer->SetVisibility(EVisibility::Collapsed);
		ImageInfoText->SetText(FText::GetEmpty());
		OpenFolderButton->SetVisibility(EVisibility::Collapsed);
	}
}

void SMinimapGeneratorWindow::OnCaptureProgress(const FText& Status, float Percentage, int32 CurrentTile, int32 TotalTiles)
{
	if (IsEngineExitRequested() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	if (ProgressBar.IsValid() && StatusText.IsValid())
	{
		ProgressBar->SetPercent(Percentage);
		StatusText->SetText(Status);
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
	const bool bNeedsAssetPath = ImportAsAssetCheckbox->IsChecked()
		|| (ExportTileSetCheckbox.IsValid() && ExportTileSetCheckbox->IsChecked());
	return bNeedsAssetPath ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SMinimapGeneratorWindow::GetDefinitionAssetPathVisibility() const
{
	const bool bNeedsDefinitionPath = ExportDefinitionAssetCheckbox->IsChecked()
		|| (ExportTileSetCheckbox.IsValid() && ExportTileSetCheckbox->IsChecked());
	return bNeedsDefinitionPath ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SMinimapGeneratorWindow::GetTilingSettingsVisibility() const
{
	return UseTilingCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMinimapGeneratorWindow::GetTileSetSettingsVisibility() const
{
	return ExportTileSetCheckbox.IsValid() && ExportTileSetCheckbox->IsChecked()
		       ? EVisibility::Visible
		       : EVisibility::Collapsed;
}

// START ZOOM FUNCTIONALITY
FReply SMinimapGeneratorWindow::OnZoomInClicked()
{
	if (PreviewZoomFactor == 0.0f) PreviewZoomFactor = 1.0f;
	PreviewZoomFactor = FMath::Min(PreviewZoomFactor * 1.25f, 10.0f);
	PreviewSwitcher->SetActiveWidgetIndex(1);
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnZoomOutClicked()
{
	if (PreviewZoomFactor == 0.0f) PreviewZoomFactor = 1.0f;
	PreviewZoomFactor = FMath::Max(PreviewZoomFactor / 1.25f, 0.1f);
	PreviewSwitcher->SetActiveWidgetIndex(1);
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnZoomFitClicked()
{
	PreviewZoomFactor = 0.0f;
	PreviewSwitcher->SetActiveWidgetIndex(0);
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnZoom100Clicked()
{
	PreviewZoomFactor = 1.0f;
	PreviewSwitcher->SetActiveWidgetIndex(1);
	return FReply::Handled();
}

FText SMinimapGeneratorWindow::GetZoomText() const
{
	if (PreviewZoomFactor == 0.0f) return LOCTEXT("ZoomFit", "Fit");
	return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(PreviewZoomFactor * 100.0f)));
}

FOptionalSize SMinimapGeneratorWindow::GetPreviewZoomedWidth() const
{
	if (FinalImageBrushSource.IsValid())
	{
		return FOptionalSize(FinalImageBrushSource->GetSlateBrush()->ImageSize.X * PreviewZoomFactor);
	}
	return FOptionalSize(512.f * PreviewZoomFactor);
}

FOptionalSize SMinimapGeneratorWindow::GetPreviewZoomedHeight() const
{
	if (FinalImageBrushSource.IsValid())
	{
		return FOptionalSize(FinalImageBrushSource->GetSlateBrush()->ImageSize.Y * PreviewZoomFactor);
	}
	return FOptionalSize(512.f * PreviewZoomFactor);
}
// END ZOOM FUNCTIONALITY

FReply SMinimapGeneratorWindow::OnResolutionPresetClicked(int32 Res)
{
	for (auto& Option : ResolutionOptions)
	{
		if (*Option == Res)
		{
			CurrentOutputWidth = Option;
			CurrentOutputHeight = Option;
			OutputWidthComboBox->SetSelectedItem(Option);
			OutputHeightComboBox->SetSelectedItem(Option);
			break;
		}
	}
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnOpenFolderClicked()
{
	if (!LastSavedImagePath.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*LastSavedImagePath);
	}
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnCancelCaptureClicked()
{
	if (Manager.IsValid())
	{
		Manager->CancelCapture();
	}
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnStartCaptureClicked()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Start Capture button clicked."));
	ReleasePreviewResources();
	if (ImageContainer.IsValid())
	{
		ImageContainer->SetVisibility(EVisibility::Collapsed);
	}
	CollectGarbage(RF_NoFlags);

	// Validation
	const FVector MinBounds(BoundsMinX->GetValue(), BoundsMinY->GetValue(), BoundsMinZ->GetValue());
	const FVector MaxBounds(BoundsMaxX->GetValue(), BoundsMaxY->GetValue(), BoundsMaxZ->GetValue());
	if (MinBounds.X >= MaxBounds.X || MinBounds.Y >= MaxBounds.Y)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("InvalidBoundsMsg", "Capture region is invalid. Min bounds must be less than Max bounds on X and Y axes.\n\nTip: Use 'Get Bounds from Selected Actor' to auto-fill."));
		return FReply::Handled();
	}

	FString PathStr = OutputPath->GetText().ToString();
	if (PathStr.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("EmptyPathMsg", "Output path cannot be empty."));
		return FReply::Handled();
	}

	if (!IFileManager::Get().DirectoryExists(*PathStr))
	{
		if (!IFileManager::Get().MakeDirectory(*PathStr, true))
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidPathMsg", "Output path does not exist and could not be created."));
			return FReply::Handled();
		}
	}

	StartButton->SetEnabled(false);
	CancelButton->SetVisibility(EVisibility::Visible);

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
	Settings.bExportDefinitionAsset = ExportDefinitionAssetCheckbox->IsChecked();
	Settings.bUseTiling = UseTilingCheckbox->IsChecked();
	Settings.bExportTileSet = Settings.bUseTiling && ExportTileSetCheckbox.IsValid() && ExportTileSetCheckbox->IsChecked();
	if (Settings.bExportTileSet)
	{
		Settings.bExportDefinitionAsset = true;
		Settings.AssetPath = AssetPathTextBox->GetText().ToString();
		Settings.DefinitionAssetPath = DefinitionAssetPathTextBox->GetText().ToString();
	}
	else if (Settings.bExportDefinitionAsset)
	{
		Settings.DefinitionAssetPath = DefinitionAssetPathTextBox->GetText().ToString();
		Settings.bImportAsTextureAsset = true;
		Settings.AssetPath = AssetPathTextBox->GetText().ToString();
	}
	Settings.TileResolution = TileResolution->GetValue();
	Settings.TileOverlap = TileOverlap->GetValue();
	if (Settings.bExportTileSet)
	{
		Settings.TileSetWorldTileSize = TileSetWorldTileSize.IsValid() ? TileSetWorldTileSize->GetValue() : 1000.0f;
		Settings.TileSetMaxLOD = TileSetMaxLOD.IsValid() ? TileSetMaxLOD->GetValue() : 3;
		Settings.TileSetOverviewResolution = TileSetOverviewResolution.IsValid() ? TileSetOverviewResolution->GetValue() : 1024;
	}
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
	Settings.OverlayLayers = OverlayLayers;
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

	SaveSettings(); // Save user preferences when a capture successfully starts

	Manager->StartCaptureProcess(Settings);
	return FReply::Handled();
}

// START SETTINGS PERSISTENCE
void SMinimapGeneratorWindow::SaveSettings() const
{
	const FString ConfigPath = GEditorPerProjectIni;
	const FString Section = TEXT("OBPanoramicMinimapGenerator");

	GConfig->SetFloat(*Section, TEXT("BoundsMinX"), BoundsMinX->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("BoundsMinY"), BoundsMinY->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("BoundsMinZ"), BoundsMinZ->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("BoundsMaxX"), BoundsMaxX->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("BoundsMaxY"), BoundsMaxY->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("BoundsMaxZ"), BoundsMaxZ->GetValue(), ConfigPath);

	GConfig->SetInt(*Section, TEXT("OutputWidth"), *CurrentOutputWidth, ConfigPath);
	GConfig->SetInt(*Section, TEXT("OutputHeight"), *CurrentOutputHeight, ConfigPath);
	GConfig->SetString(*Section, TEXT("OutputPath"), *OutputPath->GetText().ToString(), ConfigPath);
	GConfig->SetString(*Section, TEXT("FileName"), *FileName->GetText().ToString(), ConfigPath);
	GConfig->SetBool(*Section, TEXT("AutoFilename"), AutoFilenameCheckbox->IsChecked(), ConfigPath);
	GConfig->SetBool(*Section, TEXT("ImportAsAsset"), ImportAsAssetCheckbox->IsChecked(), ConfigPath);
	GConfig->SetString(*Section, TEXT("AssetPath"), *AssetPathTextBox->GetText().ToString(), ConfigPath);
	GConfig->SetBool(*Section, TEXT("ExportDefinitionAsset"), ExportDefinitionAssetCheckbox->IsChecked(), ConfigPath);
	GConfig->SetString(*Section, TEXT("DefinitionAssetPath"), *DefinitionAssetPathTextBox->GetText().ToString(), ConfigPath);

	GConfig->SetInt(*Section, TEXT("BackgroundMode"), static_cast<int32>(CurrentBackgroundMode), ConfigPath);
	GConfig->SetColor(*Section, TEXT("BackgroundColor"), SelectedBackgroundColor.ToFColor(true), ConfigPath);

	GConfig->SetBool(*Section, TEXT("UseTiling"), UseTilingCheckbox->IsChecked(), ConfigPath);
	GConfig->SetBool(*Section, TEXT("ExportTileSet"), ExportTileSetCheckbox.IsValid() && ExportTileSetCheckbox->IsChecked(), ConfigPath);
	GConfig->SetInt(*Section, TEXT("TileResolution"), TileResolution->GetValue(), ConfigPath);
	GConfig->SetInt(*Section, TEXT("TileOverlap"), TileOverlap->GetValue(), ConfigPath);
	if (TileSetWorldTileSize.IsValid())
	{
		GConfig->SetFloat(*Section, TEXT("TileSetWorldTileSize"), TileSetWorldTileSize->GetValue(), ConfigPath);
	}
	if (TileSetMaxLOD.IsValid())
	{
		GConfig->SetInt(*Section, TEXT("TileSetMaxLOD"), TileSetMaxLOD->GetValue(), ConfigPath);
	}
	if (TileSetOverviewResolution.IsValid())
	{
		GConfig->SetInt(*Section, TEXT("TileSetOverviewResolution"), TileSetOverviewResolution->GetValue(), ConfigPath);
	}

	GConfig->SetFloat(*Section, TEXT("CameraHeight"), CameraHeight->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("RotationPitch"), RotationPitchSpinBox->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("RotationYaw"), RotationYawSpinBox->GetValue(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("RotationRoll"), RotationRollSpinBox->GetValue(), ConfigPath);
	GConfig->SetBool(*Section, TEXT("IsOrthographic"), IsOrthographicCheckbox->IsChecked(), ConfigPath);
	GConfig->SetFloat(*Section, TEXT("CameraFOV"), CameraFOV->GetValue(), ConfigPath);

	GConfig->Flush(false, ConfigPath);
}

void SMinimapGeneratorWindow::LoadSettings()
{
	const FString ConfigPath = GEditorPerProjectIni;
	const FString Section = TEXT("OBPanoramicMinimapGenerator");

	float FloatVal = 0.0f;
	int32 IntVal = 0;
	FString StringVal;
	bool bBoolVal = false;
	FColor ColorVal;

	if (GConfig->GetFloat(*Section, TEXT("BoundsMinX"), FloatVal, ConfigPath)) BoundsMinX->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("BoundsMinY"), FloatVal, ConfigPath)) BoundsMinY->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("BoundsMinZ"), FloatVal, ConfigPath)) BoundsMinZ->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("BoundsMaxX"), FloatVal, ConfigPath)) BoundsMaxX->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("BoundsMaxY"), FloatVal, ConfigPath)) BoundsMaxY->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("BoundsMaxZ"), FloatVal, ConfigPath)) BoundsMaxZ->SetValue(FloatVal);

	if (GConfig->GetInt(*Section, TEXT("OutputWidth"), IntVal, ConfigPath))
	{
		for (auto& Res : ResolutionOptions)
		{
			if (*Res == IntVal) { CurrentOutputWidth = Res; OutputWidthComboBox->SetSelectedItem(Res); break; }
		}
	}
	if (GConfig->GetInt(*Section, TEXT("OutputHeight"), IntVal, ConfigPath))
	{
		for (auto& Res : ResolutionOptions)
		{
			if (*Res == IntVal) { CurrentOutputHeight = Res; OutputHeightComboBox->SetSelectedItem(Res); break; }
		}
	}

	if (GConfig->GetString(*Section, TEXT("OutputPath"), StringVal, ConfigPath)) OutputPath->SetText(FText::FromString(StringVal));
	if (GConfig->GetString(*Section, TEXT("FileName"), StringVal, ConfigPath)) FileName->SetText(FText::FromString(StringVal));
	if (GConfig->GetBool(*Section, TEXT("AutoFilename"), bBoolVal, ConfigPath)) AutoFilenameCheckbox->SetIsChecked(bBoolVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	if (GConfig->GetBool(*Section, TEXT("ImportAsAsset"), bBoolVal, ConfigPath)) ImportAsAssetCheckbox->SetIsChecked(bBoolVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	if (GConfig->GetString(*Section, TEXT("AssetPath"), StringVal, ConfigPath)) AssetPathTextBox->SetText(FText::FromString(StringVal));
	if (GConfig->GetBool(*Section, TEXT("ExportDefinitionAsset"), bBoolVal, ConfigPath)) ExportDefinitionAssetCheckbox->SetIsChecked(bBoolVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	if (GConfig->GetString(*Section, TEXT("DefinitionAssetPath"), StringVal, ConfigPath)) DefinitionAssetPathTextBox->SetText(FText::FromString(StringVal));

	if (GConfig->GetInt(*Section, TEXT("BackgroundMode"), IntVal, ConfigPath)) CurrentBackgroundMode = static_cast<EMinimapBackgroundMode>(IntVal);
	if (GConfig->GetColor(*Section, TEXT("BackgroundColor"), ColorVal, ConfigPath)) SelectedBackgroundColor = FLinearColor(ColorVal);

	if (GConfig->GetBool(*Section, TEXT("UseTiling"), bBoolVal, ConfigPath)) UseTilingCheckbox->SetIsChecked(bBoolVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	if (GConfig->GetBool(*Section, TEXT("ExportTileSet"), bBoolVal, ConfigPath) && ExportTileSetCheckbox.IsValid()) ExportTileSetCheckbox->SetIsChecked(bBoolVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	if (GConfig->GetInt(*Section, TEXT("TileResolution"), IntVal, ConfigPath)) TileResolution->SetValue(IntVal);
	if (GConfig->GetInt(*Section, TEXT("TileOverlap"), IntVal, ConfigPath)) TileOverlap->SetValue(IntVal);
	if (GConfig->GetFloat(*Section, TEXT("TileSetWorldTileSize"), FloatVal, ConfigPath) && TileSetWorldTileSize.IsValid()) TileSetWorldTileSize->SetValue(FloatVal);
	if (GConfig->GetInt(*Section, TEXT("TileSetMaxLOD"), IntVal, ConfigPath) && TileSetMaxLOD.IsValid()) TileSetMaxLOD->SetValue(IntVal);
	if (GConfig->GetInt(*Section, TEXT("TileSetOverviewResolution"), IntVal, ConfigPath) && TileSetOverviewResolution.IsValid()) TileSetOverviewResolution->SetValue(IntVal);

	if (GConfig->GetFloat(*Section, TEXT("CameraHeight"), FloatVal, ConfigPath)) CameraHeight->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("RotationPitch"), FloatVal, ConfigPath)) RotationPitchSpinBox->SetValue(FloatVal);
	if (GConfig->GetFloat(*Section, TEXT("RotationYaw"), FloatVal, ConfigPath)) 
	{
		RotationYawSpinBox->SetValue(FloatVal);
		if (ImageRotationSpinBox.IsValid())
		{
			ImageRotationSpinBox->SetValue(FloatVal);
		}
	}
	if (GConfig->GetFloat(*Section, TEXT("RotationRoll"), FloatVal, ConfigPath)) RotationRollSpinBox->SetValue(FloatVal);
	if (GConfig->GetBool(*Section, TEXT("IsOrthographic"), bBoolVal, ConfigPath)) IsOrthographicCheckbox->SetIsChecked(bBoolVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	if (GConfig->GetFloat(*Section, TEXT("CameraFOV"), FloatVal, ConfigPath)) CameraFOV->SetValue(FloatVal);
}
// END SETTINGS PERSISTENCE

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

EVisibility SMinimapGeneratorWindow::GetImageRotationVisibility() const
{
	return IsOrthographicCheckbox.IsValid() && IsOrthographicCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SMinimapGeneratorWindow::OnImageRotationChanged(float NewValue)
{
	if (RotationYawSpinBox.IsValid())
	{
		RotationYawSpinBox->SetValue(NewValue);
	}
}

FReply SMinimapGeneratorWindow::OnRotationPresetClicked(float Angle)
{
	if (ImageRotationSpinBox.IsValid())
	{
		ImageRotationSpinBox->SetValue(Angle);
		OnImageRotationChanged(Angle);
	}
	return FReply::Handled();
}

FLinearColor SMinimapGeneratorWindow::GetSelectedOverlayColor() const
{
	return SelectedOverlayColor;
}

void SMinimapGeneratorWindow::OnOverlayColorChanged(const FLinearColor NewColor)
{
	SelectedOverlayColor = NewColor;
}

FReply SMinimapGeneratorWindow::OnOverlayColorBlockMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FColorPickerArgs PickerArgs;
		PickerArgs.bUseAlpha = true;
		PickerArgs.DisplayGamma = TAttribute<float>(2.2f);
		PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SMinimapGeneratorWindow::OnOverlayColorChanged);
		PickerArgs.InitialColor = SelectedOverlayColor;
		PickerArgs.ParentWidget = AsShared();
		OpenColorPicker(PickerArgs);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FMinimapOverlayElement SMinimapGeneratorWindow::MakeOverlayElementFromSelection(const EMinimapOverlayElementType Type) const
{
	FMinimapOverlayElement Element;
	Element.Type = Type;
	Element.Id = FName(*FGuid::NewGuid().ToString(EGuidFormats::Short));
	Element.Label = OverlayLabelTextBox.IsValid() ? OverlayLabelTextBox->GetText() : LOCTEXT("DefaultOverlayElementLabel", "Overlay");
	Element.Category = OverlayCategoryTextBox.IsValid() ? FName(*OverlayCategoryTextBox->GetText().ToString()) : TEXT("Default");
	Element.Style.Color = SelectedOverlayColor;
	Element.Style.Opacity = SelectedOverlayColor.A;
	Element.Style.LineWidth = OverlayLineWidthSpinBox.IsValid() ? OverlayLineWidthSpinBox->GetValue() : 3.0f;

	TArray<AActor*> Actors;
	if (GEditor && GEditor->GetSelectedActors())
	{
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Actors);
	}

	if (Type == EMinimapOverlayElementType::Zone)
	{
		FBox Bounds(ForceInit);
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Bounds += Actor->GetComponentsBoundingBox(true, true);
			}
		}

		if (Bounds.IsValid)
		{
			Element.WorldPoints = {
				FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z),
				FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Min.Z),
				FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Min.Z),
				FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z)
			};
		}
	}
	else
	{
		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Element.WorldPoints.Add(Actor->GetActorLocation());
			}
		}
	}

	return Element;
}

void SMinimapGeneratorWindow::RefreshOverlayList()
{
	OverlayElementNames.Empty();
	if (OverlayLayers.Num() == 0)
	{
		OverlayLayers.AddDefaulted();
	}

	for (const FMinimapOverlayElement& Element : OverlayLayers[0].Elements)
	{
		const FString TypeName = StaticEnum<EMinimapOverlayElementType>()->GetDisplayNameTextByValue(static_cast<int64>(Element.Type)).ToString();
		OverlayElementNames.Add(MakeShared<FString>(FString::Printf(TEXT("%s | %s | %s | %d point(s)"),
			*Element.Id.ToString(),
			*TypeName,
			*Element.Label.ToString(),
			Element.WorldPoints.Num())));
	}

	if (OverlayElementsListView.IsValid())
	{
		OverlayElementsListView->RequestListRefresh();
	}
}

FReply SMinimapGeneratorWindow::OnAddMarkerFromSelectionClicked()
{
	FMinimapOverlayElement Element = MakeOverlayElementFromSelection(EMinimapOverlayElementType::Marker);
	if (Element.WorldPoints.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelectionForMarker", "Select at least one actor to create marker points."));
		return FReply::Handled();
	}

	if (OverlayLayers.Num() == 0) OverlayLayers.AddDefaulted();
	OverlayLayers[0].Elements.Add(MoveTemp(Element));
	RefreshOverlayList();
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnAddZoneFromSelectionClicked()
{
	FMinimapOverlayElement Element = MakeOverlayElementFromSelection(EMinimapOverlayElementType::Zone);
	if (Element.WorldPoints.Num() < 3)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelectionForZone", "Select actor(s) with valid bounds to create a zone."));
		return FReply::Handled();
	}

	if (OverlayLayers.Num() == 0) OverlayLayers.AddDefaulted();
	OverlayLayers[0].Elements.Add(MoveTemp(Element));
	RefreshOverlayList();
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnAddPathFromSelectionClicked()
{
	FMinimapOverlayElement Element = MakeOverlayElementFromSelection(EMinimapOverlayElementType::Path);
	if (Element.WorldPoints.Num() < 2)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelectionForPath", "Select at least two actors to create a path."));
		return FReply::Handled();
	}

	if (OverlayLayers.Num() == 0) OverlayLayers.AddDefaulted();
	OverlayLayers[0].Elements.Add(MoveTemp(Element));
	RefreshOverlayList();
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnAddFreehandFromSelectionClicked()
{
	FMinimapOverlayElement Element = MakeOverlayElementFromSelection(EMinimapOverlayElementType::Freehand);
	if (Element.WorldPoints.Num() < 2)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelectionForFreehand", "Select at least two actors to create a freehand/polyline stroke."));
		return FReply::Handled();
	}

	if (OverlayLayers.Num() == 0) OverlayLayers.AddDefaulted();
	OverlayLayers[0].Elements.Add(MoveTemp(Element));
	RefreshOverlayList();
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnRemoveSelectedOverlayClicked()
{
	if (!OverlayElementsListView.IsValid() || OverlayLayers.Num() == 0)
	{
		return FReply::Handled();
	}

	TSet<FString> SelectedIds;
	for (const TSharedPtr<FString>& SelectedItem : OverlayElementsListView->GetSelectedItems())
	{
		if (SelectedItem.IsValid())
		{
			FString IdPart;
			FString Remainder;
			if (SelectedItem->Split(TEXT(" | "), &IdPart, &Remainder))
			{
				SelectedIds.Add(IdPart);
			}
		}
	}

	OverlayLayers[0].Elements.RemoveAll([&SelectedIds](const FMinimapOverlayElement& Element)
	{
		return SelectedIds.Contains(Element.Id.ToString());
	});
	RefreshOverlayList();
	return FReply::Handled();
}

FReply SMinimapGeneratorWindow::OnClearOverlayClicked()
{
	OverlayLayers.Empty();
	OverlayLayers.AddDefaulted();
	RefreshOverlayList();
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
