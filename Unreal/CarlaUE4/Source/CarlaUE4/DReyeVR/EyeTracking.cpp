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
#include "Engine/EngineTypes.h"
#include <zmq.hpp>									// ZeroMQ plugin
#include <string>									// Raw string for ZeroMQ
#include "DcTypes.h"			
#include "MsgPack/DcMsgPackReader.h"				// MsgPack Reader
#include "Property/DcPropertyDatum.h"				// Datum to load retrived data
#include "Deserialize/DcDeserializeTypes.h"			// Deserialize data types
#include "Deserialize/DcDeserializer.h"				// Deserialize retrived data (R)
#include "Property/DcPropertyReader.h"				// DC property (data type)
#include "MsgPackDatatypes.h"					// Custom datatype to handle incoming data

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
		//  Prepare our context and subscriber
		zmq::context_t Context(1); // setting the context of ZeroMQ
		std::string Address = "127.0.0.1";
		std::string RequestPort = "50020";
		zmq::socket_t Requester(Context, ZMQ_REQ);
		Requester.connect("tcp://" + Address + ":" + RequestPort);

		// Get the SUBSRIBE port to connect for communication
		std::string RequestString = "SUB_PORT";
		zmq::message_t Request(RequestString.begin(), RequestString.end());
		Requester.send(Request);
		zmq::message_t Reply;
		Requester.recv(&Reply);
		std::string SubscribePort = std::string(static_cast<char*>(Reply.data()), Reply.size());

		// Setup the Subscriber socket
		Subscriber = new zmq::socket_t(Context, ZMQ_SUB);
		Subscriber->connect("tcp://" + Address + ":" + SubscribePort);
		Subscriber->setsockopt(ZMQ_SUBSCRIBE, "surface", 7);
	}
	catch (...) {
		LOG("Unable to connect to the Pupil labs Network API");
		return false;
	}
	return true;
}

FVector2D AEgoVehicle::GetGazeScreenLocation() {
	// Note: using raw C++ types in the following code as it does not interact with UE interface
	// Establish connection if not already
	if (Subscriber == nullptr) EstablishEyeTrackerConnection();
	
	// Receive an update and update to a string
	zmq::message_t Update;
	Subscriber->recv(&Update);
	std::string Topic(static_cast<char*>(Update.data()), Update.size());

	// Receive a message from the server
	zmq::message_t Message;
	Subscriber->recv(&Message);

	// Store the serialized data into a TArray
	TArray<uint8> DataArray;
	DataArray.Append(static_cast<uint8*>(Message.data()), Message.size());

	// Create a MsgPack reader for the message
	FDcBlobViewData Blob = FDcBlobViewData::From(DataArray);
	FDcMsgPackReader Reader(Blob);

	// Create a deserialize context and set the reader in context
	FDcDeserializeContext Ctx;
	Ctx.Reader = &Reader;

	// Create a property writer
	// Here, you need to specify the Unreal Engine property you want to write into
	// For example, if you have a UObject* MyObject and a FProperty* MyProperty,
	// you can create a property writer like this:
	FDictionaryProperty MyDictionary;
	UProperty* MyProperty = FindField<UProperty>(FDictionaryProperty::StaticStruct(), TEXT("Data"));
	FDcPropertyDatum Datum(MyProperty, &MyDictionary);
	FDcPropertyWriter Writer(Datum);
	Ctx.Writer = &Writer;

	// Now you can deserialize the MsgPack data into your Unreal Engine property
	FDcResult DeserializationResult = FDcDeserializeDatum(Ctx, Datum);
	if (!DeserializationResult.Ok())
	{
		// Handle the error
		UE_LOG(LogTemp, Error, TEXT("Deserialization failed: %s"), *DeserializationResult.ToString());
	}

	// Now MyDictionary.Data contains the deserialized data
	for (const TPair<FString, FVariant>& Pair : MyDictionary.Data)
	{
		// Use Pair.Key and Pair.Value
	}
}