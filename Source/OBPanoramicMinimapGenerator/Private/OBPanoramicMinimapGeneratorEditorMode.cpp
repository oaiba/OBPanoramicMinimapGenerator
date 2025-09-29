// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBPanoramicMinimapGeneratorEditorMode.h"
#include "OBPanoramicMinimapGeneratorEditorModeToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "OBPanoramicMinimapGeneratorEditorModeCommands.h"


//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
// AddYourTool Step 1 - include the header file for your Tools here
//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
#include "Tools/OBPanoramicMinimapGeneratorSimpleTool.h"
#include "Tools/OBPanoramicMinimapGeneratorInteractiveTool.h"

// step 2: register a ToolBuilder in FOBPanoramicMinimapGeneratorEditorMode::Enter() below


#define LOCTEXT_NAMESPACE "OBPanoramicMinimapGeneratorEditorMode"

const FEditorModeID UOBPanoramicMinimapGeneratorEditorMode::EM_OBPanoramicMinimapGeneratorEditorModeId = TEXT("EM_OBPanoramicMinimapGeneratorEditorMode");

FString UOBPanoramicMinimapGeneratorEditorMode::SimpleToolName = TEXT("OBPanoramicMinimapGenerator_ActorInfoTool");
FString UOBPanoramicMinimapGeneratorEditorMode::InteractiveToolName = TEXT("OBPanoramicMinimapGenerator_MeasureDistanceTool");


UOBPanoramicMinimapGeneratorEditorMode::UOBPanoramicMinimapGeneratorEditorMode()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	// appearance and icon in the editing mode ribbon can be customized here
	Info = FEditorModeInfo(UOBPanoramicMinimapGeneratorEditorMode::EM_OBPanoramicMinimapGeneratorEditorModeId,
		LOCTEXT("ModeName", "OBPanoramicMinimapGenerator"),
		FSlateIcon(),
		true);
}


UOBPanoramicMinimapGeneratorEditorMode::~UOBPanoramicMinimapGeneratorEditorMode()
{
}


void UOBPanoramicMinimapGeneratorEditorMode::ActorSelectionChangeNotify()
{
}

void UOBPanoramicMinimapGeneratorEditorMode::Enter()
{
	UEdMode::Enter();

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// AddYourTool Step 2 - register the ToolBuilders for your Tools here.
	// The string name you pass to the ToolManager is used to select/activate your ToolBuilder later.
	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 
	const FOBPanoramicMinimapGeneratorEditorModeCommands& SampleToolCommands = FOBPanoramicMinimapGeneratorEditorModeCommands::Get();

	RegisterTool(SampleToolCommands.SimpleTool, SimpleToolName, NewObject<UOBPanoramicMinimapGeneratorSimpleToolBuilder>(this));
	RegisterTool(SampleToolCommands.InteractiveTool, InteractiveToolName, NewObject<UOBPanoramicMinimapGeneratorInteractiveToolBuilder>(this));

	// active tool type is not relevant here, we just set to default
	GetToolManager()->SelectActiveToolType(EToolSide::Left, SimpleToolName);
}

void UOBPanoramicMinimapGeneratorEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FOBPanoramicMinimapGeneratorEditorModeToolkit);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UOBPanoramicMinimapGeneratorEditorMode::GetModeCommands() const
{
	return FOBPanoramicMinimapGeneratorEditorModeCommands::Get().GetCommands();
}

#undef LOCTEXT_NAMESPACE
