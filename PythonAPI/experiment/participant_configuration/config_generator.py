
import numpy as np
import random
import os

# Function to generate Latin square
def balanced_latin_square(array, id):
    result = []
    # Based on "Bradley, J. V. Complete counterbalancing of immediate sequential effects in a Latin square design. J. Amer. Statist. Ass.,.1958, 53, 525-528. "
    j, h = 0, 0
    for i in range(len(array)):
        if i < 2 or i % 2 != 0:
            val = j
            j += 1
        else:
            val = len(array) - h - 1
            h += 1

        idx = (val + id) % len(array)
        result.append(array[idx])

    if len(array) % 2 != 0 and id % 2 != 0:
        result.reverse()

    return result

# Function to create and write to the configuration file
def write_config_file(participant_id, paradigm, task_order, traffic_order):
    lines = []
    # Inputting general section data
    lines.append("[General]")
    lines.append(f'ParticipantID="{participant_id}"')
    lines.append('LogPerformance="True"')
    lines.append(f'InterruptionParadigm="{paradigm}"')
    lines.append('CurrentBlock="Block0Trial1"')  # This will be updated by the script
    lines.append("")  # Empty line for separation

    # These are trial blocks for n-back task trials. There is no take-over request in these blocks
    # There will be 3 blocks, one for each n-back task
    for task_load in ["One", "Two", "Three"]:
        lines.append(f"[{task_load}PMTestTrial]")
        lines.append('SkipSR="True"') # This will be used by client and CARLA to skip executing scenario runner and CARLA to run some custom behavior
        lines.append('NDRTTaskType="PatternMatchingTask"')
        lines.append(f'TaskSetting="{task_load}"')
        lines.append(f'Traffic="1RV"') # Doesn't matter what traffic is here
        lines.append("")

    # These blocks are for take-over request trials. But, the data is not important.
    # There will be two blocks, each with a randomized traffic complexity and n-back task
    NDRT_TASKS_SETTING_COPY = NDRT_TASKS_SETTING.copy()
    TRAFFIC_COMPLEXITIES_COPY = TRAFFIC_COMPLEXITIES.copy()
    for trial_num in range(1, 3):
        block_trial = f"Block0Trial{trial_num}"
        lines.append(f"[{block_trial}]")
        lines.append('SkipSR="False"')
        lines.append('NDRTTaskType="PatternMatchingTask"')
        NDRT_TASK_SETTING = random.choice(NDRT_TASKS_SETTING_COPY)
        lines.append(f'TaskSetting="{NDRT_TASK_SETTING}"')
        NDRT_TASKS_SETTING_COPY.remove(NDRT_TASK_SETTING)
        TRAFFIC_COMPLEXITY = random.choice(TRAFFIC_COMPLEXITIES_COPY)
        lines.append(f'Traffic="{TRAFFIC_COMPLEXITY}"')
        TRAFFIC_COMPLEXITIES_COPY.remove(TRAFFIC_COMPLEXITY)
        lines.append("")  # Empty line for separation

    # These blocks are for the main trial (where the data is important)
    for block_num, task in enumerate(task_order, 1):
        for trial_num, traffic in enumerate(traffic_order[block_num-1], 1):
            block_trial = f"Block{block_num}Trial{trial_num}"
            lines.append(f"[{block_trial}]")
            lines.append('SkipSR="False"') # This will be used by client and CARLA to skip executing scenario runner and CARLA to run some custom behavior
            lines.append('NDRTTaskType="PatternMatchingTask"')
            lines.append(f'TaskSetting="{NDRT_TASKS_SETTING[task]}"')
            lines.append(f'Traffic="{traffic}"')
            lines.append("")  # Empty line for separation
            
    with open(os.path.join(config_dir, f"ExperimentConfig_{participant_id}.ini"), 'w') as configfile:
        configfile.write("\n".join(lines))

# Constants
NDRT_TASKS_SETTING = ["One", "Two", "Three"]
TRAFFIC_COMPLEXITIES = ["1RV", "3RV", "5RV"]
INTERRUPTION_PARADIGMS = ["SelfRegulated", "SystemRecommended", "SystemInitiated"]
config_dir = "config_files"

# Create directory if it doesn't exist
if not os.path.exists(config_dir):
    os.makedirs(config_dir)


# Create the config files
participant_counter = 1
for i in range(0, 6): # 3 cognitive loads of the task, multiplied by two because 3 is odd and we want to balance the Latin square
    task_order = balanced_latin_square([0, 1, 2], i)
    for paradigm in INTERRUPTION_PARADIGMS:  # Alternate paradigms for every file
        # Generate randomized traffic order for each NDRT task
        traffic_order = [random.sample(TRAFFIC_COMPLEXITIES, len(TRAFFIC_COMPLEXITIES)) for _ in range(3)]
        participant_id = f"P{str(participant_counter).zfill(2)}"
        write_config_file(participant_id, paradigm, task_order, traffic_order)
        participant_counter += 1