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
#include "MsgPack/DcMsgPackReader.h"				// MsgPack Reader
#include "Property/DcPropertyDatum.h"				// Datum to load retrived message
#include "Property/DcPropertyWriter.h"				// Property writer
#include "Deserialize/DcDeserializer.h"				// Deserialize retrived data
#include "MsgPackDatatypes.h"						// Custom datatype to handle incoming data

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
    // Note: using raw C++ types in the following code as it does not interact with UE interface
    // Establish connection if not already
    if (Subscriber == nullptr) {
        if (!EstablishEyeTrackerConnection()) {
            UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to connect to the Pupil labs Network API"));
            return FVector2D::ZeroVector;
        }
    }

    // Receive an update and update to a string
    zmq::message_t Update;
    if (!Subscriber->recv(&Update)) {
        UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to receive update from subscriber"));
        return FVector2D::ZeroVector;
    }
    std::string Topic(static_cast<char*>(Update.data()), Update.size());

    // Receive a message from the server
    zmq::message_t Message;
    if (!Subscriber->recv(&Message)) {
        UE_LOG(LogTemp, Error, TEXT("ZeroMQ: Failed to receive message from subscriber"));
        return FVector2D::ZeroVector;
    }

    // Store the serialized data into a TArray
    TArray<uint8> DataArray;
    DataArray.Append(static_cast<uint8*>(Message.data()), Message.size());

    // Create a destination variable and deserializer
    FSurfaceData Destination;
    FDcDeserializer Deserializer;

    // Prepare context for this run
    FDcPropertyDatum Datum(&Destination);
    FDcMsgPackReader Reader(FDcBlobViewData::From(DataArray));
    FDcPropertyWriter Writer(Datum);

    FDcDeserializeContext Ctx;
    Ctx.Reader = &Reader;
    Ctx.Writer = &Writer;
    Ctx.Deserializer = &Deserializer;
    FDcResult Result = Ctx.Prepare();

    // Check if Prepare was successful
    if (!Result.Ok())
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroMQ: Failed to prepare context"));
        return FVector2D::ZeroVector;
    }

    //  kick off deserialization
    Result = Deserializer.Deserialize(Ctx);

    // If everything was successful, one can now use your Destination object
    // We return the last gaze position as a FVector2D

    if (Destination.gaze_on_surfaces.Num() > 0)
    {
        FGazeOnSurface LastGaze = Destination.gaze_on_surfaces[Destination.gaze_on_surfaces.Num() - 1];
        return FVector2D(LastGaze.norm_pos[0], LastGaze.norm_pos[1]);
    }
    UE_LOG(LogTemp, Warning, TEXT("ZeroMQ: Failed to get an instance of gaze on surface subtopic"));
    return FVector2D::ZeroVector;
}