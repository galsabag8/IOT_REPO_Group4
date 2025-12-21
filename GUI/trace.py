import numpy as np
import time
from vispy import app, scene
from vispy.scene import visuals
import socket
import sys

# --- CONFIGURATION ---
UDP_IP = "127.0.0.1"
UDP_PORT = 5006
TRAIL_LENGTH = 200

# The point in 3D space representing the center of the screen
CAMERA_CENTER = np.array([-0.6044, 0.4183, 0.1770], dtype=np.float32)

# CAMERA SETTINGS
INIT_ELEVATION = -0.50
INIT_AZIMUTH = 8.00
INIT_DISTANCE = 2.0
INIT_FOV = 40.00

# Timing Constants
CALIB_COUNTDOWN_DURATION = 4.0
SUCCESS_MSG_DURATION = 1.0

# --- UDP SETUP ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.setblocking(False)

# --- GLOBAL DATA ---
raw_wand_vector = np.array([1.0, 0.0, 0.0], dtype=np.float32) # Default start

# This matrix will correct the rotation based on calibration
correction_matrix = np.identity(3, dtype=np.float32)

# Track data flow
last_packet_time = 0 

# Initialize trail
pos_data = np.tile(CAMERA_CENTER, (TRAIL_LENGTH, 1)).astype(np.float32)

# --- STATE MANAGEMENT ---
app_state = 0 
state_timer = time.time()

# --- VISPY SETUP ---
canvas = scene.SceneCanvas(keys='interactive', show=True, size=(1200, 900), title='Conductor Wand (Rotation Fix)')
view = canvas.central_widget.add_view()

# Camera
view.camera = 'turntable'
view.camera.distance = INIT_DISTANCE
view.camera.fov = INIT_FOV
view.camera.azimuth = INIT_AZIMUTH
view.camera.elevation = -INIT_ELEVATION
view.camera.center = tuple(CAMERA_CENTER)

# Visuals
trail_line = visuals.Line(pos=pos_data, color='cyan', width=3, parent=view.scene)
trail_line.visible = False

tip_marker = visuals.Markers(pos=np.array([CAMERA_CENTER]), size=25, face_color='orange', edge_color='white', parent=view.scene)
tip_marker.visible = False

axis = visuals.XYZAxis(parent=view.scene, width=3)
message_text = visuals.Text("", pos=CAMERA_CENTER, color='white', font_size=24, bold=True, anchor_x='center', anchor_y='center', parent=view.scene)

# --- MATH HELPER: ROTATION MATRIX FROM TWO VECTORS ---
def get_rotation_matrix(vec1, vec2):
    """ Find the rotation matrix that rotates vec1 to vec2 """
    # Normalize vectors
    a = vec1 / np.linalg.norm(vec1)
    b = vec2 / np.linalg.norm(vec2)
    
    # Cross product (axis of rotation)
    v = np.cross(a, b)
    
    # Dot product (cosine of angle)
    c = np.dot(a, b)
    
    s = np.linalg.norm(v)
    
    # If vectors are already aligned
    if s == 0:
        return np.identity(3)
        
    # Skew-symmetric cross-product matrix
    k = np.array([[0, -v[2], v[1]], 
                  [v[2], 0, -v[0]], 
                  [-v[1], v[0], 0]])
                  
    # Rodriguez rotation formula
    R = np.identity(3) + k + np.dot(k, k) * ((1 - c) / (s**2))
    return R

def restart_process():
    global app_state, state_timer
    print("-> Restarting Calibration...")
    app_state = 0
    state_timer = time.time()
    trail_line.visible = False
    tip_marker.visible = False
    message_text.visible = True

@canvas.events.key_press.connect
def on_key_press(event):
    if event.text.lower() == 'r':
        restart_process()

def update(event):
    global pos_data, app_state, state_timer, raw_wand_vector, correction_matrix, last_packet_time
    
    # --- 1. UDP RECEIVER ---
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            line = data.decode('utf-8', errors='ignore').strip()
            if line.startswith("DATA,"):
                parts = line.split(',')
                if len(parts) >= 4:
                    rx = float(parts[1])
                    ry = float(parts[2])
                    rz = float(parts[3])
                    
                    # Store Raw Vector
                    vec = np.array([rx, ry, rz], dtype=np.float32)
                    # Safety: Avoid zero vector
                    if np.linalg.norm(vec) > 0:
                        raw_wand_vector = vec
                        last_packet_time = time.time()
        except BlockingIOError:
            break
        except Exception:
            continue

    # --- 2. LOGIC ---
    current_time = time.time()
    elapsed = current_time - state_timer
    
    if app_state == 0: # Countdown
        remaining = CALIB_COUNTDOWN_DURATION - elapsed
        if current_time - last_packet_time > 1.0:
            message_text.text = "NO DATA..."
            message_text.color = 'red'
            state_timer = current_time 
            return

        if remaining > 0:
            message_text.text = f"Point Wand at Screen\nCalibrating in {remaining:.1f}..."
            message_text.color = 'yellow'
        else:
            # --- CALIBRATION MOMENT (THE FIX) ---
            print(f"-> Calibrating Rotation...")
            print(f"-> Current Wand Vector: {raw_wand_vector}")
            target_forward = np.array([1.0, 0.0, 0.0], dtype=np.float32)
            # Calculate the matrix that rotates the CURRENT vector to the TARGET vector
            correction_matrix = get_rotation_matrix(raw_wand_vector, target_forward)
            pos_data[:] = CAMERA_CENTER 
            app_state = 1
            state_timer = current_time

    elif app_state == 1: # Success
        remaining = SUCCESS_MSG_DURATION - elapsed
        if remaining > 0:
            message_text.text = "ALIGNED!"
            message_text.color = '#00ff00' 
        else:
            app_state = 2
            message_text.visible = False
            trail_line.visible = True
            tip_marker.visible = True

    elif app_state == 2: # Running
        aligned_vector = np.dot(correction_matrix, raw_wand_vector)
        screen_x = -aligned_vector[1] # Map Wand Y to Screen X
        screen_y = 0                  # Keep depth flat or use aligned_vector[0]
        screen_z = aligned_vector[2] # Map Wand Z to Screen Z (Inverted?)
        
        relative_pos = np.array([screen_x, screen_y, screen_z])
        
        scale = 2.0 
        final_tip = (relative_pos * scale) + CAMERA_CENTER
        
        pos_data = np.roll(pos_data, -1, axis=0)
        pos_data[-1] = final_tip
        trail_line.set_data(pos=pos_data)
        tip_marker.set_data(pos=np.array([final_tip]))

timer = app.Timer(interval=0, connect=update, start=True)

if __name__ == '__main__':
    app.run()