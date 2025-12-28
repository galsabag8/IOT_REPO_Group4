import asyncio
import websockets
import json
import socket
import numpy as np
import time

# --- CONFIGURATION ---
UDP_IP = "127.0.0.1"
UDP_PORT = 5006
WS_PORT = 8765

# --- STATE ---
# State 0 = Calibration Mode (Adjustable)
# State 2 = Running Mode (Locked)
app_state = 0 
state_lock = asyncio.Lock()

raw_wand_vector = np.array([1.0, 0.0, 0.0], dtype=np.float32)
correction_matrix = np.identity(3, dtype=np.float32)
last_packet_time = 0

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
    global app_state, correction_matrix, raw_wand_vector
    try:
        async for message in websocket:
            async with state_lock:
                # 1. Start Fresh (When Wand Mode opens)
                if message == "CMD_RESET_CALIB":
                    print("--- TRACE: Entering Calibration Mode ---")
                    app_state = 0
                    correction_matrix = np.identity(3) # Reset to raw

                # 2. Recalibrate (Only allowed in State 0)
                elif message == "CMD_RECALIBRATE":
                    if app_state == 0:
                        print("--- TRACE: SNAP! Re-aligning center... ---")
                        target = np.array([1.0, 0.0, 0.0], dtype=np.float32)
                        correction_matrix = get_rotation_matrix(raw_wand_vector, target)
                
                # 3. Solidify (Enter key)
                elif message == "CMD_CONFIRM":
                    if app_state == 0:
                        print("--- TRACE: Calibration SOLIDIFIED. Running... ---")
                        app_state = 2

    except Exception as e:
        print(f"Listener Error: {e}")

# --- TASK 2: STREAM DATA (Python -> Browser) ---
async def data_streamer(websocket):
    global raw_wand_vector, correction_matrix, app_state, last_packet_time
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setblocking(False)

    try:
        while True:
            # 1. DRAIN UDP
            while True:
                try:
                    data, _ = sock.recvfrom(1024)
                    line = data.decode('utf-8', errors='ignore').strip()
                    if line.startswith("DATA,"):
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

                # Timeout Check
                if current_time - last_packet_time > 1.5:
                    status_msg = "WAITING FOR WAND..."
                    msg_color = "#ff4757" # Red
                
                # MODE: CALIBRATION
                elif app_state == 0:
                    status_msg = "CALIBRATION MODE\nHold Forward, Press 'R' to Align\nPress ENTER to Confirm"
                    msg_color = "#f39c12" # Orange
                
                # MODE: RUNNING
                elif app_state == 2:
                    status_msg = "READY"
                    msg_color = "#2ed573" # Green
                
                # 3. CALCULATE VISUALS
                aligned = np.dot(correction_matrix, raw_wand_vector)
                
                packet = {
                    "x": float(-aligned[1]),
                    "y": float(aligned[2]),
                    "z": float(aligned[0]),
                    "state": app_state,
                    "msg": status_msg,
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
    print("--- TRACE: Client Connected ---")
    listener_task = asyncio.create_task(command_listener(websocket))
    streamer_task = asyncio.create_task(data_streamer(websocket))
    
    done, pending = await asyncio.wait(
        [listener_task, streamer_task],
        return_when=asyncio.FIRST_COMPLETED,
    )
    for task in pending: task.cancel()

async def main():
    print(f"--- TRACE: WebSocket Server running on port {WS_PORT} ---")
    async with websockets.serve(connection_handler, "localhost", WS_PORT):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())