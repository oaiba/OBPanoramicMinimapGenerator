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
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScaleBox.h"
#define LOCTEXT_NAMESPACE "SMinimapGeneratorWindow"


void SMinimapGeneratorWindow::Construct(const FArguments& InArgs)
{
	// Tạo đối tượng Manager và bind delegate (giữ nguyên)
	Manager = TStrongObjectPtr<UMinimapGeneratorManager>(NewObject<UMinimapGeneratorManager>());
	Manager->OnProgress.AddSP(this, &SMinimapGeneratorWindow::OnCaptureProgress);
	Manager->OnCaptureComplete.AddSP(this, &SMinimapGeneratorWindow::HandleCaptureCompleted);

	const FString DefaultPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

	// Khai báo một biến để dễ dàng tái sử dụng padding cho các nhãn
	const FMargin LabelPadding(5, 5, 10, 5);

	if (const UEnum* CaptureSourceEnum = StaticEnum<ESceneCaptureSource>())
	{
		for (int32 i = 0; i < CaptureSourceEnum->NumEnums() - 1; ++i) // -1 để bỏ qua giá trị MAX
		{
			CaptureSourceOptions.Add(MakeShared<FString>(CaptureSourceEnum->GetDisplayNameTextByIndex(i).ToString()));
		}
		// Đặt giá trị mặc định
		CurrentCaptureSource = CaptureSourceOptions[0]; // SCS_FinalColorHDR
	}

	ChildSlot
	[
		// Wrap everything in a ScrollBox to handle vertical overflow
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			// The main layout is now a GridPanel for clean, two-column alignment
			+ SVerticalBox::Slot()
			.Padding(10)
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.0f) // Cho phép cột thứ hai (widget) co giãn theo chiều rộng

				// --- SECTION: REGION ---
				+ SGridPanel::Slot(0, 0).ColumnSpan(2).Padding(5)
				[
					SNew(STextBlock).Text(LOCTEXT("RegionHeader", "1. Capture Region"))
				]

				// Min Bounds
				+ SGridPanel::Slot(0, 1).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("MinBoundsLabel", "Min Bounds"))
				]
				+ SGridPanel::Slot(1, 1)
				[
					// Use a WrapBox for responsive bounds input
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

				// Max Bounds
				+ SGridPanel::Slot(0, 2).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("MaxBoundsLabel", "Max Bounds"))
				]
				+ SGridPanel::Slot(1, 2)
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

				// Get Bounds Button
				+ SGridPanel::Slot(1, 3).HAlign(HAlign_Right).Padding(5)
				[
					SNew(SButton)
					.Text(LOCTEXT("GetBoundsButton", "Get Bounds from Selected Actor"))
					.OnClicked(this, &SMinimapGeneratorWindow::OnGetBoundsFromSelectionClicked)
				]

				// --- SECTION: OUTPUT ---
				+ SGridPanel::Slot(0, 4).ColumnSpan(2).Padding(5, 15, 5, 5)
				[
					SNew(STextBlock).Text(LOCTEXT("OutputHeader", "2. Output Settings"))
				]
				// Output Width
				+ SGridPanel::Slot(0, 5).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("OutputWidthLabel", "Output Width"))
				]
				+ SGridPanel::Slot(1, 5)
				[
					SAssignNew(OutputWidth, SSpinBox<int32>).MinValue(256).MaxValue(16384).Value(4096)
				]
				// Output Height
				+ SGridPanel::Slot(0, 6).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("OutputHeightLabel", "Output Height"))
				]
				+ SGridPanel::Slot(1, 6)
				[
					SAssignNew(OutputHeight, SSpinBox<int32>).MinValue(256).MaxValue(16384).Value(4096)
				]
				// Output Path
				+ SGridPanel::Slot(0, 7).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("OutputPathLabel", "Output Path"))
				]
				+ SGridPanel::Slot(1, 7)
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
				// File Name
				+ SGridPanel::Slot(0, 8).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("FileNameLabel", "File Name"))
				]
				+ SGridPanel::Slot(1, 8)
				[
					SAssignNew(FileName, SEditableTextBox).Text(LOCTEXT("DefaultFileName", "Minimap_Result"))
				]
				// Auto Filename
				+ SGridPanel::Slot(1, 9)
				[
					SAssignNew(AutoFilenameCheckbox, SCheckBox).IsChecked(ECheckBoxState::Checked)
					[
						SNew(STextBlock).Text(LOCTEXT("AutoFilenameLabel", "Auto-Generate Filename with Timestamp"))
					]
				]

				// --- SECTION: TILING ---
				+ SGridPanel::Slot(0, 10).ColumnSpan(2).Padding(5, 15, 5, 5)
				[
					SNew(STextBlock).Text(LOCTEXT("TilingHeader", "3. Tiling Settings"))
				]
				// Tile Resolution
				+ SGridPanel::Slot(0, 11).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("TileResLabel", "Tile Resolution"))
				]
				+ SGridPanel::Slot(1, 11)
				[
					SAssignNew(TileResolution, SSpinBox<int32>).MinValue(256).MaxValue(8192).Value(2048)
				]
				// Tile Overlap
				+ SGridPanel::Slot(0, 12).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("TileOverlapLabel", "Tile Overlap (px)"))
				]
				+ SGridPanel::Slot(1, 12)
				[
					SAssignNew(TileOverlap, SSpinBox<int32>).MinValue(0).MaxValue(1024).Value(64)
				]

				// --- SECTION: CAMERA ---
				+ SGridPanel::Slot(0, 13).ColumnSpan(2).Padding(5, 15, 5, 5)
				[
					SNew(STextBlock).Text(LOCTEXT("CameraHeader", "4. Camera Settings"))
				]
				// Camera Height
				+ SGridPanel::Slot(0, 14).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("CameraHeightLabel", "Camera Height"))
				]
				+ SGridPanel::Slot(1, 14)
				[
					SAssignNew(CameraHeight, SSpinBox<float>).MinValue(100.f).MaxValue(999999.f).Value(50000.f)
				]
				// Camera Rotation
				+ SGridPanel::Slot(0, 15).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("CameraRotationLabel", "Camera Rotation (Roll, Pitch, Yaw)"))
				]
				+ SGridPanel::Slot(1, 15)
				[
					SNew(SWrapBox).UseAllottedSize(true)
					+ SWrapBox::Slot().Padding(2)
					[
						SAssignNew(RotationRollSpinBox, SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value(-180.f)
					]
					+ SWrapBox::Slot().Padding(2)
					[
						SAssignNew(RotationPitchSpinBox, SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value(-90.f)
					]
					+ SWrapBox::Slot().Padding(2)
					[
						SAssignNew(RotationYawSpinBox, SSpinBox<float>).MinValue(-360.f).MaxValue(360.f).Value(0.f)
					]
				]
				// Orthographic Checkbox
				+ SGridPanel::Slot(1, 16)
				[
					SAssignNew(IsOrthographicCheckbox, SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged(this, &SMinimapGeneratorWindow::OnProjectionTypeChanged)
					[
						SNew(STextBlock).Text(LOCTEXT("IsOrthoLabel", "Use Orthographic Projection"))
					]
				]
				// Camera FOV
				+ SGridPanel::Slot(0, 17).HAlign(HAlign_Right).Padding(LabelPadding)
				[
					SNew(STextBlock).Text(LOCTEXT("CameraFOVLabel", "Camera FOV (Perspective only)"))
				]
				+ SGridPanel::Slot(1, 17)
				[
					SAssignNew(CameraFOV, SSpinBox<float>).MinValue(10.f).MaxValue(170.f).Value(90.f).IsEnabled(
						this, &SMinimapGeneratorWindow::IsPerspectiveMode)
				]

				// --- SECTION: QUALITY ---
				+ SGridPanel::Slot(0, 18).ColumnSpan(2).Padding(5, 15, 5, 5)
				[
					SNew(STextBlock).Text(LOCTEXT("QualityHeader", "5. Quality Settings"))
				]
				+ SGridPanel::Slot(1, 19)
				[
					SAssignNew(OverrideQualityCheckbox, SCheckBox).IsChecked(ECheckBoxState::Unchecked)
					[
						SNew(STextBlock).Text(LOCTEXT("OverrideQualityLabel", "Override With High Quality Settings"))
					]
				]
				// === THÊM KHỐI UI ĐỘNG MỚI ===
				+ SGridPanel::Slot(0, 20).ColumnSpan(2).Padding(20, 5, 5, 5)
				[
					SNew(SVerticalBox)
					.Visibility(this, &SMinimapGeneratorWindow::GetOverrideSettingsVisibility)
					// <<-- ĐIỀU KHIỂN HIỂN THỊ

					// Capture Source
					+ SVerticalBox::Slot().AutoHeight().Padding(5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("CaptureSourceLabel", "Capture Source"))
						]
						+ SHorizontalBox::Slot().FillWidth(0.6f)
						[
							SAssignNew(CaptureSourceComboBox, SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&CaptureSourceOptions)
							.OnSelectionChanged(this, &SMinimapGeneratorWindow::OnCaptureSourceChanged)
							.OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption)
							{
								return SNew(STextBlock).Text(FText::FromString(*InOption));
							})
							[
								SNew(STextBlock).Text_Lambda(
									[this] { return FText::FromString(*CurrentCaptureSource); })
							]
						]
					]
					// Ambient Occlusion Intensity
					+ SVerticalBox::Slot().AutoHeight().Padding(5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("AOIntensityLabel", "AO Intensity"))
						]
						+ SHorizontalBox::Slot().FillWidth(0.6f)
						[
							SAssignNew(AOIntensitySpinBox, SSpinBox<float>)
							.MinValue(0.f)
							.MaxValue(1.f)
							.Value(0.5f)
							.Delta(0.05f)
						]
					]
					// Ambient Occlusion Quality
					+ SVerticalBox::Slot().AutoHeight().Padding(5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("AOQualityLabel", "AO Quality"))
						]
						+ SHorizontalBox::Slot().FillWidth(0.6f)
						[
							SAssignNew(AOQualitySpinBox, SSpinBox<float>).MinValue(0.f).MaxValue(100.f).Value(100.f)
						]
					]
					// Screen Space Reflection Intensity
					+ SVerticalBox::Slot().AutoHeight().Padding(5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SSRIntensityLabel", "SSR Intensity"))
						]
						+ SHorizontalBox::Slot().FillWidth(0.6f)
						[
							SAssignNew(SSRIntensitySpinBox, SSpinBox<float>).MinValue(0.f).MaxValue(100.f).Value(100.f)
						]
					]
					// Screen Space Reflection Quality
					+ SVerticalBox::Slot().AutoHeight().Padding(5)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SSRQualityLabel", "SSR Quality"))
						]
						+ SHorizontalBox::Slot().FillWidth(0.6f)
						[
							SAssignNew(SSRQualitySpinBox, SSpinBox<float>).MinValue(0.f).MaxValue(100.f).Value(100.f)
						]
					]
				]
			]

			// --- SECTION: ACTION & PROGRESS ---
			// Các nút bấm và thanh progress bar được đặt bên ngoài GridPanel để chúng luôn cố định ở cuối
			+ SVerticalBox::Slot().AutoHeight().Padding(10, 20, 10, 5)
			[
				SAssignNew(StartButton, SButton)
				.Text(LOCTEXT("StartCaptureButton", "Start Capture Process"))
				.HAlign(HAlign_Center)
				.OnClicked(this, &SMinimapGeneratorWindow::OnStartCaptureClicked)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(5)
			[
				SAssignNew(ProgressBar, SProgressBar).Percent(0.0f).Visibility(EVisibility::Collapsed)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(5)
			[
				SAssignNew(StatusText, STextBlock).Text(FText::GetEmpty()).Visibility(EVisibility::Collapsed)
			]
			+ SVerticalBox::Slot()
			.Padding(10)
			.AutoHeight()
			[
				SAssignNew(ImageContainer, SBox)
												.Visibility(EVisibility::Collapsed) // Ban đầu ẩn đi
												.HAlign(HAlign_Center)
												.MaxDesiredHeight(512.f) // Vẫn giữ để giới hạn kích thước tối đa
				[
					// SScaleBox sẽ tự động xử lý việc co giãn theo tỉ lệ
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit) // Đây là thuộc tính quan trọng nhất
					[
						SAssignNew(FinalImageView, SImage)
					]
				]
			]
		]
	];
}

void SMinimapGeneratorWindow::HandleCaptureCompleted(bool bSuccess, const FString& FinalImagePath)
{
	StartButton->SetEnabled(true);

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
	Settings.OutputWidth = OutputWidth->GetValue();
	Settings.OutputHeight = OutputHeight->GetValue();
	Settings.OutputPath = OutputPath->GetText().ToString();
	Settings.FileName = FileName->GetText().ToString();
	Settings.bUseAutoFilename = AutoFilenameCheckbox->IsChecked();
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

	// Manager->StartCaptureProcess(Settings);
	Manager->StartSingleCaptureForValidation(Settings);

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
