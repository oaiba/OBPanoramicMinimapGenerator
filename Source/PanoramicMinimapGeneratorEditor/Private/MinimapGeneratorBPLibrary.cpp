// Fill out your copyright notice in the Description page of Project Settings.


#include "MinimapGeneratorBPLibrary.h"

void UMinimapGeneratorBPLibrary::StartMinimapCapture(const FMinimapCaptureSettings& Settings)
{
#if WITH_EDITOR
	// This is an editor-only process.
	UMinimapGeneratorManager* Manager = NewObject<UMinimapGeneratorManager>();
	// Keep the manager alive during the process
	Manager->AddToRoot();
	Manager->StartCaptureProcess(Settings);
	// You would need a callback system here to know when it's done to RemoveFromRoot()
#endif
}
