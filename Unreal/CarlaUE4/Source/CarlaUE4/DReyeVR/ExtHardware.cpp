#include "EgoVehicle.h"
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include "Kismet/GameplayStatics.h"					// GetRealTimeSeconds
#include <zmq.hpp>									// ZeroMQ plugin
#include <string>									// Raw string for ZeroMQ
#include "MsgPack/DcMsgPackReader.h"				// MsgPackReader
#include "Property/DcPropertyDatum.h"				// Datum
#include "Property/DcPropertyWriter.h"				// PropertyWriter
#include "Deserialize/DcDeserializer.h"				// Deserializer
#include "Deserialize/DcDeserializerSetup.h"		// EDcMsgPackDeserializeType

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
	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to establish python hardware stream"));
		//  Prepare our context
		EyeContext = new zmq::context_t(1);

		// Preparing addresses
		const std::string ReceiveAddress = "tcp://localhost";
		const std::string ReceiveAddressPort = "5558";

		// Setup the Subscriber socket
		EyeSubscriber = new zmq::socket_t(*EyeContext, ZMQ_SUB);

		// Setting 10 ms recv timeout to have non-blocking behaviour
		const int timeout = 10;  // 100 ms
		EyeSubscriber->setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
		int conflate = 1;
		EyeSubscriber->setsockopt(ZMQ_CONFLATE, &conflate, sizeof(conflate));

		// Setup default topic
		EyeSubscriber->setsockopt(ZMQ_SUBSCRIBE, "", 0);

		// Connect
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connecting to the python hardware stream client"));
		EyeSubscriber->connect(ReceiveAddress + ":" + ReceiveAddressPort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: python hardware stream client connection successful"));
		bZMQEyeConnection = true;
	}
	catch (const std::exception& e) {
		// Log a generic error message
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to connect to the python hardware stream client"));
		FString ExceptionMessage = FString(ANSI_TO_TCHAR(e.what()));
		UE_LOG(LogTemp, Error, TEXT("Exception caught: %s"), *ExceptionMessage);
		return false;
	}
	return true;
}

bool AEgoVehicle::TerminateEyeTrackerConnection() {
	// Check if the connection was even established
	if (!bZMQEyeConnection) {
		UE_LOG(LogTemp, Warning, TEXT("ZeroMQ: Attempting to terminate an eye-tracker connection that was never established."));
		return false;
	}

	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to terminate eye-tracker connection"));

		// Close the Subscriber socket
		if (EyeSubscriber) {
			EyeSubscriber->close();
			delete EyeSubscriber;
			EyeSubscriber = nullptr;
		}

		// Terminate the context
		if (EyeContext) {
			delete EyeContext;
			EyeContext = nullptr;
		}

		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Python hardware stream client terminated successfully"));
	}
	catch (const std::exception& e) {
		// Log a generic error message
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to terminate the hardware stream client connection"));
		FString ExceptionMessage = FString(ANSI_TO_TCHAR(e.what()));
		UE_LOG(LogTemp, Error, TEXT("Exception caught: %s"), *ExceptionMessage);
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Terminated connection to the python hardware stream client"));
	bZMQEyeConnection = false;
	return true;
}

FDcResult AEgoVehicle::RetrieveOnSurf()
{
	// Note: using raw C++ types in the following code as it does not interact with UE interface

	// Establish connection if not already
	if (!bZMQEyeConnection && !EstablishEyeTrackerConnection()) {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connection not established!"));
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Receive a message from the server
	zmq::message_t Message;
	if (!EyeSubscriber->recv(&Message)) {
		bZmqEyeDataRetrieve = true;
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Store the serialized data into a TArray
	TArray<uint8> DataArray;
	DataArray.Append(static_cast<uint8*>(Message.data()), Message.size());

	// Create a deserializer
	FDcDeserializer Deserializer;
	DcSetupMsgPackDeserializeHandlers(Deserializer, EDcMsgPackDeserializeType::Default);

	// Prepare context for this run
	FDcPropertyDatum Datum(&HardwareData);
	FDcMsgPackReader Reader(FDcBlobViewData::From(DataArray));
	FDcPropertyWriter Writer(Datum);

	FDcDeserializeContext Ctx;
	Ctx.Reader = &Reader;
	Ctx.Writer = &Writer;
	Ctx.Deserializer = &Deserializer;
	DC_TRY(Ctx.Prepare());
	DC_TRY(Deserializer.Deserialize(Ctx));

	if (HardwareData.HUD_OnSurf == "True")
	{
		bLatestOnSurfValue = true;
	} else if (HardwareData.HUD_OnSurf == "False")
	{
		bLatestOnSurfValue = false;
	}
	// Another potential OnSurf value can be "Unknown". In that case, do not change the value.

	return DcOk();
}