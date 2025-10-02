// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MinimapGeneratorManager.h" // Include để biết về UMinimapGeneratorManager và FMinimapCaptureSettings
#include "PropertyCustomizationHelpers.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Widgets/Colors/SColorBlock.h"
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

	// === THÊM CÁC BIẾN VÀ HÀM MỚI CHO BACKGROUND ===
	/** Chế độ nền hiện tại được chọn trên UI */
	EMinimapBackgroundMode CurrentBackgroundMode = EMinimapBackgroundMode::Transparent;

	/** Con trỏ tới widget chọn màu */
	FLinearColor SelectedBackgroundColor;
	FLinearColor GetSelectedBackgroundColor() const;
	FReply OnBackgroundColorBlockMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void OnBackgroundColorChanged(FLinearColor NewColor);

	/** Được gọi khi người dùng thay đổi chế độ nền */
	void OnBackgroundModeChanged(ECheckBoxState NewState, EMinimapBackgroundMode Mode);
	/** Kiểm tra xem một chế độ nền có đang được chọn hay không (dùng cho radio button) */
	ECheckBoxState IsBackgroundModeChecked(EMinimapBackgroundMode Mode) const;
	
	/** Quyết định việc hiển thị ô chọn màu */
	EVisibility GetBackgroundColorPickerVisibility() const;
	EVisibility GetRotationWarningVisibility() const;

	// --- CON TRỎ TỚI CÁC WIDGET CẦN TƯƠNG TÁC ---
	// Chúng ta cần lưu lại con trỏ tới các widget nhập liệu để có thể đọc giá trị của chúng
	void HandleCaptureCompleted(bool bSuccess, const FString& FinalImagePath);

	FTimerHandle TimerHandle_HideProgress;

	// Region Settings
	TSharedPtr<SSpinBox<float>> BoundsMinX, BoundsMinY, BoundsMinZ;
	TSharedPtr<SSpinBox<float>> BoundsMaxX, BoundsMaxY, BoundsMaxZ;

	// Output Settings
	TArray<TSharedPtr<int32>> ResolutionOptions;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> OutputWidthComboBox;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> OutputHeightComboBox;
	TSharedPtr<int32> CurrentOutputWidth;
	TSharedPtr<int32> CurrentOutputHeight;
	void OnOutputWidthChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo);
	void OnOutputHeightChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo);
	
	TSharedPtr<SCheckBox> ImportAsAssetCheckbox;
	TSharedPtr<SEditableTextBox> AssetPathTextBox;
	EVisibility GetAssetPathVisibility() const;
	
	TSharedPtr<SEditableTextBox> OutputPath;
	TSharedPtr<SEditableTextBox> FileName;
	TSharedPtr<SCheckBox> AutoFilenameCheckbox;

	// Tiling Settings
	TSharedPtr<SCheckBox> UseTilingCheckbox; // Thêm con trỏ này
	TSharedPtr<SSpinBox<int32>> TileResolution;
	TSharedPtr<SSpinBox<int32>> TileOverlap;
	EVisibility GetTilingSettingsVisibility() const; // Thêm hàm này

	// Camera Settings
	TSharedPtr<SSpinBox<float>> CameraHeight;
	TSharedPtr<SSpinBox<float>> RotationRollSpinBox;
	TSharedPtr<SSpinBox<float>> RotationPitchSpinBox;
	TSharedPtr<SSpinBox<float>> RotationYawSpinBox;
	TSharedPtr<SCheckBox> IsOrthographicCheckbox;
	TSharedPtr<SSpinBox<float>> CameraFOV;

	// Quality Settings
	TSharedPtr<SCheckBox> OverrideQualityCheckbox; 
	// === THÊM CÁC WIDGET VÀ HÀM MỚI ===
	// Con trỏ tới các widget cài đặt chi tiết
	TSharedPtr<SSpinBox<float>> AOIntensitySpinBox;
	TSharedPtr<SSpinBox<float>> AOQualitySpinBox;
	TSharedPtr<SSpinBox<float>> SSRIntensitySpinBox;
	TSharedPtr<SSpinBox<float>> SSRQualitySpinBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CaptureSourceComboBox;

	// Dữ liệu cho ComboBox
	TArray<TSharedPtr<FString>> CaptureSourceOptions;
	TSharedPtr<FString> CurrentCaptureSource;

	// Hàm delegate cho ComboBox
	void OnCaptureSourceChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

	// Hàm quyết định việc hiển thị các cài đặt override
	EVisibility GetOverrideSettingsVisibility() const;
	
	// FILTERING
	TSharedPtr<SListView<TSharedPtr<FString>>> ShowOnlyActorsListView;
	TSharedPtr<SListView<TSharedPtr<FString>>> HiddenActorsListView;
	TArray<TSharedPtr<FString>> ShowOnlyActorNames;
	TArray<TSharedPtr<FString>> HiddenActorNames;

	FReply OnAddSelectedToShowOnlyList();
	FReply OnClearShowOnlyList();
	FReply OnAddSelectedToHiddenList();
	FReply OnClearHiddenList();

	TSharedPtr<SClassPropertyEntryBox> ActorClassFilterBox;
	TSharedPtr<SEditableTextBox> ActorTagFilterTextBox;

	/** Lấy class hiện tại đang được chọn để hiển thị trên UI */
	const UClass* GetSelectedActorClass() const;
	/** Được gọi khi người dùng chọn một class mới */
	void OnActorClassChanged(const UClass* NewClass);
	
	// Progress Reporting
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SButton> StartButton;

	TSharedPtr<SBox> ImageContainer; // Container để dễ dàng ẩn/hiện
	TSharedPtr<SImage> FinalImageView;
	TSharedPtr<ISlateBrushSource> FinalImageBrushSource; // Lưu trữ nguồn brush để quản lý vòng đời

	// --- LOGIC BACKEND ---

	/** 
	 * Con trỏ tới đối tượng xử lý logic.
	 * TStrongObjectPtr đảm bảo UObject này không bị Garbage Collect (dọn rác)
	 * chừng nào widget của chúng ta còn tồn tại. Rất quan trọng!
	 */
	TStrongObjectPtr<UMinimapGeneratorManager> Manager;

	FMinimapCaptureSettings Settings;
};
