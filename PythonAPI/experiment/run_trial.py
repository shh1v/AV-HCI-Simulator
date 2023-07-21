#!/usr/bin/env python

# Copyright (c) 2023 Okanagan Visualization & Interaction (OVI) Lab
# The University of British Columbia, BC, Canada
#
# This work is licensed under the terms of the MIT license.
# For a copy, see <https://opensource.org/licenses/MIT>.

"""Script used to run trials for the research study."""

import glob
import os
import sys
import time

import carla

import argparse
import logging

def main(**kargs):
    logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO)

    client = carla.Client(kargs['host'], kargs['port'])
    client.set_timeout(10.0)
    synchronous_master = False

    try:
        world = client.get_world()

        traffic_manager = client.get_trafficmanager(kargs['tm_port'])
        traffic_manager.set_global_distance_to_leading_vehicle(2.5)
        traffic_manager.set_respawn_dormant_vehicles(True) # See
        traffic_manager.set_hybrid_physics_mode(True) # TODO: Set DReyeVR's role to hero
        traffic_manager.set_hybrid_physics_radius(70.0)

        # Simulation Syncronization
        #
        # Run the simulation in asynchronous mode with variable time (default)
        # step when not recording driving performance or running any traffic scenarios.
        #
        # Run the simulation in synchronous mode with fixed time
        # step when not recording driving performance or running any traffic scenarios.
    
    finally:
        pass

if __name__ == '__main__':

    try:
        main()
    except KeyboardInterrupt:
        pass
    finally:
        print('\ndone.')
