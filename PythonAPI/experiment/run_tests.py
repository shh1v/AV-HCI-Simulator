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
    world = client.get_world()

    ExperimentHelper.sleep(world, 3)
    ExperimentHelper.set_simulation_mode(client=client, synchronous_mode=False)
    while True:
        ExperimentHelper.send_vehicle_status(vehicle_status="AutoPilot")
        print(f"Received VehicleStatus", ExperimentHelper.receive_vehicle_status())
        world.tick()

if __name__ == '__main__':

    try:
        main(host='127.0.0.1', port=2000, tm_port=8000)
    except KeyboardInterrupt:
        print('\nInterrupted by keyboard, exiting...')