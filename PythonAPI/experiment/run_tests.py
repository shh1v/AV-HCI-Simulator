# Standard library imports
import zmq
import datetime
import json
import traceback

carla_subscriber_context = None
carla_subscriber_socket = None

def _establish_CARLA_connection():
    global carla_subscriber_context
    global carla_subscriber_socket

    # Create ZMQ socket for receiving vehicle status from carla server
    if (carla_subscriber_context == None or carla_subscriber_socket == None):
        carla_subscriber_context = zmq.Context()
        carla_subscriber_socket = carla_subscriber_context.socket(zmq.SUB)
        carla_subscriber_socket.setsockopt_string(zmq.SUBSCRIBE, "")
        carla_subscriber_socket.setsockopt(zmq.RCVTIMEO, 2000)  # 1 ms timeout
        carla_subscriber_socket.connect("tcp://localhost:5557")
        print("ZMQ: Connected to carla server")

def receive_carla_vehicle_status():
    global carla_subscriber_context
    global carla_subscriber_socket

    # Create ZMQ socket if not created
    if (carla_subscriber_context == None or carla_subscriber_socket == None):
        _establish_CARLA_connection()
    try:
        message = carla_subscriber_socket.recv()
        message_dict = json.loads(message)
        print("Received message:", message_dict)
    except zmq.Again:  # This exception is raised on timeout
        # print(f"Didn't receive any message from carla server at {datetime.datetime.now().strftime('%d/%m/%Y %H:%M:%S.%f')[:-3]}")
        return {"from": "carla",
                "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                "vehicle_status": "Unknown"}
    except Exception as e:
        print("Unexpected error:")
        print(traceback.format_exc())
        return {"from": "carla",
                "timestamp": datetime.datetime.now().strftime("%d/%m/%Y %H:%M:%S.%f")[:-3],
                "vehicle_status": "Unknown"}
    
    # message_dict will have sender name, timestamp, and vehicle status
    return message_dict

if __name__ == '__main__':
    while True:
        receive_carla_vehicle_status()