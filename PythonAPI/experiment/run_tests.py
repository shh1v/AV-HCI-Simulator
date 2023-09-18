# Standard library imports
import time

# Local imports
import carla
from examples.DReyeVR_utils import find_ego_vehicle

def sleep(world, duration=10):
    """
    Sleep for the given number of seconds while the world ticks in the background
    """
    curr_time = time.time()
    while time.time() - curr_time < duration:
        world.tick()

if __name__ == "__main__":
    # Connect to the server
    client = carla.Client("127.0.0.1", 2000)
    client.set_timeout(10.0)
    world = client.get_world()

    # Make the server synchronous
    settings = client.get_world().get_settings()
    settings.synchronous_mode = True
    settings.fixed_delta_seconds = 1/20.0 # 20 Hz
    client.get_world().apply_settings(settings)

    # Setting Traffic Manager 1 parameters
    tm1 = client.get_trafficmanager(8000)
    tm1.set_synchronous_mode(True)

    # Setting Traffic Manager 2 parameters
    tm2 = client.get_trafficmanager(8005)
    tm2.set_synchronous_mode(False)

    # Now, find the ego vehicle and turn on autopilot
    DReyeVR_vehicle = find_ego_vehicle(world, True)
    DReyeVR_vehicle.set_autopilot(True, tm1.get_port())
    print("DReyeVR vehicle autopilot turned on.")

    # Now, wait for 5 seconds. The sleep method simply calls world.tick() for the given number of seconds
    sleep(world, 5.0)

    # Turn on autopilot for the DReyeVR vehicle using Traffic Manager 2
    DReyeVR_vehicle.set_autopilot(False, tm2.get_port())
    print("DReyeVR vehicle autopilot turned off.")

    # Now wait for 5 seconds
    sleep(world, 5.0)

    # Make the server asynchronous again
    settings = client.get_world().get_settings()
    settings.synchronous_mode = False
    settings.fixed_delta_seconds = None
    client.get_world().apply_settings(settings)