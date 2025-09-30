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

#define LOCTEXT_NAMESPACE "SMinimapGeneratorWindow"

void SMinimapGeneratorWindow::Construct(const FArguments& InArgs)
{
	// Tính toán đường dẫn một lần để code dễ đọc hơn
	const FString DefaultPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

	// Tạo đối tượng Manager để xử lý logic
	Manager = TStrongObjectPtr<UMinimapGeneratorManager>(NewObject<UMinimapGeneratorManager>());

	// Bind delegate để Manager có thể gọi lại và cập nhật UI
	Manager->OnProgress.AddSP(this, &SMinimapGeneratorWindow::OnCaptureProgress);

	// Cấu trúc UI chính, một hộp xếp dọc
	ChildSlot
	[
		SNew(SVerticalBox)

		// --- SECTION: REGION ---
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SNew(STextBlock).Text(LOCTEXT("RegionHeader", "1. Capture Region (Min Bounds)"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SAssignNew(BoundsMinX, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
			]
			+ SHorizontalBox::Slot()
			[
				SAssignNew(BoundsMinY, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
			]
			+ SHorizontalBox::Slot()
			[
				SAssignNew(BoundsMinZ, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SNew(STextBlock).Text(LOCTEXT("RegionMaxHeader", "Capture Region (Max Bounds)"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SAssignNew(BoundsMaxX, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
			]
			+ SHorizontalBox::Slot()
			[
				SAssignNew(BoundsMaxY, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
			]
			+ SHorizontalBox::Slot()
			[
				SAssignNew(BoundsMaxZ, SSpinBox<float>).MinValue(-999999).MaxValue(999999)
			]
		]
		// THÊM NÚT BẤM MỚI VÀO ĐÂY
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5)
		.HAlign(HAlign_Right)
		[
			SNew(SButton)
			.Text(LOCTEXT("GetBoundsButton", "Get Bounds from Selected Actor"))
			.OnClicked(this, &SMinimapGeneratorWindow::OnGetBoundsFromSelectionClicked)
		]

		// --- SECTION: OUTPUT ---
		+ SVerticalBox::Slot().AutoHeight().Padding(5, 10, 5, 5)
		[
			SNew(STextBlock).Text(LOCTEXT("OutputHeader", "2. Output Settings"))
		]
		// Output Width & Height
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("OutputWidthLabel", "Output Width"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(OutputWidth, SSpinBox<int32>).MinValue(256).MaxValue(16384).Value(4096)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("OutputHeightLabel", "Output Height"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(OutputHeight, SSpinBox<int32>).MinValue(256).MaxValue(16384).Value(4096)
			]
		]
		// Output Path
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.25f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("OutputPathLabel", "Output Path"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.65f)
			[
				SAssignNew(OutputPath, SEditableTextBox).Text(FText::FromString(DefaultPath))
			]
			+ SHorizontalBox::Slot().FillWidth(0.1f)
			[
				SNew(SButton).Text(LOCTEXT("BrowseButton", "...")).OnClicked(
					this, &SMinimapGeneratorWindow::OnBrowseButtonClicked)
			]
		]
		// File Name
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("FileNameLabel", "File Name"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(FileName, SEditableTextBox).Text(LOCTEXT("DefaultFileName", "Minimap_Result"))
			]
		]
		// Auto FileName Checkbox
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SAssignNew(AutoFilenameCheckbox, SCheckBox)
			.IsChecked(ECheckBoxState::Checked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AutoFilenameLabel", "Auto-Generate Filename with Timestamp"))
				.ToolTipText(LOCTEXT("AutoFilenameTooltip",
				                     "If checked, appends a timestamp (_DD_MM_HH_mm) to the filename."))
			]
		]

		// --- SECTION: TILING ---
		+ SVerticalBox::Slot().AutoHeight().Padding(5, 10, 5, 5)
		[
			SNew(STextBlock).Text(LOCTEXT("TilingHeader", "3. Tiling Settings"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("TileResLabel", "Tile Resolution"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(TileResolution, SSpinBox<int32>).MinValue(256).MaxValue(8192).Value(2048)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("TileOverlapLabel", "Tile Overlap (px)"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(TileOverlap, SSpinBox<int32>).MinValue(0).MaxValue(1024).Value(64)
			]
		]

		// --- SECTION: CAMERA ---
		+ SVerticalBox::Slot().AutoHeight().Padding(5, 10, 5, 5)
		[
			SNew(STextBlock).Text(LOCTEXT("CameraHeader", "4. Camera Settings"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("CameraHeightLabel", "Camera Height"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(CameraHeight, SSpinBox<float>).MinValue(100.f).MaxValue(999999.f).Value(50000.f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SAssignNew(IsOrthographicCheckbox, SCheckBox)
			.IsChecked(ECheckBoxState::Checked)
			.OnCheckStateChanged(this, &SMinimapGeneratorWindow::OnProjectionTypeChanged)
			[
				SNew(STextBlock).Text(LOCTEXT("IsOrthoLabel", "Use Orthographic Projection"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SNew(SHorizontalBox)
			.IsEnabled(this, &SMinimapGeneratorWindow::IsPerspectiveMode) // Bật/tắt dựa trên checkbox
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(LOCTEXT("CameraFOVLabel", "Camera FOV (Perspective only)"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SAssignNew(CameraFOV, SSpinBox<float>).MinValue(10.f).MaxValue(170.f).Value(90.f)
			]
		]

		// === THÊM ĐOẠN MÃ NÀY VÀO ĐÂY ===
		// --- SECTION: QUALITY ---
		+ SVerticalBox::Slot().AutoHeight().Padding(5, 10, 5, 5)
		[
			SNew(STextBlock).Text(LOCTEXT("QualityHeader", "5. Quality Settings"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 5, 5, 5)
		[
			SAssignNew(OverrideQualityCheckbox, SCheckBox)
			.IsChecked(ECheckBoxState::Unchecked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OverrideQualityLabel", "Override With High Quality Settings"))
				.ToolTipText(LOCTEXT("OverrideQualityTooltip",
				                     "If checked, forces cinematic-quality post-processing.\nIf unchecked (default), uses the current editor viewport scalability settings for better performance."))
			]
		]
		// ===================================

		// --- SECTION: ACTION ---
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 20, 10, 5)
		[
			SAssignNew(StartButton, SButton)
			.Text(LOCTEXT("StartCaptureButton", "Start Capture Process"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &SMinimapGeneratorWindow::OnStartCaptureClicked) // Gắn sự kiện click
		]

		// --- SECTION: PROGRESS ---
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SAssignNew(ProgressBar, SProgressBar)
			.Percent(0.0f)
			.Visibility(EVisibility::Collapsed) // Ẩn đi lúc đầu
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(FText::GetEmpty())
			.Visibility(EVisibility::Collapsed) // Ẩn đi lúc đầu
		]
	];
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
	Settings.bIsOrthographic = IsOrthographicCheckbox->IsChecked();
	Settings.CameraFOV = CameraFOV->GetValue();
	Settings.bOverrideWithHighQualitySettings = OverrideQualityCheckbox->IsChecked();

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
