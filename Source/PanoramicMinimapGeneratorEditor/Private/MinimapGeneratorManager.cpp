// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorManager.h"

#include "Editor.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "RHICommandList.h"

class FSaveImageTask : public FNonAbandonableTask
{
public:
	FSaveImageTask(TArray<FColor> InPixelData, const int32 InWidth, const int32 InHeight, FString InFullPath,
				   const TWeakObjectPtr<UMinimapGeneratorManager> InManager)
		: PixelData(MoveTemp(InPixelData)), Width(InWidth), Height(InHeight), FullPath(MoveTemp(InFullPath)),
		  ManagerPtr(InManager)
	{
	}

	void DoWork()
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(
			FName("ImageWrapper"));
		if (const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper.IsValid() && ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width,
														   Height, ERGBFormat::BGRA, 8))
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

	// ReSharper disable once CppMemberFunctionMayBeStatic
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveImageTask, STATGROUP_ThreadPoolAsyncTasks);
	}

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
		: PixelData(MoveTemp(InPixelData)), Width(InWidth), Height(InHeight), FullPath(MoveTemp(InFullPath))
	{
	}

	void DoWork()
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(
			FName("ImageWrapper"));
		if (const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper.IsValid() && ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width,
														   Height, ERGBFormat::BGRA, 8))
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

	// ReSharper disable once CppMemberFunctionMayBeStatic
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSaveDebugTileTask, STATGROUP_ThreadPoolAsyncTasks);
	}

protected:
	TArray<FColor> PixelData;
	int32 Width;
	int32 Height;
	FString FullPath;
};

// Helper function to start the debug tile saving task
void SaveDebugTileImage(const FString& BasePath, const FString& BaseFileName, const TArray<FColor>& PixelData,
						const int32 TileX, const int32 TileY, int32 TileResolution)
{
	// Create a descriptive filename for the debug tile, e.g., "Minimap_Result_Tile_0_1.png"
	const FString DebugFileName = FString::Printf(TEXT("%s_Tile_%d_%d.png"), *BaseFileName, TileX, TileY);
	const FString FullPath = FPaths::Combine(BasePath, DebugFileName);

	// Start the dedicated async task for saving the debug tile.
	// We pass a copy of PixelData because the original will be moved into the main TMap.
	(new FAutoDeleteAsyncTask<FSaveDebugTileTask>(PixelData, TileResolution, TileResolution, FullPath))->
		StartBackgroundTask();
}

// =================== END OF NEW CODE ===================

void UMinimapGeneratorManager::StartCaptureProcess(const FMinimapCaptureSettings& InSettings)
{
	UE_LOG(LogTemp, Warning, TEXT("[%s::%s] - Starting minimap capture process."), *GetName(), *FString(__FUNCTION__));
	this->Settings = InSettings;
	OnProgress.Broadcast(FText::FromString(TEXT("Starting capture process...")), 0.0f, 0, 1);
	if (Settings.bUseTiling)
	{
		StartTiledCaptureProcess();
	}
	else
	{
		StartSingleCaptureForValidation();
	}
}

void UMinimapGeneratorManager::OnSaveTaskCompleted(const bool bSuccess, const FString& SavedImagePath) const
{
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Async save task completed successfully. Path: %s"), *SavedImagePath);
		OnProgress.Broadcast(FText::FromString(TEXT("Done!")), 1.0f, 0, 0);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Async save task failed."));
		OnProgress.Broadcast(FText::FromString(TEXT("Failed!")), 1.0f, 0, 0);
	}
	OnCaptureComplete.Broadcast(bSuccess, SavedImagePath);

	if (bSuccess && Settings.bImportAsTextureAsset && !SavedImagePath.IsEmpty())
	{
		// const FString AssetName = TEXT("T_") + FPaths::GetBaseFilename(SavedImagePath);
		FString PackagePath = Settings.AssetPath;
		if (!PackagePath.EndsWith(TEXT("/")))
		{
			PackagePath += TEXT("/");
		}
		const FString FullAssetPath = PackagePath + FPaths::GetBaseFilename(SavedImagePath);

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
				UTexture2D* NewTexture = NewObject<UTexture2D>(Package, *FPaths::GetBaseFilename(SavedImagePath), RF_Public | RF_Standalone);
				NewTexture->AddToRoot();

				NewTexture->Source.Init(ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), 1, 1, TSF_BGRA8,
										UncompressedBGRA.GetData());
				NewTexture->SRGB = true;
				NewTexture->CompressionSettings = TC_Default;
				NewTexture->UpdateResource();
				// Package->MarkPackageDirty();

				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
					TEXT("AssetRegistry"));
				AssetRegistryModule.AssetCreated(NewTexture);

				UE_LOG(LogTemp, Log, TEXT("Successfully created texture asset: %s"), *FullAssetPath);

				TArray<UObject*> AssetsToSync;
				AssetsToSync.Add(NewTexture);
				GEditor->SyncBrowserToObjects(AssetsToSync);

				NewTexture->RemoveFromRoot();

				OnProgress.Broadcast(FText::FromString(TEXT("")), 0, 0, 0);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to decode PNG data from file: %s"), *SavedImagePath);
		}
	}
}

// ===================================================================
// LOGIC 1: SINGLE CAPTURE 
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
	UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	if (!RenderTarget || !RenderTarget->GetResource())
	{
		OnSaveTaskCompleted(false, TEXT("Render Target became invalid before readback."));
		if (const UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			World->GetTimerManager().ClearTimer(ReadbackPollTimer);
		}
		return;
	}

	StagingPixelBuffer.Reset();
	StagingPixelBuffer.AddUninitialized(Settings.OutputWidth * Settings.OutputHeight);

	FTextureRenderTargetResource* RenderTargetResource = static_cast<FTextureRenderTargetResource*>(RenderTarget->
		GetResource());

	ENQUEUE_RENDER_COMMAND(ReadPixelsCommand)(
		[RenderTargetResource, PixelBuffer = &this->StagingPixelBuffer, Fence = &this->ReadbackFence](
		FRHICommandListImmediate& RHICmdList)
		{
			FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
			ReadPixelFlags.SetLinearToGamma(true);
			RHICmdList.ReadSurfaceData(
				RenderTargetResource->GetRenderTargetTexture(),
				FIntRect(0, 0, RenderTargetResource->GetSizeX(), RenderTargetResource->GetSizeY()),
				*PixelBuffer, ReadPixelFlags);

			Fence->BeginFence();
		}
	);

	const UWorld* World = GEditor->GetEditorWorldContext().World();
	World->GetTimerManager().SetTimer(ReadbackPollTimer, this, &UMinimapGeneratorManager::CheckReadbackStatus, 0.1f,
									  true);

	OnProgress.Broadcast(FText::FromString(TEXT("GPU is rendering... Editor remains responsive.")), 0.5f, 0, 0);
}

void UMinimapGeneratorManager::CheckReadbackStatus()
{
	if (ReadbackFence.IsFenceComplete())
	{
		const UWorld* World = GEditor->GetEditorWorldContext().World();
		World->GetTimerManager().ClearTimer(ReadbackPollTimer);

		if (ActiveCaptureActor.IsValid())
		{
			ActiveCaptureActor->Destroy();
		}
		ActiveCaptureActor.Reset();
		ActiveRenderTarget.Reset();

		if (StagingPixelBuffer.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("GPU readback complete. Starting async save task."));
			StartImageSaveTask(MoveTemp(StagingPixelBuffer), Settings.OutputWidth, Settings.OutputHeight);
			OnProgress.Broadcast(FText::FromString(TEXT("Saving image...")), 0.95f, 0, 0);
		}
		else
		{
			OnSaveTaskCompleted(false, TEXT("GPU readback resulted in empty pixel data."));
		}
	}
}

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

// ===================================================================
// LOGIC 2: TILED CAPTURE
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
	if (EffectiveTileRes <= 0)
	{
		NumTilesX = 0;
		NumTilesY = 0;
		return;
	}

	NumTilesX = (Settings.OutputWidth <= Settings.TileResolution)
					? 1
					: (1 + FMath::CeilToInt(
						static_cast<float>(Settings.OutputWidth - Settings.TileResolution) / EffectiveTileRes));
	NumTilesY = (Settings.OutputHeight <= Settings.TileResolution)
					? 1
					: (1 + FMath::CeilToInt(
						static_cast<float>(Settings.OutputHeight - Settings.TileResolution) / EffectiveTileRes));
	UE_LOG(LogTemp, Log, TEXT("Calculated Grid: %d x %d tiles"), NumTilesX, NumTilesY);
}

void UMinimapGeneratorManager::CaptureNextTile()
{
	if (CurrentTileIndex >= NumTilesX * NumTilesY)
	{
		StartStitching();
		return;
	}
	if (!ActiveCaptureActor.IsValid() || !ActiveRenderTarget.IsValid())
	{
		OnCaptureComplete.
			Broadcast(false, TEXT("Capture actor or render target became invalid during tiling process."));
		return;
	}

	const int32 TileX = CurrentTileIndex % NumTilesX;
	const int32 TileY = CurrentTileIndex / NumTilesX;
	
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


	CaptureComponent->CaptureScene();

	const float CurrentProgress = static_cast<float>(CurrentTileIndex) / (NumTilesX * NumTilesY);

	OnProgress.Broadcast(
		FText::Format(FText::FromString("Capturing tile {0}/{1}..."), FText::AsNumber(CurrentTileIndex + 1), FText::AsNumber(NumTilesX * NumTilesY)),
		CurrentProgress * 0.9f,
		CurrentTileIndex, 
		NumTilesX * NumTilesY
	);
	
	GEditor->GetEditorWorldContext().World()->GetTimerManager().SetTimerForNextTick(
		this, &UMinimapGeneratorManager::OnTileRenderedAndContinue);
}

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

		CapturedTileData.Add(FIntPoint(TileX, TileY), MoveTemp(TilePixels));
	}

	CurrentTileIndex++;
	CaptureNextTile();
}

void UMinimapGeneratorManager::StartStitching()
{
	UE_LOG(LogTemp, Log, TEXT("All tiles captured. Starting stitching process..."));
	OnProgress.Broadcast(FText::FromString(TEXT("Stitching tiles...")), 0.9f, 0, 0);

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
	// Initialize the final image with background color for proper blending
	const FColor BackgroundColor = (Settings.BackgroundMode == EMinimapBackgroundMode::Transparent)
									   ? FColor::Transparent
									   : Settings.BackgroundColor.ToFColor(true);
	for (int32 i = 0; i < FinalImageData.Num(); ++i)
	{
		FinalImageData[i] = BackgroundColor;
	}


	const int32 EffectiveTileRes = Settings.TileResolution - Settings.TileOverlap;
	const bool bIsPortrait = Settings.OutputHeight > Settings.OutputWidth;

	// Sort tiles from left to right, top to bottom to ensure proper blending
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

					// === START OF BLENDING LOGIC ===
					float BlendAlphaX = 1.0f;
					float BlendAlphaY = 1.0f;

					// Calculate the blend factor for X axis (left overlap region)
					if (TileCoord.X > 0 && x < Settings.TileOverlap)
					{
						BlendAlphaX = static_cast<float>(x) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Calculate the blend factor for Y axis (top overlap region)
					if (TileCoord.Y > 0 && y < Settings.TileOverlap)
					{
						BlendAlphaY = static_cast<float>(y) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Choose the minimum blend factor to create smooth diagonal corners 
					if (const float FinalBlendAlpha = FMath::Min(BlendAlphaX, BlendAlphaY); FinalBlendAlpha < 1.0f)
					{
						// === START OF FIXED BLENDING LOGIC ===
						// 1. Get existing color at the destination position
						const FColor& DstPixelColor = FinalImageData[DstIndex];

						// 2. Convert both FColor to FLinearColor
						const FLinearColor DstLinear = DstPixelColor; // Automatic conversion
						const FLinearColor SrcLinear = SrcPixelColor; // Automatic conversion 

						// 3. Perform Lerp in Linear space (using HSV for better colors)
						const FLinearColor BlendedLinear = FLinearColor::LerpUsingHSV(
							DstLinear, SrcLinear, FinalBlendAlpha);

						// 4. Convert the result back to FColor and write to image
						FinalImageData[DstIndex] = BlendedLinear.ToFColor(true);
					}
					else
					{
						FinalImageData[DstIndex] = SrcPixelColor;
					}
					// === END OF BLENDING LOGIC ===
				}
			}
		}
	}

	CapturedTileData.Empty();
	OnProgress.Broadcast(FText::FromString(TEXT("Saving final image...")), 0.95f, 0, 0);
	StartImageSaveTask(MoveTemp(FinalImageData), Settings.OutputWidth, Settings.OutputHeight);
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
