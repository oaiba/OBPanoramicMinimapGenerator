// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MinimapGeneratorManager.generated.h"

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
	int32 TileResolution = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiling")
	int32 TileOverlap = 64; // in pixels

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraHeight = 50000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bIsOrthographic = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (EditCondition = "!bIsOrthographic"))
	float CameraFOV = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString OutputPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString FileName = "Minimap_Result";
};

class FMinimapStreamingSourceProvider;

// Delegate to report progress back to the UI
DECLARE_MULTICAST_DELEGATE_FourParams(FOnMinimapProgress, const FText&, /*Status*/ float, /*Percentage*/ int32,
                                      /*CurrentTile*/ int32 /*TotalTiles*/);

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
	void StartSingleCaptureForValidation(const FMinimapCaptureSettings& InSettings);

	// Delegate for UI updates
	// UPROPERTY()
	FOnMinimapProgress OnProgress;

	void OnTileCaptureCompleted(int32 TileX, int32 TileY, TArray<FColor> PixelData);
	
	// Callback function when the async save task is complete
	void OnSaveTaskCompleted(bool bSuccess);
private:
	// Main steps of the process
	void CalculateGrid();
	void StartStitching();
	void OnAllTasksCompleted();

	bool SaveFinalImage(const TArray<FColor>& ImageData, int32 Width, int32 Height);

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
	int32 NumTilesX = 0;
	int32 NumTilesY = 0;
	int32 CurrentTileIndex = 0;

	// We'll store captured tile data here before stitching
	TMap<FIntPoint, TArray<FColor>> CapturedTileData;

	void CaptureTileWithScreenshot();
	void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& PixelData);

	FIntPoint CurrentCaptureTilePosition;
	FDelegateHandle ScreenshotCapturedDelegateHandle;

	bool bIsSingleCaptureMode = false;

};
