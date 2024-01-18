#pragma once
#include "CoreMinimal.h"
#include "DataConfigDatatypes.generated.h"

// NOTE: The following document has inconsistent naming conventions compared to the recommended conventions of Unreal Engine.
// This is because the names, or keys, must match the key names in the retrieved data.

USTRUCT()
struct FSurfaceData
{
	GENERATED_BODY()

	UPROPERTY() FString topic;

	UPROPERTY() FString name;

	UPROPERTY() FString surf_to_img_trans;

	UPROPERTY() FString img_to_surf_trans;

	UPROPERTY() FString surf_to_dist_img_trans;

	UPROPERTY() FString dist_img_to_surf_trans;

	UPROPERTY() FString gaze_on_surfaces;

	UPROPERTY() FString fixations_on_surfaces;

	UPROPERTY() FString timestamp;
};

USTRUCT()
struct FVehicleStatusData
{
	GENERATED_BODY()

	UPROPERTY() FString from;

	UPROPERTY() FString timestamp;

	UPROPERTY() FString vehicle_status;
};

USTRUCT()
struct FHardwareData
{
	GENERATED_BODY()

	UPROPERTY() FString from;

	UPROPERTY() FString timestamp;

	UPROPERTY() FString HUD_OnSurf;
};