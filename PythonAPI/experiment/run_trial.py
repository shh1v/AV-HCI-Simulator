#!/usr/bin/env python

# Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab
# The University of British Columbia, BC, Canada
#
# This work is licensed under the terms of the MIT license.
# For a copy, see <https://opensource.org/licenses/MIT>.

"""Script used to run trials for the research study."""

# Standard library imports
import os
import subprocess
import multiprocessing
import argparse

import traceback
import time

# Local imports
import os
import carla
from experiment_utils import ExperimentHelper, VehicleBehaviourSuite, HardwareSuite

# Other library imports
import logging

def main(args):
    logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO)
    
    ############# HARD CODED CONFIGURATION FILE PATH #############
    config_file_path = "D:/CarlaDReyeVR/carla/Unreal/CarlaUE4/Config/ExperimentConfig.ini"
    # config_file_path = "D:/CarlaDReyeVR/carla/Build/UE4Carla/c8ffee5-dirty/WindowsNoEditor/CarlaUE4/Config/ExperimentConfig.ini"
    ##############################################################

    # Set the experiment configuration file based on the participant ID
    participant_id = input("Enter participant ID: ").strip().upper()
    ExperimentHelper.copy_experiment_config(from_file_path=f'config_files/ExperimentConfig_{participant_id}.ini', to_file_path=config_file_path)

    # Get config file and read the contents of the file
    config_file = ExperimentHelper.get_experiment_config(config_file=config_file_path)

    # Change directory to scenario runner
    os.chdir("../../../scenario_runner")

    index = 1
    sections = config_file.sections()
    while index < len(sections):
        section = sections[index]
        
        print("=================================")
        print(f"Trial [{section}] is configured.")
        for key, value in config_file[section].items():
            print(f"{key}: {value}")
        print("=================================")

        # Ask whether to run the current trial, go back to the previous trial, or skip the current trial
        action = get_prompt()
        
        if action == "previous":
            index = max(index - 1, 1)
        elif action == "skip":
            index += 1
            continue
        else: # current
            # First set the current block name in the configuration file
            ExperimentHelper.update_current_block(config_file_path, section)

            # Now, run the scenario runner if SkipSR is False
            if config_file[section]["SkipSR"].strip("\"") == "False":
                command = [
                    'python', 'scenario_runner.py',
                    '--route', 'srunner/data/take_over_routes.xml', 'srunner/data/traffic_complexity_{}.json'.format(config_file[section]["Traffic"].strip("\"")), '0',
                    '--agent', 'srunner/autoagents/npc_agent.py',
                    '--timeout', '5',
                    '--sync', '--output'
                ]
                try:
                    # Run the necessary parallel modules                    
                    parallel_modules = multiprocessing.Process(target=run_parallel_modules,
                                                                     args=(args.host, args.port, args.worker_threads, config_file, index))
                    parallel_modules.start()

                    # Directly run the scenario in the main flow
                    subprocess.run(command, stderr=subprocess.STDOUT)

                    # Wait for the vehicle status process to terminate. This will be done when CARLA sends a signal that the trial is over.
                    parallel_modules.join()
                except (TypeError, ValueError, AttributeError) as e:      
                    print(f"{type(e).__name__} occurred: {e}")
                    print(traceback.format_exc())

                except Exception as e:
                    print(f"An unexpected error of type {type(e).__name__} occurred: {e}")
                    print(traceback.format_exc())
            else:
                # Wait for three seconds so that the user has activated UE window
                time.sleep(3)
                # We need to reload once so that CARLA can read the updated configuration file with the current block name
                client = carla.Client(args.host, args.port, worker_threads=args.worker_threads)
                client.set_timeout(10.0)
                client.reload_world()
                print("Reloaded world.")

            
            index += 1

def run_parallel_modules(host, port, threads, config_file, index):
    try:
        # Connect to the server
        client = carla.Client(host, port, worker_threads=threads)
        client.set_timeout(10.0)
        world = client.get_world()

        # Set synchronous mode as the scenario runner is also running in synchronous mode
        # NOTE: The provided tm_port is set to 8005 instead of 8000 as the scenario runner is using 8000
        ExperimentHelper.set_simulation_mode(client=client, synchronous_mode=False, tm_synchronous_mode=False, tm_port=8005)

        # Start checking vehicle status/behaviour and sending hardware data
        while True:

            # Check the vehicle status and execute any required behaviour. Also return a bool that tells you if the trial is over.
            trial_status = VehicleBehaviourSuite.vehicle_status_tick(client, world, config_file, index)

            # If the trial is over, terminate the process
            if not trial_status:
                HardwareSuite.terminate_hardware_connection() # Terminate UDP connection
                break

            # If the trial is still running, send hardware data
            HardwareSuite.send_hardware_data()
            
    except Exception as e:
        print("Exception occurred in vehicle status check thread:", e)
        print(traceback.format_exc())

def get_prompt():
    choices = ["current", "previous", "skip"]
    prompt = input("Choices: 'current' (start the current scenario), 'previous' (go back), 'skip' (skip current scenario). Enter choice: ").lower().strip()
    
    while prompt not in choices:
        prompt = input(f"Invalid input. Choose from {choices}. Enter choice: ").lower().strip()

    return prompt

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Run trials for research study.")
    parser.add_argument("--host", default="127.0.0.1", help="Host IP for Carla Client")
    parser.add_argument("--port", type=int, default=2000, help="Port for Carla Client") # WARNING: This port is used for secondary client connection
    parser.add_argument("--tm_port", type=int, default=8000, help="Traffic Manager port for Carla Client")
    parser.add_argument("--worker_threads", type=int, default=0, help="Worker Threads for Carla Client")

    args = parser.parse_args()

    try:
        main(args)
    except KeyboardInterrupt:
        print("Exiting the script.")
