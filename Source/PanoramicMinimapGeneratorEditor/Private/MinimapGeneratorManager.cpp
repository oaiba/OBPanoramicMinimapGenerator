// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorManager.h"
#include "PanoramicMinimapGeneratorEditor.h"

#include "Editor.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "HAL/FileManager.h"
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
constexpr int32 TileSetGarbageCollectBatchSize = 12;

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

	CleanupCaptureResources();
	CapturedTileData.Empty();
	TileSetExportLevels.Empty();
	TileSetTilesSinceGarbageCollect = 0;
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
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Async save task failed."));
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

	SaveAssetPackage(Package, NewTexture);

	const FSoftObjectPath TexturePath(FString::Printf(TEXT("%s.%s"), *FullAssetPath, *SafeAssetName));
	FText UnloadErrorMessage;
	UPackageTools::UnloadPackages({Package}, UnloadErrorMessage, true);

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

void UMinimapGeneratorManager::CleanupCaptureResources()
{
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
		ActiveCaptureActor->Destroy();
	}
	ActiveCaptureActor.Reset();

	if (ActiveRenderTarget.IsValid())
	{
		ActiveRenderTarget->ConditionalBeginDestroy();
	}
	ActiveRenderTarget.Reset();
	StagingPixelBuffer.Empty();
}

UTextureRenderTarget2D* UMinimapGeneratorManager::CreateRenderTarget()
{
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
	return RenderTarget;
}

ASceneCapture2D* UMinimapGeneratorManager::SpawnAndConfigureCaptureActor(UTextureRenderTarget2D* RenderTarget)
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

	CurrentTileIndex = 0;
	CurrentTileLOD = 0;
	CapturedTileData.Empty();
	TileSetExportLevels.Empty();
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();
	TileSetTilesSinceGarbageCollect = 0;

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
}

bool UMinimapGeneratorManager::AdvanceToNextTileSetLevel()
{
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

	CurrentTileLOD++;
	CurrentTileIndex = 0;
	if (!TileSetExportLevels.IsValidIndex(CurrentTileLOD))
	{
		return false;
	}

	CurrentTileSetGrid = TileSetExportLevels[CurrentTileLOD].GridDimensions;
	ActiveRenderTarget = CreateRenderTarget();
	ActiveCaptureActor = SpawnAndConfigureCaptureActor(ActiveRenderTarget.Get());

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
		const TSoftObjectPtr<UTexture2D> TileTexture = ImportTileTextureAssetFromPixels(
			TilePixels, TileWidth, TileHeight, TexturePackagePath, TextureAssetName);
		Telemetry.LastImportSeconds = FPlatformTime::Seconds() - ImportStartTime;
		BroadcastTelemetry();

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
		TileSetTilesSinceGarbageCollect++;
		if (TileSetTilesSinceGarbageCollect >= TileSetGarbageCollectBatchSize)
		{
			TileSetTilesSinceGarbageCollect = 0;
			UpdateTelemetryPhase(TEXT("Garbage Collection"));
			const double GCStartTime = FPlatformTime::Seconds();
			CollectGarbage(RF_NoFlags);
			Telemetry.LastGCSeconds = FPlatformTime::Seconds() - GCStartTime;
			BroadcastTelemetry();
		}
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Tile-set LOD %d tile %d rendered with empty pixel data."),
			CurrentTileLOD, CurrentTileIndex);
	}

	CompleteTelemetryTile();
	CurrentTileIndex++;
	CaptureNextTileSetTile();
}

void UMinimapGeneratorManager::FinalizeTileSetExport()
{
	UpdateTelemetryPhase(TEXT("Saving TileSet Metadata"));
	CleanupCaptureResources();
	OnProgress.Broadcast(FText::FromString(TEXT("Saving tile-set metadata...")), 0.98f, 0, 0);

	UMinimapTileSetDataAsset* TileSetAsset = CreateOrUpdateTileSetAsset();
	if (!TileSetAsset)
	{
		OnCaptureComplete.Broadcast(false, TEXT("Failed to create tile-set asset."));
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

	OnProgress.Broadcast(FText::FromString(TEXT("Done!")), 1.0f, 0, 0);
	Telemetry.CurrentPhase = TEXT("Done");
	Telemetry.bIsCapturing = false;
	BroadcastTelemetry();
	OnCaptureComplete.Broadcast(true, TileSetAsset->GetPathName());
	TileSetExportLevels.Empty();
	const double GCStartTime = FPlatformTime::Seconds();
	CollectGarbage(RF_NoFlags);
	Telemetry.LastGCSeconds = FPlatformTime::Seconds() - GCStartTime;
	BroadcastTelemetry();
}

void UMinimapGeneratorManager::StartTiledCaptureProcess()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Starting tiled capture process."));
	CurrentTileIndex = 0;
	CapturedTileData.Empty();
	ActiveCaptureActor.Reset();
	ActiveRenderTarget.Reset();

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

		CapturedTileData.Add(FIntPoint(TileX, TileY), MoveTemp(TilePixels));
		UE_LOG(OBPanoramicMinimapGenerator, Verbose, TEXT("Tile (%d, %d) captured and stored."), TileX, TileY);
	}
	else
	{
		UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("Tile %d rendered with empty pixel data."), CurrentTileIndex);
	}

	CompleteTelemetryTile();
	CurrentTileIndex++;
	CaptureNextTile();
}

void UMinimapGeneratorManager::StartStitching()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("All tiles captured. Starting stitching process..."));
	UpdateTelemetryPhase(TEXT("Stitching"));
	OnProgress.Broadcast(FText::FromString(TEXT("Stitching tiles...")), 0.9f, 0, 0);

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

	if (CapturedTileData.Num() == 0)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("No tile data was captured. Aborting stitching."));
		OnCaptureComplete.Broadcast(false, TEXT("No tile data was captured."));
		return;
	}

	TArray<FColor> FinalImageData;
	FinalImageData.AddUninitialized(Settings.OutputWidth * Settings.OutputHeight);
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

					// Calculate blend factor for X axis overlap.
					if (TileCoord.X > 0 && x < Settings.TileOverlap)
					{
						BlendAlphaX = static_cast<float>(x) / FMath::Max(1, Settings.TileOverlap - 1);
					}

					// Calculate blend factor for Y axis overlap.
					if (TileCoord.Y > 0 && y < Settings.TileOverlap)
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
	}

	CapturedTileData.Empty();
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
