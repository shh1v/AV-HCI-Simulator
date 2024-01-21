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
	if (bLastOnSurfValue == bLatestOnSurfValue) {
		GazeShiftCounter = 0; // Reset the counter when the value is consistent
	}
	else {
		// Increment the gaze shift counter and check if it exceeds the threshold
		if (++GazeShiftCounter >= 10) {
			bLastOnSurfValue = bLatestOnSurfValue;
			GazeShiftCounter = 0;
		}
	}

	return bLastOnSurfValue; // Return the current or updated value of OnSurf
}

float AEgoVehicle::GazeOnHUDTime()
{
	if (IsUserGazingOnHUD())
	{
		return UGameplayStatics::GetRealTimeSeconds(GetWorld()) - GazeOnHUDTimestamp;
	}
	GazeOnHUDTimestamp = UGameplayStatics::GetRealTimeSeconds(GetWorld());
	return 0;
}

bool AEgoVehicle::EstablishEyeTrackerConnection() {
	const FString YourChosenSocketName = TEXT("EyeTrackerData");
	const FString IP = TEXT("127.0.0.1");
	const int32 Port = 12345;

	FIPv4Address Address;
	FIPv4Address::Parse(IP, Address);
	Endpoint = FIPv4Endpoint(Address, Port);

	ListenSocket = FTcpSocketBuilder(*YourChosenSocketName)
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(2);

	if (!ListenSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("UDP: Failed to create socket!"));
		bUDPEyeConnection = false;
	}

	bUDPEyeConnection = true;
	return ListenSocket != nullptr;
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
	}
}