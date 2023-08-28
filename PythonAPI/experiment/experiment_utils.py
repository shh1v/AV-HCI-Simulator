import time
import datetime
import sys
import os
import configparser
import json

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
    def set_simulation_mode(client, synchronous_mode=True, fixed_delta_seconds=0.05):
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
        traffic_manager = client.get_trafficmanager()
        traffic_manager.set_synchronous_mode(synchronous_mode)

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

    # Store the local vehicle status currently known
    local_vehicle_status = "Unknown"

    @staticmethod
    def _establish_CARLA_connection():
        # Create ZMQ socket for receiving vehicle status from carla server
        if (VehicleBehaviourSuite.carla_subscriber_context == None or VehicleBehaviourSuite.subscriber_socket == None):
            VehicleBehaviourSuite.carla_subscriber_context = zmq.Context()
            VehicleBehaviourSuite.carla_subscriber_socket = VehicleBehaviourSuite.subscriber_context.socket(zmq.SUB)
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            VehicleBehaviourSuite.carla_subscriber_socket.connect("tcp://localhost:5556")

        # Create ZMQ socket for receiving vehicle status from scenario runner
        if (VehicleBehaviourSuite.carla_subscriber_context == None or VehicleBehaviourSuite.subscriber_socket == None):
            VehicleBehaviourSuite.scenario_runner_context = zmq.Context()
            VehicleBehaviourSuite.scenario_runner_socket = VehicleBehaviourSuite.subscriber_context.socket(zmq.SUB)
            VehicleBehaviourSuite.scenario_runner_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            VehicleBehaviourSuite.scenario_runner_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            VehicleBehaviourSuite.scenario_runner_socket.connect("tcp://localhost:5557")

        # Create ZMQ socket for sending vehicle control to carla server/scenario_runner
        if (VehicleBehaviourSuite.publisher_context == None or VehicleBehaviourSuite.publisher_socket == None):
            VehicleBehaviourSuite.publisher_context = zmq.Context()
            VehicleBehaviourSuite.publisher_socket = VehicleBehaviourSuite.publisher_context.socket(zmq.PUB)
            VehicleBehaviourSuite.publisher_socket.bind("tcp://*:5555")

    @staticmethod
    def send_vehicle_status(vehicle_status):
        """
        Send vehicle status to the carla
        """
        # Create ZMQ socket if not created
        if (VehicleBehaviourSuite.publisher_context == None or VehicleBehaviourSuite.publisher_context == None):
            VehicleBehaviourSuite._establish_CARLA_connection()

        if vehicle_status not in ["ManualDrive", "AutoPilot", "PreAlertAutopilot", "TakeOver"]:
            raise Exception(f"Invalid vehicle status: {vehicle_status}")
        
        VehicleBehaviourSuite.local_vehicle_status = vehicle_status
        # Send vehicle status
        message = {
            "from": "client",
            "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
            "vehicle_status": vehicle_status
        }
        serialized_message = serializer.packb(message, use_bin_type=True)
        
        # Send the the message
        VehicleBehaviourSuite.publisher_socket.send(serialized_message)
        print(f"Sent vehicle status: {vehicle_status}")

    @staticmethod
    def receive_carla_vehicle_status():
        # Create ZMQ socket if not created
        if (VehicleBehaviourSuite.carla_subscriber_socket == None or VehicleBehaviourSuite.carla_subscriber_socket == None):
            VehicleBehaviourSuite._establish_CARLA_connection()
        try:
            message = VehicleBehaviourSuite.carla_subscriber_socket.recv()
            message_dict = json.loads(message)
        except Exception as e:
            print(f"Exception: {e}")
            return {"from": "carla",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        
        # message_dict will have sender name, timestamp, and vehicle status
        return message_dict
    
    @staticmethod
    def receive_scenario_runner_vehicle_status():
        # Create ZMQ socket if not created
        if (VehicleBehaviourSuite.scenario_runner_socket == None or VehicleBehaviourSuite.scenario_runner_socket == None):
            VehicleBehaviourSuite._establish_CARLA_connection()
        try:
            message = VehicleBehaviourSuite.scenario_runner_socket.recv()
            message_dict = json.loads(message)
        except Exception as e:
            print(f"Exception: {e}")
            return {"from": "scenario_runner",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        
        # message_dict will have sender name, timestamp, and vehicle status
        return message_dict
    
    @staticmethod
    def vehicle_status_tick(world, config):
        """
        This method should be called every tick to update the vehicle status
        This will automatically also change the behaviour of the ego vehicle required.
        """
        # Receive vehicle status from carla server and scenario runner
        carla_vehicle_status = VehicleBehaviourSuite.receive_carla_vehicle_status()
        scenario_runner_vehicle_status = VehicleBehaviourSuite.receive_scenario_runner_vehicle_status()

        # Check if the received vehicle status is valid based on the publishers individual role in the experiment
        if carla_vehicle_status["vehicle_status"] not in ["Unknown", "ManualDrive"]:
            raise Exception(f"Carla server does not have permission to send vehicle status: {carla_vehicle_status['vehicle_status']}")
        if scenario_runner_vehicle_status["vehicle_status"] not in ["Unknown", "AutoPilot", "PreAlertAutopilot", "TakeOver"]:
            raise Exception(f"Scenario runner does not have permission to send vehicle status: {scenario_runner_vehicle_status['vehicle_status']}")

        # Check if there is not vehicle status conflicts
        if carla_vehicle_status["vehicle_status"] != "Unknown" and scenario_runner_vehicle_status["vehicle_status"] != "Unknown":
            raise Exception(f"Both carla server and scenario runner are sending vehicle status: {carla_vehicle_status['vehicle_status']} and {scenario_runner_vehicle_status['vehicle_status']}")

        # Get the ego vehicle as it is required to change the behaviour
        ego_vehicle = find_ego_vehicle(world)
 
        # Now, we can update the local vehicle status and execute any required behaviour for the experiment
        if carla_vehicle_status["vehicle_status"] != "Unknown": #  i.e., carla_vehicle_status["vehicle_status"] == "ManualDrive"
            # This means that the carla server is sending the vehicle status, and it has to be "ManualDrive"
            old_vehicle_status = VehicleBehaviourSuite.local_vehicle_status
            VehicleBehaviourSuite.local_vehicle_status = "ManualDrive"

            if old_vehicle_status == "TakeOver":
                # Turn ego_vehicle's autopilot off. This needs to be through client side as there is a bug in carla server
                ego_vehicle.set_autopilot(False)
                # TODO: Start logging the driving performance data here

        elif scenario_runner_vehicle_status["vehicle_status"] != "Unknown":
            # This means that the scenario runner is sending the vehicle status, and it has to be "AutoPilot", "PreAlertAutopilot", or "TakeOver"
            # Turn ego_vehicle's autopilot on. This needs to be through client side as there is a bug in carla server
            VehicleBehaviourSuite.send_vehicle_status(scenario_runner_vehicle_status["vehicle_status"])
            VehicleBehaviourSuite.local_vehicle_status = scenario_runner_vehicle_status["vehicle_status"]
            
            # TODO: Start logging reaction time when TOR status is retrived
        else:
            # This means that both carla server and scenario runner are sending "Unknown" vehicle status
            # This is not an error as it is possible that both carla server and scenario runner are not sending any vehicle status
            pass

class DrivingPerformance:

    # Class variables to store the reaction timestamps
    reaction_time_start_timestamp = None
    reaction_time = None

    # Store the configuration
    config = None

    # Header for all the metrics
    common_header = ["ParticipantID", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp"]

    # Pandas dataframe to store the driving performance data
    reaction_time_df = None
    braking_input_df = None
    throttle_input_df = None
    steering_angles_df = None
    lane_offset_df = None
    speed_df = None

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
    def set_configuration(config):
        DrivingPerformance.config = config

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
        lane_offset = vehicle_location.distance(world.get_map().get_waypoint(vehicle_location).transform.location)

        #         
        common_row_elements = [DrivingPerformance.config["ParticipantID"], DrivingPerformance.config["BlockNumber"], DrivingPerformance.config["TrialNumber"], DrivingPerformance.config["TaskType"], DrivingPerformance.config["TaskSetting"], DrivingPerformance.config["TrafficComplexity"], timestamp]

        # Store all the raw measurements in the dataframes
        DrivingPerformance.braking_input_df.loc[len(DrivingPerformance.braking_input_df)] = common_row_elements + [braking_input]
        DrivingPerformance.throttle_input_df.loc[len(DrivingPerformance.throttle_input_df)] = common_row_elements + [throttle_input]
        DrivingPerformance.steering_angles_df.loc[len(DrivingPerformance.steering_angles_df)] = common_row_elements + [steering_angle]
        DrivingPerformance.lane_offset_df.loc[len(DrivingPerformance.lane_offset_df)] = common_row_elements + [lane_offset]

        # TODO: Store the eye-data into the dataframes
    
    @staticmethod
    def save_performance_data():
        # Save the dataframes into csv files
        DrivingPerformance.braking_input_df.to_csv("PerformanceData/braking_input.csv")
        DrivingPerformance.throttle_input_df.to_csv("PerformanceData/throttle_input.csv")
        DrivingPerformance.steering_angles_df.to_csv("PerformanceData/steering_angles.csv")
        DrivingPerformance.lane_offset_df.to_csv("PerformanceData/lane_offset.csv")
        DrivingPerformance.speed_df.to_csv("PerformanceData/speed.csv")


class EyeTracking:
    pass