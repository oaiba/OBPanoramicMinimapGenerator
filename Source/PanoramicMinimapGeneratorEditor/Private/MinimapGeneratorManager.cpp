// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorManager.h"

#include "Editor.h"
#include "HighResScreenshot.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "LevelEditorViewport.h"
#include "MinimapStreamingSource.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
// ADD THIS NEW CLASS AT THE TOP OF YOUR .CPP FILE
#include "Async/Async.h"

// AsyncTask to save image data to disk on a background thread
class FSaveImageTask : public FNonAbandonableTask
{
public: // <-- MOVED TO PUBLIC
	FSaveImageTask(TArray<FColor> InPixelData, int32 InWidth, int32 InHeight, FString InFullPath,
	               TWeakObjectPtr<UMinimapGeneratorManager> InManager)
		: PixelData(MoveTemp(InPixelData))
		  , Width(InWidth)
		  , Height(InHeight)
		  , FullPath(MoveTemp(InFullPath))
		  , ManagerPtr(InManager)
	{
	}

	// This is the work that will be done on a background thread
	void DoWork()
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(
			FName("ImageWrapper"));
		if (const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper.IsValid() && ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width,
			                                               Height, ERGBFormat::BGRA, 8))
		{
			const bool bSuccess = FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *FullPath);

			// After work is done, schedule a task on the Game Thread to report completion
			AsyncTask(ENamedThreads::GameThread, [ManagerPtr = this->ManagerPtr, bSuccess]
			{
				if (UMinimapGeneratorManager* Manager = ManagerPtr.Get())
				{
					Manager->OnSaveTaskCompleted(bSuccess);
				}
			});
		}
		else
		{
			// Also report failure back to the Game Thread
			AsyncTask(ENamedThreads::GameThread, [ManagerPtr = this->ManagerPtr]
			{
				if (UMinimapGeneratorManager* Manager = ManagerPtr.Get())
				{
					Manager->OnSaveTaskCompleted(false);
				}
			});
		}
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveImageTask, STATGROUP_ThreadPoolAsyncTasks);
	}

protected: // <-- Member variables remain protected
	TArray<FColor> PixelData;
	int32 Width;
	int32 Height;
	FString FullPath;
	TWeakObjectPtr<UMinimapGeneratorManager> ManagerPtr;
};

void UMinimapGeneratorManager::StartCaptureProcess(const FMinimapCaptureSettings& InSettings)
{
	UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - Starting minimap capture process."), *GetName(), *FString(__FUNCTION__));
	Settings = InSettings;
	CapturedTileData.Empty();
	CurrentTileIndex = 0;

	CalculateGrid();

	if (NumTilesX * NumTilesY == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s::%s] - Invalid grid size calculated."), *GetName(), *FString(__FUNCTION__));
		return;
	}
	MinimapStreamer = MakeShared<FMinimapStreamingSourceProvider>();
	ScreenshotCapturedDelegateHandle = FScreenshotRequest::OnScreenshotCaptured().AddUObject(
		this, &UMinimapGeneratorManager::OnScreenshotCaptured);

	// Bắt đầu xử lý tile đầu tiên
	ProcessNextTile();
}

void UMinimapGeneratorManager::OnSaveTaskCompleted(const bool bSuccess)
{
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Async save task completed successfully."));
		OnProgress.Broadcast(FText::FromString(TEXT("High Quality Capture Completed! Image saved.")), 1.0f, 1, 1);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Async save task failed."));
		OnProgress.Broadcast(FText::FromString(TEXT("Error saving high quality image!")), 1.0f, 1, 1);
	}
}

void UMinimapGeneratorManager::StartSingleCaptureForValidation(const FMinimapCaptureSettings& InSettings)
{
	UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - Starting SINGLE capture using ASceneCapture2D (High Quality)."),
	       *GetName(), *FString(__FUNCTION__));

	// Log message now reflects the chosen quality setting
	if (Settings.bOverrideWithHighQualitySettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - Starting ASYNC capture (Forcing High Quality)."), *GetName(),
		       *FString(__FUNCTION__));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - Starting ASYNC capture (Using Editor Scalability Settings)."),
		       *GetName(), *FString(__FUNCTION__));
	}
	Settings = InSettings;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s::%s] - Cannot get World."), *GetName(), *FString(__FUNCTION__));
		return;
	}

	// === 1. Calculate Camera Properties (No change here) ===
	const FVector BoundsSize = Settings.CaptureBounds.GetSize();
	const FVector BoundsCenter = Settings.CaptureBounds.GetCenter();
	const FVector CameraLocation = FVector(BoundsCenter.X, BoundsCenter.Y, Settings.CameraHeight);
	const FRotator CameraRotation = FRotator(-90.f, 0.f, -180.f);

	const float OutputAspectRatio = static_cast<float>(Settings.OutputWidth) / static_cast<float>(Settings.
		OutputHeight);
	const float CameraOrthoWidth = FMath::Max(BoundsSize.X, BoundsSize.Y * OutputAspectRatio);

	// === 2. Create an HDR Render Target ===
	// Use a floating-point format to support High Dynamic Range rendering for better lighting.
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(Settings.OutputWidth, Settings.OutputHeight, PF_FloatRGBA, true); // Use HDR format
	RenderTarget->UpdateResource();

	// === 3. Spawn and Configure the Scene Capture Actor ===
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(CameraLocation, CameraRotation);
	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();

	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	// Capture in HDR to get the best lighting and color data before post-processing.
	// CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
	CaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureComponent->OrthoWidth = CameraOrthoWidth;
	CaptureComponent->TextureTarget = RenderTarget;

	CaptureComponent->CaptureSource = Settings.bOverrideWithHighQualitySettings
		                                  ? ESceneCaptureSource::SCS_FinalColorHDR
		                                  : ESceneCaptureSource::SCS_FinalColorLDR;

	// // === 4. CRITICAL: Enable High-Quality Post-Processing ===
	// FPostProcessSettings& PPSettings = CaptureComponent->PostProcessSettings;
	// // --- Ambient Occlusion (for soft shadows and depth) ---
	// PPSettings.bOverride_AmbientOcclusionIntensity = true;
	// PPSettings.AmbientOcclusionIntensity = 0.5f; // Tweak this value as needed
	// PPSettings.bOverride_AmbientOcclusionQuality = true;
	// PPSettings.AmbientOcclusionQuality = 100.0f;
	//
	// // --- Reflections (for shiny surfaces) ---
	// PPSettings.bOverride_ScreenSpaceReflectionIntensity = true;
	// PPSettings.ScreenSpaceReflectionIntensity = 100.0f;
	// PPSettings.bOverride_ScreenSpaceReflectionQuality = true;
	// PPSettings.ScreenSpaceReflectionQuality = 100.0f;

	// === NEW LOGIC: Only apply high-quality overrides if the user wants to ===
	if (Settings.bOverrideWithHighQualitySettings)
	{
		// This block is now conditional. It only runs if the checkbox is ticked.
		FPostProcessSettings& PPSettings = CaptureComponent->PostProcessSettings;

		// --- Ambient Occlusion (for soft shadows and depth) ---
		PPSettings.bOverride_AmbientOcclusionIntensity = true;
		PPSettings.AmbientOcclusionIntensity = 0.5f;
		PPSettings.bOverride_AmbientOcclusionQuality = true;
		PPSettings.AmbientOcclusionQuality = 100.0f;

		// --- Reflections (for shiny surfaces) ---
		PPSettings.bOverride_ScreenSpaceReflectionIntensity = true;
		PPSettings.ScreenSpaceReflectionIntensity = 100.0f;
		PPSettings.bOverride_ScreenSpaceReflectionQuality = true;
		PPSettings.ScreenSpaceReflectionQuality = 100.0f;
	}

	// === 5. Perform the Capture ===
	CaptureComponent->CaptureScene();

	// Create a latent command to read pixels back on the next tick.
	// This gives the GPU one frame to process the capture command.
	FTimerHandle TempHandle;
	World->GetTimerManager().SetTimer(TempHandle, [this, RenderTarget, CaptureActor]()
	{
		TArray<FColor> CapturedPixels;
		if (FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource())
		{
			// This ReadPixels will still cause a small hitch, but it's now decoupled
			// from the much slower save operation.
			FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
			ReadPixelFlags.SetLinearToGamma(true);
			RTResource->ReadPixels(CapturedPixels, ReadPixelFlags);
		}

		// Clean up the actor immediately
		if (CaptureActor)
		{
			CaptureActor->Destroy();
		}

		if (CapturedPixels.Num() > 0)
		{
			// Offload the expensive saving operation to a background thread
			FString FullPath = FPaths::Combine(Settings.OutputPath, Settings.FileName + ".png");
			(new FAutoDeleteAsyncTask<FSaveImageTask>(CapturedPixels, Settings.OutputWidth, Settings.OutputHeight,
			                                          FullPath, this))->StartBackgroundTask();
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read pixels from Render Target."));
			OnSaveTaskCompleted(false);
		}
	}, 0.033f, false); // Delay for ~1-2 frames
}

void UMinimapGeneratorManager::OnScreenshotCaptured(const int32 Width, const int32 Height,
                                                    const TArray<FColor>& PixelData)
{
	if (bIsSingleCaptureMode)
	{
		// In single capture mode, we expect the screenshot to match the final output resolution.
		if (Width != Settings.OutputWidth || Height != Settings.OutputHeight)
		{
			UE_LOG(LogTemp, Warning,
			       TEXT(
				       "OnScreenshotCaptured: Mismatched resolution for single capture. Ignoring. Expected %dx%d, got %dx%d"
			       ), Settings.OutputWidth, Settings.OutputHeight, Width, Height);
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("Validation screenshot captured successfully. Saving image..."));

		// Save the image directly
		if (SaveFinalImage(PixelData, Width, Height))
		{
			OnProgress.Broadcast(FText::FromString(TEXT("Validation Completed! Image saved.")), 1.0f, 1, 1);
		}
		else
		{
			OnProgress.Broadcast(FText::FromString(TEXT("Error saving validation image!")), 1.0f, 1, 1);
		}

		// Clean up
		if (ScreenshotCapturedDelegateHandle.IsValid())
		{
			FScreenshotRequest::OnScreenshotCaptured().Remove(ScreenshotCapturedDelegateHandle);
			ScreenshotCapturedDelegateHandle.Reset();
		}
		bIsSingleCaptureMode = false;
	}
	else // Đây là logic chia ô cũ của bạn
	{
		// We need to check if this screenshot is for one of our tiles
		if (Width != Settings.TileResolution || Height != Settings.TileResolution || PixelData.Num() == 0)
		{
			return; // Ignore unwanted screenshots
		}

		const UWorld* World = GEditor->GetEditorWorldContext().World();
		if (UWorldPartitionSubsystem* Wp = World->GetSubsystem<UWorldPartitionSubsystem>())
		{
			if (MinimapStreamer.IsValid() && Wp->IsStreamingSourceProviderRegistered(MinimapStreamer.Get()))
			{
				Wp->UnregisterStreamingSourceProvider(MinimapStreamer.Get());
			}
		}

		OnTileCaptureCompleted(CurrentCaptureTilePosition.X, CurrentCaptureTilePosition.Y, PixelData);

		CurrentTileIndex++;

		FTimerHandle NextTileDelayHandle;
		World->GetTimerManager().SetTimer(NextTileDelayHandle, this, &UMinimapGeneratorManager::ProcessNextTile, 0.1f,
		                                  false);
	}
}

void UMinimapGeneratorManager::CalculateGrid()
{
	// The logic should be based on pixel space, not world space, to ensure the final stitched image is correct.
	const int32 EffectiveTileWidth = Settings.TileResolution - Settings.TileOverlap;
	const int32 EffectiveTileHeight = Settings.TileResolution - Settings.TileOverlap;

	if (EffectiveTileWidth <= 0 || EffectiveTileHeight <= 0)
	{
		UE_LOG(LogTemp, Error,
		       TEXT("[%s::%s] - Effective tile size is zero or negative. TileOverlap might be too large."), *GetName(),
		       *FString(__FUNCTION__));
		NumTilesX = 0;
		NumTilesY = 0;
		return;
	}

	// Calculate how many tiles are needed to cover the output dimensions.
	// The first tile covers 'TileResolution' pixels. Each following tile adds 'EffectiveTileWidth' pixels.
	if (Settings.OutputWidth <= Settings.TileResolution)
	{
		NumTilesX = 1;
	}
	else
	{
		NumTilesX = 1 + FMath::CeilToInt(
			static_cast<float>(Settings.OutputWidth - Settings.TileResolution) / EffectiveTileWidth);
	}

	if (Settings.OutputHeight <= Settings.TileResolution)
	{
		NumTilesY = 1;
	}
	else
	{
		NumTilesY = 1 + FMath::CeilToInt(
			static_cast<float>(Settings.OutputHeight - Settings.TileResolution) / EffectiveTileHeight);
	}

	UE_LOG(LogTemp, Log, TEXT("[%s::%s] - Calculated Grid: %d x %d tiles"), *GetName(), *FString(__FUNCTION__),
	       NumTilesX, NumTilesY);
}

void UMinimapGeneratorManager::ProcessNextTile()
{
	if (CurrentTileIndex >= NumTilesX * NumTilesY)
	{
		OnAllTasksCompleted();
		return;
	}

	if (!MinimapStreamer.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[%s::%s] - MinimapStreamer is not valid! Aborting process."), *GetName(),
		       *FString(__FUNCTION__));
		OnAllTasksCompleted();
		return;
	}

	const int32 TileX = CurrentTileIndex % NumTilesX;
	const int32 TileY = CurrentTileIndex / NumTilesX;

	const FVector BoundsMin = Settings.CaptureBounds.Min;
	const FVector BoundsSize = Settings.CaptureBounds.GetSize();
	const float TileWorldWidth = BoundsSize.X / NumTilesX;
	const float TileWorldHeight = BoundsSize.Y / NumTilesY;

	const FVector TileCenter = BoundsMin + FVector(
		(TileX + 0.5f) * TileWorldWidth,
		(TileY + 0.5f) * TileWorldHeight,
		Settings.CameraHeight
	);

	const float StreamingRadius = FMath::Max(TileWorldWidth, TileWorldHeight) * 0.7f;

	UE_LOG(LogTemp, Log, TEXT("Processing Tile (%d, %d) at location %s"), TileX, TileY, *TileCenter.ToString());

	const UWorld* World = GEditor->GetEditorWorldContext().World();
	// CORRECTED: Using the proper GetSubsystem method.
	if (UWorldPartitionSubsystem* Wp = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		MinimapStreamer->SetSourceLocation(TileCenter, StreamingRadius);
		Wp->RegisterStreamingSourceProvider(MinimapStreamer.Get());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[%s::%s] - Failed to get WorldPartitionSubsystem! Aborting process."), *GetName(),
		       *FString(__FUNCTION__));
		OnAllTasksCompleted();
		return;
	}

	World->GetTimerManager().SetTimer(StreamingCheckTimer, this, &UMinimapGeneratorManager::CheckStreamingAndCapture,
	                                  0.5f, true);
}

void UMinimapGeneratorManager::CheckStreamingAndCapture()
{
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (const UWorldPartitionSubsystem* Wp = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		if (Wp->IsStreamingCompleted(MinimapStreamer.Get()))
		{
			World->GetTimerManager().ClearTimer(StreamingCheckTimer);
			UE_LOG(LogTemp, Log, TEXT("Streaming complete for tile %d."), CurrentTileIndex);

			CaptureTileWithScreenshot(); // GỌI HÀM MỚI
		}
	}
}

void UMinimapGeneratorManager::CaptureTileWithScreenshot()
{
	const int32 TileX = CurrentTileIndex % NumTilesX;
	const int32 TileY = CurrentTileIndex / NumTilesX;
	CurrentCaptureTilePosition = FIntPoint(TileX, TileY);

	// ================== CRITICAL FIX STARTS HERE ==================
	// Calculate the correct OrthoWidth and camera position again, just like in ProcessNextTile
	const FVector BoundsSize = Settings.CaptureBounds.GetSize();
	const float WorldUnitsPerPixelX = BoundsSize.X / Settings.OutputWidth;
	const float WorldUnitsPerPixelY = BoundsSize.Y / Settings.OutputHeight;

	const float TileWorldSizeX = Settings.TileResolution * WorldUnitsPerPixelX;
	const float TileWorldSizeY = Settings.TileResolution * WorldUnitsPerPixelY;
	const float CameraOrthoWidth = FMath::Max(TileWorldSizeX, TileWorldSizeY);

	const float StepWorldSizeX = (Settings.TileResolution - Settings.TileOverlap) * WorldUnitsPerPixelX;
	const float StepWorldSizeY = (Settings.TileResolution - Settings.TileOverlap) * WorldUnitsPerPixelY;

	const FVector BoundsMin = Settings.CaptureBounds.Min;
	const FVector FirstTileCenter = BoundsMin + FVector(CameraOrthoWidth / 2.0f, CameraOrthoWidth / 2.0f, 0);

	const FVector TileCenter = FirstTileCenter + FVector(
		TileX * StepWorldSizeX,
		TileY * StepWorldSizeY,
		Settings.CameraHeight - BoundsMin.Z
	);

	if (FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(GEditor->
		GetActiveViewport()->
		GetClient()))
	{
		// 1. Set viewport to Top Orthographic
		ViewportClient->SetViewportType(ELevelViewportType::LVT_OrthoXY);

		// 2. Set the camera position and rotation directly
		ViewportClient->SetViewLocation(TileCenter);
		ViewportClient->SetViewRotation(FRotator(-90.f, 0.f, 0.f));

		// 3. THIS IS THE KEY: Set the OrthoWidth explicitly and consistently for every tile.
		ViewportClient->SetOrthoZoom(CameraOrthoWidth * 0.5f); // SetOrthoZoom takes half of the desired OrthoWidth

		// 4. Redraw the viewport with the new settings
		ViewportClient->Invalidate();
		GEditor->GetActiveViewport()->Draw();
	}
	// ================== CRITICAL FIX ENDS HERE ==================

	// Request screenshot
	FHighResScreenshotConfig& HighResScreenshotConfig = GetHighResScreenshotConfig();
	HighResScreenshotConfig.SetResolution(Settings.TileResolution, Settings.TileResolution, 1.0f);
	HighResScreenshotConfig.bDumpBufferVisualizationTargets = false;
	HighResScreenshotConfig.bCaptureHDR = false;
	FScreenshotRequest::RequestScreenshot(false);
}

// void UMinimapGeneratorManager::OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& PixelData)
// {
// 	// Delegate đã được gọi! Dữ liệu đã sẵn sàng trên Game Thread.
//
// 	// Chúng ta cần kiểm tra xem screenshot này có phải là của chúng ta không
// 	// (ví dụ: kích thước có khớp không), vì đây là delegate toàn cục.
// 	if (Width != Settings.TileResolution || Height != Settings.TileResolution || PixelData.Num() == 0)
// 	{
// 		if (Width != Settings.TileResolution)
// 		{
// 			UE_LOG(LogTemp, Error, TEXT("Screenshot width mismatch. Expected: %d, Got: %d"), Settings.TileResolution,
// 				   Width);
// 		}
// 		if (Height != Settings.TileResolution)
// 		{
// 			UE_LOG(LogTemp, Error, TEXT("Screenshot height mismatch. Expected: %d, Got: %d"), Settings.TileResolution,
// 				   Height);
// 		}
// 		if (PixelData.Num() == 0)
// 		{
// 			UE_LOG(LogTemp, Error, TEXT("Screenshot pixel data is empty"));
// 		}
// 		return; // Bỏ qua screenshot không mong muốn
// 	}
//
// 	const UWorld* World = GEditor->GetEditorWorldContext().World();
// 	if (UWorldPartitionSubsystem* Wp = World->GetSubsystem<UWorldPartitionSubsystem>())
// 	{
// 		if (MinimapStreamer.IsValid() && Wp->IsStreamingSourceProviderRegistered(MinimapStreamer.Get()))
// 		{
// 			Wp->UnregisterStreamingSourceProvider(MinimapStreamer.Get());
// 		}
// 	}
//
// 	OnTileCaptureCompleted(CurrentCaptureTilePosition.X, CurrentCaptureTilePosition.Y, PixelData);
//
// 	CurrentTileIndex++;
//
// 	FTimerHandle NextTileDelayHandle;
// 	World->GetTimerManager().SetTimer(NextTileDelayHandle, this, &UMinimapGeneratorManager::ProcessNextTile, 0.1f,
// 									  false);
// }

void UMinimapGeneratorManager::OnTileCaptureCompleted(const int32 TileX, const int32 TileY, TArray<FColor> PixelData)
{
	// Hàm này được gọi từ Game Thread, nên nó an toàn
	if (PixelData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Received empty pixel data for tile (%d, %d)."), TileX, TileY);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Tile (%d, %d) capture completed."), TileX, TileY);
	CapturedTileData.Add(FIntPoint(TileX, TileY), MoveTemp(PixelData));

	// Update progress UI
	const float Progress = static_cast<float>(CapturedTileData.Num()) / (NumTilesX * NumTilesY);
	OnProgress.Broadcast(
		FText::FromString(
			FString::Printf(TEXT("Captured tile %d / %d"), CapturedTileData.Num(), NumTilesX * NumTilesY)),
		Progress, CapturedTileData.Num(), NumTilesX * NumTilesY);
}

void UMinimapGeneratorManager::StartStitching()
{
	const int32 FinalWidth = Settings.OutputWidth;
	const int32 FinalHeight = Settings.OutputHeight;
	TArray<FColor> FinalImageData;
	FinalImageData.AddUninitialized(FinalWidth * FinalHeight);

	const int32 TileRes = Settings.TileResolution;
	const int32 Overlap = Settings.TileOverlap;
	const int32 EffectiveTileRes = TileRes - Overlap;

	for (int32 TileY = 0; TileY < NumTilesY; ++TileY)
	{
		for (int32 TileX = 0; TileX < NumTilesX; ++TileX)
		{
			const FIntPoint CurrentTilePos(TileX, TileY);
			if (!CapturedTileData.Contains(CurrentTilePos))
			{
				UE_LOG(LogTemp, Error, TEXT("[%s::%s] - Missing data for tile (%d, %d)!"), *GetName(),
				       *FString(__FUNCTION__), TileX, TileY);
				continue;
			}

			const TArray<FColor>& TilePixels = CapturedTileData[CurrentTilePos];

			// Calculate the top-left starting position on the final canvas for this tile.
			const int32 CanvasStartX = TileX * EffectiveTileRes;
			const int32 CanvasStartY = TileY * EffectiveTileRes;

			for (int32 y = 0; y < TileRes; ++y)
			{
				for (int32 x = 0; x < TileRes; ++x)
				{
					// Calculate the target pixel coordinate on the final canvas.
					const int32 CanvasX = CanvasStartX + x;
					const int32 CanvasY = CanvasStartY + y;

					// Ensure we don't write outside the bounds of the final image.
					if (CanvasX >= FinalWidth || CanvasY >= FinalHeight) continue;

					const int32 CanvasIndex = CanvasY * FinalWidth + CanvasX;
					const int32 TileIndex = y * TileRes + x;

					const FColor NewPixel = TilePixels[TileIndex];

					// Blending logic
					float BlendX = 1.0f;
					float BlendY = 1.0f;

					// Left overlap
					if (TileX > 0 && x < Overlap)
					{
						BlendX = static_cast<float>(x) / (Overlap - 1);
					}
					// Top overlap
					if (TileY > 0 && y < Overlap)
					{
						BlendY = static_cast<float>(y) / (Overlap - 1);
					}

					if (const float BlendFactor = FMath::Min(BlendX, BlendY); BlendFactor < 1.0f)
					{
						const FColor ExistingPixel = FinalImageData[CanvasIndex];
						const FLinearColor FromColor(ExistingPixel);
						const FLinearColor ToColor(NewPixel);
						const FLinearColor BlendedLinearColor = FLinearColor::LerpUsingHSV(
							FromColor, ToColor, BlendFactor);
						FinalImageData[CanvasIndex] = BlendedLinearColor.ToFColor(true);
					}
					else
					{
						FinalImageData[CanvasIndex] = NewPixel;
					}
				}
			}
		}
	}

	// Free up memory
	CapturedTileData.Empty();

	if (SaveFinalImage(FinalImageData, FinalWidth, FinalHeight))
	{
		OnProgress.Broadcast(FText::FromString(TEXT("Completed! Image saved.")), 1.0f, NumTilesX * NumTilesY,
		                     NumTilesX * NumTilesY);
	}
	else
	{
		OnProgress.Broadcast(FText::FromString(TEXT("Error saving final image!")), 1.0f, NumTilesX * NumTilesY,
		                     NumTilesX * NumTilesY);
	}
}

void UMinimapGeneratorManager::OnAllTasksCompleted()
{
	UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - All tiles captured. Starting stitching process."), *GetName(),
	       *FString(__FUNCTION__));

	if (ScreenshotCapturedDelegateHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotCaptured().Remove(ScreenshotCapturedDelegateHandle);
		ScreenshotCapturedDelegateHandle.Reset();
	}

	StartStitching();
}

bool UMinimapGeneratorManager::SaveFinalImage(const TArray<FColor>& ImageData, int32 Width, int32 Height)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(
		FName("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid() || !ImageWrapper->SetRaw(ImageData.GetData(), ImageData.Num() * sizeof(FColor), Width,
	                                                     Height, ERGBFormat::BGRA, 8))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to set raw image data for PNG wrapper."));
		return false;
	}

	const FString FullPath = FPaths::Combine(Settings.OutputPath, Settings.FileName + ".png");
	return FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *FullPath);
}
