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
	if (bZMQEyeDataRetrive)
	{
		return HighestTimestampGazeData.OnSurf;
	}
	return false;
}


bool AEgoVehicle::EstablishEyeTrackerConnection() {
	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to establish eye-tracker connection"));
		//  Prepare our context and subscriber
		EyeContext = new zmq::context_t(1);
		std::string Address = "127.0.0.1";
		std::string RequestPort = "50020";
		int bCommSuccess = false; // Used for error handling
		zmq::socket_t Requester(*EyeContext, ZMQ_REQ);
		Requester.connect("tcp://" + Address + ":" + RequestPort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connected to the eye-tracker TCP port"));

		// Set send and recv timeout to 0 millisecond to have non-blocking behaviour
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

		// Setting o ms recv timeout to have non-blocking behaviour
		EyeSubscriber->setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

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
	if (bZMQEyeDataRetrive)
	{
		return FVector2D(HighestTimestampGazeData.NormPos[0], HighestTimestampGazeData.NormPos[1]);
	}
	return FVector2D(-1, -1);
}

FDcResult AEgoVehicle::GetSurfaceData() {
	// Note: using raw C++ types in the following code as it does not interact with UE interface
	// Establish connection if not already
	if (!bZMQEyeConnection) {
		return FDcResult{ FDcResult::EStatus::Error };
	}

	// Receive an update and update to a string
	zmq::message_t Update;
	if (!EyeSubscriber->recv(&Update)) {
		bZMQEyeDataRetrive = false;
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to receive update from subscriber"));
		return FDcResult{ FDcResult::EStatus::Error };
	}
	std::string Topic(static_cast<char*>(Update.data()), Update.size());

	// Receive a message from the server
	zmq::message_t Message;
	if (!EyeSubscriber->recv(&Message)) {
		bZMQEyeDataRetrive = false;
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Failed to receive message from subscriber"));
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
	bZMQEyeDataRetrive = true;
	return DcOk();
}

void AEgoVehicle::ParseGazeData(FString GazeDataString) {
	// Return if data was not retrived from the eye-tracker
	if (!bZMQEyeDataRetrive) {
		return;
	}

	float HighestTimestamp = -1.0f;
	// Remove the square brackets in the strings
	GazeDataString = GazeDataString.Mid(1, GazeDataString.Len() - 2);

	// Split the FString into individual gaze data entries
	TArray<FString> GazeDataEntries;
	GazeDataString.ParseIntoArray(GazeDataEntries, TEXT("}, "), true);

	for (FString Entry : GazeDataEntries) {
		LOG("Working on the gaze entry: %s", *Entry);
		Entry = Entry.Replace(TEXT("{"), TEXT(""));

		FTypedGazeData GazeData;
		TArray<FString> KeyValuePairs;
		Entry.ParseIntoArray(KeyValuePairs, TEXT(", '"), true);

		for (FString KeyValuePair : KeyValuePairs) {
			KeyValuePair = KeyValuePair.Replace(TEXT("'"), TEXT(""));
			LOG("Entry: %s, KeyValuePair: %s", *Entry, *KeyValuePair);

			TArray<FString> KeyAndValue;
			KeyValuePair.ParseIntoArray(KeyAndValue, TEXT(": "), true);
			if (KeyAndValue.Num() == 2) {
				FString Key = KeyAndValue[0].TrimStartAndEnd();
				FString Value = KeyAndValue[1].TrimStartAndEnd();

				if (Key == TEXT("topic")) {
					GazeData.Topic = Value.TrimQuotes();
				}
				else if (Key == TEXT("norm_pos")) {
					// Remove the paranthesis in the key
					Value = Value.Mid(1, Value.Len() - 2);
					TArray<FString> NormPosValues;
					Value.ParseIntoArray(NormPosValues, TEXT(", "), true);
					for (FString NormPosValue : NormPosValues) {
						LOG("NormPosValue: %s", *NormPosValue);
						GazeData.NormPos.Add(FCString::Atof(*NormPosValue));
					}
				}
				else if (Key == TEXT("confidence")) {
					GazeData.Confidence = FCString::Atof(*Value);
				}
				else if (Key == TEXT("on_surf")) {
					GazeData.OnSurf = Value == TEXT("True") ? true : false;
				}
				else if (Key == TEXT("base_data")) {
					// Remove the paranthesis in the key
					Value = Value.Mid(1, Value.Len() - 2);
					TArray<FString> BaseDataValues;
					Value.TrimStartAndEnd().ParseIntoArray(BaseDataValues, TEXT(", "), true);
					GazeData.BaseData.TopicPrefix = BaseDataValues[0].TrimQuotes();
					GazeData.BaseData.TimeStamp = FCString::Atof(*BaseDataValues[1]);
				} 
				else if (Key == TEXT("timestamp")) {
					GazeData.TimeStamp = FCString::Atof(*Value);
				}
			}
		}

		// If this gaze data has a higher timestamp, store it
		if (GazeData.TimeStamp > HighestTimestamp) {
			HighestTimestampGazeData = GazeData;
			HighestTimestamp = GazeData.TimeStamp;
		}
	}
}