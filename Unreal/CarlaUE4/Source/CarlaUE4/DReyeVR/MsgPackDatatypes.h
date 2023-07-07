#pragma once
#include "CoreMinimal.h"
#include "MsgPackDatatypes.generated.h"

// NOTE: The following document has inconsistent naming conventions compared to the recommended conventions of Unreal Engine.
// This is because the names, or keys, must match the key names in the retrieved data.

// FFloatArray is a USTRUCT that contains an array of floats. 
// This is used to represent the arrays of floats in the original data structure.
USTRUCT()
struct FFloatArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<float> data;
};

// FGazeOnSurface and FFixationsOnSurface are USTRUCTs that represent the individual elements 
// in the gaze_on_surfaces and fixations_on_surfaces arrays, respectively.
USTRUCT()
struct FGazeOnSurface
{
	GENERATED_BODY()

	UPROPERTY()
	FString topic;

	UPROPERTY()
		TArray<float> norm_pos;

	UPROPERTY()
		float confidence;

	UPROPERTY()
		bool on_surf;

	UPROPERTY()
		TArray<FString> base_data;

	UPROPERTY()
		float timestamp;
};

USTRUCT()
struct FFixationsOnSurface
{
	GENERATED_BODY()

	UPROPERTY()
	FString topic;

	UPROPERTY()
		TArray<float> norm_pos;

	UPROPERTY()
		float confidence;

	UPROPERTY()
		bool on_surf;

	UPROPERTY()
		TArray<FString> base_data;

	UPROPERTY()
		float timestamp;

	UPROPERTY()
		float duration;

	UPROPERTY()
		float dispersion;
};


// FMonitorData is a USTRUCT that contains all the data from the original data structure. 
// It uses the other USTRUCTs to represent the nested arrays.
USTRUCT()
struct FSurfaceData
{
	GENERATED_BODY()

	UPROPERTY()
	FString topic;

	UPROPERTY()
		FString name;

	UPROPERTY()
		TArray<FFloatArray> surf_to_img_trans;

	UPROPERTY()
		TArray<FFloatArray> img_to_surf_trans;

	UPROPERTY()
		TArray<FFloatArray> surf_to_dist_img_trans;

	UPROPERTY()
		TArray<FFloatArray> dist_img_to_surf_trans;

	UPROPERTY()
		TArray<FGazeOnSurface> gaze_on_surfaces;

	UPROPERTY()
		TArray<FFixationsOnSurface> fixations_on_surfaces;

	UPROPERTY()
		float timestamp;
};