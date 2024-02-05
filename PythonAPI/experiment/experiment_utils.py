import time
import datetime
import sys
import os
import configparser
import json
import re
import traceback

import zmq
import socket
import msgpack as serializer
from msgpack import loads
import pandas as pd
import configparser

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

    @staticmethod
    def update_current_block(filename, new_block_value):
        class CaseSensitiveConfigParser(configparser.ConfigParser):
            def optionxform(self, optionstr):
                return optionstr

            def write(self, fp, space_around_delimiters=True):
                """Write an .ini-format representation of the configuration state."""
                if space_around_delimiters:
                    delimiter = " = "
                else:
                    delimiter = "="
                for section in self._sections:
                    fp.write("[{}]\n".format(section))
                    for (key, value) in self._sections[section].items():
                        if key == "__name__":
                            continue
                        if (value is not None) or (self._optcre == self.OPTCRE):
                            key = delimiter.join((key, str(value).replace('\n', '\n\t')))
                        fp.write("{}\n".format(key))
                    fp.write("\n")

        # Create a CaseSensitiveConfigParser object
        config = CaseSensitiveConfigParser()
        
        # Read the entire file content
        with open(filename, 'r') as file:
            content = file.readlines()
        
        # Extract comments at the top
        comments = []
        for line in content:
            if line.startswith("#"):
                comments.append(line)
            else:
                break
        
        # Read the INI file
        with open(filename, 'r') as file:
            config.read_file(file)
        
        # Update the CurrentBlock value in the General section
        new_block_value_valid = new_block_value
        if not new_block_value_valid.startswith("\""):
            new_block_value_valid = "\"" + new_block_value_valid
        if not new_block_value_valid.endswith("\""):
            new_block_value_valid = new_block_value_valid + "\""

        config['General']['CurrentBlock'] = new_block_value_valid
        
        # Write the comments and the updated configuration back to the INI file
        with open(filename, 'w') as file:
            for comment in comments:
                file.write(comment)
            config.write(file, space_around_delimiters=False)
    
    @staticmethod
    def copy_experiment_config(from_file_path, to_file_path):
        with open(from_file_path, 'r') as from_file:
            with open(to_file_path, 'w') as to_file:
                to_file.write(from_file.read())

class HardwareSuite:
    # ZMQ communication subsriber variables for receiving eye-tracking data from pupil core
    pupil_context = None
    pupil_socket = None

    # UDP Connection variables for sending vehicle control to carla server
    hardware_socket = None
    hardware_address = None

    # Other variables
    pupil_addr = "127.0.0.1"
    pupil_request_port = "50020"

    # Store the clock offset
    clock_offset = None

    @staticmethod
    def establish_eye_tracking_connection():
        if HardwareSuite.pupil_context is None or HardwareSuite.pupil_socket is None:
            HardwareSuite.pupil_context = zmq.Context()
            req = HardwareSuite.pupil_context.socket(zmq.REQ)
            req.connect(f"tcp://{HardwareSuite.pupil_addr}:{HardwareSuite.pupil_request_port}")
            
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
            HardwareSuite.clock_offset = sum(clock_offsets) / len(clock_offsets)

            # Open a sub port to listen to pupil core eye tracker
            HardwareSuite.pupil_socket = HardwareSuite.pupil_context.socket(zmq.SUB)
            HardwareSuite.pupil_socket.connect(f"tcp://{HardwareSuite.pupil_addr}:{sub_port}")

            # Subscribe to the HUD topic
            HardwareSuite.pupil_socket.setsockopt_string(zmq.SUBSCRIBE, "surfaces.HUD")

    def establish_publish_connection():
        if HardwareSuite.hardware_socket is None or HardwareSuite.hardware_address is None:
            HardwareSuite.hardware_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            HardwareSuite.hardware_address = ('127.0.0.1', 5558)

    @staticmethod
    def send_hardware_data():
        """
        Send send eye-tracker (for now) data to the carla server
        """
        # Create ZMQ socket if not created
        if (HardwareSuite.hardware_socket == None or HardwareSuite.hardware_address == None):
            HardwareSuite.establish_publish_connection()

        # Retrieve the hardware data
        HUD_OnSurf = HardwareSuite.retrieve_eye_tracking_data()

        if HUD_OnSurf is None:
            return # Exit if no data is received

        # Send the hardware data (for now, just send the HUD_OnSurf data)
        try:
            message = b'\x01' if HUD_OnSurf else b'\x00'
            HardwareSuite.hardware_socket.sendto(message, HardwareSuite.hardware_address)
        except socket.error as e:
            print(f"Socket error: {e}")
        except Exception as e:
            print(f"Other exception: {e}")
    
    @staticmethod
    def terminate_hardware_connection():
        """
        Close the hardware connection
        """
        HardwareSuite.hardware_socket.close()

    @staticmethod
    def retrieve_eye_tracking_data():
        """
        Receive eye-tracking data from the pupil core
        """
        # Create ZMQ socket if not created
        if (HardwareSuite.pupil_context == None or HardwareSuite.pupil_socket == None):
            HardwareSuite.establish_eye_tracking_connection()

        latest_gaze_position = None

        try:
            topic = HardwareSuite.pupil_socket.recv_string(flags=zmq.NOBLOCK)
            msg = HardwareSuite.pupil_socket.recv(flags=zmq.NOBLOCK)  # bytes
            surfaces = loads(msg, raw=False)
            surface_data = {
                k: v for k, v in surfaces.items()
            }
            try:
                # note that we may have more than one gaze position data point (this is expected behavior)
                gaze_positions = surface_data["gaze_on_surfaces"]
                latest_gaze_position = gaze_positions[-1]["on_surf"]
            except Exception as e:
                pass
        except Exception as e:
            pass

        return latest_gaze_position


class VehicleBehaviourSuite:
    """
    The VehicleBehaviourSuite class is used to send and receive vehicle status between the scenario runner and carla server.
    Note that this class exclusively has class variables and static methods to ensure that only one instance of this class is created.
    This is also done to ensure that the ZMQ socket is not created multiple times.

    WARNING: This class will assume that scenario runner will send all the vehicle status to python client except
    for the "ManualMode" status, which will be send by the carla server.
    """

    # ZMQ communication subsriber variables for receiving vehicle status from carla server
    carla_subscriber_context = None
    carla_subscriber_socket = None

    # ZMQ communication subsriber variables for sending vehicle status to scenario runner
    scenario_runner_context = None
    scenario_runner_socket = None

    # ZMQ communication publisher variables for sending vehicle control to carla server/scenario_runner
    publisher_context = None
    publisher_socket = None

    # Store the ordered vehicle status
    ordered_vehicle_status = ["Unknown", "ManualDrive", "Autopilot", "PreAlertAutopilot", "TakeOver", "TakeOverManual", "ResumedAutopilot", "TrialOver"]

    # Store the local vehicle status currently known
    previous_local_vehicle_status = "Unknown"
    local_vehicle_status = "Unknown"

    # log performance data variable
    log_driving_performance_data = False
    log_eye_data = False

    @staticmethod
    def _establish_vehicle_status_connection():
        # Create ZMQ socket for receiving vehicle status from carla server
        if (VehicleBehaviourSuite.carla_subscriber_context == None or VehicleBehaviourSuite.carla_subscriber_socket == None):
            VehicleBehaviourSuite.carla_subscriber_context = zmq.Context()
            VehicleBehaviourSuite.carla_subscriber_socket = VehicleBehaviourSuite.carla_subscriber_context.socket(zmq.SUB)
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt(zmq.CONFLATE, 1) # Setting conflate option to reduce overload
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            VehicleBehaviourSuite.carla_subscriber_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            VehicleBehaviourSuite.carla_subscriber_socket.connect("tcp://localhost:5556")

        # Create ZMQ socket for receiving vehicle status from scenario runner
        if (VehicleBehaviourSuite.scenario_runner_context == None or VehicleBehaviourSuite.scenario_runner_socket == None):
            VehicleBehaviourSuite.scenario_runner_context = zmq.Context()
            VehicleBehaviourSuite.scenario_runner_socket = VehicleBehaviourSuite.scenario_runner_context.socket(zmq.SUB)
            VehicleBehaviourSuite.scenario_runner_socket.setsockopt(zmq.CONFLATE, 1) # Setting conflate option to reduce overload
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
        # First off, check if the trial is still running, If not, immediately exit
        if VehicleBehaviourSuite.local_vehicle_status == "TrialOver":
            return False
        else:
            # Wait for the simulation to tick
            world.wait_for_tick()
        
        # Receive vehicle status from carla server and scenario runner
        carla_vehicle_status = VehicleBehaviourSuite.receive_carla_vehicle_status()
        scenario_runner_vehicle_status = VehicleBehaviourSuite.receive_scenario_runner_vehicle_status()

        # Check if there are no vehicle status conflicts. If there is a conflict, determine the correct vehicle status
        # Furthermore, also store the timestamp of when the vehicle status changed (i.e., when the vehicle status is sent by the publisher)
        sent_timestamp = None
        VehicleBehaviourSuite.previous_local_vehicle_status = VehicleBehaviourSuite.local_vehicle_status
        if VehicleBehaviourSuite.ordered_vehicle_status.index(carla_vehicle_status["vehicle_status"]) <= VehicleBehaviourSuite.ordered_vehicle_status.index(scenario_runner_vehicle_status["vehicle_status"]):
            # This means that scenario runner is sending a vehicle status that comes after in a trial procedure. Hence its the most up to date vehicle status
            # NOTE: There may be a chance that carla is sending unknown, and scenario runner is sending an old address. So, check for that
            if VehicleBehaviourSuite.ordered_vehicle_status.index(VehicleBehaviourSuite.local_vehicle_status) <= VehicleBehaviourSuite.ordered_vehicle_status.index(scenario_runner_vehicle_status["vehicle_status"]):
                VehicleBehaviourSuite.local_vehicle_status = scenario_runner_vehicle_status["vehicle_status"]
                sent_timestamp = scenario_runner_vehicle_status["timestamp"] # Store the timestamp of when the vehicle status changed
                # If scenario runner has sent an updated vehicle status, let carla server know about it
                VehicleBehaviourSuite.send_vehicle_status(scenario_runner_vehicle_status["vehicle_status"])
        else:
            # This means that carla server is sending a vehicle status that comes after in a trial procedure. Hence its the most up to date vehicle status
            # NOTE: There may be a chance that scenario runner is sending unknown, and carla is sending an old address. So, check for that
            if VehicleBehaviourSuite.ordered_vehicle_status.index(VehicleBehaviourSuite.local_vehicle_status) <= VehicleBehaviourSuite.ordered_vehicle_status.index(carla_vehicle_status["vehicle_status"]):
                sent_timestamp = scenario_runner_vehicle_status["timestamp"] # Store the timestamp of when the vehicle status changed
                VehicleBehaviourSuite.local_vehicle_status = carla_vehicle_status["vehicle_status"]

        # Get the ego vehicle as it is required to change the behaviour
        ego_vehicle = find_ego_vehicle(world, verbose=False)

        # Now, execute any required behaviour based on the potential updated vehicle status
        if VehicleBehaviourSuite.previous_local_vehicle_status != VehicleBehaviourSuite.local_vehicle_status:
            # This means that the vehicle status has changed. Hence, execute the required behaviour
            if VehicleBehaviourSuite.local_vehicle_status == "Autopilot":
                # Record the timestamp of the autopilot start
                CarlaPerformance.autopilot_start_timestamp = sent_timestamp

                # Setting the metadata for the eye-tracker data
                EyeTracking.set_configuration(config_file, index)

                # Once the autopilot is turned, start logging the eye-tracking data
                VehicleBehaviourSuite.log_eye_data = True

                # Setup logging behaviour for the eye-tracking data
                EyeTracking.log_interleaving_performance = True
                EyeTracking.log_driving_performance = False
            elif VehicleBehaviourSuite.local_vehicle_status == "PreAlertAutopilot":
                # Record the timestamp of the pre-alert autopilot start
                CarlaPerformance.pre_alert_autopilot_start_timestamp = sent_timestamp
            elif VehicleBehaviourSuite.local_vehicle_status == "TakeOver":
                # Record the timestamp of the take over start
                CarlaPerformance.take_over_start_timestamp = sent_timestamp

                # Start measuring the driver's reaction time
                CarlaPerformance.start_logging_reaction_time(True)

                # Change the eye-tracking logging behaviour
                EyeTracking.log_interleaving_performance = False
                EyeTracking.log_driving_performance = True

                # Set metadata for the driving performance data
                CarlaPerformance.set_configuration(config_file, index)

                # Start logging the performance data
                VehicleBehaviourSuite.log_driving_performance_data = True
            elif VehicleBehaviourSuite.local_vehicle_status == "TakeOverManual":
                # Record the timestamp of the take over manual start
                CarlaPerformance.take_over_manual_start_timestamp = sent_timestamp

                # Log the reaction time (by stopping the timer)
                CarlaPerformance.start_logging_reaction_time(False)
            elif VehicleBehaviourSuite.local_vehicle_status == "ResumedAutopilot":
                # Record the timestamp of the resumed autopilot start
                CarlaPerformance.resumed_autopilot_start_timestamp = sent_timestamp

                # Stop logging the performance data and save it
                VehicleBehaviourSuite.log_driving_performance_data = False
                CarlaPerformance.save_performance_data()

                # NOTE: Continue to record eye-tracking data to observe the aftereffects of take-over requests or task-switching.
                # However, change the data logging behaviour
                EyeTracking.log_interleaving_performance = True
                EyeTracking.log_driving_performance = False

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
                # Record the timestamp of the trial over
                CarlaPerformance.trial_over_timestamp = sent_timestamp

                # Save all the timestamps of the vehicle status phases
                CarlaPerformance.save_timestamp_data()

                # Turn off the autopilot now
                ego_vehicle.set_autopilot(False, 8005)

                # NOTE: The traffic manager does not have to be terminated; it will do so when client terminates

                # Stop logging the eye-tracking data
                VehicleBehaviourSuite.log_eye_data = False
                
                # Save the eye-tracking data
                EyeTracking.save_performance_data()
                
                # Terminate the parallel process
                print("Trial is successfully completed! Terminating the parallel process...")
                return False


        # Log the driving performance data
        if VehicleBehaviourSuite.log_driving_performance_data:
            CarlaPerformance.performance_tick(world, ego_vehicle)
        
        # Log the eye-tracking data
        if VehicleBehaviourSuite.log_eye_data:
            EyeTracking.eye_data_tick()
        
        return True
    
    @staticmethod
    def vehicle_status_terminate():
        """
        This method is used to terminate the the whole trial
        """
        # Save driving performance data
        CarlaPerformance.save_performance_data()

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

class CarlaPerformance:

    # Class variables to store the reaction timestamps
    reaction_time_start_timestamp = None
    reaction_time = None

    # Store the configuration
    config_file = None
    index = -1

    # Header for all the metrics
    common_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity"]

    # Store all the timestamps of vehicle status phases
    autopilot_start_timestamp = None
    pre_alert_autopilot_start_timestamp = None
    take_over_start_timestamp = None
    take_over_manual_start_timestamp = None
    resumed_autopilot_start_timestamp = None
    trial_over_timestamp = None

    # Pandas dataframe to store the driving performance data
    reaction_time_df = None
    braking_input_df = None
    throttle_input_df = None
    steering_angles_df = None
    lane_offset_df = None
    speed_df = None
    intervals_df = None

    # Storing map as get_map() is a heavy operation
    world_map = None

    @staticmethod
    def _init_dfs():
        def init_or_load_dataframe(attribute_name, folder_name, csv_name, performance_cols):
            file_path = f"{folder_name}/{csv_name}.csv"
            
            # Check if attribute already has a value
            df = getattr(CarlaPerformance, attribute_name)
            
            # If the df attribute is None and CSV file exists, read the CSV file
            if df is None:
                if os.path.exists(file_path):
                    setattr(CarlaPerformance, attribute_name, pd.read_csv(file_path))
                else:
                    columns = CarlaPerformance.common_header + performance_cols
                    setattr(CarlaPerformance, attribute_name, pd.DataFrame(columns=columns))

        # Call the function for each attribute
        init_or_load_dataframe("reaction_time_df", "DrivingData", "reaction_time", ["Timestamp", "ReactionTime"])
        init_or_load_dataframe("braking_input_df", "DrivingData", "braking_input", ["Timestamp", "BrakingInput"])
        init_or_load_dataframe("throttle_input_df", "DrivingData", "throttle_input", ["Timestamp", "AccelerationInput"])
        init_or_load_dataframe("steering_angles_df", "DrivingData", "steering_angles", ["Timestamp", "SteeringAngle"])
        init_or_load_dataframe("lane_offset_df", "DrivingData", "lane_offset", ["Timestamp", "LaneID", "LaneOffset"])
        init_or_load_dataframe("speed_df", "DrivingData", "speed", ["Timestamp", "Speed"])
        init_or_load_dataframe("intervals_df", "IntervalData", "interval_timestamps", ["Autopilot", "PreAlertAutopilot", "TakeOver", "TakeOverManual", "ResumedAutopilot", "TrialOver"])

    @staticmethod
    def start_logging_reaction_time(running):
        if running and CarlaPerformance.reaction_time_start_timestamp is None:
            CarlaPerformance.reaction_time_start_timestamp = time.time()
        elif CarlaPerformance.reaction_time is None and CarlaPerformance.reaction_time_start_timestamp is not None:
            CarlaPerformance.reaction_time = time.time() - CarlaPerformance.reaction_time_start_timestamp
            if CarlaPerformance.reaction_time <= 0:
                raise Exception("Reaction time cannot be lte 0!")
        else:
            raise Exception("Reaction variables are not in the correct state!")

    @staticmethod
    def set_configuration(config_file, index):
        # config is a dictionary with the required IV values set for the trial
        CarlaPerformance.config_file = config_file
        CarlaPerformance.index = index

    @staticmethod
    def performance_tick(world, ego_vehicle):
        # Initialize the dataframes if not already
        CarlaPerformance._init_dfs()

        # Now, start logging the data of the ego vehicle
        vehicle_control = ego_vehicle.get_control()
        vehicle_location = ego_vehicle.get_location()

        # Get all the raw measurements
        timestamp = datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3]
        braking_input = vehicle_control.brake # NOTE: This is normalized between 0 and 1
        throttle_input = vehicle_control.throttle # NOTE: This is normalized between 0 and 1
        steering_angle = vehicle_control.steer * 450 # NOTE: The logitech wheel can rotate 450 degrees on one side.
        ego_speed = ego_vehicle.get_velocity().length() * 3.6 # NOTE: The speed is in m/s, so convert it to km/h

        # Make sure we have the map
        CarlaPerformance.world_map = world.get_map() if CarlaPerformance.world_map is None else CarlaPerformance.world_map

        vehicle_waypoint = CarlaPerformance.world_map.get_waypoint(vehicle_location)
        lane_offset = vehicle_location.distance(vehicle_waypoint.transform.location)
        lane_id = str(vehicle_waypoint.lane_id)

        # Store the common elements for ease of use
        gen_section = CarlaPerformance.config_file[CarlaPerformance.config_file.sections()[0]]
        curr_section_name = CarlaPerformance.config_file.sections()[CarlaPerformance.index]
        curr_section = CarlaPerformance.config_file[curr_section_name]
        match = re.match(r"(Block\d+)(Trial\d+)", curr_section_name)
        common_row_elements = [gen_section["ParticipantID"].replace("\"", ""),
                               gen_section["InterruptionParadigm"].replace("\"", ""),
                               match.group(1) if match else "UnknownBlock",
                               match.group(2) if match else "UnknownTrial",
                               curr_section["NDRTTaskType"].replace("\"", ""),
                               curr_section["TaskSetting"].replace("\"", ""),
                               curr_section["Traffic"].replace("\"", ""),
                               timestamp]

        # Store all the raw measurements in the dataframes
        CarlaPerformance.braking_input_df.loc[len(CarlaPerformance.braking_input_df)] = common_row_elements + [braking_input]
        CarlaPerformance.throttle_input_df.loc[len(CarlaPerformance.throttle_input_df)] = common_row_elements + [throttle_input]
        CarlaPerformance.steering_angles_df.loc[len(CarlaPerformance.steering_angles_df)] = common_row_elements + [steering_angle]
        CarlaPerformance.speed_df.loc[len(CarlaPerformance.speed_df)] = common_row_elements + [ego_speed]
        CarlaPerformance.lane_offset_df.loc[len(CarlaPerformance.lane_offset_df)] = common_row_elements + [lane_id, lane_offset]
    
    @staticmethod
    def save_performance_data():
        # Ensure the directory exists
        if not os.path.exists("DrivingData"):
            os.makedirs("DrivingData")

        # Save the dataframes to the files only if they are initialized
        if CarlaPerformance.braking_input_df is not None:
            CarlaPerformance.braking_input_df.to_csv("DrivingData/braking_input.csv", index=False)
        if CarlaPerformance.throttle_input_df is not None:
            CarlaPerformance.throttle_input_df.to_csv("DrivingData/throttle_input.csv", index=False)
        if CarlaPerformance.steering_angles_df is not None:
            CarlaPerformance.steering_angles_df.to_csv("DrivingData/steering_angles.csv", index=False)
        if CarlaPerformance.lane_offset_df is not None:
            CarlaPerformance.lane_offset_df.to_csv("DrivingData/lane_offset.csv", index=False)
        if CarlaPerformance.speed_df is not None:
            CarlaPerformance.speed_df.to_csv("DrivingData/speed.csv", index=False)

    @staticmethod
    def save_timestamp_data():
        # Ensure the directory exists
        if not os.path.exists("IntervalData"):
            os.makedirs("IntervalData")
        
        # Store the common elements for ease of use
        gen_section = CarlaPerformance.config_file[CarlaPerformance.config_file.sections()[0]]
        curr_section_name = CarlaPerformance.config_file.sections()[CarlaPerformance.index]
        curr_section = CarlaPerformance.config_file[curr_section_name]
        match = re.match(r"(Block\d+)(Trial\d+)", curr_section_name)
        common_row_elements = [gen_section["ParticipantID"].replace("\"", ""),
                               gen_section["InterruptionParadigm"].replace("\"", ""),
                               match.group(1) if match else "UnknownBlock",
                               match.group(2) if match else "UnknownTrial",
                               curr_section["NDRTTaskType"].replace("\"", ""),
                               curr_section["TaskSetting"].replace("\"", ""),
                               curr_section["Traffic"].replace("\"", "")]

        # Store all the the timestamps for this trial in the dataframe
        CarlaPerformance.intervals_df.loc[len(CarlaPerformance.intervals_df)] = common_row_elements + [CarlaPerformance.autopilot_start_timestamp,
                                                                                                       CarlaPerformance.pre_alert_autopilot_start_timestamp,
                                                                                                       CarlaPerformance.take_over_start_timestamp,
                                                                                                       CarlaPerformance.take_over_manual_start_timestamp,
                                                                                                       CarlaPerformance.resumed_autopilot_start_timestamp,
                                                                                                       CarlaPerformance.trial_over_timestamp]
        
        # Finally, save the dataframe
        CarlaPerformance.intervals_df.to_csv("IntervalData/interval_timestamps.csv", index=False)
        
class EyeTracking:
    # ZMQ variables
    pupil_context = None
    pupil_socket = None
    pupil_addr = "127.0.0.1"
    pupil_request_port = "50020"

    # Store the clock offset
    clock_offset = None

    # Store all the topics so that they can be looped through
    ordered_sub_topics = ["pupil.1.3d", "pupil.0.3d", "surfaces.RightMirror", "surfaces.LeftMirror", "surfaces.RearMirror", "surfaces.LeftMonitor", "surfaces.CenterMonitor", "surfaces.RightMonitor", "surfaces.HUD", "blinks"]
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
    eye_mapping_header = ["ParticipantID", "InterruptionParadigm", "BlockNumber", "TrialNumber", "TaskType", "TaskSetting", "TrafficComplexity", "Timestamp", "Surface", "GazePosX", "GazePosY"]
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
            EyeTracking.pupil_context = zmq.Context()
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

            # Subscribe to all the required topics
            for topic in EyeTracking.ordered_sub_topics:
                EyeTracking.pupil_socket.setsockopt(zmq.SUBSCRIBE, topic.encode('utf-8'))


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
        for raw_var_name in EyeTracking.ordered_sub_topics:
            var_name = raw_var_name.replace(".", "_") + "_data"
            setattr(eye_tracker_data, var_name, None)
            

        if EyeTracking.log_interleaving_performance or EyeTracking.log_driving_performance:
            # Establish connection if not already
            if EyeTracking.pupil_context is None or EyeTracking.pupil_socket is None:
                EyeTracking.establish_eye_tracking_connection()

            # Loop until all the required data is received
            data_retrival_index = -1

            while True:
                try:
                    # Get the data from the pupil network API
                    topic = EyeTracking.pupil_socket.recv_string(flags=zmq.NOBLOCK)
                    byte_message = EyeTracking.pupil_socket.recv(flags=zmq.NOBLOCK)
                    message_dict = loads(byte_message, raw=False)

                    # Check where does the data stand in the ordered flow of data
                    # NOTE: pupil_0 and pupil_1 data is sent in randomized order
                    if data_retrival_index <= 1 or data_retrival_index < EyeTracking.ordered_sub_topics.index(topic):
                        # This means new data has been received that has not been received before
                        setattr(eye_tracker_data, topic.replace(".", "_") + "_data", message_dict)
                        data_retrival_index = EyeTracking.ordered_sub_topics.index(topic)
                    else:
                        # Data of topic has been received before. Either this can be repeated data or data from the next cycle
                        if data_retrival_index > 1:
                            # This means that all the data has been received (at least of pupils and one or more surfaces). and we have reached next cycle
                            break
                        else:
                            # This means that the data of lower index is received while we moved ahead
                            raise Exception(f"Data of lower index {data_retrival_index} is received while we moved ahead to {EyeTracking.ordered_sub_topics.index(topic)}")
                except zmq.error.Again:
                    # This exception is raised on timeout
                    pass
            
            # Get the common row elements for ease of use
            gen_section = EyeTracking.config_file[EyeTracking.config_file.sections()[0]]
            curr_section_name = EyeTracking.config_file.sections()[EyeTracking.index]
            curr_section = EyeTracking.config_file[curr_section_name]
            match = re.match(r"(Block\d+)(Trial\d+)", curr_section_name)
            common_row_elements = [gen_section["ParticipantID"].replace("\"", ""),
                                gen_section["InterruptionParadigm"].replace("\"", ""),
                                match.group(1) if match else "UnknownBlock",
                                match.group(2) if match else "UnknownTrial",
                                curr_section["NDRTTaskType"].replace("\"", ""),
                                curr_section["TaskSetting"].replace("\"", ""),
                                curr_section["Traffic"].replace("\"", "")]

            if EyeTracking.log_interleaving_performance or EyeTracking.log_driving_performance:
                # Find out what surface is the driver looking at and get the required data
                surfaces_with_eye_gaze = []
                surfaces_with_fixations = []
                for surface in EyeTracking.ordered_sub_topics:
                    if "surfaces" in surface:
                        surface_data = getattr(eye_tracker_data, surface.replace(".", "_") + "_data")
                        if surface_data is None:
                            # print(f"WARNING: Surface data for {surface} is None!")
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
                                surface_data = surface_with_eye_gaze[1][0] # The last indexing ([0]) is to get the latest data
                                sys_time = EyeTracking.convert_to_sys_time(surface_data['timestamp']) # Calculate current time stamp
                                EyeTracking.eye_mapping_df.loc[len(EyeTracking.eye_mapping_df)] = common_row_elements + [sys_time, surface[9:], surface_data['norm_pos'][0], surface_data['norm_pos'][1]]
                                found_first_surface = True
                                break
                    if not found_first_surface: # Surface is one of the monitors
                        # This means that the highest priority surface was not found. Hence, add the data to the dataframe
                        surface_name = surfaces_with_eye_gaze[0][0][9:]
                        surface_data = surfaces_with_eye_gaze[0][1][0] # The last indexing ([0]) is to get the latest data
                        sys_time = EyeTracking.convert_to_sys_time(surface_data['timestamp']) # Calculate current time stamp
                        EyeTracking.eye_mapping_df.loc[len(EyeTracking.eye_mapping_df)] = common_row_elements + [sys_time, surface_name, surface_data['norm_pos'][0], surface_data['norm_pos'][1]]
                else:
                    # print("WARNING: Driver not looking at the screen")
                    pass

                # Add fixation data to the dataframe
                if len(surfaces_with_fixations) > 0:
                    # Now, add the data to the dataframes
                    found_first_surface = False
                    for surface in first_priority_surfaces:
                        for surface_with_fixations in surfaces_with_fixations:
                            if surface in surface_with_fixations[0]:
                                # This means that this is the surface with the highest priority
                                # Now, add the data to the dataframe
                                surface_data = surface_with_fixations[1][0] # The last indexing ([0]) is to get the latest data
                                sys_time = EyeTracking.convert_to_sys_time(surface_data["timestamp"]) # Calculate current time stamp
                                # NOTE: Only add the fixation data if the id is unique (does not exist in the df)
                                if surface_data['id'] not in EyeTracking.eye_fixations_df['FixationID'].values:
                                    EyeTracking.eye_fixations_df.loc[len(EyeTracking.eye_fixations_df)] = common_row_elements + [sys_time, surface[9:], surface_data['id'], surface_data['duration'], surface_data['dispersion']]
                                else:
                                    # print("INFO: Duplicate fixation ID found!")
                                    pass
                                found_first_surface = True
                                break
                    
                    # If no priority surface was found, that means the driver is looking at the road
                    if not found_first_surface:
                        # This means that the highest priority surface was not found. Hence, add the data to the dataframe
                        surface_name = surfaces_with_fixations[0][0][9:]
                        surface_data = surfaces_with_fixations[0][1][0] # The last indexing ([0]) is to get the latest data
                        sys_time = EyeTracking.convert_to_sys_time(surface_data["timestamp"]) # Calculate current time stamp
                        # NOTE: Only add the fixation data if the id is unique (does not exist in the df)
                        if surface_data['id'] not in EyeTracking.eye_fixations_df['FixationID'].values:
                            EyeTracking.eye_fixations_df.loc[len(EyeTracking.eye_fixations_df)] = common_row_elements + [sys_time, surface_name, surface_data['id'], surface_data['duration'], surface_data['dispersion']]
                        else:
                            # print("INFO: Duplicate fixation ID found!")
                            pass

        # Mandatorily, log the pupil diameter data and blink data
        right_eye_data = getattr(eye_tracker_data, "pupil_0_3d_data")
        left_eye_data = getattr(eye_tracker_data, "pupil_1_3d_data")
        if right_eye_data is not None and left_eye_data is not None:
            sys_time = EyeTracking.convert_to_sys_time((right_eye_data["timestamp"] + left_eye_data["timestamp"])/2.0) # Calculate current time stamp
            if "diameter_3d" in right_eye_data.keys() and "diameter_3d" in left_eye_data.keys():
                EyeTracking.eye_diameter_df.loc[len(EyeTracking.eye_diameter_df)] = common_row_elements + [sys_time, right_eye_data["diameter_3d"], left_eye_data["diameter_3d"]]
            else:
                # print("WARNING: 3D model data is unavailable! Using 2D model data instead.")
                # NOTE: The diameters are in pixels here.
                EyeTracking.eye_diameter_df.loc[len(EyeTracking.eye_diameter_df)] = common_row_elements + [sys_time, right_eye_data["diameter"]*0.2645833333, left_eye_data["diameter"]*0.2645833333]
        else:
            # print("WARNING: Right or left pupil data is unavailable!")
            pass
        blink_data = getattr(eye_tracker_data, "blinks_data")
        if blink_data is not None:
            sys_time = EyeTracking.convert_to_sys_time(blink_data["timestamp"]) # Calculate current time stamp
            EyeTracking.eye_blinks_df.loc[len(EyeTracking.eye_blinks_df)] = common_row_elements + [sys_time, blink_data["type"]]

    @staticmethod
    def save_performance_data():
        # Ensure the directory exists
        if not os.path.exists("EyeData"):
            os.makedirs("EyeData")

        # Save the dataframes to the files only if they are initialized
        if EyeTracking.eye_mapping_df is not None:
            EyeTracking.eye_mapping_df.to_csv("EyeData/eye_mapping.csv", index=False)
        if EyeTracking.eye_fixations_df is not None:
            EyeTracking.eye_fixations_df.to_csv("EyeData/eye_fixations.csv", index=False)
        if EyeTracking.eye_diameter_df is not None:
            EyeTracking.eye_diameter_df.to_csv("EyeData/eye_diameter.csv", index=False)
        if EyeTracking.eye_blinks_df is not None:
            EyeTracking.eye_blinks_df.to_csv("EyeData/eye_blinks.csv", index=False)