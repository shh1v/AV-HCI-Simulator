import time
import datetime
import sys
import os
import configparser
import json
import re
import traceback

import zmq
import msgpack as serializer
import pandas as pd

# DReyeVR import
from examples.DReyeVR_utils import find_ego_vehicle

class ExperimentHelper:
    """
    This class contains helper functions for the experiment.
    """
    @staticmethod
    def set_simulation_mode(client, synchronous_mode=True, tm_synchronous_mode=True, tm_port=8000, fixed_delta_seconds=0.05):
        # Setting simulation mode
        settings = client.get_world().get_settings()
        if synchronous_mode:
            settings.synchronous_mode = True
            settings.fixed_delta_seconds = fixed_delta_seconds # 20 Hz
        else:
            settings.synchronous_mode = False
            settings.fixed_delta_seconds = None
        client.get_world().apply_settings(settings)

        # Setting Traffic Manager parameters
        traffic_manager = client.get_trafficmanager(tm_port)
        traffic_manager.set_synchronous_mode(tm_synchronous_mode)

    @staticmethod
    def sleep(world, duration):
        start_time = time.time()
        while (time.time() - start_time) < duration:
            if world.get_settings().synchronous_mode:
                world.tick()
            else:
                world.wait_for_tick()

    @staticmethod
    def get_experiment_config(config_file):
        try:
            config = configparser.ConfigParser()
            config.read(config_file)
            return config
        except:
            print(f"Unexpected error: {sys.exc_info()[0]}")
        return None



class VehicleBehaviourSuite:
    """
    The VehicleStatusSuite class is used to send and receive vehicle status between the scenario runner and carla server.
    Note that this class exclusively has class variables and static methods to ensure that only one instance of this class is created.
    This is also done to ensure that the ZMQ socket is not created multiple times.

    WARNING: This class will assume that scenario runner will send all the vehicle status to python client except
    for the "ManualMode" status, which will be send by the carla server.
    """

    # ZMQ communication subsriber variables for receiving vehicle status from carla server
    carla_subscriber_context = None
    carla_subscriber_socket = None

    # ZMQ communication subsriber variables for receiving vehicle status to scenario runner
    scenario_runner_context = None
    scenario_runner_socket = None

    # ZMQ communication publisher variables for sending vehicle control to carla server/scenario_runner
    publisher_context = None
    publisher_socket = None

    # Store the ordered vehicle status
    ordered_vehicle_status = ["Unknown", "ManualDrive", "AutoPilot", "PreAlertAutopilot", "TakeOver", "TakeOverManual", "ResumedAutopilot"]

    # Store the local vehicle status currently known
    previous_local_vehicle_status = "Unknown"
    local_vehicle_status = "Unknown"

    # log performance data variable
    log_takeover_performance_data = False
    log_interleaving_performance = False

    @staticmethod
    def _establish_CARLA_connection():
        # Create ZMQ socket for receiving vehicle status from carla server
        if (VehicleBehaviourSuite.carla_subscriber_context == None or VehicleBehaviourSuite.carla_subscriber_socket == None):
            VehicleBehaviourSuite.carla_subscriber_context = zmq.Context()
            VehicleBehaviourSuite.carla_subscriber_socket = VehicleBehaviourSuite.carla_subscriber_context.socket(zmq.SUB)
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            VehicleBehaviourSuite.carla_subscriber_socket.connect("tcp://localhost:5556")
            print("ZMQ: Connected to carla server")

        # Create ZMQ socket for receiving vehicle status from scenario runner
        if (VehicleBehaviourSuite.scenario_runner_context == None or VehicleBehaviourSuite.scenario_runner_socket == None):
            VehicleBehaviourSuite.scenario_runner_context = zmq.Context()
            VehicleBehaviourSuite.scenario_runner_socket = VehicleBehaviourSuite.scenario_runner_context.socket(zmq.SUB)
            VehicleBehaviourSuite.scenario_runner_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            VehicleBehaviourSuite.scenario_runner_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            VehicleBehaviourSuite.scenario_runner_socket.connect("tcp://localhost:5557")
            print("ZMQ: Connected to scenario runner")

        # Create ZMQ socket for sending vehicle control to carla server/scenario_runner
        if (VehicleBehaviourSuite.publisher_context == None or VehicleBehaviourSuite.publisher_socket == None):
            VehicleBehaviourSuite.publisher_context = zmq.Context()
            VehicleBehaviourSuite.publisher_socket = VehicleBehaviourSuite.publisher_context.socket(zmq.PUB)
            VehicleBehaviourSuite.publisher_socket.bind("tcp://*:5555")
            print("ZMQ: Bound to port 5555 for sending vehicle status")

    @staticmethod
    def send_vehicle_status(vehicle_status):
        """
        Send vehicle status to the carla
        """
        # Create ZMQ socket if not created
        if (VehicleBehaviourSuite.publisher_context == None or VehicleBehaviourSuite.publisher_context == None):
            VehicleBehaviourSuite._establish_CARLA_connection()

        if vehicle_status not in VehicleBehaviourSuite.ordered_vehicle_status:
            raise Exception(f"Invalid vehicle status: {vehicle_status}")
        
        VehicleBehaviourSuite.local_vehicle_status = vehicle_status
        # Send vehicle status
        message = {
            "from": "client",
            "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
            "vehicle_status": vehicle_status
        }
        serialized_message = serializer.packb(message, use_bin_type=True) # Note: The message is serialized because carla by default deserialize the message
        
        # Send the the message
        VehicleBehaviourSuite.publisher_socket.send(serialized_message)

    @staticmethod
    def receive_carla_vehicle_status():
        # Create ZMQ socket if not created
        if (VehicleBehaviourSuite.carla_subscriber_context == None or VehicleBehaviourSuite.carla_subscriber_socket == None):
            VehicleBehaviourSuite._establish_CARLA_connection()
        try:
            message = VehicleBehaviourSuite.carla_subscriber_socket.recv()
            message_dict = json.loads(message)
            # print("Received message:", message_dict)
        except zmq.Again:  # This exception is raised on timeout
            # print(f"Didn't receive any message from carla server at {datetime.datetime.now().strftime('%d/%m/%Y %H:%M:%S.%f')[:-3]}")
            return {"from": "carla",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        except Exception as e:
            # print("Unexpected error:")
            print(traceback.format_exc())
            return {"from": "carla",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        
        # message_dict will have sender name, timestamp, and vehicle status
        return message_dict
    
    @staticmethod
    def receive_scenario_runner_vehicle_status():
        # Create ZMQ socket if not created
        if (VehicleBehaviourSuite.scenario_runner_context == None or VehicleBehaviourSuite.scenario_runner_socket == None):
            VehicleBehaviourSuite._establish_CARLA_connection()
        try:
            message = VehicleBehaviourSuite.scenario_runner_socket.recv()
            message_dict = json.loads(message)
            # print("Received message:", message_dict)
        except zmq.Again:  # This exception is raised on timeout
            # print(f"Didn't receive any message from scenario runner at {datetime.datetime.now().strftime('%d/%m/%Y %H:%M:%S.%f')[:-3]}")
            return {"from": "scenario_runner",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        except Exception as e:
            # print("Unexpected error:")
            print(traceback.format_exc())
            return {"from": "scenario_runner",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        
        # message_dict will have sender name, timestamp, and vehicle status
        return message_dict
    
    @staticmethod
    def vehicle_status_tick(client, world, config_file, index):
        """
        This method should be called every tick to update the vehicle status
        This will automatically also change the behaviour of the ego vehicle required, and call logging functions if required.
        NOTE: that the status will be continously sent by the two components (carla and scenario runner) until the trial is over.
        Carla must at all times receieve the most up to date vehicle status. Scenario runner does not require the most up to date vehicle status.
        """
        # Receive vehicle status from carla server and scenario runner
        carla_vehicle_status = VehicleBehaviourSuite.receive_carla_vehicle_status()
        scenario_runner_vehicle_status = VehicleBehaviourSuite.receive_scenario_runner_vehicle_status()

        # Check if there is not vehicle status conflicts. If there is a conflict, determine the correct vehicle status
        VehicleBehaviourSuite.previous_local_vehicle_status = VehicleBehaviourSuite.local_vehicle_status
        if VehicleBehaviourSuite.ordered_vehicle_status.index(carla_vehicle_status["vehicle_status"]) <= VehicleBehaviourSuite.ordered_vehicle_status.index(scenario_runner_vehicle_status["vehicle_status"]):
            # This means that scenario runner is sending a vehicle status that comes after in a trial procedure. Hence its the most up to date vehicle status
            # NOTE: There may be a chance that carla is sending unknown, and scenario runner is sending an old address. So, check for that
            if VehicleBehaviourSuite.ordered_vehicle_status.index(VehicleBehaviourSuite.local_vehicle_status) <= VehicleBehaviourSuite.ordered_vehicle_status.index(scenario_runner_vehicle_status["vehicle_status"]):
                VehicleBehaviourSuite.local_vehicle_status = scenario_runner_vehicle_status["vehicle_status"]
                # If scenario runner has sent an updated vehicle status, let carla server know about it
                VehicleBehaviourSuite.send_vehicle_status(scenario_runner_vehicle_status["vehicle_status"])
        else:
            # This means that carla server is sending a vehicle status that comes after in a trial procedure. Hence its the most up to date vehicle status
            # NOTE: There may be a chance that scenario runner is sending unknown, and carla is sending an old address. So, check for that
            if VehicleBehaviourSuite.ordered_vehicle_status.index(VehicleBehaviourSuite.local_vehicle_status) <= VehicleBehaviourSuite.ordered_vehicle_status.index(carla_vehicle_status["vehicle_status"]):
                VehicleBehaviourSuite.local_vehicle_status = carla_vehicle_status["vehicle_status"]

        # Get the ego vehicle as it is required to change the behaviour
        ego_vehicle = find_ego_vehicle(world, verbose=False)

        # Now, execute any required behaviour based on the potential updated vehicle status
        if VehicleBehaviourSuite.previous_local_vehicle_status != VehicleBehaviourSuite.local_vehicle_status:
            # This means that the vehicle status has changed. Hence, execute the required behaviour
            if VehicleBehaviourSuite.local_vehicle_status == "PreAlertAutopilot":
                VehicleBehaviourSuite.log_interleaving_performance = True
            elif VehicleBehaviourSuite.local_vehicle_status == "TakeOver":
                DrivingPerformance.start_logging_reaction_time(True)
            elif VehicleBehaviourSuite.local_vehicle_status == "TakeOverManual":
                # Turn ego_vehicle's autopilot off. This needs to be through client side as there is a bug in carla server
                # NOTE/Update: This is now taken care by scenario runner; however, data logging is done here
                # Stop logging the eye-interleaving data
                VehicleBehaviourSuite.log_interleaving_performance = False
                # TODO: Save the eye-interleaving data
                # Start logging the performance data
                VehicleBehaviourSuite.log_takeover_performance_data = True
                DrivingPerformance.set_configuration(config_file, index)
                DrivingPerformance.start_logging_reaction_time(False)
            elif VehicleBehaviourSuite.local_vehicle_status == "ResumedAutopilot":
                # Stop logging the performance data
                VehicleBehaviourSuite.log_takeover_performance_data = False
                DrivingPerformance.save_performance_data()
                # Turn on autopilot for the DReyeVR vehicle using Traffic Manager with some parameters
                tm = client.get_trafficmanager(8005)
                # Disable auto lane change
                tm.auto_lane_change(ego_vehicle, False)
                # Change the vehicle percentage speed difference
                max_road_speed = ego_vehicle.get_speed_limit()
                percentage = (max_road_speed - 100) / max_road_speed * 100.0
                tm.vehicle_percentage_speed_difference(ego_vehicle, percentage)
                # Do not ignore any vehicles to avoid collision
                tm.ignore_vehicles_percentage(ego_vehicle, 0)
                # Finally, turn on the autopilot
                ego_vehicle.set_autopilot(True, 8005)
            elif VehicleBehaviourSuite.local_vehicle_status == "TrialOver":
                return False


        # Log the performance data if required
        if VehicleBehaviourSuite.log_takeover_performance_data:
            DrivingPerformance.performance_tick(world, ego_vehicle)

        if VehicleBehaviourSuite.log_interleaving_performance:
            EyeTracking.interleaving_performance_tick()
        
        return True
    
    @staticmethod
    def vehicle_status_terminate():
        """
        This method is used to terminate the the whole trial
        """
        # Stop logging the performance data
        DrivingPerformance.save_performance_data()

        # Peacefully terminating all the ZMQ variables
        VehicleBehaviourSuite.reset_variables()

    @staticmethod
    def reset_variables():
        VehicleBehaviourSuite.carla_subscriber_context.term()
        VehicleBehaviourSuite.carla_subscriber_socket.close()
        VehicleBehaviourSuite.scenario_runner_context.term()
        VehicleBehaviourSuite.scenario_runner_socket.close()
        VehicleBehaviourSuite.publisher_context.term()
        VehicleBehaviourSuite.publisher_socket.close()

        # Setting them None so that they are re-initialized when the next trial starts
        VehicleBehaviourSuite.carla_subscriber_context = None
        VehicleBehaviourSuite.carla_subscriber_context = None
        VehicleBehaviourSuite.scenario_runner_context = None
        VehicleBehaviourSuite.scenario_runner_socket = None
        VehicleBehaviourSuite.publisher_context = None
        VehicleBehaviourSuite.publisher_socket = None

        # Resetting the other variables
        VehicleBehaviourSuite.local_vehicle_status = "Unknown"
        VehicleBehaviourSuite.previous_local_vehicle_status = "Unknown"
        VehicleBehaviourSuite.log_performance_data = False

        # TODO: Reset all the eye-tracking and driving performance variables


class DrivingPerformance:

    # Class variables to store the reaction timestamps
    reaction_time_start_timestamp = None
    reaction_time = None

    # Store the configuration
    config_file = None
    index = -1

    # Header for all the metrics
    common_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp"]

    # Pandas dataframe to store the driving performance data
    reaction_time_df = None
    braking_input_df = None
    throttle_input_df = None
    steering_angles_df = None
    lane_offset_df = None
    speed_df = None

    # Storing map as get_map() is a heavy operation
    world_map = None

    @staticmethod
    def _init_dfs():
        def init_or_load_dataframe(attribute_name, csv_name, column_suffix):
            file_path = f"PerformanceData/{csv_name}.csv"
            
            # Check if attribute already has a value
            df = getattr(DrivingPerformance, attribute_name)
            
            # If the df attribute is None and CSV file exists, read the CSV file
            if df is None:
                if os.path.exists(file_path):
                    setattr(DrivingPerformance, attribute_name, pd.read_csv(file_path))
                else:
                    columns = DrivingPerformance.common_header + [column_suffix]
                    setattr(DrivingPerformance, attribute_name, pd.DataFrame(columns=columns))

        # Call the function for each attribute
        init_or_load_dataframe("reaction_time_df", "reaction_time", "ReactionTime")
        init_or_load_dataframe("braking_input_df", "braking_input", "BrakingInput")
        init_or_load_dataframe("throttle_input_df", "throttle_input", "AccelerationInput")
        init_or_load_dataframe("steering_angles_df", "steering_angles", "SteeringAngle")
        init_or_load_dataframe("lane_offset_df", "lane_offset", "LaneOffset")
        init_or_load_dataframe("speed_df", "speed", "Speed")

    @staticmethod
    def start_logging_reaction_time(running):
        if running and DrivingPerformance.reaction_time_start_timestamp is None:
            DrivingPerformance.reaction_time_start_timestamp = time.time()
        elif DrivingPerformance.reaction_time is None and DrivingPerformance.reaction_time_start_timestamp is not None:
            DrivingPerformance.reaction_time = time.time() - DrivingPerformance.reaction_time_start_timestamp
            if DrivingPerformance.reaction_time <= 0:
                raise Exception("Reaction time cannot be lte 0!")
        else:
            raise Exception("Reaction variables are not in the correct state!")

    @staticmethod
    def set_configuration(config_file, index):
        # config is a dictionary with the required IV values set for the trial
        DrivingPerformance.config_file = config_file
        DrivingPerformance.index = index

    @staticmethod
    def performance_tick(world, ego_vehicle):
        # Initialize the dataframes if not already
        DrivingPerformance._init_dfs()

        # Now, start logging the data of the ego vehicle
        vehicle_control = ego_vehicle.get_control()
        vehicle_location = ego_vehicle.get_location()

        # Get all the raw measurements
        timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]
        braking_input = vehicle_control.brake # NOTE: This is normalized between 0 and 1
        throttle_input = vehicle_control.throttle # NOTE: This is normalized between 0 and 1
        steering_angle = vehicle_control.steer * 450 # NOTE: The logitech wheel can rotate 450 degrees on one side.

        # Make sure we have the map
        DrivingPerformance.world_map = world.get_map() if DrivingPerformance.world_map is None else DrivingPerformance.world_map

        lane_offset = vehicle_location.distance(DrivingPerformance.world_map.get_waypoint(vehicle_location).transform.location)

        # Store the common elements for ease of use
        gen_section = DrivingPerformance.config_file[DrivingPerformance.config_file.sections()[0]]
        curr_section_name = DrivingPerformance.config_file.sections()[DrivingPerformance.index]
        curr_section = DrivingPerformance.config_file[curr_section_name]
        match = re.match(r"(Block\d+)(Trial\d+)", curr_section_name)
        common_row_elements = [gen_section["ParticipantID"],
                               gen_section["InterruptionParadigm"],
                               match.group(1) if match else "UnknownBlock",
                               match.group(2) if match else "UnknownTrial",
                               curr_section["NDRTTaskType"],
                               curr_section["TaskSetting"],
                               curr_section["Traffic"],
                               timestamp]

        # Store all the raw measurements in the dataframes
        DrivingPerformance.braking_input_df.loc[len(DrivingPerformance.braking_input_df)] = common_row_elements + [braking_input]
        DrivingPerformance.throttle_input_df.loc[len(DrivingPerformance.throttle_input_df)] = common_row_elements + [throttle_input]
        DrivingPerformance.steering_angles_df.loc[len(DrivingPerformance.steering_angles_df)] = common_row_elements + [steering_angle]
        DrivingPerformance.lane_offset_df.loc[len(DrivingPerformance.lane_offset_df)] = common_row_elements + [lane_offset]

        # Log the eye-tracking data here
        EyeTracking.TOR_performance_tick()
    
    @staticmethod
    def save_performance_data():
        # Ensure the directory exists
        if not os.path.exists("PerformanceData"):
            os.makedirs("PerformanceData")

        # Save the dataframes to the files only if they are initialized
        if DrivingPerformance.braking_input_df is not None:
            DrivingPerformance.braking_input_df.to_csv("PerformanceData/braking_input.csv", index=False)
        if DrivingPerformance.throttle_input_df is not None:
            DrivingPerformance.throttle_input_df.to_csv("PerformanceData/throttle_input.csv", index=False)
        if DrivingPerformance.steering_angles_df is not None:
            DrivingPerformance.steering_angles_df.to_csv("PerformanceData/steering_angles.csv", index=False)
        if DrivingPerformance.lane_offset_df is not None:
            DrivingPerformance.lane_offset_df.to_csv("PerformanceData/lane_offset.csv", index=False)
        if DrivingPerformance.speed_df is not None:
            DrivingPerformance.speed_df.to_csv("PerformanceData/speed.csv", index=False)


class EyeTracking:
    @staticmethod
    def establish_eye_tracking_connection():
        pass

    @staticmethod
    def interleaving_performance_tick():
        pass

    @staticmethod
    def TOR_performance_tick():
        pass