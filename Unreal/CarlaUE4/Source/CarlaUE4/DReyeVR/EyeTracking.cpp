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
#include "Json/DcJsonReader.h"
#include "Json/DcJsonWriter.h"
#include "Deserialize/DcDeserializeTypes.h"
#include "Deserialize/DcDeserializeUtils.h"
#include "Deserialize/DcDeserializerSetup.h"
#include "DcTypes.h"
#include "Property/DcPropertyDatum.h"

#include "DataConfig/DcEnv.h"
#include "DataConfig/Automation/DcAutomation.h"
#include "DataConfig/Deserialize/DcDeserializer.h"
#include "DataConfig/Deserialize/DcDeserializerSetup.h"
#include "DataConfig/Property/DcPropertyWriter.h"
#include "DataConfig/Serialize/DcSerializer.h"
#include "DataConfig/Serialize/DcSerializerSetup.h"
#include "DataConfig/Property/DcPropertyReader.h"
#include "DataConfig/Automation/DcAutomationUtils.h"
#include "DataConfig/Diagnostic/DcDiagnosticSerDe.h"
#include "DataConfig/Serialize/DcSerializeUtils.h"
#include "DataConfig/DcEnv.h"
#include "DataConfig/DcTypes.h"
#include "DataConfig/Json/DcJsonReader.h"
#include "DataConfig/Diagnostic/DcDiagnosticJSON.h"
#include "DataConfig/Diagnostic/DcDiagnosticCommon.h"

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


// The data from the eye tracker is received in a serialized format, which is then deserialized into a custom Unreal Engine data structure.
// This structure, FSurfaceData, holds the gaze data and other related information.
// The function uses the DataConfig library to handle the deserialization process. 
// This involves preparing a deserialization context, performing the deserialization, and handling any errors that may occur.
// Once the data is deserialized, the function extracts the gaze location from the data and returns it as a FVector2D.
// This vector represents the normalized position of the gaze on the surface being tracked, which can be used in the game for various purposes.
// This is the message that is recevied from ZeroMQ in msgpack format:
// {
//     "topic": "surfaces.surface_name",
//     "name": "surface_name",
//     "surf_to_img_trans": (
//         (-394.2704714040225, 62.996680859974035, 833.0782341017057),
//         (24.939461954010476, 264.1698344383364, 171.09768247735033),
//         (-0.0031580300961504023, 0.07378146751738948, 1.0),
//     ),
//     "img_to_surf_trans": (
//         (-0.002552357406770253, 1.5534025217146223e-05, 2.1236555655143734),
//         (0.00025853538051076233, 0.003973842600569134, -0.8952954577358644),
//         (-2.71355412859636e-05, -0.00029314688183396006, 1.0727627809231568),
//     ),
//     "gaze_on_surfaces": (
//         {
//             "topic": "gaze.3d.1._on_surface",
//             "norm_pos": (-0.6709809899330139, 0.41052111983299255),
//             "confidence": 0.5594810076623645,
//             "on_surf": False,
//             "base_data": ("gaze.3d.1.", 714040.132285),
//             "timestamp": 714040.132285,
//         },
//         ...,
//     ),
//     # list of fixations associated with
//     "fixations_on_surfaces": (
//         {
//             "topic": "fixations_on_surface",
//             "norm_pos": (-0.9006409049034119, 0.7738968133926392),
//             "confidence": 0.8663407531808505,
//             "on_surf": False,
//             "base_data": ("fixations", 714039.771958),
//             "timestamp": 714039.771958,
//             "id": 27,
//             "duration": 306.62299995310605,  # in milliseconds
//             "dispersion": 1.4730711610581475,  # in degrees
//         },
//         ...,
//     ),
//     # timestamp of the world video frame in which the surface was detected
//     "timestamp": 714040.103912,
// }
FVector2D AEgoVehicle::GetGazeScreenLocation() {
	// If everything was successful, one can now use your Destination object
	// We return the last gaze position as a FVector2D
	if (SurfaceData->gaze_on_surfaces.Num() > 0)
	{
		FGazeOnSurface LastGaze = SurfaceData->gaze_on_surfaces[SurfaceData->gaze_on_surfaces.Num() - 1];
		return FVector2D(LastGaze.norm_pos[0], LastGaze.norm_pos[1]);
    }
    else {
        return FVector2D(-1, -1);
    }
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
    DC_TRY(Ctx.Prepare());

    DC_TRY(Deserializer.Deserialize(Ctx));
    return DcOk();
}