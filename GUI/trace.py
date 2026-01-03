import asyncio
import websockets
import json
import socket
import numpy as np
import time
import config

# --- STATE ---
# State 0 = Calibration Mode (Adjustable)
# State 1 = Countdown Active (NEW)
# State 2 = Running Mode (Locked)
app_state = 0 
state_lock = asyncio.Lock()

raw_wand_vector = np.array([1.0, 0.0, 0.0], dtype=np.float32)
correction_matrix = np.identity(3, dtype=np.float32)
last_packet_time = 0
countdown_start_time = 0  # NEW: Track when countdown starts
calibration_locked = False  # NEW: Prevents re-calibration during countdown
track_loaded = False


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
    global app_state, correction_matrix, raw_wand_vector, countdown_start_time, calibration_locked, track_loaded
    try:
        async for message in websocket:
            async with state_lock:
                if message == "CMD_ENTER_CALIBRATED":
                    print("--- TRACE: Entering CALIBRATED state (state 1) after STOP ---")
                    app_state = 1
                    calibration_locked = False
                    notify_app_calibration_complete()
                
                # --- NEW: Track loaded notification ---
                elif message == "CMD_TRACK_LOADED":
                    print("--- TRACE: Track loaded! Awaiting button press ---")
                    track_loaded = True

                # 2. Recalibrate (Only allowed in State 0)
                elif message == "CMD_RECALIBRATE":
                    if app_state == 1:
                        disable_button()
                        notify_app_calibration_lost()
                        print("--- TRACE: Re-Calibration Requested - Starting Countdown ---")
                        app_state = 0
                        countdown_start_time = time.time()
                        calibration_locked = True
                        # Reset calibration status in app.py


    except Exception as e:
        print(f"Listener Error: {e}")

# --- NEW HELPER FUNCTIONS ---
def notify_esp32_calibration_complete():
    """Send calibration complete command to ESP32 via listener.py"""
    try:
        cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        cmd_sock.sendto(b"CALIB_COMPLETE", (config.IP, config.PORT_CMD))
        print("--- TRACE: Sent CALIB_COMPLETE to ESP32 ---")
    except Exception as e:
        print(f"Error notifying ESP32: {e}")

def notify_app_calibration_complete():
    """Send calibration complete status to app.py"""
    try:
        status_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        status_sock.sendto(b"CALIB_STATUS:READY", (config.IP, config.PORT_MUSIC))
        print("--- TRACE: Sent CALIB_STATUS:READY to app.py ---")
    except Exception as e:
        print(f"Error notifying app: {e}")

def notify_app_calibration_lost():
    """Notify app.py that calibration was lost due to disconnection"""
    try:
        status_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        status_sock.sendto(b"CALIB_STATUS:LOST", (config.IP, config.PORT_MUSIC))
        print("--- TRACE: Sent CALIB_STATUS:LOST to app.py ---")
    except Exception as e:
        print(f"Error notifying app of calibration loss: {e}")

def disable_button():
    """Disable the physical button on ESP32"""
    try:
        cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        cmd_sock.sendto(b"DISABLE_BUTTON", (config.IP, config.PORT_CMD))
        print("--- TRACE: Sent DISABLE_BUTTON to ESP32 ---")
    except Exception as e:
        print(f"Error disabling button: {e}")

# --- NEW FUNCTION ---
def enable_button():
    """Enable the physical button on ESP32"""
    try:
        cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        cmd_sock.sendto(b"ENABLE_BUTTON", (config.IP, config.PORT_CMD))
        print("--- TRACE: Sent ENABLE_BUTTON to ESP32 ---")
    except Exception as e:
        print(f"Error enabling button: {e}")

# --- TASK 2: STREAM DATA (Python -> Browser) ---
async def data_streamer(websocket):
    global raw_wand_vector, correction_matrix, app_state, last_packet_time, countdown_start_time, calibration_locked, track_loaded
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((config.IP, config.PORT_VIS))
    sock.setblocking(False)

    # --- NEW: Track if we were previously connected ---
    was_connected = True

    try:
        while True:
            # 1. DRAIN UDP (Get latest data)
            beat_detected = False  # Flag to track if a beat happened in this frame
            log_buffer = None # DEBUG Log buffer for Ardino stuff (used for weight detect debugging)

            # 1. DRAIN UDP
            while True:
                try:
                    data, _ = sock.recvfrom(1024)
                    line = data.decode('utf-8', errors='ignore').strip()

                    # --- NEW: Handle Track Loaded Command ---
                    if line == "CMD_TRACK_LOADED":
                        async with state_lock:
                            track_loaded = True
                            print("--- TRACE: Track loaded via UDP! ---")
                        continue
                    
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

                    # --- NEW: Handle button press from ESP32 ---
                    elif line.startswith("Button: Play"):
                        print(f"--- TRACE: Received '{line}' from ESP32 ---")
                        # Only process if we're in calibrated state (state 1)
                        async with state_lock:
                            if app_state == 1:
                                app_state = 2  # Move to running/warmup state
                            else:
                                print(f"--- TRACE: Button press ignored - Current state: {app_state} ---")

                except BlockingIOError: break
                except Exception: break

            # 2. STATE LOGIC
            async with state_lock:
                current_time = time.time()
                status_msg = ""
                msg_color = "white"

                # --- NEW: Detect Disconnection & Force Re-Calibration ---
                is_connected = (current_time - last_packet_time <= 1.5)
                track_loaded = False
                if not is_connected and was_connected:
                    # Wand just disconnected
                    print("--- TRACE: Wand Disconnected! Forcing Re-Calibration... ---")
                    if app_state == 2:  # Only reset if we were in running mode
                        app_state = 1
                        correction_matrix = np.identity(3)
                        disable_button()  # Disable button on disconnect
                        notify_app_calibration_lost()  # --- NEW FUNCTION ---

                        # --- NEW: Flush UDP buffer on disconnect ---
                        while True:
                            try:
                                sock.recvfrom(1024)
                            except BlockingIOError:
                                break
                        # ----------------------------------------
                    # --- NEW: Also reset track_loaded for state 1 ---
                    elif app_state == 1:  # Calibrated but not playing yet
                        print("--- TRACE: Disconnected during calibrated state - resetting track_loaded ---")
                    # ------------------------------------------------

                was_connected = is_connected
                # --- END NEW ---

                # Timeout Check
                if current_time - last_packet_time > 1.5:
                    status_msg = "WAITING FOR WAND..."
                    msg_color = "#ff4757" # Red
                
                # MODE: CALIBRATION
                elif app_state == 0:
                    elapsed = current_time - countdown_start_time
                    remaining = config.COUNTDOWN_DURATION - elapsed
                    
                    if remaining > 0:
                        status_msg = f"GET READY\n{int(remaining) + 1}"
                        msg_color = "#f39c12"
                    else:
                        # Countdown finished - Auto-calibrate
                        print("--- TRACE: Countdown Complete - SNAP! Calibrating... ---")
                        target = np.array([1.0, 0.0, 0.0], dtype=np.float32)
                        correction_matrix = get_rotation_matrix(raw_wand_vector, target)
                        app_state = 1  # Move to calibration mode
                        calibration_locked = False
                        notify_app_calibration_complete()
                
                # STATE 1: CALIBRATION MODE
                elif app_state == 1:
                    if track_loaded:
                        # Track is loaded, ready for button press
                        status_msg = "READY TO START\nPress 'R' to Re-Calibrate\nPress Button to Begin"
                        msg_color = "#2ed573"  # Green
                    else:
                        # Still waiting for user to load a track
                        status_msg = "CALIBRATED\nPress 'R' to Re-Calibrate\nLoad a track to continue"
                        msg_color = "#f39c12"  # Orange
                
                # STATE 2: RUNNING
                elif app_state == 2:
                    status_msg = "READY"
                    msg_color = "#2ed573"
                
                # 3. CALCULATE VISUALS
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
    print("--- TRACE: Client Connected ---")
    listener_task = asyncio.create_task(command_listener(websocket))
    streamer_task = asyncio.create_task(data_streamer(websocket))
    
    done, pending = await asyncio.wait(
        [listener_task, streamer_task],
        return_when=asyncio.FIRST_COMPLETED,
    )
    for task in pending: task.cancel()

async def main():
    print(f"--- TRACE: WebSocket Server running on port {config.WS_PORT} ---")
    async with websockets.serve(connection_handler, "localhost", config.WS_PORT):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())