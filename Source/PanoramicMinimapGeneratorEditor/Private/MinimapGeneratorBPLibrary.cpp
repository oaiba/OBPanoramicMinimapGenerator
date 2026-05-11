// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorBPLibrary.h"
#include "PanoramicMinimapGeneratorEditor.h"

void UMinimapGeneratorBPLibrary::StartMinimapCapture(const FMinimapCaptureSettings& Settings)
{
#if WITH_EDITOR
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("StartMinimapCapture called from Blueprint library."));
	// This is an editor-only process.
	UMinimapGeneratorManager* Manager = NewObject<UMinimapGeneratorManager>();
	if (!Manager)
	{
		UE_LOG(OBPanoramicMinimapGenerator, Error, TEXT("Failed to create UMinimapGeneratorManager."));
		return;
	}

	// Keep the manager alive during the process
	Manager->AddToRoot();
	Manager->StartCaptureProcess(Settings);
	// You would need a callback system here to know when it's done to RemoveFromRoot()
#else
	UE_LOG(OBPanoramicMinimapGenerator, Warning, TEXT("StartMinimapCapture called outside editor; ignored."));
#endif
}
