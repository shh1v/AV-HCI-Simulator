#pragma once
#include "CoreMinimal.h"
#include "MsgPackDatatypes.generated.h"

// NOTE: The following document has inconsistent naming conventions compared to the recommended conventions of Unreal Engine.
// This is because the names, or keys, must match the key names in the retrieved data.

// FFloatArray is a USTRUCT that contains an array of floats. 
// This is used to represent the arrays of floats in the original data structure.
USTRUCT(BlueprintType)
struct FFloatArray
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	TArray<float> data;
};

// FGazeOnSurface and FFixationsOnSurface are USTRUCTs that represent the individual elements 
// in the gaze_on_surfaces and fixations_on_surfaces arrays, respectively.
USTRUCT(BlueprintType)
struct FGazeOnSurface
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString topic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<float> norm_pos;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float confidence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		bool on_surf;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FString> base_data;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float timestamp;
};

USTRUCT(BlueprintType)
struct FFixationsOnSurface
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString topic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<float> norm_pos;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float confidence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		bool on_surf;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FString> base_data;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float timestamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float duration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float dispersion;
};


// FMonitorData is a USTRUCT that contains all the data from the original data structure. 
// It uses the other USTRUCTs to represent the nested arrays.
USTRUCT(BlueprintType)
struct FSurfaceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString topic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		FString name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FFloatArray> surf_to_img_trans;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FFloatArray> img_to_surf_trans;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FFloatArray> surf_to_dist_img_trans;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FFloatArray> dist_img_to_surf_trans;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FGazeOnSurface> gaze_on_surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FFixationsOnSurface> fixations_on_surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float timestamp;
};