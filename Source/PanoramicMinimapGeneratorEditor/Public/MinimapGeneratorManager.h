// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/SceneCapture2D.h"
#include "UObject/Object.h"
#include "MinimapGeneratorManager.generated.h"

UENUM(BlueprintType)
enum class EMinimapBackgroundMode : uint8
{
	Transparent UMETA(DisplayName = "Transparent"),
	SolidColor UMETA(DisplayName = "Solid Color"),
};

// Struct to hold all capture settings, easily passed around and exposed to UI/BP
USTRUCT(BlueprintType)
struct FMinimapCaptureSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Region")
	FBox CaptureBounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 OutputWidth = 4096;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	int32 OutputHeight = 4096;

	// === THÊM CÁC THUỘC TÍNH MỚI CHO TILING ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling")
	bool bUseTiling = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling", meta = (EditCondition = "bUseTiling"))
	int32 TileResolution = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling", meta = (EditCondition = "bUseTiling"))
	int32 TileOverlap = 64; // in pixels

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (
	Tooltip = "If checked, forces cinematic-quality post-processing. If unchecked (default), uses the current editor viewport scalability settings for better performance."))
	bool bOverrideWithHighQualitySettings = false;

	// === CÁC THUỘC TÍNH MỚI CHO OVERRIDE ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality|Overrides", meta = (EditCondition = "bOverrideWithHighQualitySettings"))
	TEnumAsByte<ESceneCaptureSource> CaptureSource = SCS_FinalColorHDR;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality|Overrides", meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bOverrideWithHighQualitySettings"))
	float AmbientOcclusionIntensity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality|Overrides", meta = (UIMin = "0.0", UIMax = "100.0", EditCondition = "bOverrideWithHighQualitySettings"))
	float AmbientOcclusionQuality = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality|Overrides", meta = (UIMin = "0.0", UIMax = "100.0", EditCondition = "bOverrideWithHighQualitySettings"))
	float ScreenSpaceReflectionIntensity = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality|Overrides", meta = (UIMin = "0.0", UIMax = "100.0", EditCondition = "bOverrideWithHighQualitySettings"))
	float ScreenSpaceReflectionQuality = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraHeight = 50000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FRotator CameraRotation = FRotator(-90.f, 0.f, -180.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bIsOrthographic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (EditCondition = "!bIsOrthographic"))
	float CameraFOV = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString OutputPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString FileName = "Minimap_Result";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (
	Tooltip = "If checked, automatically appends a timestamp (_DD_MM_HH_mm) to the filename."))
	bool bUseAutoFilename = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|Import")
	bool bImportAsTextureAsset = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|Import", meta = (EditCondition = "bImportAsTextureAsset"))
	FString AssetPath = TEXT("/Game/Minimaps/");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	EMinimapBackgroundMode BackgroundMode = EMinimapBackgroundMode::SolidColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (EditCondition = "BackgroundMode == EMinimapBackgroundMode::SolidColor"))
	FLinearColor BackgroundColor = FLinearColor::Black;
};

class FMinimapStreamingSourceProvider;

// Delegate to report progress back to the UI
DECLARE_MULTICAST_DELEGATE_FourParams(FOnMinimapProgress, const FText&, /*Status*/ float, /*Percentage*/ int32,
                                      /*CurrentTile*/ int32 /*TotalTiles*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMinimapCaptureComplete, bool /*bSuccess*/, const FString& /*FinalImagePath*/);

/**
 * 
 */
UCLASS()
class PANORAMICMINIMAPGENERATOREDITOR_API UMinimapGeneratorManager : public UObject
{
	GENERATED_BODY()

public:
	// Starts the entire capture and stitching process
	void StartCaptureProcess(const FMinimapCaptureSettings& InSettings);
	void StartSingleCaptureForValidation();

	// Delegate for UI updates
	FOnMinimapProgress OnProgress;
	FOnMinimapCaptureComplete OnCaptureComplete;

	void OnTileCaptureCompleted(int32 TileX, int32 TileY, TArray<FColor> PixelData);
	
	// Callback function when the async save task is complete
	void OnSaveTaskCompleted(bool bSuccess, const FString& SavedImagePath) const;
private:
	// Main steps of the process
	void OnAllTasksCompleted();

	bool SaveFinalImage(const TArray<FColor>& ImageData, int32 Width, int32 Height);

	// === CÁC HÀM HELPER MỚI CHO VIỆC CHỤP ẢNH ĐƠN LẺ ===
	/** Tạo và cấu hình Render Target để vẽ vào. */
	UTextureRenderTarget2D* CreateRenderTarget() const;

	/** Tạo, cấu hình và định vị Scene Capture Actor. */
	ASceneCapture2D* SpawnAndConfigureCaptureActor(UTextureRenderTarget2D* RenderTarget) const;

	/** Được gọi bởi Timer để đọc pixel, dọn dẹp và bắt đầu lưu. */
	void ReadPixelsAndFinalize();

	/** Tạo tên file cuối cùng và khởi động AsyncTask để lưu ảnh. */
	void StartImageSaveTask(TArray<FColor> PixelData, int32 ImageWidth, int32 ImageHeight);

	// The main function to drive the process
	void ProcessNextTile();
	// New function to check streaming status and trigger capture
	void CheckStreamingAndCapture();

	// Handle to the timer that polls streaming completion
	FTimerHandle StreamingCheckTimer;

	// Our custom streaming source provider
	// FORWARD DECLARATION: Good practice to avoid including the full header in our public .h file
	TSharedPtr<FMinimapStreamingSourceProvider> MinimapStreamer;

	// Member variables
	FMinimapCaptureSettings Settings;
	TMap<FIntPoint, TArray<FColor>> CapturedTileData;
	int32 NumTilesX = 0;
	int32 NumTilesY = 0;
	int32 CurrentTileIndex = 0;
	void StartTiledCaptureProcess();
	void CalculateGrid();
	void CaptureNextTile();
	void OnTileRenderedAndContinue();
	void StartStitching();
	
	void CaptureTileWithScreenshot();
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& PixelData);

	FIntPoint CurrentCaptureTilePosition;
	FDelegateHandle ScreenshotCapturedDelegateHandle;

	bool bIsSingleCaptureMode = false;

	TWeakObjectPtr<ASceneCapture2D> ActiveCaptureActor;
	TWeakObjectPtr<UTextureRenderTarget2D> ActiveRenderTarget;

};
