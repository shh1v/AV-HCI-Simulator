#include "EgoVehicle.h"
#include "Carla/Actor/ActorAttribute.h"             // FActorAttribute
#include "Carla/Actor/ActorRegistry.h"              // Register
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include "Carla/Vehicle/CarlaWheeledVehicleState.h" // ECarlaWheeledVehicleState
#include "DReyeVRPawn.h"                            // ADReyeVRPawn
#include "Engine/EngineTypes.h"                     // EBlendMode
#include "Engine/World.h"                           // GetWorld
#include "GameFramework/Actor.h"                    // Destroy
#include "GameFramework/PlayerController.h"         // Deproject screen coordinates
#include "Kismet/KismetSystemLibrary.h"             // PrintString, QuitGame
#include "Math/Rotator.h"                           // RotateVector, Clamp
#include "Math/UnrealMathUtility.h"                 // Clamp
#include "UObject/ConstructorHelpers.h"				// ConstructorHelpers
#include "Engine/EngineTypes.h"						// Engine Types
#include <zmq.hpp>									// ZeroMQ plugin
#include <string>									// Raw string for ZeroMQ
#include "MsgPack/DcMsgPackReader.h"				// MsgPackReader
#include "Property/DcPropertyDatum.h"				// Datum
#include "Property/DcPropertyWriter.h"				// PropertyWriter
#include "Deserialize/DcDeserializer.h"				// Deserializer
#include "Deserialize/DcDeserializerSetup.h"		// EDcMsgPackDeserializeType
#include "MsgPackDatatypes.h"						// MsgPackDatatypes

bool AEgoVehicle::IsUserGazingOnHUD(const FVector2D& ScreenPosition) {
	FVector WorldLocation, WorldDirection; // These variables will be set by DeprojectScreenPositionToWorld
	if (Cast<APlayerController>(this->GetController())->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, WorldLocation, WorldDirection))
	{
		FHitResult HitResult;
		FVector StartLocation = WorldLocation;
		FVector EndLocation = WorldLocation + WorldDirection * 100; // 100 is just the ray length. Change if necessary

		// Perform the line trace and collect all the static mesh that were hit
		if (GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECC_Visibility))
		{
			// Check if the hit actor is your static mesh actor
			if (HitResult.Actor.Get() == this)
			{
				// The user was looking at the static mesh
				UStaticMeshComponent* HitMeshComponent = Cast<UStaticMeshComponent>(HitResult.Component.Get());
				if (HitMeshComponent == PrimaryHUD) return true;
			}
		}
	}
	return false;
}

bool AEgoVehicle::EstablishEyeTrackerConnection() {
	try {
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Attempting to establish connection"));
		//  Prepare our context and subscriber
		Context = new zmq::context_t(1);
		std::string Address = "127.0.0.1";
		std::string RequestPort = "50020";
		zmq::socket_t Requester(*Context, ZMQ_REQ);
		Requester.connect("tcp://" + Address + ":" + RequestPort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connected to the TCP port"));

		// Get the SUBSRIBE port to connect for communication
		std::string RequestString = "SUB_PORT";
		zmq::message_t Request(RequestString.begin(), RequestString.end());
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Sending request to get SUB PORT"));
		Requester.send(Request);
		zmq::message_t Reply;
		Requester.recv(&Reply);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Received SUB PORT"));
		std::string SubscribePort = std::string(static_cast<char*>(Reply.data()), Reply.size());

		// Setup the Subscriber socket
		Subscriber = new zmq::socket_t(*Context, ZMQ_SUB);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connecting to the SUB PORT"));
		Subscriber->connect("tcp://" + Address + ":" + SubscribePort);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Connection successful"));
		Subscriber->setsockopt(ZMQ_SUBSCRIBE, "surface", 7);
		UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Subscribed to surface topic"));
	}
	catch (...) {
		// Log a generic error message
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to connect to the Pupil labs Network API"));
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("ZeroMQ: Established connection to the Pupil labs Network API"));
	return true;
}

FVector2D AEgoVehicle::GetGazeScreenLocation() {
	// Get the updated message from using Zero-MW PUB-SUB
	GetSurfaceData();

	// Load the data into FGazeData
	ParseGazeData(SurfaceData.gaze_on_surfaces);

	for (int32 i = 0; i < HighestTimestampGazeData.NormPos.Num(); ++i) {
		LOG("NormPos[%d]: %f", i, HighestTimestampGazeData.NormPos[i]);
	}

	return FVector2D(-1, -1);
}

FDcResult AEgoVehicle::GetSurfaceData() {
	// Note: using raw C++ types in the following code as it does not interact with UE interface
// Establish connection if not already
	if (Subscriber == nullptr) {
		if (!EstablishEyeTrackerConnection()) {
			UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to connect to the Pupil labs Network API"));
		}
	}

	// Receive an update and update to a string
	zmq::message_t Update;
	if (!Subscriber->recv(&Update)) {
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to receive update from subscriber"));
	}
	std::string Topic(static_cast<char*>(Update.data()), Update.size());

	// Receive a message from the server
	zmq::message_t Message;
	if (!Subscriber->recv(&Message)) {
		UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to receive message from subscriber"));
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
	return DcOk();
}

void AEgoVehicle::ParseGazeData(FString GazeDataString) {
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
					TArray<FString> NormPosValues;
					Value.ParseIntoArray(NormPosValues, TEXT(", "), true);
					for (FString NormPosValue : NormPosValues) {
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