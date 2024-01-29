"""
Receive data from Pupil server broadcast over TCP
test script to see what the stream looks like
and for debugging
"""
import zmq
from msgpack import loads

context = zmq.Context()
# open a req port to talk to pupil
addr = "127.0.0.1"  # remote ip or localhost
req_port = "50020"  # same as in the pupil remote gui
req = context.socket(zmq.REQ)
req.connect("tcp://{}:{}".format(addr, req_port))
# ask for the sub port
req.send_string("SUB_PORT")
sub_port = req.recv_string()
    
# open a sub port to listen to pupil
sub = context.socket(zmq.SUB)
sub.connect("tcp://{}:{}".format(addr, sub_port))
sub.setsockopt_string(zmq.SUBSCRIBE, "surfaces._HUD")

while True:
    try:
        topic = sub.recv_string()
        msg = sub.recv()  # bytes
        surfaces = loads(msg, raw=False)
        surface_data = {
            k: v for k, v in surfaces.items()
        }
        try:
            # note that we may have more than one gaze position data point (this is expected behavior)
            gaze_positions = surface_data["gaze_on_surfaces"]
            latest_gaze_position = gaze_positions[-1]
            if latest_gaze_position["on_surf"]:
                print("TRUE")
            else:
                print("FALSE")
        except Exception as e:
            print(e)
    except KeyboardInterrupt:
        break