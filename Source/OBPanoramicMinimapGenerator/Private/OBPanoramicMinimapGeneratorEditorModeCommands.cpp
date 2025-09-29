// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBPanoramicMinimapGeneratorEditorModeCommands.h"
#include "OBPanoramicMinimapGeneratorEditorMode.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "OBPanoramicMinimapGeneratorEditorModeCommands"

FOBPanoramicMinimapGeneratorEditorModeCommands::FOBPanoramicMinimapGeneratorEditorModeCommands()
	: TCommands<FOBPanoramicMinimapGeneratorEditorModeCommands>("OBPanoramicMinimapGeneratorEditorMode",
		NSLOCTEXT("OBPanoramicMinimapGeneratorEditorMode", "OBPanoramicMinimapGeneratorEditorModeCommands", "OBPanoramicMinimapGenerator Editor Mode"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FOBPanoramicMinimapGeneratorEditorModeCommands::RegisterCommands()
{
	TArray <TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);

	UI_COMMAND(SimpleTool, "Show Actor Info", "Opens message box with info about a clicked actor", EUserInterfaceActionType::Button, FInputChord());
	ToolCommands.Add(SimpleTool);

	UI_COMMAND(InteractiveTool, "Measure Distance", "Measures distance between 2 points (click to set origin, shift-click to set end point)", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(InteractiveTool);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FOBPanoramicMinimapGeneratorEditorModeCommands::GetCommands()
{
	return FOBPanoramicMinimapGeneratorEditorModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
