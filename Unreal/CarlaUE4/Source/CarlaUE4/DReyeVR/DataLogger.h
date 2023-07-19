// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic

#pragma once

#include "CoreMinimal.h"

#ifndef _WIN32
// can only use LogitechWheel plugin on Windows! :(
#undef USE_LOGITECH_PLUGIN
#define USE_LOGITECH_PLUGIN false
#endif

#if USE_LOGITECH_PLUGIN
#include "LogitechSteeringWheelLib.h" // LogitechWheel plugin for hardware integration & force feedback
#endif

/**
 * This code works in company with DReyeVRPawn
 */

class CARLAUE4_API DataLogger
{
private:
	float ReactionTime = -1.0f;
	TArray<FDateTime> Timestamps;
	TArray<FString> SteeringWheelAngles;
	TArray<FString> SteeringWheelVelocities;
	TArray<FString> AccelerationInputs;
	TArray<FString> BrakingInputs;
	const TArray<FString> FixedHeaderRow = { "ParticipantID", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp"};

public:
	enum class RTTimer{ Start, Stop };
	TArray<FString> ReturnHeaderRow(const FString& DataPoint, bool IncludeTimestamp);
	void LogReactionTime(const FDateTime& TORIssuanceTime);
	void LogLogitechData(const struct DIJOYSTATE2* WheelState);	//	Will be called in every tick to append new data retrived from the logitech steering wheels
	void AppendArrayToCSV(const TArray<FString>& HeaderNames, const TArray<FString>& HeaderData, const TArray<FString>& LoggedData, bool WithTimeStamp);
	void WriteData();		// Finally write data once a TOR is finished
	void EraseData();	// Reset the raw data arrays, preparing for recording the next TOR performance
};
