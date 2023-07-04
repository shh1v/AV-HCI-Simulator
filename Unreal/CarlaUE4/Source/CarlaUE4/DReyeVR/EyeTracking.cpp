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
	// Note: using raw C++ types the following code does not interface with UE interface
	// Establish connection if not already
	
	// Get the gaze data from the eye-tracker
	return NULL;
}