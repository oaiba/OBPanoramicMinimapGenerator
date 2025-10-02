// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorWindow.h"

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

// In MinimapGeneratorWindow.cpp

void SMinimapGeneratorWindow::Construct(const FArguments& InArgs)
{
	// --- PHẦN KHỞI TẠO DỮ LIỆU (GIỮ NGUYÊN) ---
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

	// Đặt giá trị mặc định là 4096 (2^12)
	CurrentOutputWidth = ResolutionOptions[7];
	CurrentOutputHeight = ResolutionOptions[7];

	// === BẮT ĐẦU CẤU TRÚC LAYOUT MỚI ===
	ChildSlot
	[
		SNew(SVerticalBox)

		// Slot 1: Chứa SSplitter, chiếm phần lớn không gian
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)

			// --- PANE BÊN TRÁI (KHU VỰC CÀI ĐẶT) ---
			+ SSplitter::Slot()
			.Value(0.4f) // Tỉ lệ 40%
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(10)
				[
					SNew(SVerticalBox)

					// --- SECTION: REGION (dùng SExpandableArea) ---
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

					// --- SECTION: TILING (dùng SExpandableArea) ---
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
										SNew(STextBlock).Text(LOCTEXT("TileOverlapLabel", "Tile Overlap (px)"))
									]
									+ SHorizontalBox::Slot().FillWidth(0.6f)
									[
										SAssignNew(TileOverlap, SSpinBox<int32>).MinValue(0).MaxValue(1024).Value(64)
									]
								]
							]
						]
					]

					// --- SECTION: CAMERA (dùng SExpandableArea) ---
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

					// --- SECTION: QUALITY (dùng SExpandableArea) ---
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
								// ... (Các widget override AO, SSR, CaptureSource... giữ nguyên)
							]
						]
					]
				]
			]
			// --- PANE BÊN PHẢI (PREVIEW VÀ OUTPUT) ---
			+ SSplitter::Slot()
			.Value(0.6f) // Tỉ lệ 60%
			[
				SNew(SVerticalBox)

				// --- KHU VỰC PREVIEW ẢNH ---
				+ SVerticalBox::Slot()
				  .FillHeight(1.0f) // Cho khu vực preview chiếm phần lớn không gian
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

				// --- KHU VỰC CÀI ĐẶT OUTPUT (dùng SExpandableArea) ---
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
						+ SGridPanel::Slot(1, 6) // <<-- Đây là slot mới cho Checkbox
						[
							SAssignNew(ImportAsAssetCheckbox, SCheckBox).IsChecked(ECheckBoxState::Unchecked)
							[
								SNew(STextBlock).Text(LOCTEXT("ImportAsAssetLabel", "Import as Texture Asset"))
							]
						]
						+ SGridPanel::Slot(0, 7).HAlign(HAlign_Right).Padding(LabelPadding)
						// <<-- Slot mới cho nhãn Path
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AssetPathLabel", "Asset Path"))
							.Visibility(this, &SMinimapGeneratorWindow::GetAssetPathVisibility)
						]
						+ SGridPanel::Slot(1, 7) // <<-- Slot mới cho TextBox Path
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
		// Slot 2: Chứa các nút bấm và progress bar, nằm cố định ở dưới cùng
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
				SAssignNew(ProgressBar, SProgressBar).Percent(0.0f).Visibility(EVisibility::Collapsed)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
			[
				SAssignNew(StatusText, STextBlock).Text(FText::GetEmpty()).Visibility(EVisibility::Collapsed)
			]
		]
	];
}

void SMinimapGeneratorWindow::HandleCaptureCompleted(bool bSuccess, const FString& FinalImagePath)
{
	StartButton->SetEnabled(true);
	// Đặt một timer ngắn để ẩn thanh progress và text sau khi hoàn tất
	// Điều này cho phép người dùng nhìn thấy trạng thái "Done!" hoặc "Failed!" trong giây lát
	GEditor->GetTimerManager()->SetTimer(
		TimerHandle_HideProgress,
		[this]()
		{
			ProgressBar->SetVisibility(EVisibility::Collapsed);
			StatusText->SetVisibility(EVisibility::Collapsed);
		},
		2.0f, // Ẩn sau 2 giây
		false
	);

	if (bSuccess && !FinalImagePath.IsEmpty() && IFileManager::Get().FileExists(*FinalImagePath))
	{
		if (UTexture2D* LoadedTexture = FImageUtils::ImportFileAsTexture2D(FinalImagePath))
		{
			// 1. Tạo một FDeferredCleanupSlateBrush và lưu nó vào biến thành viên để quản lý vòng đời.
			//    Hàm CreateBrush trả về TSharedRef, chúng ta gán nó cho TSharedPtr.
			FinalImageBrushSource = FDeferredCleanupSlateBrush::CreateBrush(
				LoadedTexture,
				FVector2D(LoadedTexture->GetSizeX(), LoadedTexture->GetSizeY())
			);

			// 2. Lấy con trỏ FSlateBrush* từ bên trong nó và gán cho SImage.
			FinalImageView->SetImage(FinalImageBrushSource->GetSlateBrush());
			ImageContainer->SetVisibility(EVisibility::Visible);

			UE_LOG(LogTemp, Log, TEXT("Successfully loaded and displayed image from %s"), *FinalImagePath);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load texture from file %s"), *FinalImagePath);
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
		PickerArgs.bUseAlpha = true; // Cho phép chọn cả độ trong suốt
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
	return CurrentBackgroundMode == EMinimapBackgroundMode::SolidColor ? EVisibility::Visible : EVisibility::Collapsed;
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
	return ImportAsAssetCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMinimapGeneratorWindow::GetTilingSettingsVisibility() const
{
	return UseTilingCheckbox->IsChecked() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SMinimapGeneratorWindow::OnStartCaptureClicked()
{
	UE_LOG(LogTemp, Log, TEXT("Start Capture button clicked."));

	StartButton->SetEnabled(false);

	// Thu thập dữ liệu từ tất cả các widget UI
	FMinimapCaptureSettings Settings;
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
	// Chú ý thứ tự (Pitch, Yaw, Roll) của constructor FRotator
	Settings.CameraRotation = FRotator(
		RotationPitchSpinBox->GetValue(),
		RotationYawSpinBox->GetValue(),
		RotationRollSpinBox->GetValue()
	);
	Settings.bIsOrthographic = IsOrthographicCheckbox->IsChecked();
	Settings.CameraFOV = CameraFOV->GetValue();
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
	ProgressBar->SetVisibility(EVisibility::Visible);
	StatusText->SetVisibility(EVisibility::Visible);
	OnCaptureProgress(LOCTEXT("StartingProcess", "Starting..."), 0.f, 0, 0);

	Manager->StartCaptureProcess(Settings);
	// Manager->StartSingleCaptureForValidation(Settings);

	return FReply::Handled();
}

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
		// BẮT ĐẦU LOGIC NÂNG CẤP
		// 1. Tạo một FBox không hợp lệ để bắt đầu
		FBox CombinedBounds(ForceInit);

		// 2. Lặp qua tất cả các actor đã chọn
		for (AActor* SelectedActor : Actors)
		{
			if (SelectedActor)
			{
				// SỬA LỖI: Sử dụng hàm GetComponentsBoundingBox chính xác
				// Tham số 'true' đầu tiên để bao gồm cả các component không có va chạm (visuals)
				// Tham số 'true' thứ hai để bao gồm cả các Child Actor Component
				const FBox ActorBounds = SelectedActor->GetComponentsBoundingBox(true, true);

				// Thêm bounds của actor này vào bounds tổng hợp
				CombinedBounds += ActorBounds;
			}
		}

		// 3. Kiểm tra xem bounds tổng hợp có hợp lệ không (ví dụ: nếu actor không có component nào)
		if (CombinedBounds.IsValid)
		{
			// Cập nhật giá trị cho các ô SSpinBox trên UI
			BoundsMinX->SetValue(CombinedBounds.Min.X);
			BoundsMinY->SetValue(CombinedBounds.Min.Y);
			BoundsMinZ->SetValue(CombinedBounds.Min.Z);

			BoundsMaxX->SetValue(CombinedBounds.Max.X);
			BoundsMaxY->SetValue(CombinedBounds.Max.Y);
			BoundsMaxZ->SetValue(CombinedBounds.Max.Z);

			UE_LOG(LogTemp, Log, TEXT("Updated bounds from %d selected actor(s)."), Actors.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Selected actor(s) do not have valid bounds."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("No actors selected to get bounds from."));
	}

	return FReply::Handled();
}

bool SMinimapGeneratorWindow::IsPerspectiveMode() const
{
	return IsOrthographicCheckbox.IsValid() ? !IsOrthographicCheckbox->IsChecked() : false;
}

void SMinimapGeneratorWindow::OnProjectionTypeChanged(ECheckBoxState NewState)
{
	// Không cần làm gì ở đây, vì thuộc tính IsEnabled của ô FOV đã được bind với hàm IsPerspectiveMode()
	// Slate sẽ tự động cập nhật UI khi trạng thái của checkbox thay đổi.
}

void SMinimapGeneratorWindow::OnCaptureProgress(const FText& Status, float Percentage, int32 CurrentTile,
												int32 TotalTiles)
{
	// Đây là hàm được gọi từ Manager thông qua delegate
	ProgressBar->SetPercent(Percentage);
	StatusText->SetText(Status);

	// 5. Kích hoạt lại nút khi quá trình hoàn tất (hoặc thất bại)
	if (Percentage >= 1.0f)
	{
		StartButton->SetEnabled(true);
	}
}

#undef LOCTEXT_NAMESPACE
