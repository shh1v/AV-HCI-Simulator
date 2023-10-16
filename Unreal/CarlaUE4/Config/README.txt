NOTE: this is a weird config file bc it is custom written for research studies (Credits: HARPLab)
- use hashtags '#' for comments (supports inline)

some serialization formats:
FVector: (X=..., Y=..., Z=...) (along XYZ-axes)
FRotator: (R=..., P=..., Y=...) (for roll, pitch, yaw)
FTransform: (FVector | FRotator | FVector) = (X=..., Y=..., Z=... | R=..., P=..., Y=... | X=..., Y=..., Z=...)
bool: True or False

General section sample:
ParticipantID="P00"
LogPerformance=True
InterruptionParadigm="SelfRegulated" # enum class InterruptionParadigm { SelfRegulated, SystemRecommended, SystemInitiated}
CurrentBlock="Block1Trial1"

N-back task section sample:
NDRTTaskType="NBackTask" # enum class TaskType {NBackTask, TVShowTask};
TaskSetting="Zero" # enum class NValue{One=1, Two, Three};
Traffic="5RV" # Traffic complexity

TV-show task section sample:
NDRTTaskType="TVShowTask" # enum class TaskType {NBackTask, TVShowTask};
TaskSetting="FileMediaSource'/Game/NDRT/TVShow/MediaAssets/SampleVideo.SampleVideo'" # Source of the TV show
Traffic="3RV" # Traffic complexity