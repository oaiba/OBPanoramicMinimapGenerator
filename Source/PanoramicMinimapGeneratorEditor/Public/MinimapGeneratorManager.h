// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent.h"
#include "Engine/SceneCapture2D.h"
#include "MinimapDefinitionDataAsset.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling")
	bool bUseTiling = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling", meta = (EditCondition = "bUseTiling"))
	int32 TileResolution = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling", meta = (EditCondition = "bUseTiling"))
	int32 TileOverlap = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debugging", meta = (
	EditCondition = "bUseTiling", Tooltip = "If checked, saves each captured tile as a separate image for debugging the stitching process."))
	bool bSaveTiles = true;

	// QUALITY
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality", meta = (
	Tooltip = "If checked, forces cinematic-quality post-processing. If unchecked (default), uses the current editor viewport scalability settings for better performance."))
	bool bOverrideWithHighQualitySettings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quality")
	bool bCaptureDynamicShadows = true;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|Runtime")
	bool bExportDefinitionAsset = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|Runtime", meta = (EditCondition = "bExportDefinitionAsset"))
	FString DefinitionAssetPath = TEXT("/Game/Minimaps/");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	EMinimapBackgroundMode BackgroundMode = EMinimapBackgroundMode::SolidColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (EditCondition = "BackgroundMode == EMinimapBackgroundMode::SolidColor"))
	FLinearColor BackgroundColor = FLinearColor::Black;

	// FILTERING
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	ESceneCapturePrimitiveRenderMode PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering", meta = (EditCondition = "PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList"))
	TArray<TSoftObjectPtr<AActor>> ShowOnlyActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<TSoftObjectPtr<AActor>> HiddenActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TSubclassOf<AActor> ActorClassFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	FName ActorTagFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	TArray<FMinimapOverlayLayer> OverlayLayers;
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
	
	// Cancel an ongoing capture process
	void CancelCapture();
	void ShutdownCapture(bool bBroadcastResult);
	
	// Callback function when the async save task is complete
	void OnSaveTaskCompleted(bool bSuccess, const FString& SavedImagePath);

	virtual void BeginDestroy() override;
private:
	// Main steps of the process
	void OnAllTasksCompleted();

	bool SaveFinalImage(const TArray<FColor>& ImageData, int32 Width, int32 Height);
	UTexture2D* ImportTextureAssetFromSavedImage(const FString& SavedImagePath) const;
	UMinimapDefinitionDataAsset* CreateOrUpdateDefinitionAsset(const FString& SavedImagePath, UTexture2D* BaseMapTexture) const;
	void CleanupCaptureResources();

	// === FUNCTIONS FOR SINGLE CAPTURE ===
	/** Create and configure the Render Target to draw to. */
	UTextureRenderTarget2D* CreateRenderTarget() const;

	/** Spawn, configure, and position the Scene Capture Actor. */
	ASceneCapture2D* SpawnAndConfigureCaptureActor(UTextureRenderTarget2D* RenderTarget) const;

	/** Called by Timer to read pixels, clean up, and start saving. */
	void ReadPixelsAndFinalize();

	/** Generate the final file name and start AsyncTask to save the image. */
	void StartImageSaveTask(TArray<FColor> PixelData, int32 ImageWidth, int32 ImageHeight);
	
	// === ASYNC READBACK ===
	/** Called by Timer to check if the GPU has finished reading pixels */
	void CheckReadbackStatus();

	/** Fence to synchronize with the Rendering Thread */
	FRenderCommandFence ReadbackFence;

	/** Timer to poll readback fence completion */
	FTimerHandle ReadbackPollTimer;

	/** Counter for readback polls — used for timeout detection */
	int32 ReadbackPollCount = 0;

	/** Staging buffer for the rendering thread to write pixel data into */
	TArray<FColor> StagingPixelBuffer;
	// ===========================================

	// Handle to the timer that polls streaming completion
	FTimerHandle StreamingCheckTimer;

	// Our custom streaming source provider
	// FORWARD DECLARATION: Good practice to avoid including the full header in our public .h file
	TSharedPtr<FMinimapStreamingSourceProvider> MinimapStreamer;

	// HELPER FUNCTION
	void BuildFinalShowOnlyList(TArray<AActor*>& OutShowOnlyList) const;

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
	
	FIntPoint CurrentCaptureTilePosition;
	FDelegateHandle ScreenshotCapturedDelegateHandle;

	bool bIsSingleCaptureMode = false;
	bool bCancelRequested = false;
	bool bIsShuttingDown = false;

	TWeakObjectPtr<ASceneCapture2D> ActiveCaptureActor;
	TWeakObjectPtr<UTextureRenderTarget2D> ActiveRenderTarget;

};
