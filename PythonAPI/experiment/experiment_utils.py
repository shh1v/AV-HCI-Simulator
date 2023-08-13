import time
import sys
import configparser

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
        while (duration.time() - start_time) < duration:
            if world.get_settings().synchronous_mode:
                world.tick()

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