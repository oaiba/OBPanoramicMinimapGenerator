#if WITH_DEV_AUTOMATION_TESTS

#include "MinimapGeneratorManager.h"

#include "Misc/AutomationTest.h"

namespace
{
bool TestIntPointEqual(FAutomationTestBase& Test, const TCHAR* Label, const FIntPoint& Actual,
                       const FIntPoint& Expected)
{
	const bool bMatches = Actual == Expected;
	if (!bMatches)
	{
		Test.AddError(FString::Printf(TEXT("%s expected %s, got %s."), Label, *Expected.ToString(),
		                              *Actual.ToString()));
	}
	return bMatches;
}

bool TestVector2DNearlyEqual(FAutomationTestBase& Test, const TCHAR* Label, const FVector2D& Actual,
                             const FVector2D& Expected)
{
	const bool bMatches = Actual.Equals(Expected, KINDA_SMALL_NUMBER);
	if (!bMatches)
	{
		Test.AddError(FString::Printf(TEXT("%s expected %s, got %s."), Label, *Expected.ToString(),
		                              *Actual.ToString()));
	}
	return bMatches;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapAspectMatchedLongEdgeTest,
                                 "OBPanoramic.OutputAspect.RectangularBoundsDeriveLongEdgeOutput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapAspectMatchedLongEdgeTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-5000.0f, -12500.0f, -10.0f), FVector(5000.0f, 12500.0f, -10.0f));
	const FRotator CameraRotation(-90.0f, 90.0f, -180.0f);

	TestVector2DNearlyEqual(*this, TEXT("Capture-local world size"),
	                        UMinimapGeneratorManager::CalculateCaptureRegionWorldSize(Bounds, CameraRotation),
	                        FVector2D(10000.0f, 25000.0f));
	TestIntPointEqual(*this, TEXT("Long-edge output size"),
	                  UMinimapGeneratorManager::CalculateAspectMatchedOutputSize(Bounds, CameraRotation, 8192),
	                  FIntPoint(3277, 8192));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapAspectMatchedYawAxisTest,
                                 "OBPanoramic.OutputAspect.CameraYawUsesCaptureLocalAxes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapAspectMatchedYawAxisTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-5000.0f, -12500.0f, -10.0f), FVector(5000.0f, 12500.0f, -10.0f));

	TestIntPointEqual(*this, TEXT("Yaw 0 maps long world Y axis to output width"),
	                  UMinimapGeneratorManager::CalculateAspectMatchedOutputSize(
		                  Bounds,
		                  FRotator(-90.0f, 0.0f, -180.0f),
		                  8192),
	                  FIntPoint(8192, 3277));
	TestIntPointEqual(*this, TEXT("Yaw 90 maps long world Y axis to output height"),
	                  UMinimapGeneratorManager::CalculateAspectMatchedOutputSize(
		                  Bounds,
		                  FRotator(-90.0f, 90.0f, -180.0f),
		                  8192),
	                  FIntPoint(3277, 8192));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapAspectMatchedSquareBoundsTest,
                                 "OBPanoramic.OutputAspect.SquareBoundsStaySquare",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapAspectMatchedSquareBoundsTest::RunTest(const FString& Parameters)
{
	const FBox Bounds(FVector(-5000.0f, -5000.0f, -10.0f), FVector(5000.0f, 5000.0f, -10.0f));

	TestIntPointEqual(*this, TEXT("Square output size"),
	                  UMinimapGeneratorManager::CalculateAspectMatchedOutputSize(
		                  Bounds,
		                  FRotator(-90.0f, 90.0f, -180.0f),
		                  4096),
	                  FIntPoint(4096, 4096));

	return true;
}

#endif
