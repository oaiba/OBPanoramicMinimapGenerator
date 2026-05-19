// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformMemory.h"
#include "Widgets/SCompoundWidget.h"
#include "MinimapGeneratorManager.h" // Include for UMinimapGeneratorManager and FMinimapCaptureSettings.
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

	/** Required: builds the widget when it is created. */
	void Construct(const FArguments& InArgs);
	virtual ~SMinimapGeneratorWindow() override;

private:
	// --- EVENT HANDLERS (DELEGATES) ---

	/** Called when the "Start Capture" button is clicked. */
	FReply OnStartCaptureClicked();
	FReply OnCancelCaptureClicked();
	FReply OnBrowseButtonClicked();
	FReply OnOpenFolderClicked();
	FReply OnGetBoundsFromSelectionClicked();
	void OnProjectionTypeChanged(ECheckBoxState NewState);
	bool IsPerspectiveMode() const;
	/** Called by manager delegates to update UI state. */
	void OnCaptureProgress(const FText& Status, float Percentage, int32 CurrentTile, int32 TotalTiles);
	void OnTelemetryUpdated(const FMinimapCaptureTelemetry& InTelemetry);
	void UnbindManagerDelegates();
	void ReleasePreviewResources();
	EActiveTimerReturnType RefreshMemoryStats(double CurrentTime, float DeltaTime);
	void UpdateMemoryStatsCache();
	void UpdateTelemetryCache(const FMinimapCaptureTelemetry& InTelemetry);
	FText FormatMemoryBytes(uint64 Bytes) const;
	FText FormatMemoryPressureStatus(FPlatformMemoryStats::EMemoryPressureStatus Status) const;
	FText FormatDurationSeconds(double Seconds) const;
	FText FormatMilliseconds(double Seconds) const;
	FText FormatIntPoint(FIntPoint Point) const;

	// === BACKGROUND MODE STATE ===
	/** Current background mode selected in UI. */
	EMinimapBackgroundMode CurrentBackgroundMode = EMinimapBackgroundMode::Transparent;

	/** Current selected background color. */
	FLinearColor SelectedBackgroundColor = FLinearColor::Black;
	FLinearColor GetSelectedBackgroundColor() const;
	FReply OnBackgroundColorBlockMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void OnBackgroundColorChanged(FLinearColor NewColor);

	/** Called when the user changes background mode. */
	void OnBackgroundModeChanged(ECheckBoxState NewState, EMinimapBackgroundMode Mode);
	/** Returns whether a background mode is selected (used by radio buttons). */
	ECheckBoxState IsBackgroundModeChecked(EMinimapBackgroundMode Mode) const;
	
	/** Controls color picker visibility. */
	EVisibility GetBackgroundColorPickerVisibility() const;
	EVisibility GetRotationWarningVisibility() const;

	// --- WIDGET REFERENCES WE INTERACT WITH ---
	// Keep widget pointers so we can read and update values.
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
	
	// Presets
	FReply OnResolutionPresetClicked(int32 Res);
	
	TSharedPtr<SCheckBox> ImportAsAssetCheckbox;
	TSharedPtr<SEditableTextBox> AssetPathTextBox;
	TSharedPtr<SCheckBox> ExportDefinitionAssetCheckbox;
	TSharedPtr<SEditableTextBox> DefinitionAssetPathTextBox;
	EVisibility GetAssetPathVisibility() const;
	EVisibility GetDefinitionAssetPathVisibility() const;
	
	TSharedPtr<SEditableTextBox> OutputPath;
	TSharedPtr<SEditableTextBox> FileName;
	TSharedPtr<SCheckBox> AutoFilenameCheckbox;

	// Tiling Settings
	TSharedPtr<SCheckBox> UseTilingCheckbox; // Tiling toggle checkbox.
	TSharedPtr<SCheckBox> ExportTileSetCheckbox;
	TSharedPtr<SSpinBox<int32>> TileResolution;
	TSharedPtr<SSpinBox<int32>> TileOverlap;
	TSharedPtr<SSpinBox<float>> TileSetWorldTileSize;
	TSharedPtr<SSpinBox<int32>> TileSetMaxLOD;
	TSharedPtr<SSpinBox<int32>> TileSetOverviewResolution;
	EVisibility GetTilingSettingsVisibility() const; // Tiling options visibility helper.
	EVisibility GetTileSetSettingsVisibility() const;

	// Camera Settings
	TSharedPtr<SSpinBox<float>> CameraHeight;
	TSharedPtr<SSpinBox<float>> RotationRollSpinBox;
	TSharedPtr<SSpinBox<float>> RotationPitchSpinBox;
	TSharedPtr<SSpinBox<float>> RotationYawSpinBox;
	TSharedPtr<SCheckBox> IsOrthographicCheckbox;
	TSharedPtr<SSpinBox<float>> CameraFOV;
	
	// Orthographic Image Rotation
	TSharedPtr<SSpinBox<float>> ImageRotationSpinBox;
	void OnImageRotationChanged(float NewValue);
	FReply OnRotationPresetClicked(float Angle);
	EVisibility GetImageRotationVisibility() const;

	// Quality Settings
	TSharedPtr<SCheckBox> CaptureDynamicShadowsCheckbox; 
	TSharedPtr<SCheckBox> OverrideQualityCheckbox; 
	// === QUALITY OVERRIDE WIDGETS ===
	// Pointers to advanced quality setting widgets.
	TSharedPtr<SSpinBox<float>> AOIntensitySpinBox;
	TSharedPtr<SSpinBox<float>> AOQualitySpinBox;
	TSharedPtr<SSpinBox<float>> SSRIntensitySpinBox;
	TSharedPtr<SSpinBox<float>> SSRQualitySpinBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CaptureSourceComboBox;

	// Data source for CaptureSource combo box.
	TArray<TSharedPtr<FString>> CaptureSourceOptions;
	TSharedPtr<FString> CurrentCaptureSource;

	// Delegate for CaptureSource combo box selection changes.
	void OnCaptureSourceChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

	// Controls visibility of quality override settings.
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
	FReply OnRemoveSelectedFromShowOnlyList();
	FReply OnRemoveSelectedFromHiddenList();

	TSharedPtr<SClassPropertyEntryBox> ActorClassFilterBox;
	TSharedPtr<SEditableTextBox> ActorTagFilterTextBox;

	/** Returns currently selected actor class for UI display. */
	const UClass* GetSelectedActorClass() const;
	/** Called when the user selects a new actor class. */
	void OnActorClassChanged(const UClass* NewClass);

	// Overlay authoring
	TArray<FMinimapOverlayLayer> OverlayLayers;
	TArray<TSharedPtr<FString>> OverlayElementNames;
	TSharedPtr<SListView<TSharedPtr<FString>>> OverlayElementsListView;
	TSharedPtr<SEditableTextBox> OverlayLabelTextBox;
	TSharedPtr<SEditableTextBox> OverlayCategoryTextBox;
	TSharedPtr<SSpinBox<float>> OverlayLineWidthSpinBox;
	FLinearColor SelectedOverlayColor = FLinearColor::Yellow;
	FReply OnOverlayColorBlockMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void OnOverlayColorChanged(FLinearColor NewColor);
	FLinearColor GetSelectedOverlayColor() const;
	FReply OnAddMarkerFromSelectionClicked();
	FReply OnAddZoneFromSelectionClicked();
	FReply OnAddPathFromSelectionClicked();
	FReply OnAddFreehandFromSelectionClicked();
	FReply OnRemoveSelectedOverlayClicked();
	FReply OnClearOverlayClicked();
	void RefreshOverlayList();
	FMinimapOverlayElement MakeOverlayElementFromSelection(EMinimapOverlayElementType Type) const;
	
	// Progress Reporting
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> MemoryStatsText;
	TSharedPtr<STextBlock> TelemetrySummaryText;
	TSharedPtr<STextBlock> TelemetryTimingText;
	TSharedPtr<STextBlock> TelemetrySettingsText;
	FText CachedMemoryStatsText;
	FSlateColor CachedMemoryStatsColor = FSlateColor::UseForeground();
	FText CachedMemoryStatsTooltip;
	FText CachedTelemetrySummaryText;
	FText CachedTelemetryTimingText;
	FText CachedTelemetrySettingsText;
	FText CachedTelemetryTooltip;
	FMinimapCaptureTelemetry LatestTelemetry;
	double LatestTelemetryUpdateTime = 0.0;
	TSharedPtr<SButton> StartButton;
	TSharedPtr<SButton> CancelButton;

	TSharedPtr<SBox> ImageContainer; // Container used for simple show/hide behavior.
	TSharedPtr<SWidgetSwitcher> PreviewSwitcher;
	TSharedPtr<SImage> FinalImageView; // Fit to screen
	TSharedPtr<SImage> ZoomedImageView; // Scrollable zoom
	TSharedPtr<ISlateBrushSource> FinalImageBrushSource; // Keep brush source alive for lifetime management.
	
	// Post-capture UX
	TSharedPtr<SButton> OpenFolderButton;
	TSharedPtr<STextBlock> ImageInfoText;
	FString LastSavedImagePath;

	// Zoom functionality
	float PreviewZoomFactor = 0.0f;
	FReply OnZoomInClicked();
	FReply OnZoomOutClicked();
	FReply OnZoomFitClicked();
	FReply OnZoom100Clicked();
	FText GetZoomText() const;
	FOptionalSize GetPreviewZoomedWidth() const;
	FOptionalSize GetPreviewZoomedHeight() const;

	// --- BACKEND LOGIC ---

	/** 
	 * Pointer to the logic manager object.
	 * TStrongObjectPtr ensures this UObject is not garbage collected
	 * while the widget is still alive.
	 */
	TStrongObjectPtr<UMinimapGeneratorManager> Manager;

	FMinimapCaptureSettings Settings;
	
	// Settings Persistence
	void SaveSettings() const;
	void LoadSettings();
};
