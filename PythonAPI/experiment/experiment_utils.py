import time
import datetime
import sys
import configparser

import zmq
import msgpack as serializer

class ExperimentHelper:

    # ZMQ communication
    context = None
    socket = None

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
            ExperimentHelper.context = zmq.Context()
            ExperimentHelper.socket = ExperimentHelper.context.socket(zmq.PUB)
            ExperimentHelper.socket.bind("tcp://*:5555")

    @staticmethod
    def send_vehicle_status(vehicle_status):
        # Create ZMQ socket if not created
        if (ExperimentHelper.context == None or ExperimentHelper.socket == None):
            ExperimentHelper._establish_CARLA_connection()

        # Send vehicle status
        message = {
            "from": "client",
            "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
            "vehicle_status": str(vehicle_status)
        }
        serialized_message = serializer.packb(message, use_bin_type=True)
        
        # Send the the message
        ExperimentHelper.socket.send(serialized_message)
        print(f"Sent vehicle status: {vehicle_status}")