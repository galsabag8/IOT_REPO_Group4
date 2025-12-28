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
# We use a Lock to prevent reading/writing state at the exact same nanosecond
state_lock = asyncio.Lock()
raw_wand_vector = np.array([1.0, 0.0, 0.0], dtype=np.float32)
correction_matrix = np.identity(3, dtype=np.float32)
app_state = 0  # 0=Calibrating, 2=Running
last_packet_time = 0
calibration_start_time = 0

# --- MATH HELPER ---
def get_rotation_matrix(vec1, vec2):
    a, b = (vec1 / np.linalg.norm(vec1)), (vec2 / np.linalg.norm(vec2))
    v = np.cross(a, b)
    c = np.dot(a, b)
    s = np.linalg.norm(v)
    if s == 0: return np.identity(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.identity(3) + k + np.dot(k, k) * ((1 - c) / (s**2))

# --- TASK 1: RECEIVE COMMANDS (Browser -> Python) ---
async def command_listener(websocket):
    global app_state, calibration_start_time
    try:
        async for message in websocket:
            if message == "CMD_RECALIBRATE":
                print("--- TRACE: Recalibration Triggered! ---")
                async with state_lock:
                    app_state = 0
                    calibration_start_time = 0 
    except Exception as e:
        print(f"Listener Error: {e}")

# --- TASK 2: STREAM DATA (Python -> Browser) ---
async def data_streamer(websocket):
    global raw_wand_vector, correction_matrix, app_state, last_packet_time, calibration_start_time
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setblocking(False)

    try:
        while True:
            # 1. DRAIN UDP (Get latest data)
            got_data = False
            beat_detected = False  # Flag to track if a beat happened in this frame
            log_buffer = None # DEBUG Log buffer for Ardino stuff (used for weight detect debugging)

            while True:
                try:
                    data, _ = sock.recvfrom(1024)
                    line = data.decode('utf-8', errors='ignore').strip()
                    
                    # Check for Beat Trigger
                    if line == "BEAT_TRIG":
                        beat_detected = True
                        print("--- Beat Detected! ---")

                    # --- NEW: Catch Log Messages ---
                    elif line.startswith("LOG:"):
                        log_buffer = line 
                        print(f"DEBUG: {line}")
                    
                    # Check for Wand Data
                    elif line.startswith("DATA,"):
                        parts = line.split(',')
                        if len(parts) >= 4:
                            vec = np.array([float(parts[1]), float(parts[2]), float(parts[3])], dtype=np.float32)
                            if np.linalg.norm(vec) > 0:
                                raw_wand_vector = vec
                                last_packet_time = time.time()
                                got_data = True
                except BlockingIOError:
                    break # Buffer empty
                except Exception:
                    break

            # 2. STATE MACHINE
            async with state_lock:
                current_time = time.time()
                status_msg = ""
                
                # Auto-timeout
                if current_time - last_packet_time > 1.0:
                    app_state = 0
                    status_msg = "WAITING FOR WAND..."
                    calibration_start_time = 0
                
                # Calibration
                elif app_state == 0:
                    if calibration_start_time == 0: calibration_start_time = current_time
                    remaining = 3.0 - (current_time - calibration_start_time) # Reduced to 3s for faster setup
                    
                    if remaining > 0:
                        status_msg = f"CALIBRATING... {remaining:.1f}"
                    else:
                        target = np.array([1.0, 0.0, 0.0], dtype=np.float32)
                        correction_matrix = get_rotation_matrix(raw_wand_vector, target)
                        app_state = 2
                        status_msg = "ALIGNED!"
                
                # 3. CALCULATE & SEND
                aligned = np.dot(correction_matrix, raw_wand_vector)
                
                packet = {
                    "x": float(-aligned[1]),
                    "y": float(aligned[2]),
                    "z": float(aligned[0]),
                    "state": app_state,
                    "msg": status_msg,
                    "beat": beat_detected,  # Send the beat status to frontend
                    "debug_log": log_buffer  # --- NEW FIELD ---
                }
            
            await websocket.send(json.dumps(packet))
            await asyncio.sleep(0.016) # 60 FPS cap

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        sock.close()

# --- MAIN HANDLER ---
async def connection_handler(websocket):
    print("--- TRACE: Client Connected ---")
    # Create two independent tasks that run in parallel
    listener_task = asyncio.create_task(command_listener(websocket))
    streamer_task = asyncio.create_task(data_streamer(websocket))
    
    # Wait until connection closes
    done, pending = await asyncio.wait(
        [listener_task, streamer_task],
        return_when=asyncio.FIRST_COMPLETED,
    )
    
    for task in pending:
        task.cancel()

async def main():
    print(f"--- TRACE: WebSocket Server running on port {WS_PORT} ---")
    async with websockets.serve(connection_handler, "localhost", WS_PORT):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())