// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBPanoramicMinimapGeneratorModule.h"
#include "OBPanoramicMinimapGeneratorEditorModeCommands.h"

#define LOCTEXT_NAMESPACE "OBPanoramicMinimapGeneratorModule"

void FOBPanoramicMinimapGeneratorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FOBPanoramicMinimapGeneratorEditorModeCommands::Register();
}

void FOBPanoramicMinimapGeneratorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	FOBPanoramicMinimapGeneratorEditorModeCommands::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOBPanoramicMinimapGeneratorModule, OBPanoramicMinimapGeneratorEditorMode)