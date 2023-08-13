#!/usr/bin/env python

# Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab
# The University of British Columbia, BC, Canada
#
# This work is licensed under the terms of the MIT license.
# For a copy, see <https://opensource.org/licenses/MIT>.

"""Script used to run trials for the research study."""

# Standard library imports
import glob
import os
import time
import sys
import subprocess

# Local imports
import carla
from experiment_utils import ExperimentHelper
from examples.DReyeVR_utils import find_ego_vehicle

# Other library imports
import logging

def main(**kargs):
    logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO)

    global client
    client = carla.Client(kargs['host'], kargs['port'])
    client.set_timeout(10.0)

    ExperimentHelper.set_simulation_mode(client=client, synchronous_mode=False)

    # ====== Config File ======
    config_file_path = "../../Unreal/CarlaUE4/Config/ExperimentConfig.ini"  # Fixed typo in variable name
    config_file = ExperimentHelper.get_experiment_config(config_file=config_file_path)
    # =========================

    # Change the working directory to ScenarioRunner
    os.chdir("../../../scenario_runner")  # Change directory to the scenario folder

    index = 1
    sections = config_file.sections()
    while index < len(sections):
        section = sections[index]
        try:
            print(f"Scenario for trial {section} is prepared.")
            prompt = get_prompt()
            
            if prompt in ["previous", "prev"]:
                index = max(index - 1, 1)
            else:
                # python scenario_runner.py --route srunner/data/take_over_routes_debug.xml srunner/data/take_over_scenarios.json --agent srunner/autoagents/npc_agent.py --timeout --sync --output
                command = [
                    'python',
                    'scenario_runner.py',
                    '--route',
                    'srunner/data/take_over_routes_debug.xml',
                    'srunner/data/take_over_scenarios.json',
                    '--agent',
                    'srunner/autoagents/npc_agent.py',
                    '--timeout',
                    '5',
                    '--sync',
                    '--output'
                ]

                # Execute the command and connect the standard output and error streams directly
                subprocess.run(command, stderr=subprocess.STDOUT)  # Added stderr=subprocess.STDOUT
                index += 1

        except KeyError:
            print(f"KeyError: Section: {section}")
        except:
            print(f"Unexpected error: {sys.exc_info()[0]}\n{sys.exc_info()[1]}")

def get_prompt():
    prompt = input("Type 'current' to start the current scenario and 'previous' for the previous scenario: ").lower().strip()
    while prompt not in ["current", "curr", "previous", "prev"]:
        prompt = input("Invalid input: Type 'current' to start the current scenario and 'previous' for the previous scenario: ").lower().strip()
    return prompt

if __name__ == '__main__':

    try:
        main(host='127.0.0.1', port=2000, tm_port=8000)
    except KeyboardInterrupt:
        print('\nInterrupted by keyboard, exiting...')