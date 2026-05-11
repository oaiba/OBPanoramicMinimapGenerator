// Fill out your copyright notice in the Description page of Project Settings.


#include "PanoramicMinimapGeneratorCommands.h"
#include "PanoramicMinimapGeneratorEditor.h"

#define LOCTEXT_NAMESPACE "FPanoramicMinimapGeneratorModule"

void FPanoramicMinimapGeneratorCommands::RegisterCommands()
{
	UE_LOG(OBPanoramicMinimapGenerator, Log, TEXT("Registering Panoramic Minimap Generator editor commands."));
	UI_COMMAND(OpenPluginWindow, "Panoramic Minimap Tool", "Brings up the Panoramic Minimap Generator window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE