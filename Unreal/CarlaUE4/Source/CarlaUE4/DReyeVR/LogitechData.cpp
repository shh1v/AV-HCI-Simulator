// Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab at the University of British Columbia. This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/lic

#include "ConfigFile.h"
#include "Misc/App.h"
#include "LogitechData.h"

/* Have to seperately define a method for reaction time as it has to be as precise as possible */

void LogitechData::LogReactionTime(RTTimer TimerStatus)
{
	if (TimerStatus == RTTimer::Start)
	{
		check(ReactionTime == -1.0f);
		ReactionTime = FApp::GetCurrentTime(); // Store the initial timestamp
	}
	else
	{
		check(ReactionTime != -1.0f); // Ensure a valid timestamp was stored
		float TimeDifference = FApp::GetCurrentTime() - ReactionTime;
		check(TimeDifference > 0.0f); // Ensure the time difference is strictly greater than zero
		ReactionTime = TimeDifference; // Update ReactionTime with the time difference
	}
}


void LogitechData::LogLogitechData(const struct DIJOYSTATE2* WheelState)
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
	SteeringWheelAngles.Add(WheelRotation);
	AccelerationInputs.Add(AccelerationPedal);
	BrakingInputs.Add(BrakePedal);
	if (Timestamps.Num() == 1)
	{
		SteeringWheelVelocities.Add(NAN);
	}
	else {
		float DeltaDegree = SteeringWheelAngles[SteeringWheelAngles.Num() - 1] - SteeringWheelAngles[SteeringWheelAngles.Num() - 2];
		float DeltaTime = (Timestamps[Timestamps.Num() - 1] - Timestamps[Timestamps.Num() - 2]).GetTotalSeconds();
		SteeringWheelVelocities.Add(DeltaDegree * (PI / 180) / DeltaTime);
	}

	// Check for data frequency synchrony
	check(Timestamps.Num() == SteeringWheelAngles.Num() == SteeringWheelVelocities.Num() == AccelerationInputs.Num() == BrakingInputs.Num());
}

void LogitechData::WriteData()
{
	// Writing individual data arrays as files
}

void LogitechData::ResetDataArrays()
{
}