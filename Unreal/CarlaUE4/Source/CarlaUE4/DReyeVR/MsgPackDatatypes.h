#pragma once
#include "CoreMinimal.h"
#include "FDictionaryProperty.generated.h"

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
		float Confidence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		bool OnSurf;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FString> BaseData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float Timestamp;
};

USTRUCT(BlueprintType)
struct FFixationsOnSurface
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString Topic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<float> NormPos;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float Confidence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		bool OnSurf;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FString> BaseData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float Timestamp;
};

// FGazeOnSurfaceArray and FFixationsOnSurfaceArray are USTRUCTs that contain arrays of FGazeOnSurface 
// and FFixationsOnSurface, respectively. These are used to represent the gaze_on_surfaces and 
// fixations_on_surfaces arrays in the original data structure.
USTRUCT(BlueprintType)
struct FGazeOnSurfaceArray
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	TArray<FGazeOnSurface> set;
};

USTRUCT(BlueprintType)
struct FFixationsOnSurfaceArray
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	TArray<FFixationsOnSurface> set;
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
		TArray<FGazeOnSurfaceArray> gaze_on_surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		TArray<FFixationsOnSurfaceArray> fixations_on_surfaces;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
		float timestamp;
};