// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorManager.h"
#include "PanoramicMinimapGeneratorEditor.h"

#include "Editor.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/IConsoleManager.h"
#include "Async/Async.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "RHICommandList.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"

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
				if (IsEngineExitRequested())
				{
					return;
				}

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
				if (IsEngineExitRequested())
				{
					return;
				}

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

namespace
{
constexpr int64 MaxLegacyStitchedPixels = 8192LL * 8192LL;
constexpr int64 MaxImmediateTextureResourcePixels = 4096LL * 4096LL;

static TAutoConsoleVariable<int32> CVarPanoramicMemoryTrace(
	TEXT("OB.Panoramic.MemoryTrace"),
	0,
	TEXT("Enable verbose PANOMEM memory lifecycle traces for the Panoramic Minimap Generator. 0=off, 1=on."),
	ECVF_Default);

bool IsPanoramicMemoryTraceEnabled()
{
	return CVarPanoramicMemoryTrace.GetValueOnGameThread() != 0;
}

FString FormatTraceBytes(const uint64 Bytes)
{
	constexpr double OneGB = 1024.0 * 1024.0 * 1024.0;
	constexpr double OneMB = 1024.0 * 1024.0;
	if (Bytes >= static_cast<uint64>(OneGB))
	{
		return FString::Printf(TEXT("%.2f GB"), static_cast<double>(Bytes) / OneGB);
	}

	return FString::Printf(TEXT("%.0f MB"), static_cast<double>(Bytes) / OneMB);
}

FString FormatTraceBytesDelta(const int64 Bytes)
{
	const FString Prefix = Bytes >= 0 ? TEXT("+") : TEXT("-");
	const uint64 AbsoluteBytes = static_cast<uint64>(FMath::Abs(Bytes));
	return Prefix + FormatTraceBytes(AbsoluteBytes);
}

FString MakeSafeAssetName(const FString& InName)
{
	FString SafeName = InName;
	SafeName.ReplaceInline(TEXT(" "), TEXT("_"));
	SafeName.ReplaceInline(TEXT("-"), TEXT("_"));
	SafeName.ReplaceInline(TEXT("."), TEXT("_"));
	SafeName.ReplaceInline(TEXT("/"), TEXT("_"));
	SafeName.ReplaceInline(TEXT("\\"), TEXT("_"));
	return SafeName.IsEmpty() ? TEXT("Minimap") : SafeName;
}
}

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
				if (IsEngineExitRequested())
				{
					return;
				}

				if (bSuccess)
				{
					UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Debug tile saved successfully: %s"), *Path);
				}
				else
				{
					UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to save debug tile: %s"), *Path);
				}
			});
		}
		else
		{
			// Log failure
			AsyncTask(ENamedThreads::GameThread, [Path = this->FullPath]
			{
				if (IsEngineExitRequested())
				{
					return;
				}

				UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("ImageWrapper failed for debug tile: %s"), *Path);
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
	bIsShuttingDown = false;
	bCancelRequested = false;
	UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("[%s::%s] - Starting minimap capture process."), *GetName(), *FString(__FUNCTION__));
	this->Settings = InSettings;
	TracePanoramicMemory(TEXT("CAPTURE_START_BEGIN"), FString::Printf(
		TEXT("UseTiling=%s ExportTileSet=%s Output=%dx%d TileRes=%d TileOverlap=%d"),
		Settings.bUseTiling ? TEXT("true") : TEXT("false"),
		Settings.bExportTileSet ? TEXT("true") : TEXT("false"),
		Settings.OutputWidth,
		Settings.OutputHeight,
		Settings.TileResolution,
		Settings.TileOverlap));

	if (!Settings.CaptureBounds.IsValid || Settings.OutputWidth <= 0 || Settings.OutputHeight <= 0)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Invalid capture bounds or output size."));
		return;
	}

	if (Settings.bUseTiling && (Settings.TileResolution <= 0 || Settings.TileOverlap < 0 || Settings.TileOverlap >= Settings.TileResolution))
	{
		OnCaptureComplete.Broadcast(false, TEXT("Invalid tiling settings. Tile overlap must be lower than tile resolution."));
		return;
	}

	if (Settings.bUseTiling && Settings.bExportTileSet)
	{
		if (Settings.TileSetWorldTileSize <= 0.0f || Settings.TileSetMaxLOD < 0 || Settings.TileSetOverviewResolution <= 0)
		{
			OnCaptureComplete.Broadcast(false, TEXT("Invalid tile-set settings."));
			return;
		}
	}
	else if (Settings.bUseTiling)
	{
		const int64 OutputPixels = static_cast<int64>(Settings.OutputWidth) * Settings.OutputHeight;
		if (OutputPixels > MaxLegacyStitchedPixels)
		{
			OnCaptureComplete.Broadcast(false, TEXT("Legacy tiled stitching is capped at 8192x8192 to avoid RAM exhaustion. Enable Tile Set + LOD export for large maps."));
			return;
		}
	}

	const FString CaptureMode = Settings.bUseTiling
		? (Settings.bExportTileSet ? TEXT("Tile Set + LOD") : TEXT("Legacy Tiled Stitch"))
		: TEXT("Single Capture");
	ResetTelemetry(CaptureMode, TEXT("Starting"));
	TracePanoramicMemory(TEXT("CAPTURE_MODE_SELECTED"), FString::Printf(TEXT("Mode=%s"), *CaptureMode));
	OnProgress.Broadcast(FText::FromString(TEXT("Starting capture process...")), 0.0f, 0, 1);
	if (Settings.bUseTiling && Settings.bExportTileSet)
	{
		StartTileSetCaptureProcess();
	}
	else if (Settings.bUseTiling)
	{
		StartTiledCaptureProcess();
	}
	else
	{
		StartSingleCaptureForValidation();
	}
}

void UMinimapGeneratorManager::CancelCapture()
{
	UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("[%s::%s] - Capture process cancelled by user."), *GetName(), *FString(__FUNCTION__));
	ShutdownCapture(true);
}

void UMinimapGeneratorManager::ShutdownCapture(const bool bBroadcastResult)
{
	bCancelRequested = true;
	bIsShuttingDown = !bBroadcastResult;

	TracePanoramicMemory(TEXT("SHUTDOWN_BEGIN"), FString::Printf(TEXT("BroadcastResult=%s"), bBroadcastResult ? TEXT("true") : TEXT("false")));
	ReleaseWorkingMemory(true);
	TracePanoramicMemory(TEXT("SHUTDOWN_AFTER_RELEASE"), FString::Printf(TEXT("BroadcastResult=%s"), bBroadcastResult ? TEXT("true") : TEXT("false")));
	if (bBroadcastResult)
	{
		Telemetry.CurrentPhase = TEXT("Cancelled");
		Telemetry.bIsCapturing = false;
		BroadcastTelemetry();
	}

	if (bBroadcastResult && !IsEngineExitRequested())
	{
		OnCaptureComplete.Broadcast(false, TEXT("Capture cancelled by user."));
	}
	else
	{
		OnProgress.Clear();
		OnCaptureComplete.Clear();
	}
}

void UMinimapGeneratorManager::BeginDestroy()
{
	ShutdownCapture(false);
	Super::BeginDestroy();
}

void UMinimapGeneratorManager::OnSaveTaskCompleted(const bool bSuccess, const FString& SavedImagePath)
{
	if (bIsShuttingDown || IsEngineExitRequested())
	{
		return;
	}

	if (bSuccess)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Async save task completed successfully. Path: %s"), *SavedImagePath);
		TracePanoramicMemory(TEXT("SAVE_TASK_SUCCESS"), FString::Printf(TEXT("Path=%s"), *SavedImagePath));
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Async save task failed."));
		TracePanoramicMemory(TEXT("SAVE_TASK_FAILED_BEFORE_CLEANUP"), FString::Printf(TEXT("Reason=%s"), *SavedImagePath));
		ReleaseWorkingMemory(true);
		TracePanoramicMemory(TEXT("SAVE_TASK_FAILED_AFTER_CLEANUP"), FString::Printf(TEXT("Reason=%s"), *SavedImagePath));
		Telemetry.CurrentPhase = TEXT("Failed");
		Telemetry.bIsCapturing = false;
		BroadcastTelemetry();
		OnProgress.Broadcast(FText::FromString(TEXT("Failed!")), 1.0f, 0, 0);
		OnCaptureComplete.Broadcast(false, SavedImagePath);
		return;
	}

	if (TelemetryImageSaveStartTime > 0.0)
	{
		Telemetry.LastPackageSaveSeconds = FPlatformTime::Seconds() - TelemetryImageSaveStartTime;
		TelemetryImageSaveStartTime = 0.0;
	}
	UpdateTelemetryPhase(TEXT("Importing Assets"));
	UTexture2D* ImportedTexture = nullptr;
	if (!SavedImagePath.IsEmpty() && (Settings.bImportAsTextureAsset || Settings.bExportDefinitionAsset))
	{
		const double ImportStartTime = FPlatformTime::Seconds();
		ImportedTexture = ImportTextureAssetFromSavedImage(SavedImagePath);
		Telemetry.LastImportSeconds = FPlatformTime::Seconds() - ImportStartTime;
		BroadcastTelemetry();
	}

	UpdateTelemetryPhase(TEXT("Finalizing"));
	if (Settings.bExportDefinitionAsset)
	{
		if (UMinimapDefinitionDataAsset* DefinitionAsset = CreateOrUpdateDefinitionAsset(SavedImagePath, ImportedTexture))
		{
			if (GEditor && !IsEngineExitRequested())
			{
				TArray<UObject*> AssetsToSync;
				AssetsToSync.Add(DefinitionAsset);
				if (ImportedTexture)
				{
					AssetsToSync.Add(ImportedTexture);
				}
				GEditor->SyncBrowserToObjects(AssetsToSync);
			}
		}
	}

	Telemetry.CurrentPhase = TEXT("Done");
	Telemetry.bIsCapturing = false;
	BroadcastTelemetry();
	OnProgress.Broadcast(FText::FromString(TEXT("Done!")), 1.0f, 0, 0);
	OnCaptureComplete.Broadcast(true, SavedImagePath);
	TracePanoramicMemory(TEXT("CAPTURE_DONE_BEFORE_FINAL_CLEANUP"), FString::Printf(TEXT("Path=%s"), *SavedImagePath));
	ReleaseWorkingMemory(true);
	TracePanoramicMemory(TEXT("CAPTURE_DONE_AFTER_FINAL_CLEANUP"), FString::Printf(TEXT("Path=%s"), *SavedImagePath));
}

// ===================================================================
// FLOW 1: SINGLE CAPTURE
// ===================================================================

void UMinimapGeneratorManager::StartSingleCaptureForValidation()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Starting single capture validation flow."));
	UpdateTelemetryPhase(TEXT("Capturing Single"));
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to start single capture: editor world is null."));
		return;
	}

	ActiveRenderTarget = CreateRenderTarget();
	if (!ActiveRenderTarget.IsValid())
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("%hs: CreateRenderTarget() returned invalid target. Aborting."), __FUNCTION__);
		OnCaptureComplete.Broadcast(false, TEXT("Failed to create render target."));
		return;
	}

	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());
	if (!ActiveCaptureActor.IsValid())
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("%hs: SpawnAndConfigureCaptureActor() returned invalid actor. Aborting."), __FUNCTION__);
		OnCaptureComplete.Broadcast(false, TEXT("Failed to spawn capture actor."));
		return;
	}

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Calling CaptureScene()..."), __FUNCTION__);
	ActiveCaptureActor->GetCaptureComponent2D()->CaptureScene();

	// Flush the rendering pipeline so the render target is fully populated before scheduling readback.
	// This is critical on macOS Metal where async capture may not complete in time.
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Flushing rendering commands..."), __FUNCTION__);
	FlushRenderingCommands();
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: FlushRenderingCommands() completed. Scheduling ReadPixelsAndFinalize for next tick."), __FUNCTION__);

	GEditor->GetTimerManager()->SetTimerForNextTick(this, &UMinimapGeneratorManager::ReadPixelsAndFinalize);
}

// ===================================================================
// SHARED HELPER FUNCTIONS
// ===================================================================

UTexture2D* UMinimapGeneratorManager::ImportTextureAssetFromSavedImage(const FString& SavedImagePath)
{
	if (SavedImagePath.IsEmpty())
	{
		return nullptr;
	}

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Starting texture asset import..."), __FUNCTION__);
	const double ImportStartTime = FPlatformTime::Seconds();

	FString PackagePath = Settings.AssetPath;
	if (!PackagePath.StartsWith(TEXT("/Game")))
	{
		PackagePath = TEXT("/Game/Minimaps/");
	}
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	const FString AssetName = TEXT("T_") + FPaths::GetBaseFilename(SavedImagePath);
	const FString FullAssetPath = PackagePath + AssetName;

	UPackage* Package = CreatePackage(*FullAssetPath);
	Package->FullyLoad();

	TArray<uint8> PngData;
	if (!FFileHelper::LoadFileToArray(PngData, *SavedImagePath))
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to load saved PNG file from disk: %s"), *SavedImagePath);
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(PngData.GetData(), PngData.Num()))
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to decode PNG data from file: %s"), *SavedImagePath);
		return nullptr;
	}

	TArray<uint8> UncompressedBGRA;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to read PNG raw data from file: %s"), *SavedImagePath);
		return nullptr;
	}

	UTexture2D* NewTexture = FindObject<UTexture2D>(Package, *AssetName);
	if (!NewTexture)
	{
		NewTexture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.AssetCreated(NewTexture);
	}

	NewTexture->Source.Init(ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), 1, 1, TSF_BGRA8, UncompressedBGRA.GetData());
	NewTexture->SRGB = true;
	NewTexture->CompressionSettings = TC_Default;
	const int64 TexturePixels = static_cast<int64>(ImageWrapper->GetWidth()) * ImageWrapper->GetHeight();
	if (TexturePixels <= MaxImmediateTextureResourcePixels)
	{
		NewTexture->UpdateResource();
	}
	NewTexture->MarkPackageDirty();
	Package->MarkPackageDirty();
	SaveAssetPackage(Package, NewTexture);

	const double ImportDuration = FPlatformTime::Seconds() - ImportStartTime;
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Imported minimap texture asset: %s (%.2fs)"), *FullAssetPath, ImportDuration);

	return NewTexture;
}

TSoftObjectPtr<UTexture2D> UMinimapGeneratorManager::ImportTileTextureAssetFromPixels(const TArray<FColor>& PixelData,
                                                                                      const int32 Width,
                                                                                      const int32 Height,
                                                                                      const FString& PackagePath,
                                                                                      const FString& AssetName)
{
	if (PixelData.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("%hs: Invalid pixel data for %s."), __FUNCTION__, *AssetName);
		return nullptr;
	}

	const FString NormalizedPackagePath = NormalizePackagePath(PackagePath);
	const FString SafeAssetName = MakeSafeAssetName(AssetName);
	const FString FullAssetPath = NormalizedPackagePath + SafeAssetName;

	UPackage* Package = CreatePackage(*FullAssetPath);
	Package->FullyLoad();

	UTexture2D* NewTexture = FindObject<UTexture2D>(Package, *SafeAssetName);
	if (!NewTexture)
	{
		NewTexture = NewObject<UTexture2D>(Package, *SafeAssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.AssetCreated(NewTexture);
	}

	NewTexture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(PixelData.GetData()));
	NewTexture->SRGB = true;
	NewTexture->CompressionSettings = TC_Default;
	NewTexture->MarkPackageDirty();
	Package->MarkPackageDirty();

	const FSoftObjectPath TexturePath(FString::Printf(TEXT("%s.%s"), *FullAssetPath, *SafeAssetName));
	const bool bSaved = SaveAssetPackage(Package, NewTexture);
	ReleaseTileImportPackage(Package, bSaved ? NewTexture : nullptr, TexturePath.ToString());
	if (!bSaved)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Tile texture package save failed, returning empty soft path: %s"), *TexturePath.ToString());
		return nullptr;
	}

	return TSoftObjectPtr<UTexture2D>(TexturePath);
}

UMinimapDefinitionDataAsset* UMinimapGeneratorManager::CreateOrUpdateDefinitionAsset(const FString& SavedImagePath,
                                                                                     UTexture2D* BaseMapTexture,
                                                                                     UMinimapTileSetDataAsset* TileSet)
{
	FString PackagePath = Settings.DefinitionAssetPath;
	if (!PackagePath.StartsWith(TEXT("/Game")))
	{
		PackagePath = Settings.AssetPath.StartsWith(TEXT("/Game")) ? Settings.AssetPath : TEXT("/Game/Minimaps/");
	}
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	const FString SourceName = SavedImagePath.IsEmpty() ? Settings.FileName : FPaths::GetBaseFilename(SavedImagePath);
	const FString AssetName = MakeSafeAssetName(TEXT("DA_") + SourceName);
	const FString FullAssetPath = PackagePath + AssetName;
	UPackage* Package = CreatePackage(*FullAssetPath);
	Package->FullyLoad();

	UMinimapDefinitionDataAsset* DefinitionAsset = FindObject<UMinimapDefinitionDataAsset>(Package, *AssetName);
	if (!DefinitionAsset)
	{
		DefinitionAsset = NewObject<UMinimapDefinitionDataAsset>(Package, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.AssetCreated(DefinitionAsset);
	}

	DefinitionAsset->BaseMapTexture = BaseMapTexture;
	DefinitionAsset->TileSet = TileSet;
	DefinitionAsset->WorldBounds = Settings.CaptureBounds;
	DefinitionAsset->OutputSize = FIntPoint(Settings.OutputWidth, Settings.OutputHeight);
	DefinitionAsset->MapRotationDegrees = Settings.CameraRotation.Yaw;
	DefinitionAsset->OverlayLayers = Settings.OverlayLayers;
	DefinitionAsset->MarkPackageDirty();
	Package->MarkPackageDirty();
	SaveAssetPackage(Package, DefinitionAsset);

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Created/updated minimap definition asset: %s"), *FullAssetPath);
	return DefinitionAsset;
}

UMinimapTileSetDataAsset* UMinimapGeneratorManager::CreateOrUpdateTileSetAsset()
{
	const FString PackagePath = NormalizePackagePath(Settings.DefinitionAssetPath);
	const FString AssetName = MakeSafeAssetName(TEXT("DA_") + Settings.FileName + TEXT("_TileSet"));
	const FString FullAssetPath = PackagePath + AssetName;

	UPackage* Package = CreatePackage(*FullAssetPath);
	Package->FullyLoad();

	UMinimapTileSetDataAsset* TileSetAsset = FindObject<UMinimapTileSetDataAsset>(Package, *AssetName);
	if (!TileSetAsset)
	{
		TileSetAsset = NewObject<UMinimapTileSetDataAsset>(Package, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.AssetCreated(TileSetAsset);
	}

	TileSetAsset->SchemaVersion = 1;
	TileSetAsset->WorldBounds = Settings.CaptureBounds;
	TileSetAsset->OutputSize = FIntPoint(Settings.OutputWidth, Settings.OutputHeight);
	TileSetAsset->MapRotationDegrees = Settings.CameraRotation.Yaw;
	TileSetAsset->bClampQueriesToBounds = true;
	TileSetAsset->PyramidLevels = TileSetExportLevels;
	TileSetAsset->OverlayLayers = Settings.OverlayLayers;
	TileSetAsset->MarkPackageDirty();
	Package->MarkPackageDirty();
	SaveAssetPackage(Package, TileSetAsset);

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Created/updated tile-set asset: %s"), *FullAssetPath);
	return TileSetAsset;
}

bool UMinimapGeneratorManager::SaveAssetPackage(UPackage* Package, UObject* Asset)
{
	if (!Package || !Asset)
	{
		return false;
	}

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	const double SaveStartTime = FPlatformTime::Seconds();
	const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
	Telemetry.LastPackageSaveSeconds = FPlatformTime::Seconds() - SaveStartTime;
	BroadcastTelemetry();
	if (bSaved)
	{
		Package->SetDirtyFlag(false);
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Failed to save package: %s"), *Package->GetName());
	}

	return bSaved;
}

void UMinimapGeneratorManager::ResetTelemetry(const FString& CaptureMode, const FString& Phase)
{
	Telemetry = FMinimapCaptureTelemetry();
	Telemetry.CaptureMode = CaptureMode;
	Telemetry.CurrentPhase = Phase;
	Telemetry.OutputSize = FIntPoint(Settings.OutputWidth, Settings.OutputHeight);
	Telemetry.TileResolution = Settings.TileResolution;
	Telemetry.bUseTiling = Settings.bUseTiling;
	Telemetry.bExportTileSet = Settings.bExportTileSet;
	Telemetry.bCaptureDynamicShadows = Settings.bCaptureDynamicShadows;
	Telemetry.bOverrideWithHighQualitySettings = Settings.bOverrideWithHighQualitySettings;
	Telemetry.CaptureSourceName = GetCaptureSourceTelemetryName();
	Telemetry.bIsCapturing = true;
	TelemetryCaptureStartTime = FPlatformTime::Seconds();
	TelemetryCurrentTileStartTime = 0.0;
	TelemetryImageSaveStartTime = 0.0;
	TelemetryReadbackStartTime = 0.0;
	TelemetryCompletedTiles = 0;
	TelemetryTimedTileCount = 0;
	TelemetryTileSecondsTotal = 0.0;
	BroadcastTelemetry();
}

void UMinimapGeneratorManager::UpdateTelemetryPhase(const FString& Phase)
{
	Telemetry.CurrentPhase = Phase;
	BroadcastTelemetry();
}

void UMinimapGeneratorManager::BeginTelemetryTile(const int32 CurrentTile, const int32 TotalTiles, const int32 LOD)
{
	Telemetry.CurrentTile = CurrentTile;
	Telemetry.TotalTiles = TotalTiles;
	Telemetry.CurrentLOD = LOD;
	Telemetry.CurrentPhase = TEXT("Capturing Tile");
	TelemetryCurrentTileStartTime = FPlatformTime::Seconds();
	BroadcastTelemetry();
}

void UMinimapGeneratorManager::CompleteTelemetryTile()
{
	if (TelemetryCurrentTileStartTime > 0.0)
	{
		Telemetry.LastTileSeconds = FPlatformTime::Seconds() - TelemetryCurrentTileStartTime;
		TelemetryTileSecondsTotal += Telemetry.LastTileSeconds;
		TelemetryTimedTileCount++;
		Telemetry.AverageTileSeconds = TelemetryTileSecondsTotal / FMath::Max(1, TelemetryTimedTileCount);
		TelemetryCompletedTiles = FMath::Max(TelemetryCompletedTiles, Telemetry.CurrentTile);
		TelemetryCurrentTileStartTime = 0.0;
	}
	BroadcastTelemetry();
}

void UMinimapGeneratorManager::BroadcastTelemetry()
{
	Telemetry.ElapsedSeconds = TelemetryCaptureStartTime > 0.0
		                           ? FPlatformTime::Seconds() - TelemetryCaptureStartTime
		                           : 0.0;
	if (Telemetry.TotalTiles > 0 && Telemetry.AverageTileSeconds > 0.0 && TelemetryCompletedTiles < Telemetry.TotalTiles)
	{
		Telemetry.EstimatedRemainingSeconds = (Telemetry.TotalTiles - TelemetryCompletedTiles) * Telemetry.AverageTileSeconds;
	}
	else
	{
		Telemetry.EstimatedRemainingSeconds = -1.0;
	}
	OnTelemetryUpdated.Broadcast(Telemetry);
}

int32 UMinimapGeneratorManager::GetTileSetTotalTileCount() const
{
	int32 TotalTiles = 0;
	for (const FMinimapTilePyramidLevel& Level : TileSetExportLevels)
	{
		TotalTiles += Level.GridDimensions.X * Level.GridDimensions.Y;
	}
	return TotalTiles;
}

FString UMinimapGeneratorManager::GetCaptureSourceTelemetryName() const
{
	if (!Settings.bOverrideWithHighQualitySettings)
	{
		return TEXT("Default");
	}

	if (const UEnum* CaptureSourceEnum = StaticEnum<ESceneCaptureSource>())
	{
		return CaptureSourceEnum->GetDisplayNameTextByValue(Settings.CaptureSource.GetValue()).ToString();
	}

	return FString::Printf(TEXT("%d"), static_cast<int32>(Settings.CaptureSource.GetValue()));
}

void UMinimapGeneratorManager::TracePanoramicMemory(const TCHAR* Keyword, const FString& Detail) const
{
	if (!IsPanoramicMemoryTraceEnabled())
	{
		return;
	}

	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	const UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	const ASceneCapture2D* CaptureActor = ActiveCaptureActor.Get();
	const FString RenderTargetText = RenderTarget
		                                 ? FString::Printf(TEXT("%s@%p"), *RenderTarget->GetName(), RenderTarget)
		                                 : TEXT("null");
	const FString CaptureActorText = CaptureActor
		                                 ? FString::Printf(TEXT("%s@%p"), *CaptureActor->GetName(), CaptureActor)
		                                 : TEXT("null");

	UE_LOG(OBPanoramicMinimapGenerator, Log,
		TEXT("[PANOMEM][%s] %s | Avail=%s Used=%s Peak=%s Total=%s | RT=%s Actor=%s | StagingPixels=%d LegacyTiles=%d TileSetLevels=%d PendingUnload=%d TempDir=%s"),
		Keyword,
		*Detail,
		*FormatTraceBytes(Stats.AvailablePhysical),
		*FormatTraceBytes(Stats.UsedPhysical),
		*FormatTraceBytes(Stats.PeakUsedPhysical),
		*FormatTraceBytes(Stats.TotalPhysical),
		*RenderTargetText,
		*CaptureActorText,
		StagingPixelBuffer.Num(),
		LegacyCapturedTiles.Num(),
		TileSetExportLevels.Num(),
		PendingUnloadTilePackageNames.Num(),
		LegacyTileTempDirectory.IsEmpty() ? TEXT("none") : *LegacyTileTempDirectory);
}

void UMinimapGeneratorManager::CleanupCaptureResources()
{
	ReleaseCaptureResources(false);
}

void UMinimapGeneratorManager::ReleaseCaptureResources(const bool bFlushRendering)
{
	TracePanoramicMemory(TEXT("RELEASE_CAPTURE_BEGIN"), FString::Printf(TEXT("FlushRendering=%s"), bFlushRendering ? TEXT("true") : TEXT("false")));
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(ReadbackPollTimer);
		GEditor->GetTimerManager()->ClearTimer(StreamingCheckTimer);
	}

	if (ScreenshotCapturedDelegateHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotCaptured().Remove(ScreenshotCapturedDelegateHandle);
		ScreenshotCapturedDelegateHandle.Reset();
	}

	if (ActiveCaptureActor.IsValid())
	{
		TracePanoramicMemory(TEXT("POINTER_REMOVE_CAPTURE_ACTOR_BEGIN"), FString::Printf(TEXT("Actor=%s@%p"),
			*ActiveCaptureActor->GetName(),
			ActiveCaptureActor.Get()));
		if (USceneCaptureComponent2D* CaptureComponent = ActiveCaptureActor->GetCaptureComponent2D())
		{
			CaptureComponent->TextureTarget = nullptr;
		}
		ActiveCaptureActor->Destroy();
		TracePanoramicMemory(TEXT("POINTER_REMOVE_CAPTURE_ACTOR_DESTROYED"), TEXT("Capture actor destroy requested"));
	}
	ActiveCaptureActor.Reset();

	if (ActiveRenderTarget.IsValid())
	{
		TracePanoramicMemory(TEXT("POINTER_REMOVE_RENDER_TARGET_BEGIN"), FString::Printf(TEXT("RenderTarget=%s@%p"),
			*ActiveRenderTarget->GetName(),
			ActiveRenderTarget.Get()));
		ActiveRenderTarget->ReleaseResource();
		ActiveRenderTarget->ConditionalBeginDestroy();
		TracePanoramicMemory(TEXT("POINTER_REMOVE_RENDER_TARGET_RELEASED"), TEXT("Render target release requested"));
	}
	ActiveRenderTarget.Reset();
	TracePanoramicMemory(TEXT("BUFFER_CLEAR_STAGING_BEGIN"), FString::Printf(TEXT("StagingPixels=%d"), StagingPixelBuffer.Num()));
	StagingPixelBuffer.Empty();
	StagingPixelBuffer.Shrink();
	TracePanoramicMemory(TEXT("BUFFER_CLEAR_STAGING_END"), TEXT("Staging buffer emptied and shrunk"));

	if (bFlushRendering)
	{
		TracePanoramicMemory(TEXT("GPU_FLUSH_BEGIN"), TEXT("FlushRenderingCommands before returning capture resources"));
		FlushRenderingCommands();
		TracePanoramicMemory(TEXT("GPU_FLUSH_END"), TEXT("FlushRenderingCommands completed"));
	}
	TracePanoramicMemory(TEXT("RELEASE_CAPTURE_END"), FString::Printf(TEXT("FlushRendering=%s"), bFlushRendering ? TEXT("true") : TEXT("false")));
}

void UMinimapGeneratorManager::PrepareTileTextureForUnload(UTexture2D* Texture, UPackage* Package)
{
	const FString PackageName = Package ? Package->GetName() : TEXT("null");
	const FString TextureName = Texture ? Texture->GetName() : TEXT("null");
	TracePanoramicMemory(TEXT("TILE_TEXTURE_PREPARE_UNLOAD_BEGIN"), FString::Printf(TEXT("Package=%s Texture=%s@%p"),
		*PackageName,
		*TextureName,
		Texture));

	if (Texture)
	{
		if (Texture->IsRooted())
		{
			Texture->RemoveFromRoot();
		}
		Texture->ClearFlags(RF_Standalone | RF_Public);
		Texture->MarkAsGarbage();
	}

	if (Package)
	{
		Package->SetDirtyFlag(false);
	}

	TracePanoramicMemory(TEXT("TILE_TEXTURE_PREPARE_UNLOAD_END"), FString::Printf(TEXT("Package=%s Texture=%s"), *PackageName, *TextureName));
}

void UMinimapGeneratorManager::ReleaseTileImportPackage(UPackage* Package, UTexture2D* Texture, const FString& AssetPath)
{
	if (!Package)
	{
		return;
	}

	const FString PackageName = Package->GetName();
	TracePanoramicMemory(TEXT("TILE_PACKAGE_UNLOAD_BEGIN"), FString::Printf(TEXT("Package=%s Asset=%s Texture=%s@%p"),
		*PackageName,
		*AssetPath,
		Texture ? *Texture->GetName() : TEXT("null"),
		Texture));
	PrepareTileTextureForUnload(Texture, Package);
	FText UnloadErrorMessage;
	const bool bUnloaded = UPackageTools::UnloadPackages({Package}, UnloadErrorMessage, true);
	if (!bUnloaded)
	{
		PendingUnloadTilePackageNames.AddUnique(PackageName);
		if (IsPanoramicMemoryTraceEnabled())
		{
			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("[PANOMEM][TILE_PACKAGE_UNLOAD_DEFERRED] Package=%s Asset=%s Error=%s"),
				*PackageName,
				*AssetPath,
				*UnloadErrorMessage.ToString());
		}
	}
	else
	{
		TracePanoramicMemory(TEXT("TILE_PACKAGE_UNLOAD_SUCCESS"), FString::Printf(TEXT("Package=%s Asset=%s"), *PackageName, *AssetPath));
	}
}

void UMinimapGeneratorManager::ResetMemoryCleanupPolicy()
{
	MemoryCleanupPolicy = FMinimapMemoryCleanupPolicy();
	const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	MemoryCleanupPolicy.LastGCUsedPhysical = MemoryStats.UsedPhysical;
	MemoryCleanupPolicy.LastGCAvailablePhysical = MemoryStats.AvailablePhysical;
	MemoryCleanupPolicy.LastGCTimeSeconds = FPlatformTime::Seconds();
	TracePanoramicMemory(TEXT("MAINTENANCE_POLICY_RESET"), FString::Printf(TEXT("Used=%s Available=%s MinTiles=%d MaxTiles=%d UsedTrigger=%s AvailableDropTrigger=%s"),
		*FormatTraceBytes(MemoryCleanupPolicy.LastGCUsedPhysical),
		*FormatTraceBytes(MemoryCleanupPolicy.LastGCAvailablePhysical),
		MemoryCleanupPolicy.MinTilesBetweenGC,
		MemoryCleanupPolicy.MaxTilesBetweenGC,
		*FormatTraceBytes(MemoryCleanupPolicy.UsedGrowthTriggerBytes),
		*FormatTraceBytes(MemoryCleanupPolicy.AvailableDropTriggerBytes)));
}

void UMinimapGeneratorManager::MaybeRunTileSetMemoryMaintenance(const TCHAR* Reason)
{
	MemoryCleanupPolicy.TilesSinceLastGC++;
	MemoryCleanupPolicy.TilesSinceLastPackageRetry++;

	const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	const double CurrentTime = FPlatformTime::Seconds();
	const bool bPastMinTiles = MemoryCleanupPolicy.TilesSinceLastGC >= MemoryCleanupPolicy.MinTilesBetweenGC;
	const bool bReachedMaxTiles = MemoryCleanupPolicy.TilesSinceLastGC >= MemoryCleanupPolicy.MaxTilesBetweenGC;
	const bool bPastMinSeconds = (CurrentTime - MemoryCleanupPolicy.LastGCTimeSeconds) >= MemoryCleanupPolicy.MinSecondsBetweenGC;
	const uint64 UsedGrowth = MemoryStats.UsedPhysical > MemoryCleanupPolicy.LastGCUsedPhysical
		                          ? MemoryStats.UsedPhysical - MemoryCleanupPolicy.LastGCUsedPhysical
		                          : 0;
	const uint64 AvailableDrop = MemoryCleanupPolicy.LastGCAvailablePhysical > MemoryStats.AvailablePhysical
		                             ? MemoryCleanupPolicy.LastGCAvailablePhysical - MemoryStats.AvailablePhysical
		                             : 0;
	const bool bUsedGrowthTrigger = UsedGrowth >= MemoryCleanupPolicy.UsedGrowthTriggerBytes;
	const bool bAvailableDropTrigger = AvailableDrop >= MemoryCleanupPolicy.AvailableDropTriggerBytes;
	const bool bPendingRetryTrigger = PendingUnloadTilePackageNames.Num() > 0
		&& MemoryCleanupPolicy.TilesSinceLastPackageRetry >= MemoryCleanupPolicy.MinTilesBetweenGC;

	if (bReachedMaxTiles || (bPastMinTiles && bPastMinSeconds && (bUsedGrowthTrigger || bAvailableDropTrigger || bPendingRetryTrigger)))
	{
		RunTileSetMemoryMaintenance(Reason, bUsedGrowthTrigger || bAvailableDropTrigger);
	}
}

void UMinimapGeneratorManager::RunTileSetMemoryMaintenance(const TCHAR* Reason, const bool bFlushRendering)
{
	const FPlatformMemoryStats BeforeStats = FPlatformMemory::GetStats();
	const uint64 UsedDeltaSinceLastGC = BeforeStats.UsedPhysical > MemoryCleanupPolicy.LastGCUsedPhysical
		                                    ? BeforeStats.UsedPhysical - MemoryCleanupPolicy.LastGCUsedPhysical
		                                    : 0;
	const uint64 AvailableDropSinceLastGC = MemoryCleanupPolicy.LastGCAvailablePhysical > BeforeStats.AvailablePhysical
		                                        ? MemoryCleanupPolicy.LastGCAvailablePhysical - BeforeStats.AvailablePhysical
		                                        : 0;
	UpdateTelemetryPhase(TEXT("Memory Maintenance"));
	TracePanoramicMemory(TEXT("MAINTENANCE_BEGIN"), FString::Printf(TEXT("Reason=%s TilesSinceGC=%d TilesSinceRetry=%d PendingUnload=%d UsedGrowth=%s AvailableDrop=%s FlushRendering=%s"),
		Reason ? Reason : TEXT("Unknown"),
		MemoryCleanupPolicy.TilesSinceLastGC,
		MemoryCleanupPolicy.TilesSinceLastPackageRetry,
		PendingUnloadTilePackageNames.Num(),
		*FormatTraceBytes(UsedDeltaSinceLastGC),
		*FormatTraceBytes(AvailableDropSinceLastGC),
		bFlushRendering ? TEXT("true") : TEXT("false")));

	RetryUnloadPendingTilePackages();

	if (bFlushRendering)
	{
		TracePanoramicMemory(TEXT("MAINTENANCE_GPU_FLUSH_BEGIN"), TEXT("FlushRenderingCommands before adaptive GC"));
		FlushRenderingCommands();
		TracePanoramicMemory(TEXT("MAINTENANCE_GPU_FLUSH_END"), TEXT("FlushRenderingCommands completed"));
	}

	const double GCStartTime = FPlatformTime::Seconds();
	CollectGarbage(RF_NoFlags);
	Telemetry.LastGCSeconds = FPlatformTime::Seconds() - GCStartTime;
	const FPlatformMemoryStats AfterStats = FPlatformMemory::GetStats();
	const int64 UsedDelta = static_cast<int64>(AfterStats.UsedPhysical) - static_cast<int64>(BeforeStats.UsedPhysical);
	const int64 AvailableDelta = static_cast<int64>(AfterStats.AvailablePhysical) - static_cast<int64>(BeforeStats.AvailablePhysical);

	MemoryCleanupPolicy.TilesSinceLastGC = 0;
	MemoryCleanupPolicy.TilesSinceLastPackageRetry = 0;
	MemoryCleanupPolicy.LastGCUsedPhysical = AfterStats.UsedPhysical;
	MemoryCleanupPolicy.LastGCAvailablePhysical = AfterStats.AvailablePhysical;
	MemoryCleanupPolicy.LastGCTimeSeconds = FPlatformTime::Seconds();
	Telemetry.CurrentPhase = TEXT("Memory Maintenance");
	BroadcastTelemetry();
	TracePanoramicMemory(TEXT("MAINTENANCE_GC_END"), FString::Printf(TEXT("Reason=%s Duration=%.3fs UsedDelta=%s AvailableDelta=%s PendingUnload=%d"),
		Reason ? Reason : TEXT("Unknown"),
		Telemetry.LastGCSeconds,
		*FormatTraceBytesDelta(UsedDelta),
		*FormatTraceBytesDelta(AvailableDelta),
		PendingUnloadTilePackageNames.Num()));
}

void UMinimapGeneratorManager::RetryUnloadPendingTilePackages()
{
	if (PendingUnloadTilePackageNames.Num() == 0)
	{
		TracePanoramicMemory(TEXT("TILE_PACKAGE_RETRY_SKIP"), TEXT("No pending unload packages"));
		return;
	}

	UpdateTelemetryPhase(TEXT("Unloading Tile Packages"));
	TracePanoramicMemory(TEXT("TILE_PACKAGE_RETRY_BEGIN"), FString::Printf(TEXT("Pending=%d"), PendingUnloadTilePackageNames.Num()));
	TArray<FString> StillLoadedPackageNames;
	for (const FString& PackageName : PendingUnloadTilePackageNames)
	{
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			continue;
		}

		Package->SetDirtyFlag(false);
		FText UnloadErrorMessage;
		const bool bUnloaded = UPackageTools::UnloadPackages({Package}, UnloadErrorMessage, true);
		if (!bUnloaded)
		{
			StillLoadedPackageNames.Add(PackageName);
			if (IsPanoramicMemoryTraceEnabled())
			{
				UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("[PANOMEM][TILE_PACKAGE_RETRY_STILL_LOADED] Package=%s Error=%s"),
					*PackageName,
					*UnloadErrorMessage.ToString());
			}
		}
		else
		{
			TracePanoramicMemory(TEXT("TILE_PACKAGE_RETRY_UNLOADED"), FString::Printf(TEXT("Package=%s"), *PackageName));
		}
	}

	PendingUnloadTilePackageNames = MoveTemp(StillLoadedPackageNames);
	TracePanoramicMemory(TEXT("TILE_PACKAGE_RETRY_END"), FString::Printf(TEXT("Remaining=%d"), PendingUnloadTilePackageNames.Num()));
}

void UMinimapGeneratorManager::ReleaseWorkingMemory(const bool bFinalCleanup)
{
	TracePanoramicMemory(TEXT("WORKING_MEMORY_RELEASE_BEGIN"), FString::Printf(TEXT("FinalCleanup=%s"), bFinalCleanup ? TEXT("true") : TEXT("false")));
	UpdateTelemetryPhase(TEXT("Memory Cleanup"));
	ReleaseCaptureResources(bFinalCleanup);
	TracePanoramicMemory(TEXT("CONTAINER_CLEAR_TILESET_BEGIN"), FString::Printf(TEXT("Levels=%d"), TileSetExportLevels.Num()));
	TileSetExportLevels.Empty();
	TileSetExportLevels.Shrink();
	TracePanoramicMemory(TEXT("CONTAINER_CLEAR_LEGACY_BEGIN"), FString::Printf(TEXT("LegacyTiles=%d"), LegacyCapturedTiles.Num()));
	LegacyCapturedTiles.Empty();
	LegacyCapturedTiles.Shrink();
	TracePanoramicMemory(TEXT("BUFFER_CLEAR_STAGING_WORKING_BEGIN"), FString::Printf(TEXT("StagingPixels=%d"), StagingPixelBuffer.Num()));
	StagingPixelBuffer.Empty();
	StagingPixelBuffer.Shrink();
	CleanupLegacyTileTempFiles();
	RetryUnloadPendingTilePackages();
	TracePanoramicMemory(TEXT("WORKING_MEMORY_AFTER_CLEAR"), FString::Printf(TEXT("FinalCleanup=%s"), bFinalCleanup ? TEXT("true") : TEXT("false")));

	if (bFinalCleanup)
	{
		TracePanoramicMemory(TEXT("GC_BEGIN"), TEXT("CollectGarbage(RF_NoFlags)"));
		const double GCStartTime = FPlatformTime::Seconds();
		CollectGarbage(RF_NoFlags);
		Telemetry.LastGCSeconds = FPlatformTime::Seconds() - GCStartTime;
		TracePanoramicMemory(TEXT("GC_END"), FString::Printf(TEXT("Duration=%.3fs"), Telemetry.LastGCSeconds));
		RetryUnloadPendingTilePackages();
		if (PendingUnloadTilePackageNames.Num() > 0)
		{
			if (IsPanoramicMemoryTraceEnabled())
			{
				UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("[PANOMEM][PENDING_UNLOAD_REMAINING] Count=%d"), PendingUnloadTilePackageNames.Num());
			}
		}
		UpdateTelemetryPhase(TEXT("Cleanup Done"));
		ResetMemoryCleanupPolicy();
	}
	TracePanoramicMemory(TEXT("WORKING_MEMORY_RELEASE_END"), FString::Printf(TEXT("FinalCleanup=%s"), bFinalCleanup ? TEXT("true") : TEXT("false")));
}

UTextureRenderTarget2D* UMinimapGeneratorManager::CreateRenderTarget()
{
	TracePanoramicMemory(TEXT("RENDER_TARGET_CREATE_BEGIN"), FString::Printf(TEXT("UseTiling=%s ExportTileSet=%s CurrentLOD=%d"),
		Settings.bUseTiling ? TEXT("true") : TEXT("false"),
		Settings.bExportTileSet ? TEXT("true") : TEXT("false"),
		CurrentTileLOD));
	const int32 TileSetResolution = (Settings.bExportTileSet && CurrentTileLOD == 0)
		                                ? Settings.TileSetOverviewResolution
		                                : Settings.TileResolution;
	const int32 TargetWidth = Settings.bUseTiling ? TileSetResolution : Settings.OutputWidth;
	const int32 TargetHeight = Settings.bUseTiling ? TileSetResolution : Settings.OutputHeight;
	Telemetry.RenderTargetSize = FIntPoint(TargetWidth, TargetHeight);
	BroadcastTelemetry();

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();

	if (Settings.BackgroundMode == EMinimapBackgroundMode::Transparent)
	{
		RenderTarget->ClearColor = FLinearColor::Transparent;
	}
	else // SolidColor
	{
		RenderTarget->ClearColor = Settings.BackgroundColor;
	}

	// Use PF_B8G8R8A8 instead of PF_FloatRGBA. Float targets have limited readback support
	// on macOS Metal, and the output is FColor (8-bit BGRA) anyway — no precision is lost.
	RenderTarget->InitCustomFormat(TargetWidth, TargetHeight, PF_B8G8R8A8, true);

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Created Render Target (%dx%d)."), TargetWidth, TargetHeight);
	TracePanoramicMemory(TEXT("RENDER_TARGET_CREATE_END"), FString::Printf(TEXT("RenderTarget=%s@%p Size=%dx%d Format=PF_B8G8R8A8"),
		*RenderTarget->GetName(),
		RenderTarget,
		TargetWidth,
		TargetHeight));
	return RenderTarget;
}

ASceneCapture2D* UMinimapGeneratorManager::SpawnAndConfigureCaptureActor(UTextureRenderTarget2D* RenderTarget)
{
	TracePanoramicMemory(TEXT("CAPTURE_ACTOR_CREATE_BEGIN"), FString::Printf(TEXT("RenderTarget=%s@%p"),
		RenderTarget ? *RenderTarget->GetName() : TEXT("null"),
		RenderTarget));
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
	if (!CaptureActor)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("%hs: Failed to spawn ASceneCapture2D actor."), __FUNCTION__);
		return nullptr;
	}
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Spawned SceneCapture2D at (%s), OrthoWidth=%.2f"),
		__FUNCTION__, *CameraLocation.ToString(), CameraOrthoWidth);

	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	// FILTERING
	TArray<AActor*> FinalShowList;
	const double ActorFilterStartTime = FPlatformTime::Seconds();
	BuildFinalShowOnlyList(FinalShowList);
	Telemetry.LastActorFilterSeconds = FPlatformTime::Seconds() - ActorFilterStartTime;
	BroadcastTelemetry();

	// CaptureComponent->ShowFlags = FEngineShowFlags(ESFIM_Editor);
	CaptureComponent->ShowFlags.SetDynamicShadows(Settings.bCaptureDynamicShadows);
	const bool bHasFiltering = Settings.ShowOnlyActors.Num() > 0 || Settings.HiddenActors.Num() > 0 || Settings.ActorClassFilter || !Settings.ActorTagFilter.IsNone();
	CaptureComponent->PrimitiveRenderMode = bHasFiltering ? ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList : Settings.PrimitiveRenderMode;
	CaptureComponent->ShowOnlyActors = (CaptureComponent->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList) ? FinalShowList : TArray<AActor*>();
	CaptureComponent->HiddenActors.Empty();
	// ===================================

	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->ProjectionType = Settings.bIsOrthographic ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
	CaptureComponent->OrthoWidth = CameraOrthoWidth;
	CaptureComponent->FOVAngle = Settings.CameraFOV;
	CaptureComponent->CompositeMode = SCCM_Overwrite;

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
		// Use the default lightweight capture source when quality override is disabled.
		CaptureComponent->CaptureSource = SCS_FinalColorLDR;
		// CaptureComponent->CaptureSource = SCS_SceneColorHDRNoAlpha;
	}

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: SceneCapture2D fully configured. CaptureSource=%d, ShowOnlyActors=%d"),
		__FUNCTION__, static_cast<int32>(CaptureComponent->CaptureSource), CaptureComponent->ShowOnlyActors.Num());
	TracePanoramicMemory(TEXT("CAPTURE_ACTOR_CREATE_END"), FString::Printf(TEXT("Actor=%s@%p RenderTarget=%s@%p ShowOnly=%d"),
		*CaptureActor->GetName(),
		CaptureActor,
		*RenderTarget->GetName(),
		RenderTarget,
		CaptureComponent->ShowOnlyActors.Num()));
	return CaptureActor;
}

void UMinimapGeneratorManager::ReadPixelsAndFinalize()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Scheduling GPU readback for captured render target."), __FUNCTION__);
	UpdateTelemetryPhase(TEXT("Readback"));
	UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	if (!RenderTarget || !RenderTarget->GetResource())
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("%hs: RenderTarget is invalid (ptr=%p). Aborting readback."),
			__FUNCTION__, RenderTarget);
		OnSaveTaskCompleted(false, TEXT("Render Target became invalid before readback."));
		if (GEditor)
		{
			GEditor->GetTimerManager()->ClearTimer(ReadbackPollTimer);
		}
		return;
	}

	FTextureRenderTargetResource* RenderTargetResource = static_cast<FTextureRenderTargetResource*>(RenderTarget->GetResource());
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: RenderTarget resource valid. SizeX=%d, SizeY=%d, Format=%d"),
		__FUNCTION__, RenderTargetResource->GetSizeX(), RenderTargetResource->GetSizeY(),
		static_cast<int32>(RenderTarget->GetFormat()));

	StagingPixelBuffer.Reset();
	StagingPixelBuffer.AddUninitialized(Settings.OutputWidth * Settings.OutputHeight);

	ENQUEUE_RENDER_COMMAND(ReadPixelsCommand)(
		[RenderTargetResource, PixelBuffer = &this->StagingPixelBuffer](
		FRHICommandListImmediate& RHICmdList)
		{
			FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
			ReadPixelFlags.SetLinearToGamma(true);
			RHICmdList.ReadSurfaceData(
				RenderTargetResource->GetRenderTargetTexture(),
				FIntRect(0, 0, RenderTargetResource->GetSizeX(), RenderTargetResource->GetSizeY()),
				*PixelBuffer, ReadPixelFlags);
		}
	);

	ReadbackFence.BeginFence();
	TelemetryReadbackStartTime = FPlatformTime::Seconds();
	ReadbackPollCount = 0;
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Render command enqueued. Readback fence started. Starting poll timer (0.1s)."), __FUNCTION__);

	GEditor->GetTimerManager()->SetTimer(ReadbackPollTimer, this, &UMinimapGeneratorManager::CheckReadbackStatus, 0.1f, true);

	OnProgress.Broadcast(FText::FromString(TEXT("GPU is rendering... Editor remains responsive.")), 0.5f, 0, 0);
}

void UMinimapGeneratorManager::CheckReadbackStatus()
{
	if (bCancelRequested) return;

	ReadbackPollCount++;

	// Timeout safety: abort after ~15 seconds (150 polls at 0.1s interval)
	constexpr int32 MaxPollCount = 150;
	if (ReadbackPollCount > MaxPollCount)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error,
			TEXT("%hs: Readback fence did not complete after %d polls (~%.1fs). Aborting capture. This may indicate a GPU readback issue on this platform (e.g. macOS Metal)."),
			__FUNCTION__, ReadbackPollCount, ReadbackPollCount * 0.1f);

		CleanupCaptureResources();

		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Cleanup completed. All capture resources released."), __FUNCTION__);
		OnSaveTaskCompleted(false, TEXT("GPU readback timed out."));
		return;
	}

	if (ReadbackPollCount % 20 == 0)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Poll #%d — readback fence not yet complete."),
			__FUNCTION__, ReadbackPollCount);
	}

	if (ReadbackFence.IsFenceComplete())
	{
		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Readback fence completed after %d polls."),
			__FUNCTION__, ReadbackPollCount);
		GEditor->GetTimerManager()->ClearTimer(ReadbackPollTimer);
		if (TelemetryReadbackStartTime > 0.0)
		{
			Telemetry.LastReadbackSeconds = FPlatformTime::Seconds() - TelemetryReadbackStartTime;
			TelemetryReadbackStartTime = 0.0;
			BroadcastTelemetry();
		}

		if (ActiveCaptureActor.IsValid())
		{
			ActiveCaptureActor->Destroy();
		}
		ActiveCaptureActor.Reset();

		if (ActiveRenderTarget.IsValid())
		{
			ActiveRenderTarget->ConditionalBeginDestroy();
		}
		ActiveRenderTarget.Reset();

		if (StagingPixelBuffer.Num() > 0)
		{
			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: GPU readback complete. Pixel buffer has %d pixels. Starting async save task."),
				__FUNCTION__, StagingPixelBuffer.Num());
			StartImageSaveTask(MoveTemp(StagingPixelBuffer), Settings.OutputWidth, Settings.OutputHeight);

			// MoveTemp transfers data ownership but does not release allocated capacity.
			// Explicitly free the ~64MB staging buffer.
			StagingPixelBuffer.Empty();

			UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("%hs: Cleanup completed. All capture resources released."), __FUNCTION__);
			OnProgress.Broadcast(FText::FromString(TEXT("Saving image...")), 0.95f, 0, 0);
		}
		else
		{
			UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("%hs: GPU readback resulted in empty pixel data."), __FUNCTION__);
			OnSaveTaskCompleted(false, TEXT("GPU readback resulted in empty pixel data."));
		}
	}
}

void UMinimapGeneratorManager::StartImageSaveTask(TArray<FColor> PixelData, int32 ImageWidth, int32 ImageHeight)
{
	UpdateTelemetryPhase(TEXT("Saving Image"));
	FString FinalFileName = Settings.FileName;
	if (Settings.bUseAutoFilename)
	{
		const FDateTime Now = FDateTime::Now();
		const FString Timestamp = Now.ToString(TEXT("_%Y%m%d_%H%M%S"));
		FinalFileName += Timestamp;
	}
	FinalFileName += TEXT(".png");

	const FString FullPath = FPaths::Combine(Settings.OutputPath, FinalFileName);
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Starting async image save task: %s (%dx%d)."), *FullPath, ImageWidth, ImageHeight);

	TelemetryImageSaveStartTime = FPlatformTime::Seconds();
	BroadcastTelemetry();
	(new FAutoDeleteAsyncTask<FSaveImageTask>(MoveTemp(PixelData), ImageWidth, ImageHeight, FullPath, this))->
		StartBackgroundTask();
}

void UMinimapGeneratorManager::BuildFinalShowOnlyList(TArray<AActor*>& OutShowOnlyList) const
{
	OutShowOnlyList.Empty();
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	// 1) Determine the source pool.
	TArray<AActor*> CandidateActors;
	TArray<AActor*> ExplicitShowOnlyList;
	for (const TSoftObjectPtr<AActor>& ActorPtr : Settings.ShowOnlyActors)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			ExplicitShowOnlyList.Add(Actor);
		}
	}

	if (ExplicitShowOnlyList.Num() > 0)
	{
		// If ShowOnly contains actors, use it as the candidate source.
		CandidateActors = ExplicitShowOnlyList;
	}
	else
	{
		// Otherwise, use all actors in the world as the candidate source.
		UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), CandidateActors);
	}

	// 2) Build a set of actors to hide for fast lookup.
	TSet<AActor*> ActorsToHide;
	// Add explicit hidden actors.
	for (const TSoftObjectPtr<AActor>& ActorPtr : Settings.HiddenActors)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			ActorsToHide.Add(Actor);
		}
	}

	// 3) Scan all world actors and add matches from class/tag filters.
	// We still do this even when ShowOnly is used so Hide-by-Class/Tag always applies.
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
	for (AActor* Actor : AllActors)
	{
		if (!Actor) continue;

		// Hide actors matching the class filter.
		if (Settings.ActorClassFilter && Actor->IsA(Settings.ActorClassFilter))
		{
			ActorsToHide.Add(Actor);
		}

		// Hide actors matching the tag filter.
		if (!Settings.ActorTagFilter.IsNone() && Actor->Tags.Contains(Settings.ActorTagFilter))
		{
			ActorsToHide.Add(Actor);
		}
	}

	// 4) Final pass: keep only candidates that are not in the hide set.
	for (AActor* Candidate : CandidateActors)
	{
		if (Candidate && !ActorsToHide.Contains(Candidate))
		{
			OutShowOnlyList.Add(Candidate);
		}
	}

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Built final ShowOnly list: %d actors (candidates: %d, hidden: %d)."),
		OutShowOnlyList.Num(), CandidateActors.Num(), ActorsToHide.Num());
}

// ===================================================================
// FLOW 2: TILED CAPTURE
// ===================================================================

FString UMinimapGeneratorManager::NormalizePackagePath(const FString& InPackagePath) const
{
	FString PackagePath = InPackagePath;
	if (!PackagePath.StartsWith(TEXT("/Game")))
	{
		PackagePath = TEXT("/Game/Minimaps/");
	}
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	return PackagePath;
}

void UMinimapGeneratorManager::StartTileSetCaptureProcess()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Starting direct tile-set capture process."));
	TracePanoramicMemory(TEXT("TILESET_START_BEGIN"), FString::Printf(TEXT("MaxLOD=%d WorldTileSize=%.2f OverviewRes=%d TileRes=%d"),
		Settings.TileSetMaxLOD,
		Settings.TileSetWorldTileSize,
		Settings.TileSetOverviewResolution,
		Settings.TileResolution));

	CurrentTileIndex = 0;
	CurrentTileLOD = 0;
	LegacyCapturedTiles.Empty();
	TileSetExportLevels.Empty();
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();
	ResetMemoryCleanupPolicy();

	InitializeTileSetExportLevels();
	Telemetry.TotalTiles = GetTileSetTotalTileCount();
	Telemetry.CurrentLOD = 0;
	BroadcastTelemetry();
	if (TileSetExportLevels.Num() == 0)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Invalid tile-set grid."));
		return;
	}

	CurrentTileSetGrid = TileSetExportLevels[0].GridDimensions;
	ActiveRenderTarget = CreateRenderTarget();
	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());
	TracePanoramicMemory(TEXT("TILESET_START_AFTER_RESOURCES"), FString::Printf(TEXT("TotalTiles=%d Levels=%d"),
		GetTileSetTotalTileCount(),
		TileSetExportLevels.Num()));

	if (!ActiveCaptureActor.IsValid() || !ActiveRenderTarget.IsValid())
	{
		OnCaptureComplete.Broadcast(false, TEXT("Failed to create capture actor or render target for tile-set export."));
		return;
	}

	CaptureNextTileSetTile();
}

void UMinimapGeneratorManager::InitializeTileSetExportLevels()
{
	const FVector BoundsSize = Settings.CaptureBounds.GetSize();
	const int32 FullTilesX = FMath::Max(1, FMath::CeilToInt(BoundsSize.X / Settings.TileSetWorldTileSize));
	const int32 FullTilesY = FMath::Max(1, FMath::CeilToInt(BoundsSize.Y / Settings.TileSetWorldTileSize));
	const int32 MaxLOD = FMath::Clamp(Settings.TileSetMaxLOD, 0, 8);

	TileSetExportLevels.Reserve(MaxLOD + 1);
	for (int32 LOD = 0; LOD <= MaxLOD; ++LOD)
	{
		const int32 TargetGrid = 1 << LOD;
		const FIntPoint GridDimensions(
			LOD == MaxLOD ? FullTilesX : FMath::Clamp(TargetGrid, 1, FullTilesX),
			LOD == MaxLOD ? FullTilesY : FMath::Clamp(TargetGrid, 1, FullTilesY));
		const int32 TilePixels = LOD == 0 ? Settings.TileSetOverviewResolution : Settings.TileResolution;

		FMinimapTilePyramidLevel Level;
		Level.LOD = LOD;
		Level.GridDimensions = GridDimensions;
		Level.TilePixelSize = FIntPoint(TilePixels, TilePixels);
		Level.WorldTileSize = FVector2D(BoundsSize.X / GridDimensions.X, BoundsSize.Y / GridDimensions.Y);
		Level.LogicalPixelSize = FIntPoint(GridDimensions.X * TilePixels, GridDimensions.Y * TilePixels);
		Level.Tiles.Reserve(GridDimensions.X * GridDimensions.Y);
		TileSetExportLevels.Add(Level);
	}

	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Initialized tile-set pyramid: FullGrid=%dx%d, LODs=%d"),
		FullTilesX, FullTilesY, TileSetExportLevels.Num());
	TracePanoramicMemory(TEXT("TILESET_LEVELS_INITIALIZED"), FString::Printf(TEXT("FullGrid=%dx%d LODs=%d TotalTiles=%d"),
		FullTilesX,
		FullTilesY,
		TileSetExportLevels.Num(),
		GetTileSetTotalTileCount()));
}

bool UMinimapGeneratorManager::AdvanceToNextTileSetLevel()
{
	TracePanoramicMemory(TEXT("TILESET_LOD_ADVANCE_BEGIN"), FString::Printf(TEXT("CurrentLOD=%d"), CurrentTileLOD));
	ReleaseCaptureResources(true);

	CurrentTileLOD++;
	CurrentTileIndex = 0;
	if (!TileSetExportLevels.IsValidIndex(CurrentTileLOD))
	{
		TracePanoramicMemory(TEXT("TILESET_LOD_ADVANCE_END"), FString::Printf(TEXT("NoMoreLOD CurrentLOD=%d"), CurrentTileLOD));
		return false;
	}

	CurrentTileSetGrid = TileSetExportLevels[CurrentTileLOD].GridDimensions;
	ActiveRenderTarget = CreateRenderTarget();
	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());

	TracePanoramicMemory(TEXT("TILESET_LOD_ADVANCE_END"), FString::Printf(TEXT("NewLOD=%d Grid=%dx%d Valid=%s"),
		CurrentTileLOD,
		CurrentTileSetGrid.X,
		CurrentTileSetGrid.Y,
		(ActiveCaptureActor.IsValid() && ActiveRenderTarget.IsValid()) ? TEXT("true") : TEXT("false")));
	return ActiveCaptureActor.IsValid() && ActiveRenderTarget.IsValid();
}

FBox UMinimapGeneratorManager::GetTileSetTileWorldBounds(const int32 LOD, const int32 TileX, const int32 TileY) const
{
	if (!TileSetExportLevels.IsValidIndex(LOD))
	{
		return FBox(ForceInit);
	}

	const FMinimapTilePyramidLevel& Level = TileSetExportLevels[LOD];
	const FVector BoundsMin = Settings.CaptureBounds.Min;
	const FVector BoundsMax = Settings.CaptureBounds.Max;
	const FVector2D TileWorldSize = Level.WorldTileSize;

	const FVector TileMin(
		BoundsMin.X + TileX * TileWorldSize.X,
		TileY == Level.GridDimensions.Y - 1 ? BoundsMin.Y : BoundsMax.Y - (TileY + 1) * TileWorldSize.Y,
		BoundsMin.Z);
	const FVector TileMax(
		TileX == Level.GridDimensions.X - 1 ? BoundsMax.X : TileMin.X + TileWorldSize.X,
		BoundsMax.Y - TileY * TileWorldSize.Y,
		BoundsMax.Z);

	return FBox(TileMin, TileMax);
}

void UMinimapGeneratorManager::CaptureNextTileSetTile()
{
	if (bCancelRequested)
	{
		return;
	}

	if (!TileSetExportLevels.IsValidIndex(CurrentTileLOD))
	{
		FinalizeTileSetExport();
		return;
	}

	const FMinimapTilePyramidLevel& Level = TileSetExportLevels[CurrentTileLOD];
	const int32 TilesInLevel = Level.GridDimensions.X * Level.GridDimensions.Y;
	if (CurrentTileIndex >= TilesInLevel)
	{
		if (AdvanceToNextTileSetLevel())
		{
			CaptureNextTileSetTile();
		}
		else
		{
			FinalizeTileSetExport();
		}
		return;
	}

	if (!ActiveCaptureActor.IsValid() || !ActiveRenderTarget.IsValid())
	{
		OnCaptureComplete.Broadcast(false, TEXT("Capture actor or render target became invalid during tile-set capture."));
		return;
	}

	const int32 TileX = CurrentTileIndex % Level.GridDimensions.X;
	const int32 TileY = CurrentTileIndex / Level.GridDimensions.X;
	const FBox TileWorldBounds = GetTileSetTileWorldBounds(CurrentTileLOD, TileX, TileY);
	const FVector TileCenter = TileWorldBounds.GetCenter();
	const FVector TileSize = TileWorldBounds.GetSize();

	int32 TotalTiles = 0;
	int32 CompletedBeforeLevel = 0;
	for (int32 LevelIndex = 0; LevelIndex < TileSetExportLevels.Num(); ++LevelIndex)
	{
		const int32 LevelTileCount = TileSetExportLevels[LevelIndex].GridDimensions.X * TileSetExportLevels[LevelIndex].GridDimensions.Y;
		TotalTiles += LevelTileCount;
		if (LevelIndex < CurrentTileLOD)
		{
			CompletedBeforeLevel += LevelTileCount;
		}
	}

	const int32 CompletedTiles = CompletedBeforeLevel + CurrentTileIndex;
	BeginTelemetryTile(CompletedTiles + 1, TotalTiles, CurrentTileLOD);
	TracePanoramicMemory(TEXT("TILESET_TILE_CAPTURE_BEGIN"), FString::Printf(TEXT("GlobalTile=%d/%d LOD=%d Tile=%d/%d Coord=%d,%d"),
		CompletedTiles + 1,
		TotalTiles,
		CurrentTileLOD,
		CurrentTileIndex + 1,
		TilesInLevel,
		TileX,
		TileY));

	ActiveCaptureActor->SetActorLocation(FVector(TileCenter.X, TileCenter.Y, Settings.CameraHeight));
	USceneCaptureComponent2D* CaptureComponent = ActiveCaptureActor->GetCaptureComponent2D();
	CaptureComponent->OrthoWidth = FMath::Max(TileSize.X, TileSize.Y);
	CaptureComponent->CaptureScene();

	const float CurrentProgress = TotalTiles > 0 ? static_cast<float>(CompletedTiles) / TotalTiles : 0.0f;
	OnProgress.Broadcast(
		FText::Format(FText::FromString("Capturing tile-set LOD {0} tile {1}/{2}..."),
			FText::AsNumber(CurrentTileLOD),
			FText::AsNumber(CurrentTileIndex + 1),
			FText::AsNumber(TilesInLevel)),
		CurrentProgress * 0.95f,
		CurrentTileIndex,
		TilesInLevel);

	if (GEditor)
	{
		FTimerHandle TempHandle;
		GEditor->GetTimerManager()->SetTimer(
			TempHandle, this, &UMinimapGeneratorManager::OnTileSetTileRenderedAndContinue, 0.2f, false);
	}
}

void UMinimapGeneratorManager::OnTileSetTileRenderedAndContinue()
{
	if (bCancelRequested)
	{
		return;
	}
	TracePanoramicMemory(TEXT("TILESET_TILE_READBACK_BEGIN"), FString::Printf(TEXT("LOD=%d TileIndex=%d"), CurrentTileLOD, CurrentTileIndex));

	UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	if (!RenderTarget || !TileSetExportLevels.IsValidIndex(CurrentTileLOD))
	{
		OnCaptureComplete.Broadcast(false, TEXT("Render target lost during tile-set capture."));
		return;
	}

	TArray<FColor> TilePixels;
	int32 TileWidth = 0;
	int32 TileHeight = 0;
	if (auto* RTResource = static_cast<FTextureRenderTargetResource*>(RenderTarget->GetResource()))
	{
		const double ReadbackStartTime = FPlatformTime::Seconds();
		FlushRenderingCommands();
		RTResource->ReadPixels(TilePixels);
		Telemetry.LastReadbackSeconds = FPlatformTime::Seconds() - ReadbackStartTime;
		TileWidth = RTResource->GetSizeX();
		TileHeight = RTResource->GetSizeY();
		BroadcastTelemetry();
		TracePanoramicMemory(TEXT("TILESET_TILE_READBACK_END"), FString::Printf(TEXT("LOD=%d TileIndex=%d Pixels=%d Size=%dx%d Readback=%.3fs"),
			CurrentTileLOD,
			CurrentTileIndex,
			TilePixels.Num(),
			TileWidth,
			TileHeight,
			Telemetry.LastReadbackSeconds));
	}

	if (TilePixels.Num() > 0)
	{
		FMinimapTilePyramidLevel& Level = TileSetExportLevels[CurrentTileLOD];
		const int32 TileX = CurrentTileIndex % Level.GridDimensions.X;
		const int32 TileY = CurrentTileIndex / Level.GridDimensions.X;
		const FString BaseName = MakeSafeAssetName(Settings.FileName);
		const FString TexturePackagePath = NormalizePackagePath(Settings.AssetPath)
			+ BaseName + TEXT("/Tiles/LOD_") + FString::FromInt(CurrentTileLOD) + TEXT("/");
		const FString TextureAssetName = FString::Printf(TEXT("T_%s_L%d_X%d_Y%d"), *BaseName, CurrentTileLOD, TileX, TileY);

		UpdateTelemetryPhase(TEXT("Importing Tile"));
		const double ImportStartTime = FPlatformTime::Seconds();
		TracePanoramicMemory(TEXT("TILESET_TILE_IMPORT_BEGIN"), FString::Printf(TEXT("LOD=%d X=%d Y=%d Asset=%s Pixels=%d"),
			CurrentTileLOD,
			TileX,
			TileY,
			*TextureAssetName,
			TilePixels.Num()));
		const TSoftObjectPtr<UTexture2D> TileTexture = ImportTileTextureAssetFromPixels(
			TilePixels, TileWidth, TileHeight, TexturePackagePath, TextureAssetName);
		Telemetry.LastImportSeconds = FPlatformTime::Seconds() - ImportStartTime;
		BroadcastTelemetry();
		TracePanoramicMemory(TEXT("TILESET_TILE_IMPORT_END"), FString::Printf(TEXT("LOD=%d X=%d Y=%d Asset=%s Import=%.3fs"),
			CurrentTileLOD,
			TileX,
			TileY,
			*TextureAssetName,
			Telemetry.LastImportSeconds));

		const FBox TileWorldBounds = GetTileSetTileWorldBounds(CurrentTileLOD, TileX, TileY);
		const FVector BoundsSize = Settings.CaptureBounds.GetSize();
		const FVector BoundsMin = Settings.CaptureBounds.Min;

		FMinimapTileRef TileRef;
		TileRef.Coord.LOD = CurrentTileLOD;
		TileRef.Coord.X = TileX;
		TileRef.Coord.Y = TileY;
		TileRef.Texture = TileTexture;
		TileRef.ValidPixelMin = FIntPoint::ZeroValue;
		TileRef.ValidPixelMax = FIntPoint(TileWidth, TileHeight);
		TileRef.WorldBounds = TileWorldBounds;
		TileRef.UVMin = FVector2D(
			(TileWorldBounds.Min.X - BoundsMin.X) / BoundsSize.X,
			1.0f - ((TileWorldBounds.Max.Y - BoundsMin.Y) / BoundsSize.Y));
		TileRef.UVMax = FVector2D(
			(TileWorldBounds.Max.X - BoundsMin.X) / BoundsSize.X,
			1.0f - ((TileWorldBounds.Min.Y - BoundsMin.Y) / BoundsSize.Y));
		Level.Tiles.Add(TileRef);

		TilePixels.Empty();
		TilePixels.Shrink();
		TracePanoramicMemory(TEXT("TILESET_TILE_BUFFER_RELEASED"), FString::Printf(TEXT("LOD=%d X=%d Y=%d LevelTiles=%d"),
			CurrentTileLOD,
			TileX,
			TileY,
			Level.Tiles.Num()));
		MaybeRunTileSetMemoryMaintenance(TEXT("PostTile"));
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Tile-set LOD %d tile %d rendered with empty pixel data."),
			CurrentTileLOD, CurrentTileIndex);
	}

	CompleteTelemetryTile();
	TracePanoramicMemory(TEXT("TILESET_TILE_COMPLETE"), FString::Printf(TEXT("LOD=%d CompletedTileIndex=%d LastTile=%.3fs AvgTile=%.3fs"),
		CurrentTileLOD,
		CurrentTileIndex,
		Telemetry.LastTileSeconds,
		Telemetry.AverageTileSeconds));
	CurrentTileIndex++;
	CaptureNextTileSetTile();
}

void UMinimapGeneratorManager::FinalizeTileSetExport()
{
	TracePanoramicMemory(TEXT("TILESET_FINALIZE_BEGIN"), FString::Printf(TEXT("Levels=%d TotalTiles=%d PendingUnload=%d"),
		TileSetExportLevels.Num(),
		GetTileSetTotalTileCount(),
		PendingUnloadTilePackageNames.Num()));
	UpdateTelemetryPhase(TEXT("Saving TileSet Metadata"));
	ReleaseCaptureResources(true);
	OnProgress.Broadcast(FText::FromString(TEXT("Saving tile-set metadata...")), 0.98f, 0, 0);

	UMinimapTileSetDataAsset* TileSetAsset = CreateOrUpdateTileSetAsset();
	if (!TileSetAsset)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Failed to create tile-set asset."));
		ReleaseWorkingMemory(true);
		return;
	}

	UMinimapDefinitionDataAsset* DefinitionAsset = nullptr;
	if (Settings.bExportDefinitionAsset)
	{
		DefinitionAsset = CreateOrUpdateDefinitionAsset(TEXT(""), nullptr, TileSetAsset);
	}

	if (GEditor && !IsEngineExitRequested())
	{
		TArray<UObject*> AssetsToSync;
		AssetsToSync.Add(TileSetAsset);
		if (DefinitionAsset)
		{
			AssetsToSync.Add(DefinitionAsset);
		}
		GEditor->SyncBrowserToObjects(AssetsToSync);
	}

	const FString TileSetAssetPath = TileSetAsset->GetPathName();
	const FString DefinitionAssetPath = DefinitionAsset ? DefinitionAsset->GetPathName() : TEXT("");
	OnProgress.Broadcast(FText::FromString(TEXT("Done!")), 1.0f, 0, 0);
	Telemetry.CurrentPhase = TEXT("Done");
	Telemetry.bIsCapturing = false;
	BroadcastTelemetry();
	OnCaptureComplete.Broadcast(true, TileSetAssetPath);
	if (IsPanoramicMemoryTraceEnabled())
	{
		UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("[PANOMEM][OS_FILE_CACHE_NOTE] AvailablePhysical may remain lower after exporting many tile packages because the OS can keep recently written .uasset files in filesystem cache. Use UsedPhysical/GC traces to distinguish Unreal object retention from OS cache."));
	}
	ReleaseWorkingMemory(true);
	TracePanoramicMemory(TEXT("TILESET_FINALIZE_END"), FString::Printf(TEXT("TileSetAsset=%s DefinitionAsset=%s"), *TileSetAssetPath, *DefinitionAssetPath));
}

void UMinimapGeneratorManager::StartTiledCaptureProcess()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Starting tiled capture process."));
	TracePanoramicMemory(TEXT("FULLRES_TILED_START_BEGIN"), FString::Printf(TEXT("Output=%dx%d TileRes=%d Overlap=%d"),
		Settings.OutputWidth,
		Settings.OutputHeight,
		Settings.TileResolution,
		Settings.TileOverlap));
	CurrentTileIndex = 0;
	LegacyCapturedTiles.Empty();
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();
	CaptureSessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	LegacyTileTempDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("PanoramicMinimapTemp"),
		CaptureSessionId);
	if (!IFileManager::Get().MakeDirectory(*LegacyTileTempDirectory, true))
	{
		OnCaptureComplete.Broadcast(false, TEXT("Failed to create legacy tile temp directory."));
		ReleaseWorkingMemory(true);
		return;
	}
	TracePanoramicMemory(TEXT("FULLRES_TEMP_DIR_CREATED"), FString::Printf(TEXT("TempDir=%s"), *LegacyTileTempDirectory));

	CalculateGrid();
	Telemetry.TotalTiles = NumTilesX * NumTilesY;
	BroadcastTelemetry();

	if (NumTilesX * NumTilesY == 0)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Invalid grid size."));
		return;
	}

	ActiveRenderTarget = CreateRenderTarget();
	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());
	TracePanoramicMemory(TEXT("FULLRES_TILED_AFTER_RESOURCES"), FString::Printf(TEXT("Grid=%dx%d TotalTiles=%d"),
		NumTilesX,
		NumTilesY,
		NumTilesX * NumTilesY));

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
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Calculated Grid: %d x %d tiles"), NumTilesX, NumTilesY);
}

void UMinimapGeneratorManager::CaptureNextTile()
{
	if (bCancelRequested) return;

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
	UE_LOG(OBPanoramicMinimapGenerator, Verbose, TEXT("Capturing tile index %d (%d, %d)."), CurrentTileIndex, TileX, TileY);

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

	// 5. Configure the capture actor.
	ActiveCaptureActor->SetActorLocation(TileCenterLocation);
	USceneCaptureComponent2D* CaptureComponent = ActiveCaptureActor->GetCaptureComponent2D();

	// Set the OrthoWidth to the calculated square size of the tile's capture area
	CaptureComponent->OrthoWidth = TileOrthoSize;

	BeginTelemetryTile(CurrentTileIndex + 1, NumTilesX * NumTilesY, INDEX_NONE);
	TracePanoramicMemory(TEXT("FULLRES_TILE_CAPTURE_BEGIN"), FString::Printf(TEXT("Tile=%d/%d Coord=%d,%d"),
		CurrentTileIndex + 1,
		NumTilesX * NumTilesY,
		TileX,
		TileY));
	CaptureComponent->CaptureScene();

	const float CurrentProgress = static_cast<float>(CurrentTileIndex) / (NumTilesX * NumTilesY);

	OnProgress.Broadcast(
		FText::Format(FText::FromString("Capturing tile {0}/{1}..."), FText::AsNumber(CurrentTileIndex + 1),
		              FText::AsNumber(NumTilesX * NumTilesY)),
		CurrentProgress * 0.9f,
		CurrentTileIndex,
		NumTilesX * NumTilesY
	);

	// GEditor->GetEditorWorldContext().World()->GetTimerManager().SetTimerForNextTick(
	// 	this, &UMinimapGeneratorManager::OnTileRenderedAndContinue);

	if (GEditor)
	{
		FTimerHandle TempHandle;
		GEditor->GetTimerManager()->SetTimer(
			TempHandle, this, &UMinimapGeneratorManager::OnTileRenderedAndContinue, 0.2f, false);
	}
}

void UMinimapGeneratorManager::OnTileRenderedAndContinue()
{
	if (bCancelRequested) return;
	TracePanoramicMemory(TEXT("FULLRES_TILE_READBACK_BEGIN"), FString::Printf(TEXT("TileIndex=%d"), CurrentTileIndex));

	UTextureRenderTarget2D* RenderTarget = ActiveRenderTarget.Get();
	if (!RenderTarget)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Render Target lost during tiling."));
		return;
	}

	TArray<FColor> TilePixels;
	if (auto* RTResource = static_cast<FTextureRenderTargetResource*>(RenderTarget->GetResource()))
	{
		const double ReadbackStartTime = FPlatformTime::Seconds();
		FlushRenderingCommands();
		RTResource->ReadPixels(TilePixels);
		Telemetry.LastReadbackSeconds = FPlatformTime::Seconds() - ReadbackStartTime;
		BroadcastTelemetry();
		TracePanoramicMemory(TEXT("FULLRES_TILE_READBACK_END"), FString::Printf(TEXT("TileIndex=%d Pixels=%d Readback=%.3fs"),
			CurrentTileIndex,
			TilePixels.Num(),
			Telemetry.LastReadbackSeconds));
	}

	if (TilePixels.Num() > 0)
	{
		const int32 TileX = CurrentTileIndex % NumTilesX;
		const int32 TileY = CurrentTileIndex / NumTilesX;

		// Save individual debug tiles if enabled.
		if (Settings.bSaveTiles)
		{
			// Use base file name without extension for per-tile output.
			const FString BaseFileName = Settings.FileName;
			if (Settings.bUseAutoFilename)
			{
				// Keep debug tile names clean even when auto filename is enabled.
			}

			SaveDebugTileImage(Settings.OutputPath, BaseFileName, TilePixels, TileX, TileY, Settings.TileResolution);
		}

		if (!WriteLegacyTileToTempFile(TilePixels, TileX, TileY, Settings.TileResolution, Settings.TileResolution))
		{
			TilePixels.Empty();
			TilePixels.Shrink();
			OnCaptureComplete.Broadcast(false, TEXT("Failed to write legacy tile cache."));
			ReleaseWorkingMemory(true);
			return;
		}
		TilePixels.Empty();
		TilePixels.Shrink();
		TracePanoramicMemory(TEXT("FULLRES_TILE_BUFFER_RELEASED"), FString::Printf(TEXT("Tile=%d Coord=%d,%d CachedTiles=%d"),
			CurrentTileIndex + 1,
			TileX,
			TileY,
			LegacyCapturedTiles.Num()));
		UE_LOG(OBPanoramicMinimapGenerator, Verbose, TEXT("Tile (%d, %d) captured and cached to disk."), TileX, TileY);
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Tile %d rendered with empty pixel data."), CurrentTileIndex);
	}

	CompleteTelemetryTile();
	TracePanoramicMemory(TEXT("FULLRES_TILE_COMPLETE"), FString::Printf(TEXT("TileIndex=%d LastTile=%.3fs AvgTile=%.3fs"),
		CurrentTileIndex,
		Telemetry.LastTileSeconds,
		Telemetry.AverageTileSeconds));
	CurrentTileIndex++;
	CaptureNextTile();
}

bool UMinimapGeneratorManager::WriteLegacyTileToTempFile(const TArray<FColor>& PixelData,
                                                         const int32 TileX,
                                                         const int32 TileY,
                                                         const int32 Width,
                                                         const int32 Height)
{
	if (PixelData.Num() != Width * Height || LegacyTileTempDirectory.IsEmpty())
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*LegacyTileTempDirectory, true);
	const FString RawTilePath = FPaths::Combine(
		LegacyTileTempDirectory,
		FString::Printf(TEXT("Tile_X%d_Y%d_%dx%d.bgra"), TileX, TileY, Width, Height));
	TracePanoramicMemory(TEXT("FULLRES_TEMP_WRITE_BEGIN"), FString::Printf(TEXT("Path=%s Pixels=%d Bytes=%lld"),
		*RawTilePath,
		PixelData.Num(),
		static_cast<int64>(PixelData.Num()) * sizeof(FColor)));

	const TArrayView64<const uint8> RawBytes(
		reinterpret_cast<const uint8*>(PixelData.GetData()),
		static_cast<int64>(PixelData.Num()) * sizeof(FColor));
	if (!FFileHelper::SaveArrayToFile(RawBytes, *RawTilePath))
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to write legacy tile cache: %s"), *RawTilePath);
		return false;
	}
	TracePanoramicMemory(TEXT("FULLRES_TEMP_WRITE_END"), FString::Printf(TEXT("Path=%s"), *RawTilePath));

	FLegacyCapturedTileRef TileRef;
	TileRef.Coord = FIntPoint(TileX, TileY);
	TileRef.RawTilePath = RawTilePath;
	TileRef.Width = Width;
	TileRef.Height = Height;
	LegacyCapturedTiles.Add(TileRef);
	return true;
}

bool UMinimapGeneratorManager::LoadLegacyTileFromTempFile(const FLegacyCapturedTileRef& TileRef, TArray<FColor>& OutPixels) const
{
	OutPixels.Empty();
	if (TileRef.Width <= 0 || TileRef.Height <= 0 || TileRef.RawTilePath.IsEmpty())
	{
		return false;
	}

	TArray<uint8> RawBytes;
	TracePanoramicMemory(TEXT("FULLRES_TEMP_LOAD_BEGIN"), FString::Printf(TEXT("Path=%s"), *TileRef.RawTilePath));
	if (!FFileHelper::LoadFileToArray(RawBytes, *TileRef.RawTilePath))
	{
		return false;
	}

	const int64 ExpectedBytes = static_cast<int64>(TileRef.Width) * TileRef.Height * sizeof(FColor);
	if (RawBytes.Num() != ExpectedBytes)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Legacy tile cache size mismatch: %s expected %lld bytes, got %d."),
			*TileRef.RawTilePath,
			ExpectedBytes,
			RawBytes.Num());
		return false;
	}

	OutPixels.SetNumUninitialized(TileRef.Width * TileRef.Height);
	FMemory::Memcpy(OutPixels.GetData(), RawBytes.GetData(), RawBytes.Num());
	RawBytes.Empty();
	RawBytes.Shrink();
	TracePanoramicMemory(TEXT("FULLRES_TEMP_LOAD_END"), FString::Printf(TEXT("Path=%s Pixels=%d"),
		*TileRef.RawTilePath,
		OutPixels.Num()));
	return true;
}

void UMinimapGeneratorManager::CleanupLegacyTileTempFiles()
{
	if (LegacyTileTempDirectory.IsEmpty())
	{
		TracePanoramicMemory(TEXT("FULLRES_TEMP_DELETE_SKIP"), TEXT("Temp directory empty"));
		return;
	}

	TracePanoramicMemory(TEXT("FULLRES_TEMP_DELETE_BEGIN"), FString::Printf(TEXT("TempDir=%s"), *LegacyTileTempDirectory));
	IFileManager::Get().DeleteDirectory(*LegacyTileTempDirectory, false, true);
	LegacyTileTempDirectory.Reset();
	TracePanoramicMemory(TEXT("FULLRES_TEMP_DELETE_END"), TEXT("Temp directory reset"));
}

void UMinimapGeneratorManager::StartStitching()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("All tiles captured. Starting stitching process..."));
	TracePanoramicMemory(TEXT("FULLRES_STITCH_BEGIN"), FString::Printf(TEXT("CachedTiles=%d Output=%dx%d"),
		LegacyCapturedTiles.Num(),
		Settings.OutputWidth,
		Settings.OutputHeight));
	UpdateTelemetryPhase(TEXT("Stitching"));
	OnProgress.Broadcast(FText::FromString(TEXT("Stitching tiles...")), 0.9f, 0, 0);

	ReleaseCaptureResources(true);

	if (LegacyCapturedTiles.Num() == 0)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("No tile data was captured. Aborting stitching."));
		OnCaptureComplete.Broadcast(false, TEXT("No tile data was captured."));
		ReleaseWorkingMemory(true);
		return;
	}

	TArray<FColor> FinalImageData;
	FinalImageData.AddUninitialized(Settings.OutputWidth * Settings.OutputHeight);
	TracePanoramicMemory(TEXT("FULLRES_FINAL_BUFFER_ALLOCATED"), FString::Printf(TEXT("Pixels=%d Bytes=%lld"),
		FinalImageData.Num(),
		static_cast<int64>(FinalImageData.Num()) * sizeof(FColor)));
	// Initialize final image with the configured background color for correct blending.
	const FColor BackgroundColor = (Settings.BackgroundMode == EMinimapBackgroundMode::Transparent)
		                               ? FColor::Transparent
		                               : Settings.BackgroundColor.ToFColor(true);
	for (int32 i = 0; i < FinalImageData.Num(); ++i)
	{
		FinalImageData[i] = BackgroundColor;
	}


	const int32 EffectiveTileRes = Settings.TileResolution - Settings.TileOverlap;
	const bool bIsPortrait = Settings.OutputHeight > Settings.OutputWidth;

	// Sort tiles from left-to-right and top-to-bottom for deterministic blending.
	LegacyCapturedTiles.Sort([](const FLegacyCapturedTileRef& A, const FLegacyCapturedTileRef& B)
	{
		if (A.Coord.Y != B.Coord.Y) return A.Coord.Y < B.Coord.Y;
		return A.Coord.X < B.Coord.X;
	});

	for (const FLegacyCapturedTileRef& TileRef : LegacyCapturedTiles)
	{
		TArray<FColor> TilePixels;
		if (!LoadLegacyTileFromTempFile(TileRef, TilePixels))
		{
			UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to load cached tile for stitching: %s"), *TileRef.RawTilePath);
			OnCaptureComplete.Broadcast(false, TEXT("Failed to load cached tile for stitching."));
			ReleaseWorkingMemory(true);
			return;
		}

		const int32 CanvasStartX = TileRef.Coord.X * EffectiveTileRes;
		const int32 CanvasStartY = TileRef.Coord.Y * EffectiveTileRes;

		for (int32 y = 0; y < TileRef.Height; ++y)
		{
			for (int32 x = 0; x < TileRef.Width; ++x)
			{
				const int32 SrcIndex = y * TileRef.Width + x;
				const FColor& SrcPixelColor = TilePixels[SrcIndex];

				int32 DstX_on_Canvas, DstY_on_Canvas;
				if (bIsPortrait)
				{
					DstX_on_Canvas = CanvasStartX + (TileRef.Width - 1 - y);
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

					// Calculate blend factor for X axis overlap.
					if (TileRef.Coord.X > 0 && x < Settings.TileOverlap)
					{
						BlendAlphaX = static_cast<float>(x) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Calculate blend factor for Y axis overlap.
					if (TileRef.Coord.Y > 0 && y < Settings.TileOverlap)
					{
						BlendAlphaY = static_cast<float>(y) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Use the minimum factor to smooth diagonal overlap corners.
					if (const float FinalBlendAlpha = FMath::Min(BlendAlphaX, BlendAlphaY); FinalBlendAlpha < 1.0f)
					{
						// === START OF FIXED BLENDING LOGIC ===
						// 1) Read existing destination color.
						const FColor& DstPixelColor = FinalImageData[DstIndex];

						// 2) Convert both colors to linear space.
						const FLinearColor DstLinear = DstPixelColor; // Automatic conversion
						const FLinearColor SrcLinear = SrcPixelColor; // Automatic conversion 

						// 3) Blend in linear space (HSV interpolation for smoother colors).
						const FLinearColor BlendedLinear = FLinearColor::LerpUsingHSV(
							DstLinear, SrcLinear, FinalBlendAlpha);

						// 4) Convert back to FColor and write to the destination image.
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

		TilePixels.Empty();
		TilePixels.Shrink();
		IFileManager::Get().Delete(*TileRef.RawTilePath, false, true);
		TracePanoramicMemory(TEXT("FULLRES_STITCH_TILE_RELEASED"), FString::Printf(TEXT("Coord=%d,%d Path=%s"),
			TileRef.Coord.X,
			TileRef.Coord.Y,
			*TileRef.RawTilePath));
	}

	LegacyCapturedTiles.Empty();
	LegacyCapturedTiles.Shrink();
	CleanupLegacyTileTempFiles();
	TracePanoramicMemory(TEXT("FULLRES_STITCH_END"), FString::Printf(TEXT("FinalPixels=%d"), FinalImageData.Num()));
	OnProgress.Broadcast(FText::FromString(TEXT("Saving final image...")), 0.95f, 0, 0);
	StartImageSaveTask(MoveTemp(FinalImageData), Settings.OutputWidth, Settings.OutputHeight);
}

void UMinimapGeneratorManager::OnAllTasksCompleted()
{
	UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("[%s::%s] - All tiles captured. Starting stitching process."), *GetName(),
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
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to set raw image data for PNG wrapper."));
		return false;
	}

	const FString FullPath = FPaths::Combine(Settings.OutputPath, Settings.FileName + ".png");
	return FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(), *FullPath);
}
