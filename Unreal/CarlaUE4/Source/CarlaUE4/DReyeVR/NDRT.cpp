#include "EgoVehicle.h"
#include "Carla/Actor/ActorAttribute.h"             // FActorAttribute
#include "Carla/Actor/ActorRegistry.h"              // Register
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include "Carla/Vehicle/CarlaWheeledVehicleState.h" // ECarlaWheeledVehicleState
#include "DReyeVRPawn.h"                            // ADReyeVRPawn
#include "Engine/EngineTypes.h"                     // EBlendMode
#include "Engine/World.h"                           // GetWorld
#include "GameFramework/Actor.h"                    // Destroy
#include "Kismet/KismetSystemLibrary.h"             // PrintString, QuitGame
#include "Math/Rotator.h"                           // RotateVector, Clamp
#include "Math/UnrealMathUtility.h"                 // Clamp
#include "UObject/ConstructorHelpers.h"				// ConstructorHelpers

void AEgoVehicle::InitNDRT() {
	// Construct the head-up display
	ConstructHUD();

	// Present the visual elements based on the task type {n-back, TV show, etc..}
	switch (CurrentTaskType) {
	case TaskType::NBackTask:
		ConstuctNBackElements();
		break;
	case TaskType::TVShowTask:
		ConstuctTVShowElements();
		break;
	}
}
void AEgoVehicle::DisableNDRT() {

}
void AEgoVehicle::HideNDRT() {

}
void AEgoVehicle::TerminateNDRT() {

}
void AEgoVehicle::TickNDRT() {

}

void AEgoVehicle::ConstructHUD() {
	// Creating the primary head-up dispay to display the non-driving related task
	PrimaryHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Primary HUD"));
	PrimaryHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	PrimaryHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrimaryHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "PrimaryHUDLocation"));
	FString PathToMeshPHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_PrimaryHUD.SM_PrimaryHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> PHUDMeshObj(*PathToMeshPHUD);
	PrimaryHUD->SetStaticMesh(PHUDMeshObj.Object);
	PrimaryHUD->SetCastShadow(false);

	// Creating the secondary head-up dispay which will give the notification to switch task.
	SecondaryHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Secondary HUD"));
	SecondaryHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	SecondaryHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SecondaryHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "SecondaryHUDLocation"));
	FString PathToMeshSHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_SecondaryHUD.SM_SecondaryHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> SHUDMeshObj(*PathToMeshSHUD);
	SecondaryHUD->SetStaticMesh(SHUDMeshObj.Object);
	SecondaryHUD->SetCastShadow(false);
	SecondaryHUD->SetVisibility(false, false); // Set it hidden by default, and only make it appear when alerting.
}

void AEgoVehicle::ConstuctNBackElements() {
	// Creating the letter pane to show letters for the n-back task
	NBackLetter = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Letter Pane"));
	NBackLetter->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackLetter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackLetter->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "LetterLocation"));
	FString PathToMeshNBackLetter = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_LetterPane.SM_LetterPane'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackLetterMeshObj(*PathToMeshNBackLetter);
	NBackLetter->SetStaticMesh(NBackLetterMeshObj.Object);
	NBackLetter->SetCastShadow(false);
}

void AEgoVehicle::ConstuctTVShowElements() {
}

void AEgoVehicle::SetLetter(const FString& Letter) {

	if (NBackLetter == nullptr) return; // NBackLetter is not initialized yet
	FString MaterialPath = FString::Printf(TEXT("Material'/Game/NDRT/NBackTask/Letters/M_%s.M_%s'"), *Letter, *Letter);
	static ConstructorHelpers::FObjectFinder<UMaterial> NewMaterial (*MaterialPath);
	NBackLetter->SetMaterial(0, NewMaterial.Object);
}
