import asyncio
import websockets
import json
import socket
import numpy as np
import time
import config

# --- STATE ---
# State 0 = Calibration Mode (Adjustable)
# State 2 = Running Mode (Locked)
# State 3 = Replay Mode
app_state = 0 
file_loaded = False
state_lock = asyncio.Lock()

raw_wand_vector = np.array([1.0, 0.0, 0.0], dtype=np.float32)
correction_matrix = np.identity(3, dtype=np.float32)
replay_correction_matrix = None  # Loaded from recording metadata
last_packet_time = 0

# Socket for sending matrix updates to app.py
matrix_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def send_matrix_to_app(matrix):
    """Send the correction matrix to app.py via UDP"""
    try:
        # Flatten matrix to list and send as JSON
        matrix_data = json.dumps({"matrix": matrix.flatten().tolist()})
        matrix_sock.sendto(matrix_data.encode('utf-8'), (config.IP, config.PORT_MATRIX))
    except Exception as e:
        print(f"--- TRACE: Failed to send matrix: {e} ---")

# --- MATH HELPER ---
def get_rotation_matrix(vec1, vec2):
    # Normalize
    n1 = np.linalg.norm(vec1)
    n2 = np.linalg.norm(vec2)
    if n1 == 0 or n2 == 0: return np.identity(3)
    
    a, b = (vec1 / n1), (vec2 / n2)
    v = np.cross(a, b)
    c = np.dot(a, b)
    s = np.linalg.norm(v)
    
    if s == 0: return np.identity(3)
    
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.identity(3) + k + np.dot(k, k) * ((1 - c) / (s**2))

# --- TASK 1: RECEIVE COMMANDS (Browser -> Python) ---
async def command_listener(websocket):
    global app_state, correction_matrix, raw_wand_vector, file_loaded, replay_correction_matrix
    try:
        async for message in websocket:
            async with state_lock:
                # 1. Start Fresh (When Wand Mode opens)
                if message == "CMD_RESET_CALIB":
                    app_state = 0
                    correction_matrix = np.identity(3) # Reset to raw

                # 2. Recalibrate (Only allowed in State 0)
                elif message == "CMD_RECALIBRATE":
                    if app_state == 0:
                        target = np.array([1.0, 0.0, 0.0], dtype=np.float32)
                        correction_matrix = get_rotation_matrix(raw_wand_vector, target)
                        
                # 3. Solidify (Enter key)
                elif message == "CMD_CONFIRM":
                    if app_state == 0:
                        app_state = 2
                        # Send the final matrix to app.py
                        send_matrix_to_app(correction_matrix)

                # 4. Enter Replay Mode
                elif message == "CMD_REPLAY_MODE":
                    app_state = 3

                # 5. Exit Replay Mode
                elif message == "CMD_EXIT_REPLAY":
                    app_state = 0                   # Force Calibration Mode
                    correction_matrix = np.identity(3) # Wipe old calibration
                    replay_correction_matrix = None
                elif message == "CMD_CANCEL_CALIB":
                     # Only allow if we are currently in Running Mode (State 2)
                     if app_state == 2:
                        app_state = 0

                elif message == "CMD_FILE_LOADED":
                    file_loaded = True

                elif message == "CMD_FILE_UNLOADED":
                    file_loaded = False

    except Exception as e:
        print(f"Listener Error: {e}")

# --- TASK 2: STREAM DATA (Python -> Browser) ---
async def data_streamer(websocket):
    global raw_wand_vector, correction_matrix, app_state, last_packet_time
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((config.IP, config.PORT_VIS))
    sock.setblocking(False)

    try:
        while True:
            # 1. DRAIN UDP (Get latest data)
            got_data = False
            beat_detected = False  # Flag to track if a beat happened in this frame
            log_buffer = None # DEBUG Log buffer for Ardino stuff (used for weight detect debugging)

            # 1. DRAIN UDP
            while True:
                try:
                    data, _ = sock.recvfrom(1024)
                    line = data.decode('utf-8', errors='ignore').strip()
                    # --- HANDLE MATRIX MESSAGES FROM APP.PY ---
                    if line.startswith("MATRIX:"):
                        matrix_payload = line[7:]  # Remove "MATRIX:" prefix
                        
                        if matrix_payload == "CLEAR":
                            # Clear the replay matrix
                            replay_correction_matrix = None
                            async with state_lock:
                                if app_state == 3:
                                    app_state = 0
                                    correction_matrix = np.identity(3)
                        else:
                            # Parse the 9 comma-separated values
                            try:
                                values = [float(v) for v in matrix_payload.split(",")]
                                if len(values) == 9:
                                    replay_correction_matrix = np.array(values, dtype=np.float32).reshape(3, 3)
                                    app_state = 3  # Enter replay mode
                            except ValueError as e:
                                print(f"--- TRACE: Error parsing matrix: {e} ---")
                        continue

                    # Check for Beat Trigger
                    if line == "BEAT_TRIG":
                        beat_detected = True
                    # Check for Wand Data
                    elif line.startswith("DATA,"):
                        parts = line.split(',')
                        if len(parts) >= 4:
                            vec = np.array([float(parts[1]), float(parts[2]), float(parts[3])], dtype=np.float32)
                            if np.linalg.norm(vec) > 0:
                                raw_wand_vector = vec
                                last_packet_time = time.time()
                except BlockingIOError: break
                except Exception: break

            # 2. STATE LOGIC
            async with state_lock:
                current_time = time.time()
                status_msg = ""
                msg_color = "white"

                # Timeout Check (not applicable in replay mode)
                if app_state != 3 and current_time - last_packet_time > 1.5:
                    status_msg = "WAITING FOR WAND..."
                    msg_color = "#ff4757" # Red
                    app_state = 0  # Force back to calibration
                
                # MODE: CALIBRATION
                elif app_state == 0:
                    status_msg = "CALIBRATION MODE\nHold Forward, Press 'R' to Align\nPress ENTER to Confirm"
                    msg_color = "#f39c12" # Orange
                
                # MODE: RUNNING
                elif app_state == 2:
                    if file_loaded:
                        status_msg = "READY"
                    else:
                        status_msg = "READY\npress 'ESC' to Recalibrate"
                    msg_color = "#2ed573" # Green

                # MODE: REPLAY
                elif app_state == 3:
                    status_msg = "REPLAY MODE"
                    msg_color = "#f39c12"
                
                # 3. CALCULATE VISUALS
                if app_state == 3 and replay_correction_matrix is not None:
                    aligned = np.dot(replay_correction_matrix, raw_wand_vector)
                else:
                    aligned = np.dot(correction_matrix, raw_wand_vector)

                packet = {
                    "x": float(-aligned[1]),
                    "y": float(aligned[2]),
                    "z": float(aligned[0]),
                    "state": app_state,
                    "msg": status_msg,
                    "beat": beat_detected,  # Send the beat status to frontend
                    "debug_log": log_buffer,  # --- NEW FIELD ---
                    "color": msg_color
                }
            
            await websocket.send(json.dumps(packet))
            await asyncio.sleep(0.016) # ~60 FPS

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        sock.close()

# --- MAIN HANDLER ---
async def connection_handler(websocket):

    listener_task = asyncio.create_task(command_listener(websocket))
    streamer_task = asyncio.create_task(data_streamer(websocket))
    
    done, pending = await asyncio.wait(
        [listener_task, streamer_task],
        return_when=asyncio.FIRST_COMPLETED,
    )
    for task in pending: task.cancel()

async def main():
    async with websockets.serve(connection_handler, "localhost", config.WS_PORT):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())