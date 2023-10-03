#include "EgoVehicle.h"
#include "Carla/Game/CarlaStatics.h"                // GetCurrentEpisode
#include "Engine/EngineTypes.h"                     // EBlendMode
#include "GameFramework/Actor.h"                    // Destroy
#include "UObject/ConstructorHelpers.h"				// ConstructorHelpers
#include "MediaPlayer.h"
#include "FileMediaSource.h"
#include "MediaTexture.h"
#include "MediaSoundComponent.h"


void AEgoVehicle::SetupNDRT() {
	// Construct the head-up display
	ConstructHUD();

	// Present the visual elements based on the task type {n-back, TV show, etc..}
	switch (CurrTaskType) {
	case TaskType::NBackTask:
		ConstructNBackElements();
		break;
	case TaskType::TVShowTask:
		ConstructTVShowElements();
		break;
	}
}

void AEgoVehicle::StartNDRT() {
	// Start the NDRT based on the task type
	switch (CurrTaskType) {
	case TaskType::NBackTask:
		break;
	case TaskType::TVShowTask:
		// Retrive the media player material and the video source which will be used later to play the video
		MediaPlayerMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, TEXT("Material'/Game/NDRT/TVShow/MediaPlayer/M_MediaPlayer.M_MediaPlayer'")));
		MediaPlayerSource = Cast<UFileMediaSource>(StaticLoadObject(UFileMediaSource::StaticClass(), nullptr, TEXT("FileMediaSource'/Game/NDRT/TVShow/MediaPlayer/FileMediaSource.FileMediaSource'")));
		// Change the static mesh material to media player material
		MediaPlayerMesh->SetMaterial(0, MediaPlayerMaterial);
		// Retrieve the source of the video and play it
		MediaPlayer->OpenSource(MediaPlayerSource);
		break;
	}

	// Establish connection with the eye-tracker
	EstablishEyeTrackerConnection();
}

void AEgoVehicle::ToggleNDRT(bool active) {
	if (active) {
		// Make all the HUD elements appear again
	}
	else
	{
		// Make all the HUD elements disappear
	}
}

void AEgoVehicle::ToggleAlertOnNDRT(bool active) {
	if (active)
	{
		// Make a red rim appear around the HUD

		// Play a subtle alert sound if not already played
		if (!bisAlertOnNDRTOn)
		{
			// Play an alert sound

			bisAlertOnNDRTOn = true;
		}
	}
	else
	{
		// Make the red rim around the HUD disappear

		bisAlertOnNDRTOn = false;
	}
}

void AEgoVehicle::SetInteractivityOfNDRT(bool interactivity) {
	if (interactivity)
	{
		// Make a black screen appear at the front
	}
	else
	{
		// Make the black screen at front of HUD disappear
	}
}

void AEgoVehicle::TerminateNDRT() {
	// TODO: Save all the NDRT performance data here if needed
}



void AEgoVehicle::TickNDRT() {
	// Retrieve all the necessary data from all the other components
	GetSurfaceData();
	ParseGazeData(SurfaceData.gaze_on_surfaces);
	RetrieveVehicleStatus();
	GazeOnHUDTime();

	auto HandleTaskTick = [&]() {
		switch (CurrTaskType)
		{
		case TaskType::NBackTask:
			NBackTaskTick();
			break;
		case TaskType::TVShowTask:
			TVShowTaskTick();
			break;
		default:
			break;
		}
	};

	if (IsUserGazingOnHUD())
	{
		switch (CurrInterruptionParadigm)
		{
		case InterruptionParadigm::SelfRegulated:
			HandleTaskTick();
			break;

		case InterruptionParadigm::SystemRecommended:
			if (GazeOnHUDTime() >= GazeOnHUDTimeConstraint)
			{
				ToggleAlertOnNDRT(true);
			}
			HandleTaskTick();
			break;

		case InterruptionParadigm::SystemInitiated:
			if (GazeOnHUDTime() >= GazeOnHUDTimeConstraint)
			{
				ToggleAlertOnNDRT(true);
				SetInteractivityOfNDRT(false);
			}
			else
			{
				HandleTaskTick();
			}
			break;

		default:
			break;
		}
	}
	else
	{
		switch (CurrInterruptionParadigm)
		{
		case InterruptionParadigm::SystemRecommended:
			ToggleAlertOnNDRT(false);
			break;

		case InterruptionParadigm::SystemInitiated:
			ToggleAlertOnNDRT(false);
			SetInteractivityOfNDRT(true);
			break;

		default:
			break;
		}
	}
}


void AEgoVehicle::ConstructHUD() {
	// Creating the primary head-up display to display the non-driving related task
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

void AEgoVehicle::ConstructNBackElements() {
	// Creating the letter pane to show letters for the n-back task
	NBackLetter = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Letter Pane"));
	NBackLetter->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackLetter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackLetter->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "LetterLocation"));
	FString PathToMeshNBackLetter = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_LetterPane.SM_LetterPane'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackLetterMeshObj(*PathToMeshNBackLetter);
	NBackLetter->SetStaticMesh(NBackLetterMeshObj.Object);
	NBackLetter->SetCastShadow(false);

	// Creating a pane to show controls on the logitech steering wheel for the n-back task
	NBackControlsInfo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Controls Pane"));
	NBackControlsInfo->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackControlsInfo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackControlsInfo->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "ControlsInfoLocation"));
	FString PathToMeshNBackControls = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_ControlsPane.SM_ControlsPane'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackControlsMeshObj(*PathToMeshNBackControls);
	NBackControlsInfo->SetStaticMesh(NBackControlsMeshObj.Object);
	NBackControlsInfo->SetCastShadow(false);

	// Creating a pane for the title (0-back, 1-back, etc..)
	NBackTitle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("N-back Title Pane"));
	NBackTitle->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NBackTitle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NBackTitle->SetRelativeTransform(VehicleParams.Get<FTransform>("NBack", "TitleLocation"));
	FString PathToMeshNBackTitle = TEXT("StaticMesh'/Game/NDRT/NBackTask/StaticMeshes/SM_NBackTitle.SM_NBackTitle'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> NBackTitleMeshObj(*PathToMeshNBackTitle);
	NBackTitle->SetStaticMesh(NBackTitleMeshObj.Object);
	NBackTitle->SetCastShadow(false);

	// Changing the title dynamically based on the n-back task
	FString MaterialPath = FString::Printf(TEXT("Material'/Game/NDRT/NBackTask/Titles/M_%dBackTaskTitle.M_%dBackTaskTitle'"), (int32)CurrentNValue, (int32)CurrentNValue);
	static ConstructorHelpers::FObjectFinder<UMaterial> NewMaterial(*MaterialPath);
	NBackTitle->SetMaterial(0, NewMaterial.Object);
}

void AEgoVehicle::ConstructTVShowElements() {
	// Initializing the static mesh for the media player with a default texture
	MediaPlayerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TV-show Pane"));
	UStaticMesh* CubeMesh = ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'")).Object;
	MediaPlayerMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, TEXT("Material'/Game/NDRT/TVShow/MediaPlayer/M_MediaPlayerDefault.M_MediaPlayerDefault'")));
	MediaPlayerMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	MediaPlayerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MediaPlayerMesh->SetRelativeTransform(VehicleParams.Get<FTransform>("TVShow", "MediaPlayerLocation"));
	MediaPlayerMesh->SetStaticMesh(CubeMesh);
	MediaPlayerMesh->SetMaterial(0, MediaPlayerMaterial);
	MediaPlayerMesh->SetCastShadow(false);

	// Add a Media sounds component to the static mesh player
	MediaPlayer = Cast<UMediaPlayer>(StaticLoadObject(UMediaPlayer::StaticClass(), nullptr, TEXT("MediaPlayer'/Game/NDRT/TVShow/MediaPlayer/MediaPlayer.MediaPlayer'")));

	// Create a MediaSoundComponent
	MediaSoundComponent = NewObject<UMediaSoundComponent>(MediaPlayerMesh);
	MediaSoundComponent->AttachToComponent(MediaPlayerMesh, FAttachmentTransformRules::KeepRelativeTransform);

	// Set the media player to the sound component
	MediaSoundComponent->SetMediaPlayer(MediaPlayer);

	// Add the MediaSoundComponent to the actor's components
	MediaPlayerMesh->GetOwner()->AddOwnedComponent(MediaSoundComponent);
}

float AEgoVehicle::GazeOnHUDTime()
{
	if (IsUserGazingOnHUD())
	{
		if (!bGazeTimerRunning)
		{
			GazeOnHUDTimestamp = World->GetTimeSeconds();
			bGazeTimerRunning = true;
		}
		return World->GetTimeSeconds() - GazeOnHUDTimestamp;
	}
	bGazeTimerRunning = false;
	return 0;
}


// N-back task exclusive methods

void AEgoVehicle::SetLetter(const FString& Letter) {
	if (NBackLetter == nullptr) return; // NBackLetter is not initialized yet
	FString MaterialPath = FString::Printf(TEXT("Material'/Game/NDRT/NBackTask/Letters/M_%s.M_%s'"), *Letter, *Letter);
	static ConstructorHelpers::FObjectFinder<UMaterial> NewMaterial(*MaterialPath);
	NBackLetter->SetMaterial(0, NewMaterial.Object);
}

void AEgoVehicle::RecordNBackInputs(bool BtnUp, bool BtnDown)
{

}

void AEgoVehicle::NBackTaskTick()
{

}

// TV show task exclusive methods

void AEgoVehicle::TVShowTaskTick()
{
	
}
