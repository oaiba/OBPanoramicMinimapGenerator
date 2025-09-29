// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorManager.h"

#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "MinimapStreamingSource.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

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

	UWorld* World = GEditor->GetEditorWorldContext().World();
	CaptureActor = World->SpawnActor<ASceneCapture2D>();
	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();

	// --- Configure Capture Component ---
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	CaptureComponent->ShowFlags.SetMotionBlur(false);
	CaptureComponent->ShowFlags.SetTemporalAA(false);
	CaptureComponent->ShowFlags.SetAntiAliasing(true);
	CaptureComponent->PostProcessSettings.bOverride_MotionBlurAmount = true;
	CaptureComponent->PostProcessSettings.MotionBlurAmount = 0.0f;

	if (Settings.bIsOrthographic)
	{
		CaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
	}
	else
	{
		CaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
		CaptureComponent->FOVAngle = Settings.CameraFOV;
	}

	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(Settings.TileResolution, Settings.TileResolution, PF_B8G8R8A8, true);
	CaptureComponent->TextureTarget = RenderTarget;

	// Create our custom streaming source provider
	MinimapStreamer = MakeShared<FMinimapStreamingSourceProvider>();

	// Start processing the first tile, this is the one and only starting point.
	ProcessNextTile();
}

void UMinimapGeneratorManager::CalculateGrid()
{
	const FVector BoundsSize = Settings.CaptureBounds.GetSize();

	// A more robust calculation is needed here based on ortho width or FOV and camera height
	// For now, let's use a simplified approach for demonstration
	const FVector2D WorldDimensions(BoundsSize.X, BoundsSize.Y);

	// Calculate how much world space a single non-overlapping tile covers
	const float TileWorldSize = Settings.TileResolution * (WorldDimensions.X / Settings.OutputWidth); // Example scale
	const float OverlapWorldSize = Settings.TileOverlap * (WorldDimensions.X / Settings.OutputWidth);

	const float StepSize = TileWorldSize - OverlapWorldSize;

	NumTilesX = FMath::CeilToInt(WorldDimensions.X / StepSize);
	NumTilesY = FMath::CeilToInt(WorldDimensions.Y / StepSize);

	UE_LOG(LogTemp, Log, TEXT("Calculated Grid: %d x %d tiles"), NumTilesX, NumTilesY);
}

void UMinimapGeneratorManager::ProcessNextTile()
{
	if (CurrentTileIndex >= NumTilesX * NumTilesY)
	{
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

	UWorld* World = GEditor->GetEditorWorldContext().World();
	// CORRECTED: Using the proper GetSubsystem method.
	if (UWorldPartitionSubsystem* WP = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		MinimapStreamer->SetSourceLocation(TileCenter, StreamingRadius);
		WP->RegisterStreamingSourceProvider(MinimapStreamer.Get());
	}

	World->GetTimerManager().SetTimer(StreamingCheckTimer, this, &UMinimapGeneratorManager::CheckStreamingAndCapture,
	                                  0.1f, true);
}

void UMinimapGeneratorManager::CheckStreamingAndCapture()
{
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	// CORRECTED: Using the proper GetSubsystem method.
	if (const UWorldPartitionSubsystem* Wp = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		if (Wp->IsStreamingCompleted(MinimapStreamer.Get()))
		{
			World->GetTimerManager().ClearTimer(StreamingCheckTimer);
			UE_LOG(LogTemp, Log, TEXT("Streaming complete for tile %d."), CurrentTileIndex);

			// --- Now, perform the capture ---
			const FVector SourceLocation = MinimapStreamer->GetSourceLocation();
			CaptureActor->SetActorLocation(FVector(SourceLocation.X, SourceLocation.Y, Settings.CameraHeight));
			CaptureActor->SetActorRotation(FRotator(-90.f, 0.f, 0.f));

			USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
			if (Settings.bIsOrthographic)
			{
				const float TileWorldWidth = Settings.CaptureBounds.GetSize().X / NumTilesX;
				CaptureComponent->OrthoWidth = TileWorldWidth;
			}

			CaptureComponent->CaptureScene();

			// Lấy các con trỏ cần thiết trên Game Thread
			FTextureRenderTargetResource* RTResource = RenderTarget->GetRenderTargetResource();
			const int32 TileX = CurrentTileIndex % NumTilesX;
			const int32 TileY = CurrentTileIndex / NumTilesX;

			// Tự bắt (capture) 'this' một cách an toàn bằng TWeakObjectPtr
			const TWeakObjectPtr<UMinimapGeneratorManager> ThisWeakPtr(this);

			// Tạo một struct để chứa lệnh cho Rendering Thread.
			// Điều này đảm bảo tất cả các biến được copy một cách an toàn.
			struct FReadPixelContext
			{
				FTextureRenderTargetResource* RenderTargetResource;
				TWeakObjectPtr<UMinimapGeneratorManager> Manager;
				int32 TileX;
				int32 TileY;
			};

			FReadPixelContext ReadPixelContext = {RTResource, ThisWeakPtr, TileX, TileY};

			// Gửi lệnh đến Rendering Thread
			ENQUEUE_RENDER_COMMAND(ReadPixelsCommand)(
				[ReadPixelContext](FRHICommandListImmediate& RHICmdList)
				{
					// --- BÂY GIỜ CHÚNG TA ĐANG Ở TRÊN RENDERING THREAD ---
					TArray<FColor> PixelData;
					FReadSurfaceDataFlags ReadPixelFlags(ERangeCompressionMode::RCM_UNorm);
					ReadPixelFlags.SetLinearToGamma(false);

					// Lệnh này bây giờ hoàn toàn an toàn
					ReadPixelContext.RenderTargetResource->ReadPixels(PixelData, ReadPixelFlags);

					// Gửi dữ liệu đọc được về lại Game Thread
					AsyncTask(ENamedThreads::GameThread,
					          [Manager = ReadPixelContext.Manager, TileX = ReadPixelContext.TileX, TileY =
						          ReadPixelContext.TileY, Pixels = MoveTemp(PixelData)]() mutable
					          {
						          // --- BÂY GIỜ CHÚNG TA ĐÃ QUAY LẠI GAME THREAD ---
						          if (Manager.IsValid())
						          {
							          Manager->OnTileCaptureCompleted(TileX, TileY, MoveTemp(Pixels));
						          }
					          });
				}
			);

			// --- LOGIC Ở GAME THREAD TIẾP TỤC NGAY LẬP TỨC ---
			// Unregister streaming source và chuyển sang tile tiếp theo
			const UWorld* LocalWorld = GEditor->GetEditorWorldContext().World();
			if (UWorldPartitionSubsystem* WorldPartitionSubsystem = LocalWorld->GetSubsystem<UWorldPartitionSubsystem>())
			{
				WorldPartitionSubsystem->UnregisterStreamingSourceProvider(MinimapStreamer.Get());
			}
	
			CurrentTileIndex++;

			FTimerHandle NextTileDelayHandle;
			LocalWorld->GetTimerManager().SetTimer(NextTileDelayHandle, this, &UMinimapGeneratorManager::ProcessNextTile,
			                                  0.1f, false);
		}
	}
}

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
	// const int32 NonOverlap = TileRes - Overlap;

	for (int32 TileY = 0; TileY < NumTilesY; ++TileY)
	{
		for (int32 TileX = 0; TileX < NumTilesX; ++TileX)
		{
			const FIntPoint CurrentTilePos(TileX, TileY);
			if (!CapturedTileData.Contains(CurrentTilePos))
			{
				UE_LOG(LogTemp, Error, TEXT("Missing data for tile (%d, %d)!"), TileX, TileY);
				continue;
			}

			const TArray<FColor>& TilePixels = CapturedTileData[CurrentTilePos];

			const int32 CanvasStartX = TileX * (TileRes - Overlap);
			const int32 CanvasStartY = TileY * (TileRes - Overlap);

			for (int32 y = 0; y < TileRes; ++y)
			{
				for (int32 x = 0; x < TileRes; ++x)
				{
					const int32 CanvasX = CanvasStartX + x;
					const int32 CanvasY = CanvasStartY + y;

					if (CanvasX >= FinalWidth || CanvasY >= FinalHeight) continue;

					const int32 CanvasIndex = CanvasY * FinalWidth + CanvasX;
					const int32 TileIndex = y * TileRes + x;

					FColor NewPixel = TilePixels[TileIndex];

					// --- Blending Logic ---
					float BlendX = 1.0f;
					float BlendY = 1.0f;

					// Left overlap
					if (TileX > 0 && x < Overlap) BlendX = static_cast<float>(x) / Overlap;
					// Right overlap (handled by the next tile)

					// Top overlap
					if (TileY > 0 && y < Overlap) BlendY = static_cast<float>(y) / Overlap;

					if (const float BlendFactor = FMath::Min(BlendX, BlendY); BlendFactor < 1.0f)
					{
						// CHANGED: Using LerpUsingHSV for blending
						FColor ExistingPixel = FinalImageData[CanvasIndex];

						// Convert FColor (sRGB, 0-255) to FLinearColor (Linear, 0-1)
						const FLinearColor FromColor(ExistingPixel);
						const FLinearColor ToColor(NewPixel);

						// Interpolate in HSV space
						const FLinearColor BlendedLinearColor = FLinearColor::LerpUsingHSV(
							FromColor, ToColor, BlendFactor);

						// Convert back to FColor for storage. The 'true' flag handles sRGB conversion.
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

	// Save the final image to disk
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

	if (CaptureActor)
	{
		CaptureActor->Destroy();
		CaptureActor = nullptr;
	}

	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (UWorldPartitionSubsystem* Wp = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		if (MinimapStreamer.IsValid() && Wp->IsStreamingSourceProviderRegistered(MinimapStreamer.Get()))
		{
			Wp->UnregisterStreamingSourceProvider(MinimapStreamer.Get());
		}
	}
	MinimapStreamer.Reset();

	StartStitching();
}

bool UMinimapGeneratorManager::SaveFinalImage(const TArray<FColor>& ImageData, int32 Width, int32 Height)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(
		FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid() || !ImageWrapper->SetRaw(ImageData.GetData(), ImageData.Num() * sizeof(FColor), Width,
	                                                     Height, ERGBFormat::BGRA, 8))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to set raw image data for PNG wrapper."));
		return false;
	}

	const FString FullPath = FPaths::Combine(Settings.OutputPath, Settings.FileName + ".png");
	return FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *FullPath);
}
