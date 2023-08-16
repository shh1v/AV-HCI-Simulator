import time
import datetime
import sys
import configparser
import json

import zmq
import msgpack as serializer

class ExperimentHelper:

    # ZMQ communication
    subscriber_context = None
    subscriber_socket = None
    publisher_context = None
    publisher_socket = None

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
    
    @staticmethod
    def _establish_CARLA_connection():
        # Create ZMQ socket for receiving vehicle status
        if (ExperimentHelper.subscriber_context == None or ExperimentHelper.subscriber_socket == None):
            ExperimentHelper.subscriber_context = zmq.Context()
            ExperimentHelper.subscriber_socket = ExperimentHelper.subscriber_context.socket(zmq.SUB)
            ExperimentHelper.subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            ExperimentHelper.subscriber_socket.setsockopt(zmq.RCVTIMEO, 1)  # 1 ms timeout
            ExperimentHelper.subscriber_socket.connect("tcp://localhost:5556")


        # Create ZMQ socket for sending vehicle control
        if (ExperimentHelper.publisher_context == None or ExperimentHelper.publisher_socket == None):
            ExperimentHelper.publisher_context = zmq.Context()
            ExperimentHelper.publisher_socket = ExperimentHelper.publisher_context.socket(zmq.PUB)
            ExperimentHelper.publisher_socket.bind("tcp://*:5555")


    @staticmethod
    def send_vehicle_status(vehicle_status):
        # Create ZMQ socket if not created
        if (ExperimentHelper.publisher_context == None or ExperimentHelper.publisher_context == None):
            ExperimentHelper._establish_CARLA_connection()

        # Send vehicle status
        message = {
            "from": "client",
            "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
            "vehicle_status": str(vehicle_status)
        }
        serialized_message = serializer.packb(message, use_bin_type=True)
        
        # Send the the message
        ExperimentHelper.publisher_socket.send(serialized_message)
        print(f"Sent vehicle status: {vehicle_status}")

    @staticmethod
    def receive_vehicle_status():
        # Create ZMQ socket if not created
        if (ExperimentHelper.subscriber_socket == None or ExperimentHelper.subscriber_socket == None):
            ExperimentHelper._establish_CARLA_connection()
        try:
            message = ExperimentHelper.subscriber_socket.recv()
            message_dict = json.loads(message)
        except Exception as e:
            print(f"Exception: {e}")

            return "Unknown"

        return message_dict["vehicle_status"]