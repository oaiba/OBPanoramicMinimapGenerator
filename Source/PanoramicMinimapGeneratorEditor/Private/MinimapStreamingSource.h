// MinimapStreamingSource.h

#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

/**
 * A custom, temporary streaming source provider used by the minimap generator.
 * This allows us to force World Partition to load cells around a specific location.
 */
class FMinimapStreamingSourceProvider : public IWorldPartitionStreamingSourceProvider
{
public:
    virtual ~FMinimapStreamingSourceProvider() = default;

    FMinimapStreamingSourceProvider()
    {
        StreamingSource.Name = FName("MinimapCapture");
        StreamingSource.bBlockOnSlowLoading = true;
    }

    //~ Begin IWorldPartitionStreamingSourceProvider interface
    virtual bool GetStreamingSources(TArray<FWorldPartitionStreamingSource>& OutStreamingSources) const override
    {
        OutStreamingSources.Add(StreamingSource);
        return true;
    }
    //~ End IWorldPartitionStreamingSourceProvider interface

    /** Updates the position that this provider will report to the World Partition subsystem */
    void SetSourceLocation(const FVector& InLocation, const float InRadius)
    {
        StreamingSource.Location = InLocation;
        StreamingSource.Shapes.Empty();

        FStreamingSourceShape SphereShape;
        SphereShape.Location = InLocation;
        SphereShape.Radius = InRadius;
        SphereShape.bUseGridLoadingRange = false;
        SphereShape.bIsSector = false;

        StreamingSource.Shapes.Add(SphereShape);
    }
    
    FVector GetSourceLocation() const { return StreamingSource.Location; }

private:
    FWorldPartitionStreamingSource StreamingSource;
};