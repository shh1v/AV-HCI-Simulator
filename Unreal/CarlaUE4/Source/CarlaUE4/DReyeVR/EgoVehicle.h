#pragma once

#include "Camera/CameraComponent.h"                   // UCameraComponent
#include "Carla/Actor/DReyeVRCustomActor.h"           // ADReyeVRCustomActor
#include "Carla/Game/CarlaEpisode.h"                  // CarlaEpisode
#include "Carla/Sensor/DReyeVRData.h"                 // DReyeVR namespace
#include "Carla/Vehicle/CarlaWheeledVehicle.h"        // ACarlaWheeledVehicle
#include "Carla/Vehicle/WheeledVehicleAIController.h" // AWheeledVehicleAIController
#include "Components/AudioComponent.h"                // UAudioComponent
#include "Components/InputComponent.h"                // InputComponent
#include "Components/PlanarReflectionComponent.h"     // Planar Reflection
#include "Components/SceneComponent.h"                // USceneComponent
#include "CoreMinimal.h"                              // Unreal functions
#include "DReyeVRGameMode.h"                          // ADReyeVRGameMode
#include "DReyeVRUtils.h"                             // GeneralParams.Get
#include "EgoSensor.h"                                // AEgoSensor
#include "FlatHUD.h"                                  // ADReyeVRHUD
#include "ImageUtils.h"                               // CreateTexture2D
#include "WheeledVehicle.h"                           // VehicleMovementComponent
#include "RetrieveDataRunnable.h"                     // Retreive all the data in parallel
#include <zmq.hpp>                                    // ZeroMQ Plugin
#include "Sockets.h"                                  // Socket programming
#include "SocketSubsystem.h"						  // Socket programming
#include "Networking.h"								  // Socket programming
#include "DcTypes.h"                                  // FDcResult
#include "DataConfigDatatypes.h"                      // FSurfaceData, FVehicleStatusData
#include <stdio.h>
#include "EgoVehicle.generated.h"

class ADReyeVRGameMode;
class ADReyeVRPawn;

UCLASS()
class CARLAUE4_API AEgoVehicle : public ACarlaWheeledVehicle
{
    GENERATED_BODY()

    friend class ADReyeVRPawn;

  public:
    // Sets default values for this pawn's properties
    AEgoVehicle(const FObjectInitializer &ObjectInitializer);

    void ReadConfigVariables();
    void ReadExperimentVariables();

    virtual void Tick(float DeltaTime) override; // called automatically

    // Setters from external classes
    void SetGame(ADReyeVRGameMode *Game);
    ADReyeVRGameMode *GetGame();
    void SetPawn(ADReyeVRPawn *Pawn);
    void SetVolume(const float VolumeIn);

    // Getters
    const FString &GetVehicleType() const;
    FVector GetCameraOffset() const;
    FVector GetCameraPosn() const;
    FVector GetNextCameraPosn(const float DeltaSeconds) const;
    FRotator GetCameraRot() const;
    const UCameraComponent *GetCamera() const;
    UCameraComponent *GetCamera();
    const DReyeVR::UserInputs &GetVehicleInputs() const;
    const class AEgoSensor *GetSensor() const;
    const struct ConfigFile &GetVehicleParams() const;

    // autopilot API
    void SetAutopilot(const bool AutopilotOn);
    bool GetAutopilotStatus() const;
    /// TODO: add custom routes for autopilot

    // Play sounds
    void PlayGearShiftSound(const float DelayBeforePlay = 0.f) const;
    void PlayTurnSignalSound(const float DelayBeforePlay = 0.f) const;

    // Camera view
    size_t GetNumCameraPoses() const;                // how many diff poses?
    void SetCameraRootPose(const FTransform &Pose);  // give arbitrary FTransform
    void SetCameraRootPose(const FString &PoseName); // index into named FTransform
    void SetCameraRootPose(size_t PoseIdx);          // index into ordered FTransform
    const FTransform &GetCameraRootPose() const;
    void BeginThirdPersonCameraInit();
    void NextCameraView();
    void PrevCameraView();

  protected:
    // Called when the game starts (spawned) or ends (destroyed)
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginDestroy() override;

    // custom configuration file for vehicle-specific parameters
    struct ConfigFile VehicleParams;

    // World variables
    class UWorld *World;

  private:
    template <typename T> T *CreateEgoObject(const FString &Name, const FString &Suffix = "");
    FString VehicleType; // initially empty (set in GetVehicleType())

  private: // camera
    UPROPERTY(Category = Camera, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class USceneComponent *VRCameraRoot;
    UPROPERTY(Category = Camera, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UCameraComponent *FirstPersonCam;
    void ConstructCameraRoot();                 // needs to be called in the constructor
    FTransform CameraPose, CameraPoseOffset;    // camera pose (location & rotation) and manual offset
    TMap<FString, FTransform> CameraTransforms; // collection of named transforms from params
    TArray<FString> CameraPoseKeys;             // to iterate through them
    size_t CurrentCameraTransformIdx = 0;       // to index in CameraPoseKeys which indexes into CameraTransforms
    bool bCameraFollowHMD = true; // disable this (in params) to replay without following the player's HMD (replay-only)

  private: // sensor
    class AEgoSensor *GetSensor();
    void ReplayTick();
    void InitSensor();
    // custom sensor helper that holds logic for extracting useful data
    TWeakObjectPtr<class AEgoSensor> EgoSensor; // EgoVehicle should have exclusive access (TODO: unique ptr?)
    void UpdateSensor(const float DeltaTime);

  private: // pawn
    class ADReyeVRPawn *Pawn = nullptr;

  private: // mirrors
    void ConstructMirrors();
    struct MirrorParams
    {
        bool Enabled;
        FTransform MirrorTransform, ReflectionTransform;
        float ScreenPercentage;
        FString Name;
        void Initialize(class UStaticMeshComponent *SM, class UPlanarReflectionComponent *Reflection,
                        class USkeletalMeshComponent *VehicleMesh);
    };
    struct MirrorParams RearMirrorParams, LeftMirrorParams, RightMirrorParams;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *RightMirrorSM;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UPlanarReflectionComponent *RightReflection;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *LeftMirrorSM;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UPlanarReflectionComponent *LeftReflection;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *RearMirrorSM;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UPlanarReflectionComponent *RearReflection;
    // rear mirror chassis (dynamic)
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *RearMirrorChassisSM;
    FTransform RearMirrorChassisTransform;

  private: // AI controller
    class AWheeledVehicleAIController *AI_Player = nullptr;
    void InitAIPlayer();
    bool bAutopilotEnabled = false;
    void TickAutopilot();

  private: // inputs
    /// NOTE: since there are so many functions here, they are defined in EgoInputs.cpp
    struct DReyeVR::UserInputs VehicleInputs; // struct for user inputs
    // Vehicle control functions (additive for multiple input modalities (kbd/logi))
    void AddSteering(float SteeringInput);
    void AddThrottle(float ThrottleInput);
    void AddBrake(float BrakeInput);
    void TickVehicleInputs();
    bool bReverse;

    // "button presses" should have both a "Press" and "Release" function
    // And, if using the logitech plugin, should also have an "is rising edge" bool so they can only
    // be pressed after being released (cant double press w/ no release)
    // Reverse toggle
    void PressReverse();
    void ReleaseReverse();
    bool bCanPressReverse = true;
    // turn signals
    bool bEnableTurnSignalAction = true; // tune with "EnableTurnSignalAction" in config
    // left turn signal
    void PressTurnSignalL();
    void ReleaseTurnSignalL();
    float LeftSignalTimeToDie; // how long until the blinkers go out
    bool bCanPressTurnSignalL = true;
    // right turn signal
    void PressTurnSignalR();
    void ReleaseTurnSignalR();
    float RightSignalTimeToDie; // how long until the blinkers go out
    bool bCanPressTurnSignalR = true;
    void CheckTORButtonPress(bool bABXY_A, bool bABXY_B, bool bABXY_X, bool bABXY_Y);
    bool bTakeOverPress = false;

    // Camera control functions (offset by some amount)
    void CameraPositionAdjust(const FVector &Disp);
    void CameraFwd();
    void CameraBack();
    void CameraLeft();
    void CameraRight();
    void CameraUp();
    void CameraDown();
    void CameraPositionAdjust(bool bForward, bool bRight, bool bBackwards, bool bLeft, bool bUp, bool bDown);

    // changing camera views
    void PressNextCameraView();
    void ReleaseNextCameraView();
    bool bCanPressNextCameraView = true;
    void PressPrevCameraView();
    void ReleasePrevCameraView();
    bool bCanPressPrevCameraView = true;

    // Vehicle parameters
    float ScaleSteeringInput;
    float ScaleThrottleInput;
    float ScaleBrakeInput;

  private: // sounds
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent *GearShiftSound; // nice for toggling reverse
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent *TurnSignalSound; // good for turn signals
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent *EgoEngineRevSound; // driver feedback on throttle
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent *EgoCrashSound; // crashing with another actor
    void ConstructEgoSounds();            // needs to be called in the constructor
    virtual void TickSounds(float DeltaSeconds) override;

    // manually overriding these from ACarlaWheeledVehicle
    void ConstructEgoCollisionHandler(); // needs to be called in the constructor
    UFUNCTION()
    void OnEgoOverlapBegin(UPrimitiveComponent *OverlappedComp, AActor *OtherActor, UPrimitiveComponent *OtherComp,
                           int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult);

  private: // gamemode/level
    void TickGame(float DeltaSeconds);
    class ADReyeVRGameMode *DReyeVRGame;

  private: // dashboard
    // Text Render components (Like the HUD but part of the mesh and works in VR)
    void ConstructDashText(); // needs to be called in the constructor
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent *Speedometer;
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent *TurnSignals;
    float TurnSignalDuration; // time in seconds
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent *GearShifter;
    void UpdateDash();
    float SpeedometerScale = CmPerSecondToXPerHour(true); // scale from CM/s to MPH or KPH (default MPH)

  private: // steering wheel
    UPROPERTY(Category = Steering, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *SteeringWheel;
    void ConstructSteeringWheel(); // needs to be called in the constructor
    void DestroySteeringWheel();
    void TickSteeringWheel(const float DeltaTime);
    float MaxSteerAngleDeg;
    float MaxSteerVelocity;
    float SteeringAnimScale;
    bool bWheelFollowAutopilot = true; // disable steering during autopilot and follow AI
    // wheel face buttons
    void InitWheelButtons();
    void UpdateWheelButton(ADReyeVRCustomActor *Button, bool bEnabled);
    class ADReyeVRCustomActor *Button_ABXY_A, *Button_ABXY_B, *Button_ABXY_X, *Button_ABXY_Y;
    class ADReyeVRCustomActor *Button_DPad_Up, *Button_DPad_Down, *Button_DPad_Left, *Button_DPad_Right;
    bool bInitializedButtons = false;
    const FLinearColor ButtonNeutralCol = 0.2f * FLinearColor::White;
    const FLinearColor ButtonPressedCol = 1.5f * FLinearColor::White;
    // wheel face autopilot indicator
    void InitAutopilotIndicator();
    void TickAutopilotIndicator(bool);
    class ADReyeVRCustomActor *AutopilotIndicator;
    bool bInitializedAutopilotIndicator = false;

public: // Thread to run all the ZMQ networking stuff
    class RetrieveDataRunnable *GetDataRunnable;

public: // Game signaling
    enum class VehicleStatus { ManualDrive, Autopilot, PreAlertAutopilot, TakeOver, TakeOverManual, ResumedAutopilot, TrialOver, Unknown };
    /*
     * Vehicle status descriptions:
     * ManualDrive: The vehicle is driving manually.
     * AutoPilot: The vehicle is driving by itself. No supervision is required by the driver.
     * PreAlertAutopilot: AutoPilot is running, but the driver is expected to take-over soon as a TOR is issued.
     * TakeOver: TOR is issued and the driver is expected to take-over. ADS is still available and running
     * TakeOverManual: Driver has given sufficient input, automation is turned off, and the vehicle is fully controlled by the driver.
     * Unknown: Status not known. This status has no functionality. Mainly used for error handling and waiting for status to be received.
     */
    void UpdateVehicleStatus(VehicleStatus NewStatus); // Change the vehicle status by ensuring old status is robust and send to client
    void SendCurrVehicleStatus(); // Send the locally stored current vehicle status
    FDcResult RetrieveVehicleStatus(); // Update the vehicles status using ZeroMQ PUB-SUB
    VehicleStatus GetCurrVehicleStatus();
    VehicleStatus GetOldVehicleStatus();

private: // Game signaling
    VehicleStatus CurrVehicleStatus = VehicleStatus::Unknown; // This stores the current tick's vehicle status
    VehicleStatus OldVehicleStatus = VehicleStatus::Unknown; // This stores the previous tick's vehicle status
    bool bZMQVehicleStatusConnection = false; // Stores if connection is established
    bool bZMQVehicleStatusDataRetrieve = false; // Stores if connection is established
    zmq::context_t* VehicleStatusReceiveContext;    // Stores the receive context of the zmq process
    zmq::context_t* VehicleStatusSendContext;    // Stores the send context of the zmq process
    zmq::socket_t* VehicleStatusSubscriber; // Pointer to the receive socket to listen to python client
    zmq::socket_t* VehicleStatusPublisher; // Pointer to the send socket to listen to python client
    FVehicleStatusData VehicleStatusData; // Stores the vehicle status dict sent by python client

public:
    bool EstablishVehicleStatusConnection(); // Establish connection to Client ZMQ
    bool TerminateVehicleStatusConnection(); // Terminate connection to Client ZMQ

  private: // Non-Driving-Related Task
	bool IsSkippingSR = false; // Stores if the current trial is a test trial. True is its a test trial.
    enum class TaskType {NBackTask, PatternMatchingTask, TVShowTask}; // Change the behaviour of the NDRT based on the task type provided
    // The following value will determine the 
    TaskType CurrTaskType = TaskType::NBackTask; // Should be dynamically retrieved from a config file
    float NDRTStartLag = 2.0f; // Lag after which the NDRT starts (on autopilot or resumed autopilot)
    float AutopilotStartTimestamp = -1; // Store when the autopilot started
    float ResumedAutopilotStartTimestamp = -1; // Store when the autopilot is resumed

    enum class InterruptionParadigm { SelfRegulated, SystemRecommended, SystemInitiated}; // Change the behaviour of the NDRT based on the task type provided
    // The following value will determine the NDRT interaction during pre-alert time
    InterruptionParadigm CurrInterruptionParadigm = InterruptionParadigm::SelfRegulated; // Should be dynamically retrieved from a config file
    int32 InterruptionAlertFrequency = 0;
    bool bIsAlertFreqCounted = false;
    // Primary Display: Present the NDRT; Secondary Display: Present the alerts
    UPROPERTY(Category = NDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* PrimaryHUD;
    UPROPERTY(Category = NDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* SecondaryHUD;
    UPROPERTY(Category = NDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* DisableHUD;

    // Optional Common task methods
    UPROPERTY(VisibleAnywhere)
    class UWidgetComponent* ProgressBarWidgetComponent; // Component that manipulates the progress bar for various tasks
    void UpdateProgressBar(float NewProgressValue);

    //Pattern Matching Task
    enum class PMLines{One=1, Two=2, Three=3};
    PMLines CurrentPMLines = PMLines::One;
    int32 TotalPMTasks = 30;
    TArray<bool> PMResponses;
    float PMTaskLimit = 5.0f;
    UPROPERTY(Category = PMNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* PMControlsInfo;
    UPROPERTY(Category = PMNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* PMPatternPrompt;
    TArray<FString> PMCurrentPattern;
    UPROPERTY(Category = PMNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    TArray<UStaticMeshComponent*> PMPatternKeys;
    TArray<FString> PMCurrentSequence;
    UPROPERTY(Category = PMNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    TArray<UStaticMeshComponent*> PMSequenceKeys;
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent* PMCorrectSound;   // Correct answer sound
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent* PMIncorrectSound;   // Incorrect answer sound

    void PatternMatchTaskTick(); // PM Task tick
    void ConstructPMElements(); // Construct the static meshes to present the PM sequence
    void SetPseudoRandomPattern(bool GenerateNewSequence, bool SetKeys);
    void SetRandomSequence(bool GenerateNewSequence, bool SetKeys);

    float PMTrialStartTimestamp; // This will store the time stamp of the start of the trial
    bool IsPMResponseGiven = false; // Stores whether an input was given for a specific trial
    TArray<FString> PMCorrectResponses; // Store the correct response for every new sequence generated
    TArray<FString> PMUserResponses; // Stores the accuracy of each task filled by the user
    TArray<FString> PMUserResponseTimestamp; // Stores the timestamp when the user gives an input
    TArray<FString> PMResponseBuffer; // Store the PM responses temporarily in this array. Then do post-analysis
    void RecordPMInputs(bool BtnUp, bool BtnDown); // Called in every tick to check if PM response was given
    bool bWasPMBtnUpPressedLastFrame = false; // Store the last input from the Logitech joystick
    bool bWasPMBtnDownPressedLastFrame = false; // Store the last input from the Logitech joystick

    // N-back task
    enum class NValue{One=1, Two=2, Three=3}; // Change n-back task functionality based on the n-value provided
    NValue CurrentNValue = NValue::One;
    int32 TotalNBackTasks = -1; // Total trials of n-back task. Possibly Retrieve this value from the the configuration file.
    TArray<FString> NBackPrompts;   // Store the n-back task prompts in this array
    TArray<FString> NBackRecordedResponses; // Store the "considered" responses in this array
    TArray<FString> NBackResponseTimestamp; // Store the timestamp of when the response was registered by the simulator
    TArray<FString> NBackResponseBuffer; // Store the n-back responses temporarily in this array. Then do post-analysis
    float OneBackTimeLimit = 3.0; // Time limit for 1-back task (This will be used to define the other limits)
    float NBackTrialStartTimestamp; // This will store the time stamp of the start of the trial
    bool IsNBackResponseGiven = false; // Stores whether an input was given for a specific trial
    UPROPERTY(Category = NBackNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* NBackLetter;
    UPROPERTY(Category = NBackNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* NBackControlsInfo;
    UPROPERTY(Category = NBackNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* NBackTitle;
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent* NBackCorrectSound;   // Correct answer sound
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent* NBackIncorrectSound;   // Incorrect answer sound
    void RecordNBackInputs(bool BtnUp, bool BtnRight); // Record the button events for the n-back task
    bool bWasNBBtnUpPressedLastFrame = false; // Store the last input from the Logitech joystick
    bool bWasNBBtnDownPressedLastFrame = false; // Store the last input from the Logitech joystick
    void NBackTaskTick(); // Update the n-back task in every tick

    void ConstructNBackElements(); // Construct the static meshes to present the N-back task components
    void SetNBackTitle(int32 NBackValue); // Set the n-back title with the correct n-value.
    void SetLetter(const FString& letter); // Set a new letter in the n-back task.

    // TV show task
    UPROPERTY(Category = TVShowNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent* MediaPlayerMesh;
    UPROPERTY(Category = TVShowNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UMediaSoundComponent* MediaSoundComponent;
    UPROPERTY(Category = TVShowNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UFileMediaSource* MediaPlayerSource;
    UPROPERTY(Category = TVShowNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UMediaPlayer* MediaPlayer;
    UPROPERTY(Category = TVShowNDRT, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UMaterial* MediaPlayerMaterial;
    FString MediaSourcePath = TEXT("FileMediaSource'/Game/NDRT/TVShow/MediaAssets/SampleVideo.SampleVideo'"); // Stores the path to the video file
    void TVShowTaskTick(); // Update the tv show task in every tick

    void ConstructTVShowElements(); // Construct the static meshes to present the N-back task components

    // Misc
    void ConstructHUD();    // deploy the head-up display static meshes

  public: // Non-Driving-Related Task
    void SetupNDRT();    // Setup the NDRT (head-up display)
    void StartNDRT();    // Start the NDRT when automation is activated
    void ToggleNDRT(bool active);   // Pause/Resume HUD
    void ToggleAlertOnNDRT(bool active); // Present a visual and audio alert on HUD
    void SetInteractivityOfNDRT(bool interactivity);    // Completely hide/appear the head-up display (and subsequently pase the NDRT if not done already)
    void TerminateNDRT();   // Destroy the NDRT head-up display, terminate NDRT and save data
    void TickNDRT(); // Update the NDRT on every tick based on its individual implementation
    void RecordInput(); // Record the input from the Logitech steering wheel

private:
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent* TORAlertSound;   // For TOR alert sound
    bool bIsTORAlertPlaying = false;
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent* HUDAlertSound;           // For interruption alert sound
    bool bIsAlertOnNDRTOn = false;
    class UAudioComponent* PreAlertSound;           // For pre-alert notification sound
    bool bIsPreAlertOn = false;

private: // External hardware
    FHardwareData HardwareData; // Stores all the python hardware client stream data.

private: // Eye-tracking
    bool bUDPEyeConnection = false; // True if connection is established
    bool bUDPEyeDataRetrieve = false; // True if data is retrieved from ZMQ
    FSocket* ListenSocket; // Used for UDP communication
    FIPv4Endpoint Endpoint; // Used for UDP communication

public: // Eye-tracking
    bool IsUserGazingOnHUD(); // Returns true if the gaze is on the HUD
    float GazeOnHUDTime(); // Returns the time the user has been looking at the HUD
    bool EstablishEyeTrackerConnection(); // Establish connection to a TCP port for PUBLISH-SUBSCRIBE protocol communication
    bool TerminateEyeTrackerConnection(); // Terminate connection to a TCP port for PUBLISH-SUBSCRIBE protocol communication
    void RetrieveOnSurf(); // Retrieved the OnSurf value from the python hardware stream client
private:
    float GazeOnHUDTimestamp; // Store the timestamp at which the driver starts looking at the HUD
    bool bLastOnSurfValue = false; // Stores the previous OnSurf value
    bool bLatestOnSurfValue = false; // Stores the latest OnSurf value retrieved from the python hardware stream client
    float GazeShiftThreshTimeStamp = 0; // For checking if the OnSurf value change time limit threshold has been reached.
    void SetHUDTimeThreshold(float Threshold); // Set the GazeOnHUDTimeConstraint
    float GazeOnHUDTimeConstraint = 2; // Time after which alert is displayed in sys-recommended and sys-initiated modes

  private: // other
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent* MessagePane;
    void SetMessagePaneText(FString DisplayText, FColor TextColor);
    void DebugLines() const;
    bool bDrawDebugEditor = false;

public: // HUD Debugger
    void ConstructHUDDebugger();
    void HUDDebuggerTick();
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent* OnSurfValue;
    class UTextRenderComponent* HUDGazeTime;
};
