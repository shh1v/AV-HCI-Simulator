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
		// Add randomly generated elements to NBackPrompts
		for (int32 i = 0; i < TotalNBackTasks; i++)
		{
			TCHAR RandomChar = FMath::RandRange('A', 'Z');
			FString SingleLetter = FString(1, &RandomChar);
			NBackPrompts.Add(SingleLetter);
		}
		// Set the first index letter on the HUD
		SetLetter(NBackPrompts[0]);
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
	// Make all the HUD meshes appear/disappear
	PrimaryHUD->SetVisibility(active, false);
	// We only need to make them disappear. We don't need to make them visible when NDRT interaction is enabled again.
	if (!active) {
		SecondaryHUD->SetVisibility(active, false);
		DisableHUD->SetVisibility(active, false);
	}

	// Make all the NDRT-relevant elements appear/disappear
	switch(CurrTaskType)
	{
	case TaskType::NBackTask:
		NBackLetter->SetVisibility(active, false);
		NBackControlsInfo->SetVisibility(active, false);
		NBackTitle->SetVisibility(active, false);
		break;
	case TaskType::TVShowTask:
		MediaPlayerMesh->SetVisibility(active, false);
		break;
	default:
		break;
	}
}

void AEgoVehicle::ToggleAlertOnNDRT(bool active) {
	if (active)
	{
		// Make a red rim appear around the HUD
		SecondaryHUD->SetVisibility(true, false);
		// Play a subtle alert sound if not already played
		if (!bisAlertOnNDRTOn)
		{
			// Play an alert sound once (looping is disabled)
			HUDAlertSound->Play();
			bisAlertOnNDRTOn = true;
		}
	}
	else
	{
		// Make the red rim around the HUD disappear
		SecondaryHUD->SetVisibility(false, false);
		bisAlertOnNDRTOn = false;
	}
}

void AEgoVehicle::SetInteractivityOfNDRT(bool interactivity) {
	if (interactivity)
	{
		// Make a black screen appear at the front
		DisableHUD->SetVisibility(true, false);
	}
	else
	{
		// Make the black screen at front of HUD disappear
		DisableHUD->SetVisibility(false, false);
	}
}

void AEgoVehicle::TerminateNDRT() {
	// Set the vehicle status to TrialOver
	UpdateVehicleStatus(VehicleStatus::TrialOver);

	// TODO: Save all the NDRT performance data here if needed
}



void AEgoVehicle::TickNDRT() {
	// Get update from eye tracker and the vehicle status
	GetSurfaceData();
	ParseGazeData(SurfaceData.gaze_on_surfaces);
	RetrieveVehicleStatus();
	GazeOnHUDTime();

	// WARNING/NOTE: It is the responsibility of the respective NDRT tick methods to change the vehicle status
	// to TrialOver when the NDRT task is over.

	// CASE: When NDRT engagement is disabled/not expected (during TORs or manual control)
	if (CurrVehicleStatus == VehicleStatus::ManualDrive || CurrVehicleStatus == VehicleStatus::TrialOver)
	{
		// This is the case when scenario runner has not kicked in. Just do nothing
		// This means not allowing driver to interact with NDRT
		// JUST FOR TESTING PURPOSES
		ToggleAlertOnNDRT(IsUserGazingOnHUD());
		return;
	}
	if (CurrVehicleStatus == VehicleStatus::TakeOver || CurrVehicleStatus == VehicleStatus::TakeOverManual)
	{
		// Disable the NDRT interface by toggling it
		ToggleNDRT(false);

		// Show a message asking the driver to take control of the vehicle
		SetMessagePaneText(TEXT("Take Over!"), FColor::Red);
		return;
	}

	// CASE: When the user is allowed to engage in NDRT
	// Create a lambda function to call the tick method based on the NDRT set from configuration file
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
	// Simply compute the NDRT task when the vehicle is on autopilot
	if (CurrVehicleStatus == VehicleStatus::Autopilot || CurrVehicleStatus == VehicleStatus::ResumedAutopilot)
	{
		// Tell the driver that the ADS is engaged
		SetMessagePaneText(TEXT("Autopilot Engaged"), FColor::Green);

		// Make sure NDRT is toggled up again
		ToggleNDRT(true);

		// Run/computer the NDRT
		HandleTaskTick();

		// The rest of the code is only relevant for the pre-alert interval. So, exit
		return;

	}
	// During the pre-alert period, allow NDRT engagement, but with potential restriction based
	// on the interruption paradigm
	if (CurrVehicleStatus == VehicleStatus::PreAlertAutopilot)
	{
		// Show a pre-alert message suggesting that a take-over request may be issued in the future
		SetMessagePaneText(TEXT("Prepare to Take Over"), FColor::Orange);
	}


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

	// Creating the secondary head-up display which will give the notification to switch task.
	SecondaryHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Secondary HUD"));
	SecondaryHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	SecondaryHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SecondaryHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "SecondaryHUDLocation"));
	FString PathToMeshSHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_SecondaryHUD.SM_SecondaryHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> SHUDMeshObj(*PathToMeshSHUD);
	SecondaryHUD->SetStaticMesh(SHUDMeshObj.Object);
	SecondaryHUD->SetCastShadow(false);
	SecondaryHUD->SetVisibility(false, false); // Set it hidden by default, and only make it appear when alerting.

	// Creating the disabling head-up display for the system-initiated task switching
	DisableHUD = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disable HUD"));
	DisableHUD->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	DisableHUD->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisableHUD->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "DisableHUDLocation"));
	FString PathToMeshDHUD = TEXT("StaticMesh'/Game/NDRT/StaticMeshes/SM_DisableHUD.SM_DisableHUD'");
	const ConstructorHelpers::FObjectFinder<UStaticMesh> DHUDMeshObj(*PathToMeshDHUD);
	DisableHUD->SetStaticMesh(DHUDMeshObj.Object);
	DisableHUD->SetCastShadow(false);
	DisableHUD->SetVisibility(false, false); // Set it hidden by default, and only make it appear when alerting.

	// Construct the message pane here
	MessagePane = CreateEgoObject<UTextRenderComponent>("MessagePane");
	MessagePane->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	MessagePane->SetRelativeTransform(VehicleParams.Get<FTransform>("HUD", "MessagePaneLocation"));
	MessagePane->SetTextRenderColor(FColor::Black);
	MessagePane->SetText(FText::FromString(""));
	MessagePane->SetXScale(1.f);
	MessagePane->SetYScale(1.f);
	MessagePane->SetWorldSize(7); // scale the font with this
	MessagePane->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	MessagePane->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);

	// Also construct all the sounds here
	static ConstructorHelpers::FObjectFinder<USoundWave> HUDAlertSoundWave(
		TEXT("SoundWave'/Game/DReyeVR/EgoVehicle/Extra/HUDAlertSound.HUDAlertSound'"));
	HUDAlertSound = CreateDefaultSubobject<UAudioComponent>(TEXT("HUDAlert"));
	HUDAlertSound->SetupAttachment(GetRootComponent());
	HUDAlertSound->bAutoActivate = false;
	HUDAlertSound->SetSound(HUDAlertSoundWave.Object);

	static ConstructorHelpers::FObjectFinder<USoundWave> TORAlertSoundWave(
		TEXT("SoundWave'/Game/DReyeVR/EgoVehicle/Extra/TORAlertSound.TORAlertSound'"));
	TORAlertSound = CreateDefaultSubobject<UAudioComponent>(TEXT("TORAlert"));
	TORAlertSound->SetupAttachment(GetRootComponent());
	TORAlertSound->bAutoActivate = false;
	TORAlertSound->SetSound(TORAlertSoundWave.Object);

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

	// Construct all the sounds for Logitech inputs
	static ConstructorHelpers::FObjectFinder<USoundWave> CorrectSoundWave(
		TEXT("SoundWave'/Game/NDRT/NBackTask/Sounds/CorrectNBackSound.CorrectNBackSound'"));
	NBackCorrectSound = CreateDefaultSubobject<UAudioComponent>(TEXT("CorrectNBackSound"));
	NBackCorrectSound->SetupAttachment(GetRootComponent());
	NBackCorrectSound->bAutoActivate = false;
	NBackCorrectSound->SetSound(CorrectSoundWave.Object);

	static ConstructorHelpers::FObjectFinder<USoundWave> IncorrectSoundWave(
		TEXT("SoundWave'/Game/NDRT/NBackTask/Sounds/IncorrectNBackSound.IncorrectNBackSound'"));
	NBackIncorrectSound = CreateDefaultSubobject<UAudioComponent>(TEXT("IncorrectNBackSound"));
	NBackIncorrectSound->SetupAttachment(GetRootComponent());
	NBackIncorrectSound->bAutoActivate = false;
	NBackIncorrectSound->SetSound(IncorrectSoundWave.Object);
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

// N-back task exclusive methods

void AEgoVehicle::SetLetter(const FString& Letter) {
	if (NBackLetter == nullptr) return; // NBackLetter is not initialized yet

	FString MaterialPath = FString::Printf(TEXT("Material'/Game/NDRT/NBackTask/Letters/M_%s.M_%s'"), *Letter, *Letter);
	UMaterial* NewMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *MaterialPath));

	if (NewMaterial) {
		NBackLetter->SetMaterial(0, NewMaterial);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Failed to load material: %s"), *MaterialPath);
	}
}


void AEgoVehicle::RecordNBackInputs(bool BtnUp, bool BtnDown)
{
	// Check if there is any conflict of inputs. If yes, then ignore and wait for another input
	if (BtnUp && BtnDown)
	{
		return;
	}

	// Ignore input if they are not intended for NDRT (for e.g the driver presses a button by mistake when taking over)
	if (CurrVehicleStatus == VehicleStatus::TakeOver || CurrVehicleStatus == VehicleStatus::TakeOverManual)
	{
		return;
	}

	// Now, record the input if all the above conditions are satisfied
	if (BtnUp && !bWasBtnUpPressedLastFrame)
	{
		NBackResponseBuffer.Add(TEXT("M"));
	}
	else if (BtnDown && !bWasBtnDownPressedLastFrame)
	{
		NBackResponseBuffer.Add(TEXT("MM"));
	}

	// Update the previous state for the next frame
	bWasBtnUpPressedLastFrame = BtnUp;
	bWasBtnDownPressedLastFrame = BtnDown;
}


void AEgoVehicle::NBackTaskTick()
{
	// Note: This method, we assume that NBackPrompts is already initialized with randomized letters
	// Computation is only required when an input is given
	if (NBackResponseBuffer.Num() > 0)
	{
		// If multiple responses are given, just take account of the latest response
		FString LatestResponse = NBackResponseBuffer.Last();

		// Get the current game index
		int32 CurrentGameIndex = NBackRecordedResponses.Num();

		// Just a safety check so that the simulator does not crash because of index out of bounds
		// NOTE: This will only be true when NDRT is terminated, but NBackTaskTick() is still called
		// due to a small lag in ending the trial
		if (NBackPrompts.Num() == NBackRecordedResponses.Num())
		{
			return;
		}
		// Figure out if there is a match or not
		FString CorrectResponse;
		if (CurrentGameIndex < static_cast<int>(CurrentNValue))
		{
			CorrectResponse = TEXT("MM");
		} else
		{
			if (NBackPrompts[CurrentGameIndex].Equals(NBackPrompts[CurrentGameIndex - static_cast<int>(CurrentNValue)]))
			{
				CorrectResponse = TEXT("M");
			}
			else
			{
				CorrectResponse = TEXT("MM");
			}
		}

		// Check if the expected response matches the given response
		if (CorrectResponse.Equals(LatestResponse))
		{
			// Play a "correct answer" sound
			NBackCorrectSound->Play();
		}
		else
		{
			// Play an "incorrect answer" sound
			NBackIncorrectSound->Play();
		}

		// Now, add the latest response to the array just for record
		NBackRecordedResponses.Add(LatestResponse);

		// We can now clear the response buffer
		NBackResponseBuffer.Empty();

		// Check all the n-back task trials are over. If TOR is not finished, add more tasks.
		if (NBackPrompts.Num() == NBackRecordedResponses.Num())
		{
			if (CurrVehicleStatus == VehicleStatus::ResumedAutopilot)
			{
				// We can safely end the trial here
				TerminateNDRT();
			}
			else
			{
				// The driver still needs to finish/attend TOR, so add more tasks
				for (int32 i = 0; i < 10; i++)
				{
					TCHAR RandomChar = FMath::RandRange('A', 'Z');
					FString SingleLetter = FString(1, &RandomChar);
					NBackPrompts.Add(SingleLetter);
				}
			}
		} else
		{
			// Set the next letter if there are more prompts left
			SetLetter(NBackPrompts[CurrentGameIndex + 1]);
		}
	}
}

// TV show task exclusive methods

void AEgoVehicle::TVShowTaskTick()
{
	
}

// Misc methods
void AEgoVehicle::SetMessagePaneText(FString DisplayText, FColor TextColor) {
	MessagePane->SetTextRenderColor(TextColor);
	MessagePane->SetText(DisplayText);
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