// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FPanoramicMinimapGeneratorCommands : public TCommands<FPanoramicMinimapGeneratorCommands>
{
public:
	FPanoramicMinimapGeneratorCommands()
		: TCommands<FPanoramicMinimapGeneratorCommands>(
			TEXT("PanoramicMinimapGenerator"), // Context name
			NSLOCTEXT("Contexts", "PanoramicMinimapGenerator", "Panoramic Minimap Generator"), // Context title
			NAME_None, // Parent context
			FAppStyle::GetAppStyleSetName() // Icon style set
		)
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};