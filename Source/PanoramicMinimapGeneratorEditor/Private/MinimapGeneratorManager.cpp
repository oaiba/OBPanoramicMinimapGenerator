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
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Factories/TextureFactory.h"

// AsyncTask to save image data to disk on a background thread
class FSaveImageTask : public FNonAbandonableTask
{
public:
	FSaveImageTask(TArray<FColor> InPixelData, const int32 InWidth, const int32 InHeight, FString InFullPath, const TWeakObjectPtr<UMinimapGeneratorManager> InManager)
		: PixelData(MoveTemp(InPixelData)), Width(InWidth), Height(InHeight), FullPath(MoveTemp(InFullPath)), ManagerPtr(InManager) {}

	void DoWork()
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		if (const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper.IsValid() && ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
		{
			const bool bSuccess = FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *FullPath);
			AsyncTask(ENamedThreads::GameThread, [ManagerPtr = this->ManagerPtr, bSuccess, Path = this->FullPath]
			{
				if (UMinimapGeneratorManager* Manager = ManagerPtr.Get())
				{
					Manager->OnSaveTaskCompleted(bSuccess, Path);
				}
			});
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [ManagerPtr = this->ManagerPtr]
			{
				if (UMinimapGeneratorManager* Manager = ManagerPtr.Get())
				{
					Manager->OnSaveTaskCompleted(false, TEXT("ImageWrapper failed."));
				}
			});
		}
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveImageTask, STATGROUP_ThreadPoolAsyncTasks); }

protected:
	TArray<FColor> PixelData;
	int32 Width;
	int32 Height;
	FString FullPath;
	TWeakObjectPtr<UMinimapGeneratorManager> ManagerPtr;
};

// =================== START OF NEW CODE ===================
// AsyncTask to save INDIVIDUAL DEBUG TILES to disk on a background thread.
// This task is simpler and does NOT call back to the manager to avoid triggering finalization logic.
class FSaveDebugTileTask : public FNonAbandonableTask
{
public:
	FSaveDebugTileTask(TArray<FColor> InPixelData, const int32 InWidth, const int32 InHeight, FString InFullPath)
		: PixelData(MoveTemp(InPixelData)), Width(InWidth), Height(InHeight), FullPath(MoveTemp(InFullPath)) {}

	void DoWork()
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		if (const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper.IsValid() && ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
		{
			const bool bSuccess = FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *FullPath);
			
			// Log the result back on the game thread for visibility
			AsyncTask(ENamedThreads::GameThread, [bSuccess, Path = this->FullPath]
			{
				if (bSuccess)
				{
					UE_LOG(LogTemp, Log, TEXT("Debug tile saved successfully: %s"), *Path);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("Failed to save debug tile: %s"), *Path);
				}
			});
		}
		else
		{
			// Log failure
			AsyncTask(ENamedThreads::GameThread, [Path = this->FullPath]
			{
				UE_LOG(LogTemp, Error, TEXT("ImageWrapper failed for debug tile: %s"), *Path);
			});
		}
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveDebugTileTask, STATGROUP_ThreadPoolAsyncTasks); }

protected:
	TArray<FColor> PixelData;
	int32 Width;
	int32 Height;
	FString FullPath;
};

// Helper function to start the debug tile saving task
void SaveDebugTileImage(const FString& BasePath, const FString& BaseFileName, const TArray<FColor>& PixelData, int32 TileX, int32 TileY, int32 TileResolution)
{
	// Create a descriptive filename for the debug tile, e.g., "Minimap_Result_Tile_0_1.png"
	const FString DebugFileName = FString::Printf(TEXT("%s_Tile_%d_%d.png"), *BaseFileName, TileX, TileY);
	const FString FullPath = FPaths::Combine(BasePath, DebugFileName);

	// Start the dedicated async task for saving the debug tile.
	// We pass a copy of PixelData because the original will be moved into the main TMap.
	(new FAutoDeleteAsyncTask<FSaveDebugTileTask>(PixelData, TileResolution, TileResolution, FullPath))->StartBackgroundTask();
}
// =================== END OF NEW CODE ===================

void UMinimapGeneratorManager::StartCaptureProcess(const FMinimapCaptureSettings& InSettings)
{
	UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - Starting minimap capture process."), *GetName(), *FString(__FUNCTION__));
	this->Settings = InSettings;
	if (Settings.bUseTiling)
	{
		StartTiledCaptureProcess();
	}
	else
	{
		// Giữ nguyên tên hàm cũ để tương thích
		StartSingleCaptureForValidation();
	}
	// MinimapStreamer = MakeShared<FMinimapStreamingSourceProvider>();
	// ScreenshotCapturedDelegateHandle = FScreenshotRequest::OnScreenshotCaptured().AddUObject(
	// 	this, &UMinimapGeneratorManager::OnScreenshotCaptured);
	//
	// // Bắt đầu xử lý tile đầu tiên
	// ProcessNextTile();
}

void UMinimapGeneratorManager::OnSaveTaskCompleted(const bool bSuccess, const FString& SavedImagePath) const
{
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Async save task completed successfully. Path: %s"), *SavedImagePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Async save task failed."));
	}
	// Broadcast delegate mới thay vì OnProgress
	OnCaptureComplete.Broadcast(bSuccess, SavedImagePath);

	// Chỉ thực hiện import nếu thành công và người dùng đã chọn tùy chọn này
	if (bSuccess && Settings.bImportAsTextureAsset && !SavedImagePath.IsEmpty())
	{
		// --- BẮT ĐẦU LOGIC TẠO ASSET MỚI ---
		// FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

		const FString AssetName = TEXT("T_") + FPaths::GetBaseFilename(SavedImagePath);
		FString PackagePath = Settings.AssetPath;
		if (!PackagePath.EndsWith(TEXT("/")))
		{
			PackagePath += TEXT("/");
		}
		const FString FullAssetPath = PackagePath + AssetName;

		UPackage* Package = CreatePackage(*FullAssetPath);
		Package->FullyLoad();

		TArray<uint8> PngData;
		if (!FFileHelper::LoadFileToArray(PngData, *SavedImagePath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load saved PNG file from disk: %s"), *SavedImagePath);
			return;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(
			FName("ImageWrapper"));

		if (const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper.IsValid() && ImageWrapper->SetCompressed(PngData.GetData(), PngData.Num()))
		{
			if (TArray<uint8> UncompressedBGRA; ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
			{
				UTexture2D* NewTexture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
				NewTexture->AddToRoot();

				NewTexture->Source.Init(ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), 1, 1, TSF_BGRA8,
										UncompressedBGRA.GetData());
				NewTexture->SRGB = true;
				NewTexture->CompressionSettings = TC_Default;
				NewTexture->UpdateResource();
				// Package->MarkPackageDirty();

				// SỬA LỖI 1: Load module AssetRegistry trước khi sử dụng
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
					TEXT("AssetRegistry"));
				AssetRegistryModule.AssetCreated(NewTexture);

				UE_LOG(LogTemp, Log, TEXT("Successfully created texture asset: %s"), *FullAssetPath);

				// SỬA LỖI 2: Tạo TArray tường minh để tránh lỗi ambiguous call
				TArray<UObject*> AssetsToSync;
				AssetsToSync.Add(NewTexture);
				GEditor->SyncBrowserToObjects(AssetsToSync);

				NewTexture->RemoveFromRoot();
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to decode PNG data from file: %s"), *SavedImagePath);
		}
	}
}

// ===================================================================
// LUỒNG LOGIC 1: SINGLE CAPTURE (CHỤP MỘT LẦN)
// ===================================================================

void UMinimapGeneratorManager::StartSingleCaptureForValidation()
{
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	ActiveRenderTarget = CreateRenderTarget();
	if (!ActiveRenderTarget.IsValid()) return;

	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());
	if (!ActiveCaptureActor.IsValid()) return;
	
	ActiveCaptureActor->GetCaptureComponent2D()->CaptureScene();
	World->GetTimerManager().SetTimerForNextTick(this, &UMinimapGeneratorManager::ReadPixelsAndFinalize);
}

// UTextureRenderTarget2D* UMinimapGeneratorManager::CreateRenderTarget() const
// {
// 	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
// 	if (Settings.BackgroundMode == EMinimapBackgroundMode::Transparent)
// 	{
// 		RenderTarget->ClearColor = FLinearColor::Transparent;
// 	}
// 	else // SolidColor
// 	{
// 		RenderTarget->ClearColor = Settings.BackgroundColor;
// 	}
// 	const int32 Width = Settings.bUseTiling ? Settings.TileResolution : Settings.OutputWidth;
// 	const int32 Height = Settings.bUseTiling ? Settings.TileResolution : Settings.OutputHeight;
//
// 	RenderTarget->InitCustomFormat(Width, Height, PF_FloatRGBA, true);
// 	return RenderTarget;
// }

// ===================================================================
// CÁC HÀM HELPER CHUNG
// ===================================================================

UTextureRenderTarget2D* UMinimapGeneratorManager::CreateRenderTarget() const
{
	const int32 TargetWidth = Settings.bUseTiling ? Settings.TileResolution : Settings.OutputWidth;
	const int32 TargetHeight = Settings.bUseTiling ? Settings.TileResolution : Settings.OutputHeight;

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();

	if (Settings.BackgroundMode == EMinimapBackgroundMode::Transparent)
	{
		RenderTarget->ClearColor = FLinearColor::Transparent;
	}
	else // SolidColor
	{
		RenderTarget->ClearColor = Settings.BackgroundColor;
	}

	RenderTarget->InitCustomFormat(TargetWidth, TargetHeight, PF_FloatRGBA, true);

	UE_LOG(LogTemp, Log, TEXT("Created Render Target (%dx%d)."), TargetWidth, TargetHeight);
	return RenderTarget;
}

ASceneCapture2D* UMinimapGeneratorManager::SpawnAndConfigureCaptureActor(UTextureRenderTarget2D* RenderTarget) const
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !RenderTarget) return nullptr;

	const FVector BoundsSize = Settings.CaptureBounds.GetSize();
	const FVector BoundsCenter = Settings.CaptureBounds.GetCenter();
	const FVector CameraLocation = FVector(BoundsCenter.X, BoundsCenter.Y, Settings.CameraHeight);
	const FRotator CameraRotation = Settings.CameraRotation;
	float OutputAspectRatio;
	float CameraOrthoWidth;
	if (Settings.OutputWidth >= Settings.OutputHeight)
	{
		OutputAspectRatio = static_cast<float>(Settings.OutputWidth) / static_cast<float>(Settings.
			OutputHeight);
		CameraOrthoWidth = FMath::Max(BoundsSize.X, BoundsSize.Y * OutputAspectRatio);
	}
	else
	{
		OutputAspectRatio = static_cast<float>(Settings.OutputHeight) / static_cast<float>(Settings.OutputWidth);
		CameraOrthoWidth = FMath::Max(BoundsSize.Y, BoundsSize.X / OutputAspectRatio);
	}

	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(CameraLocation, CameraRotation);
	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();

	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureComponent->OrthoWidth = CameraOrthoWidth;
	CaptureComponent->CompositeMode = SCCM_Additive;

	if (Settings.bOverrideWithHighQualitySettings)
	{
		CaptureComponent->CaptureSource = Settings.CaptureSource;

		FPostProcessSettings& PPSettings = CaptureComponent->PostProcessSettings;
		PPSettings.bOverride_AmbientOcclusionIntensity = true;
		PPSettings.AmbientOcclusionIntensity = Settings.AmbientOcclusionIntensity;
		PPSettings.bOverride_AmbientOcclusionQuality = true;
		PPSettings.AmbientOcclusionQuality = Settings.AmbientOcclusionQuality;
		PPSettings.bOverride_ScreenSpaceReflectionIntensity = true;
		PPSettings.ScreenSpaceReflectionIntensity = Settings.ScreenSpaceReflectionIntensity;
		PPSettings.bOverride_ScreenSpaceReflectionQuality = true;
		PPSettings.ScreenSpaceReflectionQuality = Settings.ScreenSpaceReflectionQuality;
	}
	else
	{
		// Giữ nguyên logic cũ khi không override
		CaptureComponent->CaptureSource = SCS_FinalColorLDR;
		// CaptureComponent->CaptureSource = SCS_SceneColorHDRNoAlpha;
	}

	return CaptureActor;
}

void UMinimapGeneratorManager::ReadPixelsAndFinalize()
{
	ASceneCapture2D* CaptureActor = ActiveCaptureActor.Get();
	UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	if (!CaptureActor || !RenderTarget)
	{
		OnSaveTaskCompleted(false, TEXT("Capture Actor or Render Target became invalid."));
		return;
	}

	TArray<FColor> CapturedPixels;
	if (FTextureRenderTargetResource* RTResource = static_cast<FTextureRenderTargetResource*>(RenderTarget->GetResource()))
	{
		FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
		ReadPixelFlags.SetLinearToGamma(true);
		RTResource->ReadPixels(CapturedPixels, ReadPixelFlags);
	}

	CaptureActor->Destroy();
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();

	if (CapturedPixels.Num() > 0)
	{
		StartImageSaveTask(MoveTemp(CapturedPixels), Settings.OutputWidth, Settings.OutputHeight);
	}
	else
	{
		OnSaveTaskCompleted(false, TEXT("Failed to read pixels from Render Target."));
	}
}

// void UMinimapGeneratorManager::StartImageSaveTask(TArray<FColor> PixelData)
// {
// 	FString FinalFileName = Settings.FileName;
// 	if (Settings.bUseAutoFilename)
// 	{
// 		const FDateTime Now = FDateTime::Now();
// 		const FString Timestamp = Now.ToString(TEXT("_%Y%m%d_%H%M%S"));
// 		FinalFileName += Timestamp;
// 	}
// 	FinalFileName += TEXT(".png");
//
// 	const FString FullPath = FPaths::Combine(Settings.OutputPath, FinalFileName);
//
// 	(new FAutoDeleteAsyncTask<FSaveImageTask>(MoveTemp(PixelData), Settings.OutputWidth, Settings.OutputHeight,
// 											  FullPath, this))->StartBackgroundTask();
// }

void UMinimapGeneratorManager::StartImageSaveTask(TArray<FColor> PixelData, int32 ImageWidth, int32 ImageHeight)
{
	FString FinalFileName = Settings.FileName;
	if (Settings.bUseAutoFilename)
	{
		const FDateTime Now = FDateTime::Now();
		const FString Timestamp = Now.ToString(TEXT("_%Y%m%d_%H%M%S"));
		FinalFileName += Timestamp;
	}
	FinalFileName += TEXT(".png");

	const FString FullPath = FPaths::Combine(Settings.OutputPath, FinalFileName);

	(new FAutoDeleteAsyncTask<FSaveImageTask>(MoveTemp(PixelData), ImageWidth, ImageHeight, FullPath, this))->
		StartBackgroundTask();
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

// ===================================================================
// LUỒNG LOGIC 2: TILED CAPTURE (CHỤP THEO Ô)
// ===================================================================

void UMinimapGeneratorManager::StartTiledCaptureProcess()
{
	CurrentTileIndex = 0;
	CapturedTileData.Empty();
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();

	CalculateGrid();

	if (NumTilesX * NumTilesY == 0)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Invalid grid size."));
		return;
	}

	ActiveRenderTarget = CreateRenderTarget();
	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());

	if (!ActiveCaptureActor.IsValid() || !ActiveRenderTarget.IsValid())
	{
		OnCaptureComplete.Broadcast(false, TEXT("Failed to create capture actor or render target for tiling."));
		return;
	}

	CaptureNextTile();
}

void UMinimapGeneratorManager::CalculateGrid()
{
	const int32 EffectiveTileRes = Settings.TileResolution - Settings.TileOverlap;
	if (EffectiveTileRes <= 0) { NumTilesX = 0; NumTilesY = 0; return; }

	NumTilesX = (Settings.OutputWidth <= Settings.TileResolution) ? 1 : (1 + FMath::CeilToInt(static_cast<float>(Settings.OutputWidth - Settings.TileResolution) / EffectiveTileRes));
	NumTilesY = (Settings.OutputHeight <= Settings.TileResolution) ? 1 : (1 + FMath::CeilToInt(static_cast<float>(Settings.OutputHeight - Settings.TileResolution) / EffectiveTileRes));
	UE_LOG(LogTemp, Log, TEXT("Calculated Grid: %d x %d tiles"), NumTilesX, NumTilesY);
}

// MinimapGeneratorManager.cpp

// ... (các hàm trước CaptureNextTile giữ nguyên) ...

void UMinimapGeneratorManager::CaptureNextTile()
{
	if (CurrentTileIndex >= NumTilesX * NumTilesY)
	{
		StartStitching();
		return;
	}
	if (!ActiveCaptureActor.IsValid() || !ActiveRenderTarget.IsValid())
	{
		OnCaptureComplete.Broadcast(false, TEXT("Capture actor or render target became invalid during tiling process."));
		return;
	}
	
	const int32 TileX = CurrentTileIndex % NumTilesX;
	const int32 TileY = CurrentTileIndex / NumTilesX;
	
	// =================== CRITICAL FIX STARTS HERE ===================

	// 1. Calculate the consistent World Units Per Pixel (WUPP)
	// We use the maximum dimension of the World Bounds and the Output Resolution to get a single, 
	// non-stretched WUPP value. This ensures 1 pixel is always the same world distance.
	const FVector WorldBoundsSize = Settings.CaptureBounds.GetSize();
	const float MapWorldMaxDim = FMath::Max(WorldBoundsSize.X, WorldBoundsSize.Y);
	const int32 MapOutputMaxDim = FMath::Max(Settings.OutputWidth, Settings.OutputHeight);
	
	// WUPP: The world distance of ONE PIXEL, consistent for both X and Y.
	const float WorldUnitsPerPixel = MapWorldMaxDim / MapOutputMaxDim; 

	// 2. Calculate the World Size of the TILE's capture area (TileOrthoSize)
	// Since the Render Target is square, the world-capture area must also be square (OrthoWidth x OrthoWidth).
	const float TileOrthoSize = Settings.TileResolution * WorldUnitsPerPixel; 

	// 3. Calculate the world step size (distance between tile start points)
	const float EffectiveTileRes = Settings.TileResolution - Settings.TileOverlap;
	const float StepWorldX = EffectiveTileRes * WorldUnitsPerPixel;
	const float StepWorldY = EffectiveTileRes * WorldUnitsPerPixel;

	// 4. Calculate the Tile's Center Location in world coordinates
	const FVector BoundsMin = Settings.CaptureBounds.Min;
	const FVector TileCenterLocation = BoundsMin + FVector(
		TileX * StepWorldX + TileOrthoSize * 0.5f,
		TileY * StepWorldY + TileOrthoSize * 0.5f,
		Settings.CameraHeight);

	// 5. Configure the Capture Actor
	ActiveCaptureActor->SetActorLocation(TileCenterLocation);
	USceneCaptureComponent2D* CaptureComponent = ActiveCaptureActor->GetCaptureComponent2D();
	
	// Set the OrthoWidth to the calculated square size of the tile's capture area
	CaptureComponent->OrthoWidth = TileOrthoSize;
	
	// =================== CRITICAL FIX ENDS HERE ===================
	
	CaptureComponent->CaptureScene();

	OnProgress.Broadcast(FText::Format(NSLOCTEXT("MinimapGeneratorManager", "CapturingTileFmt", "Capturing tile {0}/{1}..."), FText::AsNumber(CurrentTileIndex + 1), FText::AsNumber(NumTilesX * NumTilesY)),
		static_cast<float>(CurrentTileIndex) / (NumTilesX * NumTilesY), CurrentTileIndex, NumTilesX * NumTilesY);
	GEditor->GetEditorWorldContext().World()->GetTimerManager().SetTimerForNextTick(this, &UMinimapGeneratorManager::OnTileRenderedAndContinue);
}

// ... (các hàm sau CaptureNextTile giữ nguyên) ...

void UMinimapGeneratorManager::OnTileRenderedAndContinue()
{
	UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	if (!RenderTarget)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Render Target lost during tiling."));
		return;
	}

	TArray<FColor> TilePixels;
	if (auto* RTResource = static_cast<FTextureRenderTargetResource*>(RenderTarget->GetResource()))
	{
		RTResource->ReadPixels(TilePixels);
	}
	
	if (TilePixels.Num() > 0)
	{
		const int32 TileX = CurrentTileIndex % NumTilesX;
		const int32 TileY = CurrentTileIndex / NumTilesX;

		// =================== START OF MODIFICATION ===================
		// If the user wants to save debug tiles, do it here.
		if (Settings.bSaveTiles)
		{
			// Get the base filename without timestamp or extension
			const FString BaseFileName = Settings.FileName;
			if (Settings.bUseAutoFilename)
			{
				// In case auto-filename is used, we just use the base name without a timestamp for the tiles
				// to keep it clean. You could add a timestamp here too if desired.
			}
			
			SaveDebugTileImage(Settings.OutputPath, BaseFileName, TilePixels, TileX, TileY, Settings.TileResolution);
		}
		// =================== END OF MODIFICATION ===================
		
		CapturedTileData.Add(FIntPoint(TileX, TileY), MoveTemp(TilePixels));
	}

	CurrentTileIndex++;
	CaptureNextTile();
}

// void UMinimapGeneratorManager::StartStitching()
// {
// 	UE_LOG(LogTemp, Log, TEXT("[%s::%s] - All tiles captured. Starting stitching process..."), *GetName(), *FString(__FUNCTION__));
//
// 	// Clean up capture actor and render target as they are no longer needed
// 	if (ActiveCaptureActor.IsValid())
// 	{
// 		ActiveCaptureActor->Destroy();
// 	}
// 	ActiveCaptureActor.Reset();
// 	ActiveRenderTarget.Reset();
//
// 	// Ensure there is data to stitch
// 	if (CapturedTileData.Num() == 0)
// 	{
// 		UE_LOG(LogTemp, Error, TEXT("[%s::%s] - No tile data was captured. Aborting stitching."), *GetName(), *FString(__FUNCTION__));
// 		OnCaptureComplete.Broadcast(false, TEXT("No tile data was captured."));
// 		return;
// 	}
// 	
// 	// Pre-allocate memory for the final image to avoid reallocations
// 	TArray<FColor> FinalImageData;
// 	FinalImageData.AddUninitialized(Settings.OutputWidth * Settings.OutputHeight);
//
// 	// The effective resolution of a tile when considering overlap (the distance to step to the next tile)
// 	const int32 EffectiveTileRes = Settings.TileResolution - Settings.TileOverlap;
//
// 	// Iterate over every captured tile
// 	for (const auto& TilePair : CapturedTileData)
// 	{
// 		const FIntPoint& TileCoord = TilePair.Key;
// 		const TArray<FColor>& TilePixels = TilePair.Value;
//
// 		// Calculate the top-left starting position (in pixels) on the final canvas for this tile
// 		const int32 CanvasStartX = TileCoord.X * EffectiveTileRes;
// 		const int32 CanvasStartY = TileCoord.Y * EffectiveTileRes;
//
// 		// =================== START OF THE FIX ===================
// 		// Pre-calculate the exact number of rows and columns to copy from this tile.
// 		// This is the crucial change. For tiles at the right or bottom edge of the final image,
// 		// we will only copy the portion that actually fits onto the canvas.
// 		const int32 RowsToCopy = FMath::Min(Settings.TileResolution, Settings.OutputHeight - CanvasStartY);
// 		const int32 ColsToCopy = FMath::Min(Settings.TileResolution, Settings.OutputWidth - CanvasStartX);
// 		// ==================== END OF THE FIX ====================
//
// 		// Loop only over the valid region that needs to be copied
// 		for (int32 y = 0; y < RowsToCopy; ++y)
// 		{
// 			for (int32 x = 0; x < ColsToCopy; ++x)
// 			{
// 				// Calculate the linear index for the source pixel data (from the tile)
// 				const int32 SrcIndex = y * Settings.TileResolution + x;
//
// 				// Calculate the destination coordinates on the final canvas
// 				const int32 CanvasX = CanvasStartX + x;
// 				const int32 CanvasY = CanvasStartY + y;
//
// 				// Calculate the linear index for the destination pixel data (the final image)
// 				const int32 DstIndex = CanvasY * Settings.OutputWidth + CanvasX;
//
// 				// Copy the pixel. No 'if' check is needed here because the loop bounds are already correct.
// 				FinalImageData[DstIndex] = TilePixels[SrcIndex];
// 			}
// 		}
// 	}
// 	
// 	// Clear the temporary tile data to free up memory
// 	CapturedTileData.Empty();
// 	
// 	// Start the async task to save the final stitched image to disk
// 	StartImageSaveTask(MoveTemp(FinalImageData), Settings.OutputWidth, Settings.OutputHeight);
// }

// MinimapGeneratorManager.cpp

// ... (các hàm khác giữ nguyên) ...

// In MinimapGeneratorManager.cpp

// In MinimapGeneratorManager.cpp

void UMinimapGeneratorManager::StartStitching()
{
	UE_LOG(LogTemp, Log, TEXT("All tiles captured. Starting stitching process..."));

	if (ActiveCaptureActor.IsValid())
	{
		ActiveCaptureActor->Destroy();
	}
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();

	if (CapturedTileData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No tile data was captured. Aborting stitching."));
		OnCaptureComplete.Broadcast(false, TEXT("No tile data was captured."));
		return;
	}

	TArray<FColor> FinalImageData;
	FinalImageData.AddUninitialized(Settings.OutputWidth * Settings.OutputHeight);
	// Khởi tạo ảnh cuối cùng với màu nền để việc blending hoạt động chính xác
	const FColor BackgroundColor = (Settings.BackgroundMode == EMinimapBackgroundMode::Transparent) ? FColor::Transparent : Settings.BackgroundColor.ToFColor(true);
	for(int32 i = 0; i < FinalImageData.Num(); ++i)
	{
		FinalImageData[i] = BackgroundColor;
	}


	const int32 EffectiveTileRes = Settings.TileResolution - Settings.TileOverlap;
	const bool bIsPortrait = Settings.OutputHeight > Settings.OutputWidth;

	// Sắp xếp các tile theo thứ tự từ trái sang phải, từ trên xuống dưới để đảm bảo blending đúng
	TArray<FIntPoint> SortedTileCoords;
	CapturedTileData.GetKeys(SortedTileCoords);
	SortedTileCoords.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		if (A.Y != B.Y) return A.Y < B.Y;
		return A.X < B.X;
	});

	for (const FIntPoint& TileCoord : SortedTileCoords)
	{
		const TArray<FColor>& TilePixels = CapturedTileData[TileCoord];
		const int32 CanvasStartX = TileCoord.X * EffectiveTileRes;
		const int32 CanvasStartY = TileCoord.Y * EffectiveTileRes;

		for (int32 y = 0; y < Settings.TileResolution; ++y)
		{
			for (int32 x = 0; x < Settings.TileResolution; ++x)
			{
				const int32 SrcIndex = y * Settings.TileResolution + x;
				const FColor& SrcPixelColor = TilePixels[SrcIndex];

				int32 DstX_on_Canvas, DstY_on_Canvas;
				if (bIsPortrait)
				{
					DstX_on_Canvas = CanvasStartX + (Settings.TileResolution - 1 - y);
					DstY_on_Canvas = CanvasStartY + x;
				}
				else
				{
					DstX_on_Canvas = CanvasStartX + x;
					DstY_on_Canvas = CanvasStartY + y;
				}

				if (DstX_on_Canvas >= 0 && DstX_on_Canvas < Settings.OutputWidth &&
					DstY_on_Canvas >= 0 && DstY_on_Canvas < Settings.OutputHeight)
				{
					const int32 DstIndex = DstY_on_Canvas * Settings.OutputWidth + DstX_on_Canvas;

					// === BẮT ĐẦU LOGIC BLENDING ===
					float BlendAlphaX = 1.0f;
					float BlendAlphaY = 1.0f;

					// Tính hệ số blend cho trục X (vùng chồng lấn bên trái)
					if (TileCoord.X > 0 && x < Settings.TileOverlap)
					{
						BlendAlphaX = static_cast<float>(x) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Tính hệ số blend cho trục Y (vùng chồng lấn bên trên)
					if (TileCoord.Y > 0 && y < Settings.TileOverlap)
					{
						BlendAlphaY = static_cast<float>(y) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Chọn hệ số blend nhỏ nhất để tạo ra đường chéo mượt mà ở các góc
					const float FinalBlendAlpha = FMath::Min(BlendAlphaX, BlendAlphaY);

					if (FinalBlendAlpha < 1.0f)
					{
						// === BẮT ĐẦU LOGIC BLENDING ĐÃ SỬA LỖI ===
						// 1. Lấy màu đã có sẵn ở vị trí đích
						const FColor& DstPixelColor = FinalImageData[DstIndex];

						// 2. Chuyển đổi cả hai màu FColor sang FLinearColor
						const FLinearColor DstLinear = DstPixelColor; // Tự động chuyển đổi
						const FLinearColor SrcLinear = SrcPixelColor; // Tự động chuyển đổi

						// 3. Thực hiện Lerp trong không gian Linear (dùng HSV để có màu đẹp hơn)
						const FLinearColor BlendedLinear = FLinearColor::LerpUsingHSV(DstLinear, SrcLinear, FinalBlendAlpha);
						
						// 4. Chuyển đổi kết quả về lại FColor và ghi vào ảnh
						FinalImageData[DstIndex] = BlendedLinear.ToFColor(true);
						// === KẾT THÚC LOGIC BLENDING ĐÃ SỬA LỖI ===
					}
					else
					{
						FinalImageData[DstIndex] = SrcPixelColor;
					}
					// === KẾT THÚC LOGIC BLENDING ===
				}
			}
		}
	}

	CapturedTileData.Empty();
	StartImageSaveTask(MoveTemp(FinalImageData), Settings.OutputWidth, Settings.OutputHeight);
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
