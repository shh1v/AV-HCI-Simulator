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
from msgpack import loads
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
    ordered_vehicle_status = ["Unknown", "ManualDrive", "Autopilot", "PreAlertAutopilot", "TakeOver", "TakeOverManual", "ResumedAutopilot"]

    # Store the local vehicle status currently known
    previous_local_vehicle_status = "Unknown"
    local_vehicle_status = "Unknown"

    # Store all the timestamps of vehicle status phases
    autopilot_start_timestamp = None
    pre_alert_autopilot_start_timestamp = None
    take_over_start_timestamp = None
    take_over_manual_start_timestamp = None
    resumed_autopilot_start_timestamp = None
    trial_over_timestamp = None

    # log performance data variable
    log_driving_performance_data = False
    log_eye_data = False

    @staticmethod
    def _establish_vehicle_status_connection():
        # Create ZMQ socket for receiving vehicle status from carla server
        if (VehicleBehaviourSuite.carla_subscriber_context == None or VehicleBehaviourSuite.carla_subscriber_socket == None):
            VehicleBehaviourSuite.carla_subscriber_context = zmq.Context()
            VehicleBehaviourSuite.carla_subscriber_socket = VehicleBehaviourSuite.carla_subscriber_context.socket(zmq.SUB)
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            VehicleBehaviourSuite.carla_subscriber_socket.connect("tcp://localhost:5556")

        # Create ZMQ socket for receiving vehicle status from scenario runner
        if (VehicleBehaviourSuite.scenario_runner_context == None or VehicleBehaviourSuite.scenario_runner_socket == None):
            VehicleBehaviourSuite.scenario_runner_context = zmq.Context()
            VehicleBehaviourSuite.scenario_runner_socket = VehicleBehaviourSuite.scenario_runner_context.socket(zmq.SUB)
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
            VehicleBehaviourSuite._establish_vehicle_status_connection()

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
            VehicleBehaviourSuite._establish_vehicle_status_connection()
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
            VehicleBehaviourSuite._establish_vehicle_status_connection()
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
            if VehicleBehaviourSuite.local_vehicle_status == "Autopilot":
                # Record the timestamp of the autopilot start
                VehicleBehaviourSuite.autopilot_start_timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]
            if VehicleBehaviourSuite.local_vehicle_status == "PreAlertAutopilot":
                # Record the timestamp of the pre-alert autopilot start
                VehicleBehaviourSuite.pre_alert_autopilot_start_timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]

                # Start Interleaving performance logging
                VehicleBehaviourSuite.log_eye_data = True
            elif VehicleBehaviourSuite.local_vehicle_status == "TakeOver":
                # Record the timestamp of the take over start
                VehicleBehaviourSuite.take_over_start_timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]

                # Start measuring driving performance
                DrivingPerformance.start_logging_reaction_time(True)
            elif VehicleBehaviourSuite.local_vehicle_status == "TakeOverManual":
                # Record the timestamp of the take over manual start
                VehicleBehaviourSuite.take_over_manual_start_timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]

                # Stop logging the eye-interleaving data
                VehicleBehaviourSuite.log_eye_data = False

                # TODO: Save the eye-interleaving data

                # Set metadata for the driving performance data
                DrivingPerformance.set_configuration(config_file, index)

                # Start logging the performance data
                VehicleBehaviourSuite.log_driving_performance_data = True
                DrivingPerformance.start_logging_reaction_time(False)
            elif VehicleBehaviourSuite.local_vehicle_status == "ResumedAutopilot":
                # Record the timestamp of the resumed autopilot start
                VehicleBehaviourSuite.resumed_autopilot_start_timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]

                # Stop logging the performance data and save it
                VehicleBehaviourSuite.log_driving_performance_data = False
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
                # Turn off the autopilot now
                ego_vehicle.set_autopilot(False, 8005)

                # NOTE: The traffic manager does not have to be terminated; it will do so when client terminates

                # Terminate the parallel process
                return False


        # Log the performance data if required
        if VehicleBehaviourSuite.log_driving_performance_data:
            DrivingPerformance.performance_tick(world, ego_vehicle)

        if VehicleBehaviourSuite.log_eye_data:
            EyeTracking.eye_data_tick()
        
        return True
    
    @staticmethod
    def vehicle_status_terminate():
        """
        This method is used to terminate the the whole trial
        """
        # Save driving performance data
        DrivingPerformance.save_performance_data()

        # TODO: Save eye-tracking performance data

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

        # Resetting vehicle status variables
        VehicleBehaviourSuite.local_vehicle_status = "Unknown"
        VehicleBehaviourSuite.previous_local_vehicle_status = "Unknown"

        # Reseting performance logging variables
        VehicleBehaviourSuite.log_driving_performance_data = False
        VehicleBehaviourSuite.log_eye_data = False

        # TODO: Reset all the driving performance variables
        # TODO: Reset all the eye-tracking performance variables


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
        if not os.path.exists("DrivingData"):
            os.makedirs("DrivingData")

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
    # ZMQ variables
    pupil_context = None
    pupil_socket = None
    pupil_addr = "127.0.0.1"
    pupil_request_port = "50020"

    # Store the clock offset
    clock_offset = None

    # Store all the topics so that they can be looped through
    subscriber_topics = [
                        # Surface mapping and fixation data
                         "surfaces.HUD",
                         "surfaces.RightMirror",
                         "surfaces.LeftMirror",
                         "surfaces.RearMirror",
                         "surfaces.LeftMonitor",
                         "surfaces.CenterMonitor",
                         "surfaces.RightMonitor"
                         # Pupil diameter data
                         "pupil.0",
                         "pupil.1",
                         # Pupil blink data
                         "blink"]

    # Variables that decide what to log
    log_interleaving_performance = False
    log_driving_performance = False
    
    # Config file
    config_file = None
    index = -1

    # All the dataframes that will stores all the eye tracking data
    eye_mapping_df = None
    eye_fixations_df = None
    eye_diameter_df = None
    eye_blinks_df = None
    
    # Common header for the above dataframes
    eye_mapping_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp", "Surface", "RightEyeDiameter", "LeftEyeDiameter", "GazePosX", "GazePosY"]
    eye_fixations_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp", "Surface", "FixationID", "FixationDuration", "FixationDispersion"]
    eye_diameter_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp", "RightEyeDiameter", "LeftEyeDiameter"]
    eye_blinks_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp", "BlinkType"]

    @staticmethod
    def set_configuration(config_file, index):
        # config is a dictionary with the required IV values set for the trial
        EyeTracking.config_file = config_file
        EyeTracking.index = index

    @staticmethod
    def establish_eye_tracking_connection():
        if EyeTracking.pupil_context is None or EyeTracking.pupil_socket is None:
            req = EyeTracking.pupil_context.socket(zmq.REQ)
            req.connect(f"tcp://{EyeTracking.pupil_addr}:{EyeTracking.pupil_request_port}")
            
            # Ask for sub port
            req.send_string("SUB_PORT")
            sub_port = req.recv_string()

            # Also calculate the clock offset to get the system time
            clock_offsets = []
            for _ in range(0, 10):
                req.send_string("t")
                local_before_time = time.time()
                pupil_time = float(req.recv())
                local_after_time = time.time()
                clock_offsets.append(((local_before_time + local_after_time) / 2.0) - pupil_time)
            EyeTracking.clock_offset = sum(clock_offsets) / len(clock_offsets)

            # Open a sub port to listen to pupil core eye tracker
            EyeTracking.pupil_socket = EyeTracking.pupil_context.socket(zmq.SUB)
            EyeTracking.pupil_socket.connect(f"tcp://{EyeTracking.pupil_addr}:{sub_port}")

            # Subsribe to all the required topics
            for topic in EyeTracking.subscriber_topics:
                EyeTracking.pupil_socket.setsockopt(zmq.SUBSCRIBE, topic)

    @staticmethod
    def _init_dfs():
        def init_or_load_dataframe(attribute_name, csv_name, header):
            file_path = f"EyeData/{csv_name}.csv"
            
            # Check if attribute already has a value
            df = getattr(EyeTracking, attribute_name, None)
            
            # If the df attribute is None and CSV file exists, read the CSV file
            if df is None:
                if os.path.exists(file_path):
                    setattr(EyeTracking, attribute_name, pd.read_csv(file_path))
                else:
                    setattr(EyeTracking, attribute_name, pd.DataFrame(columns=header))

        # Call the function for each attribute
        init_or_load_dataframe("eye_mapping_df", "eye_mapping", EyeTracking.eye_mapping_header)
        init_or_load_dataframe("eye_fixations_df", "eye_fixations", EyeTracking.eye_fixations_header)
        init_or_load_dataframe("eye_diameter_df", "eye_diameter", EyeTracking.eye_diameter_header)
        init_or_load_dataframe("eye_blinks_df", "eye_blinks", EyeTracking.eye_blinks_header)

    @staticmethod
    def convert_to_sys_time(pupil_time):
        # This method already assumes that a connection has been established to the pupil network API
        sys_time = EyeTracking.clock_offset + pupil_time
        return datetime.datetime.fromtimestamp(sys_time).strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]

    @staticmethod
    def eye_data_tick():
        # Initialize the dataframes if not already
        EyeTracking._init_dfs()

        if EyeTracking.log_interleaving_performance and EyeTracking.log_driving_performance:
            raise Exception("Cannot log both driving performance and interleaving performance at the same time!")
        
        # Class to store all the data from eye tracker temporarily
        class EyeTrackerData: pass

        eye_tracker_data = EyeTrackerData()
        for raw_var_name in EyeTracking.subscriber_topics:
            var_name = raw_var_name.replace(".", "_") + "_data"
            setattr(eye_tracker_data, var_name, None)
            

        if EyeTracking.log_interleaving_performance or EyeTracking.log_driving_performance:
            # Establish connection if not already
            if EyeTracking.pupil_context is None or EyeTracking.pupil_socket is None:
                EyeTracking.establish_eye_tracking_connection()

            # Loop through all the topics and retrieve data
            for _ in range(0, len(EyeTracking.subscriber_topics)):
                # Get the data from the pupil network API
                topic = EyeTracking.pupil_socket.recv_string(flags=zmq.NOBLOCK)
                byte_message = EyeTracking.pupil_socket.recv(flags=zmq.NOBLOCK)
                message_dict = loads(byte_message, raw=False)
                setattr(eye_tracker_data, topic.replace(".", "_") + "_data", message_dict)
            
            # Get the common row elements for ease of use
            gen_section = EyeTracking.config_file[EyeTracking.config_file.sections()[0]]
            curr_section_name = EyeTracking.config_file.sections()[EyeTracking.index]
            curr_section = EyeTracking.config_file[curr_section_name]
            match = re.match(r"(Block\d+)(Trial\d+)", curr_section_name)
            common_row_elements = [gen_section["ParticipantID"],
                                gen_section["InterruptionParadigm"],
                                match.group(1) if match else "UnknownBlock",
                                match.group(2) if match else "UnknownTrial",
                                curr_section["NDRTTaskType"],
                                curr_section["TaskSetting"],
                                curr_section["Traffic"]]

            if EyeTracking.log_interleaving_performance or EyeTracking.log_driving_performance:
                # Find out what surface is the driver looking at and get the required data
                surfaces_with_eye_gaze = []
                surfaces_with_fixations = []
                for surface in EyeTracking.subscriber_topics:
                    if "surfaces" in surface:
                        surface_data = getattr(eye_tracker_data, surface.replace(".", "_") + "_data")
                        if surface_data is None:
                            print(f"WARNING: Surface data for {surface} is None!")
                            continue # Check for the next surface as data was not received for this surface
                        # Now, get the gaze_on_surfaces object, select the one with the highest timestamp, and check if it is True
                        gaze_on_surfaces = surface_data["gaze_on_surfaces"]
                        fixations_on_surfaces = surface_data["fixations_on_surfaces"]
                        if len(gaze_on_surfaces) > 0:
                            gaze_on_surfaces.sort(key=lambda x: x["timestamp"], reverse=True)
                            if gaze_on_surfaces[0]["on_surf"]:
                                if "HUD" not in surface and not EyeTracking.log_driving_performance:
                                    surfaces_with_eye_gaze.append([surface, gaze_on_surfaces])
                        if len(fixations_on_surfaces) > 0:
                            # Here, we dont have on_surf as fixation are noticed after some time, so the fixation data may not be current
                            fixations_on_surfaces.sort(key=lambda x: x["timestamp"], reverse=True)
                            if "HUD" not in surface and not EyeTracking.log_driving_performance:
                                surfaces_with_fixations.append([surface, fixations_on_surfaces])
                        else:
                            continue # Check for the next surface as gaze_on_surfaces is empty
                
                # Add surface data to the dataframe
                first_priority_surfaces = ["surfaces.HUD", "surfaces.LeftMirror", "surfaces.RightMirror", "surfaces.RearMirror"]
                if len(surfaces_with_eye_gaze) > 0:
                    # Now, add the data to the dataframes
                    found_first_surface = False
                    for surface in first_priority_surfaces:
                        for surface_with_eye_gaze in surfaces_with_eye_gaze:
                            if surface in surface_with_eye_gaze[0]:
                                # This means that this is the surface with the highest priority
                                # Now, add the data to the dataframe
                                sys_time = EyeTracking.convert_to_sys_time() # Calculate current time stamp
                                surface_data = surface_with_eye_gaze[1]
                                EyeTracking.eye_mapping_df.loc[len(EyeTracking.eye_mapping_df)] = common_row_elements + [sys_time, surface[9:], surface_data['norm_pos'][0], surface_data['norm_pos'][1]]
                                found_first_surface = True
                                break
                    if not found_first_surface: # Surface is one of the monitors
                        # This means that the highest priority surface was not found. Hence, add the data to the dataframe
                        sys_time = EyeTracking.convert_to_sys_time() # Calculate current time stamp
                        surface_name = surfaces_with_eye_gaze[0][0][9:]
                        surface_data = surfaces_with_eye_gaze[0][1]
                        EyeTracking.eye_mapping_df.loc[len(EyeTracking.eye_mapping_df)] = common_row_elements + [sys_time, surface_name, surface_data['norm_pos'][0], surface_data['norm_pos'][1]]
                else:
                    print("WARNING: Driver not looking at the screen")

                # Add fixation data to the dataframe
                if len(surfaces_with_fixations) > 0:
                    # Now, add the data to the dataframes
                    found_first_surface = False
                    for surface in first_priority_surfaces:
                        for surface_with_fixations in surfaces_with_fixations:
                            if surface in surface_with_fixations[0]:
                                # This means that this is the surface with the highest priority
                                # Now, add the data to the dataframe
                                sys_time = EyeTracking.convert_to_sys_time() # Calculate current time stamp
                                surface_data = surface_with_fixations[1]
                                # NOTE: Only add the fixation data if the id is unique (does not exist in the df)
                                if surface_data['id'] not in EyeTracking.eye_fixations_df['FixationID'].values:
                                    EyeTracking.eye_fixations_df.loc[len(EyeTracking.eye_fixations_df)] = common_row_elements + [sys_time, surface[9:], surface_data['id'], surface_data['duration'], surface_data['dispersion']]
                                else:
                                    print("INFO: Duplicate fixation ID found!")
                                found_first_surface = True
                                break
                    
                    # If no priority surface was found, that means the driver is looking at the road
                    if not found_first_surface:
                        # This means that the highest priority surface was not found. Hence, add the data to the dataframe
                        sys_time = EyeTracking.convert_to_sys_time() # Calculate current time stamp
                        surface_name = surfaces_with_fixations[0][0][9:]
                        surface_data = surfaces_with_fixations[0][1]
                        # NOTE: Only add the fixation data if the id is unique (does not exist in the df)
                        if surface_data['id'] not in EyeTracking.eye_fixations_df['FixationID'].values:
                            EyeTracking.eye_fixations_df.loc[len(EyeTracking.eye_fixations_df)] = common_row_elements + [sys_time, surface_name, surface_data['id'], surface_data['duration'], surface_data['dispersion']]
                        else:
                            print("INFO: Duplicate fixation ID found!")

        # Mandatorily, log the pupil diameter data and blink data
        right_eye_data = getattr(eye_tracker_data, "pupil_0_data")
        left_eye_data = getattr(eye_tracker_data, "pupil_1_data")
        if right_eye_data is not None and left_eye_data is not None:
            sys_time = EyeTracking.convert_to_sys_time() # Calculate current time stamp
            if "diameter_3d" in right_eye_data.keys() and "diameter_3d" in left_eye_data.keys():
                EyeTracking.eye_diameter_df.loc[len(EyeTracking.eye_diameter_df)] = common_row_elements + [sys_time, right_eye_data["diameter_3d"], left_eye_data["diameter_3d"]]
            else:
                print("WARNING: 3D model data is unavailable! Using 2D model data instead.")
                # NOTE: The diameters are in pixels here.
                EyeTracking.eye_diameter_df.loc[len(EyeTracking.eye_diameter_df)] = common_row_elements + [sys_time, right_eye_data["diameter"]*0.2645833333, left_eye_data["diameter"]*0.2645833333]
        else:
            print("WARNING: Right or left pupil data is unavailable!")
        blink_data = getattr(eye_tracker_data, "blink_data")
        if blink_data is not None:
            sys_time = EyeTracking.convert_to_sys_time() # Calculate current time stamp
            EyeTracking.eye_blinks_df.loc[len(EyeTracking.eye_blinks_df)] = common_row_elements + [sys_time, blink_data["type"]]

    @staticmethod
    def save_performance_data():
        pass