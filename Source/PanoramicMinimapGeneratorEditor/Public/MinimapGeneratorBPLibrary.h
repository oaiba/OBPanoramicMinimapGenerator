// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MinimapGeneratorManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MinimapGeneratorBPLibrary.generated.h"

/**
 * 
 */
UCLASS()
class PANORAMICMINIMAPGENERATOREDITOR_API UMinimapGeneratorBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Minimap Generator")
	static void StartMinimapCapture(const FMinimapCaptureSettings& Settings);
};
