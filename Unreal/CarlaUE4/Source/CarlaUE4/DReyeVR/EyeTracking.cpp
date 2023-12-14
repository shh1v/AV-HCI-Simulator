#include "EgoVehicle.h"
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include <zmq.hpp>									// ZeroMQ plugin
#include <string>									// Raw string for ZeroMQ
#include "MsgPack/DcMsgPackReader.h"				// MsgPackReader
#include "Property/DcPropertyDatum.h"				// Datum
#include "Property/DcPropertyWriter.h"				// PropertyWriter
#include "Deserialize/DcDeserializer.h"				// Deserializer
#include "Deserialize/DcDeserializerSetup.h"		// EDcMsgPackDeserializeType

bool AEgoVehicle::IsUserGazingOnHUD() {
	if (bZmqEyeDataRetrieve) {
		if (bLastOnSurfValue == HighestTimestampGazeData.OnSurf) {
			GazeShiftCounter = 0; // Reset the counter when the value is consistent
		}
		else {
			// Increment the gaze shift counter and check if it exceeds the threshold
			if (++GazeShiftCounter >= 5) {
				bLastOnSurfValue = HighestTimestampGazeData.OnSurf;
				GazeShiftCounter = 0;
			}
		}

		return bLastOnSurfValue; // Return the current or updated value of OnSurf
	}

	// Handle the case when eye data retrieval is not enabled
	return false;
}


float AEgoVehicle::GazeOnHUDTime()
{
	if (IsUserGazingOnHUD())
	{
		if (!bGazeTimerRunning)
		{
			GazeOnHUDTimestamp = FPlatformTime::Seconds();
			bGazeTimerRunning = true;
		}
		return FPlatformTime::Seconds() - GazeOnHUDTimestamp;
	}
	bGazeTimerRunning = false;
	return 0;
}

bool AEgoVehicle::EstablishEyeTrackerConnection() {
	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to establish eye-tracker connection"));
		//  Prepare our context and subscriber
		EyeContext = new zmq::context_t(1);
		std::string Address = "127.0.0.1";
		std::string RequestPort = "50020";
		zmq::socket_t Requester(*EyeContext, ZMQ_REQ);
		Requester.connect("tcp://" + Address + ":" + RequestPort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connected to the eye-tracker TCP port"));

		// Set send and recv timeout to 100 millisecond to have non-blocking behaviour
		int timeout = 100;
		Requester.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
		Requester.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

		// Get the SUBSCRIBE port to connect for communication
		std::string RequestString = "SUB_PORT";
		zmq::message_t Request(RequestString.begin(), RequestString.end());
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Sending request to get eye-tracker SUB PORT"));
		if (!Requester.send(Request)) {
			throw std::runtime_error("Eye-tracker send operation timed out.");
		}
		zmq::message_t Reply;
		if (!Requester.recv(&Reply)) {
			throw std::runtime_error("Eye-tracker recieve operation timed out.");
		}
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Received eye-tracker SUB PORT"));
		std::string SubscribePort = std::string(static_cast<char*>(Reply.data()), Reply.size());

		// Setup the Subscriber socket
		EyeSubscriber = new zmq::socket_t(*EyeContext, ZMQ_SUB);

		// Setting 100 ms recv timeout to have non-blocking behaviour
		EyeSubscriber->setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
		int conflate = 1;
		EyeSubscriber->setsockopt(ZMQ_CONFLATE, &conflate, sizeof(conflate));

		// Connecting
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connecting to the eye-tracker SUB PORT"));
		EyeSubscriber->connect("tcp://" + Address + ":" + SubscribePort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Eye-tracker connection successful"));
		EyeSubscriber->setsockopt(ZMQ_SUBSCRIBE, "surfaces.HUD", 12);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Subscribed to eye-tracker surface topic"));
	}
	catch (...) {
		// Log a generic error message
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to connect to the Pupil labs Network API"));
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Established connection to the Pupil labs Network API"));
	bZMQEyeConnection = true;
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

		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Eye-tracker connection terminated successfully"));
	}
	catch (const std::exception& e) {
		// Log a generic error message
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to terminate the eye-tracker connection"));
		FString ExceptionMessage = FString(ANSI_TO_TCHAR(e.what()));
		UE_LOG(LogTemp, Error, TEXT("Exception caught: %s"), *ExceptionMessage);
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Terminated connection to the Pupil labs Network API"));
	bZMQEyeConnection = false;
	return true;
}


FVector2D AEgoVehicle::GetGazeHUDLocation() {
	// Multiply the normalized coordinates to the game resolution (i.e., equivalent to the screen resolution)
	// Note/WARNING: The screen resolution values are hard coded
	if (bZmqEyeDataRetrieve)
	{
		return FVector2D(HighestTimestampGazeData.NormPos[0], HighestTimestampGazeData.NormPos[1]);
	}
	return FVector2D(-1, -1);
}

FDcResult AEgoVehicle::GetSurfaceData() {
	// Note: using raw C++ types in the following code as it does not interact with UE interface
	// Establish connection if not already
	if (!bZMQEyeConnection && !EstablishEyeTrackerConnection()) {
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Receive an update and update to a string
	zmq::message_t Update;
	if (!EyeSubscriber->recv(&Update)) {
		bZmqEyeDataRetrieve = false;
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to receive update from eye tracker"));
		return FDcResult{ FDcResult::EStatus::Error };
	}
	std::string Topic(static_cast<char*>(Update.data()), Update.size());

	// Receive a message from the server
	zmq::message_t Message;
	if (!EyeSubscriber->recv(&Message)) {
		bZmqEyeDataRetrieve = false;
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to receive message from eye tracker"));
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Store the serialized data into a TArray
	TArray<uint8> DataArray;
	DataArray.Append(static_cast<uint8*>(Message.data()), Message.size());

	// Create a deserializer
	FDcDeserializer Deserializer;
	DcSetupMsgPackDeserializeHandlers(Deserializer, EDcMsgPackDeserializeType::Default);

	// Prepare context for this run
	FDcPropertyDatum Datum(&SurfaceData);
	FDcMsgPackReader Reader(FDcBlobViewData::From(DataArray));
	FDcPropertyWriter Writer(Datum);

	FDcDeserializeContext Ctx;
	Ctx.Reader = &Reader;
	Ctx.Writer = &Writer;
	Ctx.Deserializer = &Deserializer;
	DC_TRY(Ctx.Prepare());
	DC_TRY(Deserializer.Deserialize(Ctx));
	bZmqEyeDataRetrieve = true;
	return DcOk();
}

void AEgoVehicle::ParseGazeData() {
	// Return if data was not retrieved from the eye-tracker
	if (!bZmqEyeDataRetrieve) {
		return;
	}

	FString GazeDataString = SurfaceData.gaze_on_surfaces;

	// Extract the last data entry (as the last entry is the relevant one)
	int32 LastOpenBrace = GazeDataString.Find(TEXT("{"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	FString GazeEntry = GazeDataString.Mid(LastOpenBrace);

	FTypedGazeData GazeData;

	// Extract on_surf value
	FString OnSurfStr;
	int32 OnSurfStart = GazeEntry.Find(TEXT("'on_surf': "));
	if (OnSurfStart != INDEX_NONE) {
		OnSurfStr = GazeEntry.Mid(OnSurfStart + 11).TrimStart();
		if (OnSurfStr.StartsWith(TEXT("True"))) {
			GazeData.OnSurf = true;
		}
		else if (OnSurfStr.StartsWith(TEXT("False"))) {
			GazeData.OnSurf = false;
		}
	}

	// Extract timestamp value
	FString TimestampStr;
	int32 TimestampStart = GazeEntry.Find(TEXT("'timestamp': "));
	if (TimestampStart != INDEX_NONE) {
		TimestampStr = GazeEntry.Mid(TimestampStart + 12).TrimStart();
		int32 CommaPos = TimestampStr.Find(TEXT(","));
		int32 BracePos = TimestampStr.Find(TEXT("}"));
		int32 EndPos = (CommaPos == INDEX_NONE) ? BracePos : (BracePos == INDEX_NONE ? CommaPos : FMath::Min(CommaPos, BracePos));

		if (EndPos != INDEX_NONE) {
			TimestampStr = TimestampStr.Left(EndPos);
		}
		GazeData.TimeStamp = FCString::Atof(*TimestampStr);
	}

	HighestTimestampGazeData = GazeData;
}
