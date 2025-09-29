// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MinimapGeneratorManager.h" // Include để biết về UMinimapGeneratorManager và FMinimapCaptureSettings
#include "Widgets/Input/SSpinBox.h"

class SEditableTextBox;
class SButton;
class SProgressBar;
class STextBlock;

class SMinimapGeneratorWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMinimapGeneratorWindow)
		{
		}

	SLATE_END_ARGS()

	/** Bắt buộc phải có: Hàm này xây dựng widget khi nó được tạo ra */
	void Construct(const FArguments& InArgs);

private:
	// --- CÁC HÀM XỬ LÝ SỰ KIỆN (DELEGATES) ---

	/** Được gọi khi nút "Start Capture" được nhấn */
	FReply OnStartCaptureClicked();
	FReply OnBrowseButtonClicked();
	FReply OnGetBoundsFromSelectionClicked();
	void OnProjectionTypeChanged(ECheckBoxState NewState);
	bool IsPerspectiveMode() const;
	/** Được gọi bởi delegate của Manager để cập nhật UI */
	void OnCaptureProgress(const FText& Status, float Percentage, int32 CurrentTile, int32 TotalTiles);

private:
	// --- CON TRỎ TỚI CÁC WIDGET CẦN TƯƠNG TÁC ---
	// Chúng ta cần lưu lại con trỏ tới các widget nhập liệu để có thể đọc giá trị của chúng

	// Region Settings
	TSharedPtr<SSpinBox<float>> BoundsMinX, BoundsMinY, BoundsMinZ;
	TSharedPtr<SSpinBox<float>> BoundsMaxX, BoundsMaxY, BoundsMaxZ;

	// Output Settings
	TSharedPtr<SSpinBox<int32>> OutputWidth;
	TSharedPtr<SSpinBox<int32>> OutputHeight;
	TSharedPtr<SEditableTextBox> OutputPath;
	TSharedPtr<SEditableTextBox> FileName;

	// Tiling Settings
	TSharedPtr<SSpinBox<int32>> TileResolution;
	TSharedPtr<SSpinBox<int32>> TileOverlap;

	// Camera Settings
	TSharedPtr<SSpinBox<float>> CameraHeight;
	TSharedPtr<SCheckBox> IsOrthographicCheckbox;
	TSharedPtr<SSpinBox<float>> CameraFOV;

	// Progress Reporting
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SButton> StartButton;

	// --- LOGIC BACKEND ---

	/** 
	 * Con trỏ tới đối tượng xử lý logic.
	 * TStrongObjectPtr đảm bảo UObject này không bị Garbage Collect (dọn rác)
	 * chừng nào widget của chúng ta còn tồn tại. Rất quan trọng!
	 */
	TStrongObjectPtr<UMinimapGeneratorManager> Manager;
};
