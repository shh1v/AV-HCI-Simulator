// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic

#define M_PI    3.14159265358979323846

#include "DReyeVRUtils.h"
#include "DataLogging.h"

/* Have to seperately define a method for reaction time as it has to be as precise as possible */

void DataLogging::LogReactionTime(RTTimer TimerStatus)
{
	if (TimerStatus == RTTimer::Start)
	{
		check(ReactionTime == -1.0f);
		ReactionTime = FPlatformTime::Seconds();; // Store the initial timestamp
	}
	else
	{
		check(ReactionTime != -1.0f); // Ensure a valid timestamp was stored
		float TimeDifference = FPlatformTime::Seconds() - ReactionTime;
		check(TimeDifference > 0.0f); // Ensure the time difference is strictly greater than zero
		ReactionTime = TimeDifference; // Update ReactionTime with the time difference
	}
}


void DataLogging::LogLogitechData(const struct DIJOYSTATE2* WheelState)
{
	/// NOTE: obtained these from LogitechWheelInputDevice.cpp:~111
	// -32768 to 32767. -32768 = all the way to the left. 32767 = all the way to the right.
	const float WheelRotation = (FMath::Clamp(float(WheelState->lX), -32767.0f, 32767.0f) / 32767.0f) * 450; // (-450, +450)
	// -32768 to 32767. 32767 = pedal not pressed. -32768 = pedal fully pressed.
	const float AccelerationPedal = fabs(((WheelState->lY - 32767.0f) / (65535.0f))); // (0, 1)
	// -32768 to 32767. Higher value = less pressure on brake pedal
	const float BrakePedal = fabs(((WheelState->lRz - 32767.0f) / (65535.0f))); // (0, 1)

	// Appending the values into the data arrays
	Timestamps.Add(FDateTime::Now());
	SteeringWheelAngles.Add(FString::Printf(TEXT("%f"), WheelRotation));
	AccelerationInputs.Add(FString::Printf(TEXT("%f"), AccelerationPedal));
	BrakingInputs.Add(FString::Printf(TEXT("%f"), BrakePedal));
	if (Timestamps.Num() == 1)
	{
		SteeringWheelVelocities.Add("");
	}
	else {
		float DeltaDegree = FCString::Atof(*SteeringWheelAngles[SteeringWheelAngles.Num() - 1]) - FCString::Atof(*SteeringWheelAngles[SteeringWheelAngles.Num() - 2]);
		float DeltaTime = (Timestamps[Timestamps.Num() - 1] - Timestamps[Timestamps.Num() - 2]).GetTotalSeconds();
		SteeringWheelVelocities.Add(FString::Printf(TEXT("%f"), (DeltaDegree * (M_PI / 180) / DeltaTime)));
	}

	// Check for data frequency synchrony
	check(Timestamps.Num() == SteeringWheelAngles.Num() == SteeringWheelVelocities.Num() == AccelerationInputs.Num() == BrakingInputs.Num());
}

void DataLogging::EraseData()
{
	// Erase all the data (most likely for the next trial)
	ReactionTime = -1.0f;
	SteeringWheelAngles.Empty();
	SteeringWheelVelocities.Empty();
	AccelerationInputs.Empty();
	BrakingInputs.Empty();
}

void DataLogging::WriteData()
{
	// Preparing the Header file for the CSV files
	// [ParticipantID, BlockNumber, TrialNumber, TaskType, TaskSetting, TrafficComplexity, Timestamp, DataPoint]
	// Adding general params first so that they can be used later
	TArray<FString> HeaderRow = {
		ExperimentParams.Get<FString>("General", "ParticipantID"),
		ExperimentParams.Get<FString>("General", "CurrentBlock")
	};
	check(HeaderRow.Num() == 2);

	// Adding trial specific data
	HeaderRow.Add(TEXT("1")); // Replace this with the actual trial number
	HeaderRow.Add(ExperimentParams.Get<FString>(HeaderRow[1], "TaskType"));
	HeaderRow.Add(ExperimentParams.Get<FString>(HeaderRow[1], "TaskSetting"));
	HeaderRow.Add(ExperimentParams.Get<FString>(HeaderRow[1], "TrafficComplexity"));

	// Logging the data into the CSV files
	AppendArrayToCSV(ReturnHeaderRow("ReactionTime", false), HeaderRow, { FString::Printf(TEXT("%f"), ReactionTime) }, false);
	AppendArrayToCSV(ReturnHeaderRow("SteeringWheelAngles", true), HeaderRow, SteeringWheelAngles, true);
	AppendArrayToCSV(ReturnHeaderRow("SteeringWheelVelocities", true), HeaderRow, SteeringWheelVelocities, true);
	AppendArrayToCSV(ReturnHeaderRow("AccelerationInputs", true), HeaderRow, AccelerationInputs, true);
	AppendArrayToCSV(ReturnHeaderRow("BrakingInputs", true), HeaderRow, BrakingInputs, true);
}


/*
* Note the header row does not have the timestamp element. So, it will have to be manually added in the function
*/
void DataLogging::AppendArrayToCSV(const TArray<FString>& HeaderNames, const TArray<FString>& HeaderData, const TArray<FString>& LoggedData, bool WithTimeStamp)
{
	FString CSVFilePath = FPaths::Combine(CarlaUE4Path, FString::Printf(TEXT("LoggedData/%s"), *HeaderNames[HeaderNames.Num() - 1]));
	// Check if the file exists
	bool bFileExists = FPaths::FileExists(*CSVFilePath);

	if (!bFileExists)
	{
		// Create the CSV file and append the header row
		FString HeaderRow = FString::Join(HeaderNames, TEXT(","));
		HeaderRow.Append(TEXT("\n"));
		bool bSuccess = FFileHelper::SaveStringToFile(HeaderRow, *CSVFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		if (!bSuccess) {
			UE_LOG(LogTemp, Error, TEXT("Failed to append to the CSV file: %s"), *CSVFilePath);
		}
	}

	// Open the CSV file in append mode
	// Create a comma-separated string from the array elements
	FString	HeaderDataString = FString::Join(HeaderData, TEXT(","));
	if (WithTimeStamp) {
		for (int32 i = 0; i < LoggedData.Num(); i++)
		{
			bool bSuccess = FFileHelper::SaveStringToFile(HeaderDataString + TEXT(",") + Timestamps[i].ToString() + TEXT(",") + LoggedData[i] + TEXT("\n"), *CSVFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
			if (!bSuccess) {
				UE_LOG(LogTemp, Error, TEXT("Failed to append to the CSV file: %s"), *CSVFilePath);
			}
		}
	}
	else
	{
		FString LoggedDataString = FString::Join(LoggedData, TEXT(","));
		LoggedDataString.Append(TEXT("\n"));
		bool bSuccess = FFileHelper::SaveStringToFile(HeaderDataString + TEXT(",") + LoggedDataString, *CSVFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		if (!bSuccess) {
			UE_LOG(LogTemp, Error, TEXT("Failed to append to the CSV file: %s"), *CSVFilePath);
		}
	}
}

TArray<FString> DataLogging::ReturnHeaderRow(const FString& DataPoint, bool IncludeTimestamp) {
	// Make a copy of FixedHeaderRow
	TArray<FString> TempArray = FixedHeaderRow;
	if (!IncludeTimestamp) {
		TempArray.Remove(TEXT("Timestamp"));
	}
	// Append the name to the copied array
	TempArray.Add(DataPoint);
	// Return the modified copy
	return TempArray;
}