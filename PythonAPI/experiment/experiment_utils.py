import time
import datetime
import sys
import configparser
import json

import zmq
import msgpack as serializer

# DReyeVR import
from examples.DReyeVR_utils import find_ego_vehicle

class ExperimentHelper:

    @staticmethod
    def set_simulation_mode(client, synchronous_mode=True, fixed_delta_seconds=0.05):
        # Setting simulation mode
        settings = client.get_world().get_settings()
        if synchronous_mode:
            settings.synchronous_mode = True
            settings.fixed_delta_seconds = 0.05 # 20 Hz
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
        except KeyError:
            print(f"KeyError: Section: {section}, Key: {key}")
        except:
            print(f"Unexpected error: {sys.exc_info()[0]}")
        return None



class VehicleStatus:
    """
    The VehicleStatus class is used to send and receive vehicle status between the scenario runner and carla server.
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

    # Store the local vehicle status currently known
    vehicle_status = "Unknown"

    @staticmethod
    def _establish_CARLA_connection():
        # Create ZMQ socket for receiving vehicle status from carla server
        if (ExperimentHelper.carla_subscriber_context == None or ExperimentHelper.subscriber_socket == None):
            ExperimentHelper.carla_subscriber_context = zmq.Context()
            ExperimentHelper.carla_subscriber_socket = ExperimentHelper.subscriber_context.socket(zmq.SUB)
            ExperimentHelper.carla_subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            ExperimentHelper.carla_subscriber_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            ExperimentHelper.carla_subscriber_socket.connect("tcp://localhost:5556")

        # Create ZMQ socket for receiving vehicle status from scenario runner
        if (ExperimentHelper.carla_subscriber_context == None or ExperimentHelper.subscriber_socket == None):
            ExperimentHelper.scenario_runner_context = zmq.Context()
            ExperimentHelper.scenario_runner_socket = ExperimentHelper.subscriber_context.socket(zmq.SUB)
            ExperimentHelper.scenario_runner_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            ExperimentHelper.scenario_runner_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            ExperimentHelper.scenario_runner_socket.connect("tcp://localhost:5557")

        # Create ZMQ socket for sending vehicle control to carla server/scenario_runner
        if (ExperimentHelper.publisher_context == None or ExperimentHelper.publisher_socket == None):
            ExperimentHelper.publisher_context = zmq.Context()
            ExperimentHelper.publisher_socket = ExperimentHelper.publisher_context.socket(zmq.PUB)
            ExperimentHelper.publisher_socket.bind("tcp://*:5555")

    @staticmethod
    def send_vehicle_status(vehicle_status):
        """
        Send vehicle status to the carla
        """
        # Create ZMQ socket if not created
        if (VehicleStatus.publisher_context == None or VehicleStatus.publisher_context == None):
            VehicleStatus._establish_CARLA_connection()

        # Send vehicle status
        message = {
            "from": "client",
            "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
            "vehicle_status": str(vehicle_status)
        }
        serialized_message = serializer.packb(message, use_bin_type=True)
        
        # Send the the message
        VehicleStatus.publisher_socket.send(serialized_message)
        print(f"Sent vehicle status: {vehicle_status}")

    @staticmethod
    def receive_carla_vehicle_status():
        # Create ZMQ socket if not created
        if (VehicleStatus.carla_subscriber_socket == None or VehicleStatus.carla_subscriber_socket == None):
            VehicleStatus._establish_CARLA_connection()
        try:
            message = VehicleStatus.carla_subscriber_socket.recv()
            message_dict = json.loads(message)
        except Exception as e:
            print(f"Exception: {e}")
            return {"from": "carla",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        
        # message_dict will have sender name, timestamp, and vehicle status
        return message_dict
    
    @staticmethod
    def receive_scrnario_runner_vehicle_status():
        # Create ZMQ socket if not created
        if (VehicleStatus.scenario_runner_socket == None or VehicleStatus.scenario_runner_socket == None):
            VehicleStatus._establish_CARLA_connection()
        try:
            message = VehicleStatus.scenario_runner_socket.recv()
            message_dict = json.loads(message)
        except Exception as e:
            print(f"Exception: {e}")
            return {"from": "scenario_runner",
                    "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                    "vehicle_status": "Unknown"}
        
        # message_dict will have sender name, timestamp, and vehicle status
        return message_dict
    
    @staticmethod
    def vehicle_status_tick(world):
        """
        This method should be called every tick to update the vehicle status
        This will automatically also change the behaviour of the ego vehicle required.
        """
        # Receive vehicle status from carla server and scenario runner
        carla_vehicle_status = VehicleStatus.receive_carla_vehicle_status()
        scenario_runner_vehicle_status = VehicleStatus.receive_scenario_runner_vehicle_status()

        # Check if the received vehicle status is valid based on the publishers individual role
        if carla_vehicle_status["vehicle_status"] not in ["Unknown", "ManualDrive"]:
            raise Exception(f"Carla server does not have permission to send vehicle status: {carla_vehicle_status['vehicle_status']}")
        if scenario_runner_vehicle_status["vehicle_status"] not in ["Unknown", "AutoPilot", "PreAlertAutopilot", "TakeOver"]:
            raise Exception(f"Scenario runner does not have permission to send vehicle status: {scenario_runner_vehicle_status['vehicle_status']}")

        # Check if there is not vehicle status conflicts

        if carla_vehicle_status["vehicle_status"] != "Unknown" and scenario_runner_vehicle_status["vehicle_status"] != "Unknown":
            raise Exception(f"Both carla server and scenario runner are sending vehicle status: {carla_vehicle_status['vehicle_status']} and {scenario_runner_vehicle_status['vehicle_status']}")

        # Get the ego vehicle as it is required to change the behaviour
        ego_vehicle = find_ego_vehicle(world)
 
        # Now, we can update the local vehicle status and execute any required behaviour
        if carla_vehicle_status["vehicle_status"] != "Unknown":
            # This means that the carla server is sending the vehicle status, and it has to be "ManualDrive"
            # Turn ego_vehicle's autopilot off. This needs to be through client side as there is a bug in carla server
            ego_vehicle.set_autopilot(False)
        elif scenario_runner_vehicle_status["vehicle_status"] != "Unknown":
            # This means that the scenario runner is sending the vehicle status, and it has to be "AutoPilot", "PreAlertAutopilot", or "TakeOver"
            # Turn ego_vehicle's autopilot on. This needs to be through client side as there is a bug in carla server
            ExperimentHelper.send_vehicle_status(scenario_runner_vehicle_status["vehicle_status"])
        else:
            # This means that both carla server and scenario runner are sending "Unknown" vehicle status
            # This is not an error as it is possible that both carla server and scenario runner are not sending any vehicle status
            pass

        
        # Send the vehicle status to scenario runner
        VehicleStatus.send_vehicle_status(VehicleStatus.vehicle_status)

        # Return the vehicle status
        return VehicleStatus.vehicle_status