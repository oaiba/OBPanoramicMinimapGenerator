// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBPanoramicMinimapGeneratorEditorModeToolkit.h"
#include "OBPanoramicMinimapGeneratorEditorMode.h"
#include "Engine/Selection.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "OBPanoramicMinimapGeneratorEditorModeToolkit"

FOBPanoramicMinimapGeneratorEditorModeToolkit::FOBPanoramicMinimapGeneratorEditorModeToolkit()
{
}

void FOBPanoramicMinimapGeneratorEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
}

void FOBPanoramicMinimapGeneratorEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FOBPanoramicMinimapGeneratorEditorModeToolkit::GetToolkitFName() const
{
	return FName("OBPanoramicMinimapGeneratorEditorMode");
}

FText FOBPanoramicMinimapGeneratorEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "OBPanoramicMinimapGeneratorEditorMode Toolkit");
}

#undef LOCTEXT_NAMESPACE
