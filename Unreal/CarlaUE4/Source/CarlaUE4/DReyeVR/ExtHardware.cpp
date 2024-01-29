#include "EgoVehicle.h"
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include "Kismet/GameplayStatics.h"					// GetRealTimeSeconds
#include <string>									// Raw string for ZeroMQ
#include "MsgPack/DcMsgPackReader.h"				// MsgPackReader
#include "Property/DcPropertyDatum.h"				// Datum
#include "Property/DcPropertyWriter.h"				// PropertyWriter
#include "Deserialize/DcDeserializer.h"				// Deserializer
#include "Deserialize/DcDeserializerSetup.h"		// EDcMsgPackDeserializeType

// Eye-tracking data specific implementation
bool AEgoVehicle::IsUserGazingOnHUD() {
	// IMPORTANT: Need to be called very frequently in order to function properly.

	if (bLastOnSurfValue == bLatestOnSurfValue) {
		GazeShiftThreshTimeStamp = UGameplayStatics::GetRealTimeSeconds(GetWorld()); // Set the (latest) time the value was unchanged
	}
	else {
		if (UGameplayStatics::GetRealTimeSeconds(GetWorld()) - GazeShiftThreshTimeStamp >= GeneralParams.Get<float>("EyeTracker", "EyeBlinkThreshold"))
		{
			// NOTE: 150 milliseconds (or 0.15f seconds) is the average blink time of a human
			// Blinking causes the gaze mapper to go crazy, so we set this threshold, to prevent resetting the timer
			// on a blink. Only changes, when the value has been changed for more than the blink time.
			bLastOnSurfValue = bLatestOnSurfValue;
		}
	}

	return bLastOnSurfValue; // Return the current or updated value of OnSurf
}

float AEgoVehicle::GazeOnHUDTime()
{
	// IMPORTANT: Need to be called very frequently in order to function properly.
	if (IsUserGazingOnHUD())
	{
		return UGameplayStatics::GetRealTimeSeconds(GetWorld()) - GazeOnHUDTimestamp;
	}
	GazeOnHUDTimestamp = UGameplayStatics::GetRealTimeSeconds(GetWorld());
	return 0;
}

bool AEgoVehicle::EstablishEyeTrackerConnection() {
	const FString SocketName = TEXT("EyeTrackerData");
	const FString IP = TEXT("127.0.0.1");
	const int32 Port = 5558;

	FIPv4Address Address;
	FIPv4Address::Parse(IP, Address);
	Endpoint = FIPv4Endpoint(Address, Port);

	// Create a UDP Socket
	ListenSocket = FUdpSocketBuilder(*SocketName)
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(2);

	if (!ListenSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("UDP: Failed to create socket!"));
		bUDPEyeConnection = false;
		return false;
	}

	UE_LOG(LogTemp, Error, TEXT("UDP: Successfully created socket!"));
	bUDPEyeConnection = true;
	return true;
}

bool AEgoVehicle::TerminateEyeTrackerConnection() {
	// Check if the connection was even established at the first place
	if (!bUDPEyeConnection) {
		UE_LOG(LogTemp, Warning, TEXT("UDP: Attempting to terminate an eye-tracker connection that was never established."));
		return false;
	}

	if (ListenSocket != nullptr)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
	}

	UE_LOG(LogTemp, Display, TEXT("UDP: Terminated connection to the python hardware stream client"));
	bUDPEyeConnection = false;
	return true;
}

void AEgoVehicle::RetrieveOnSurf()
{
	// Note: using raw C++ types in the following code as it does not interact with UE interface

	// Establish connection if not already
	if (!bUDPEyeConnection && !EstablishEyeTrackerConnection()) {
		UE_LOG(LogTemp, Display, TEXT("UDP: Connection not established!"));
		return;
	}

	uint32 Size;
	if (ListenSocket->HasPendingData(Size))
	{
		TArray<uint8> ReceivedData;
		ReceivedData.SetNumUninitialized(FMath::Min(Size, 65507u));

		int32 BytesRead;
		if (ListenSocket->Recv(ReceivedData.GetData(), ReceivedData.Num(), BytesRead))
		{
			// Record the received boolean value
			bLatestOnSurfValue = ReceivedData[0] != 0;
		}
	} else
	{
		UE_LOG(LogTemp, Display, TEXT("UDP: Unable to retrieve the OnSurf value."));
	}
}