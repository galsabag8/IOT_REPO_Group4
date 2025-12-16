import numpy as np
import time
from vispy import app, scene
from vispy.scene import visuals
from ahrs.filters import Madgwick
import socket
import sys

# --- CONFIGURATION ---
UDP_IP = "127.0.0.1"
UDP_PORT = 5006
TRAIL_LENGTH = 200

# The point the camera looks at (and now the Wand's origin)
CAMERA_CENTER = np.array([-0.6044, 0.4183, 0.1770], dtype=np.float32)

# Timing Constants
CALIB_COUNTDOWN_DURATION = 3.0
SUCCESS_MSG_DURATION = 1.5

# --- UDP SETUP ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.setblocking(False)

# --- MATH SETUP ---
madgwick = Madgwick(frequency=100.0, gain=0.05) 
Q = np.array([1.0, 0.0, 0.0, 0.0])

# Initialize trail with the center position so it doesn't streak from (0,0,0)
pos_data = np.tile(CAMERA_CENTER, (TRAIL_LENGTH, 1)).astype(np.float32)

# --- STATE MANAGEMENT ---
app_state = 0 
state_timer = time.time()

# --- VISPY SETUP ---
canvas = scene.SceneCanvas(keys='interactive', show=True, size=(1200, 900), title='Conductor Wand')
view = canvas.central_widget.add_view()

# Camera Settings
view.camera = 'turntable'
view.camera.distance = 2.21
view.camera.fov = 40.00
view.camera.azimuth = 8.00
view.camera.elevation = -0.50
view.camera.center = tuple(CAMERA_CENTER) # Vispy needs a tuple here

# 1. Trail Line
trail_line = visuals.Line(pos=pos_data, color='cyan', width=3, parent=view.scene)
trail_line.visible = False

# 2. Wand Tip Marker
tip_marker = visuals.Markers(pos=np.array([CAMERA_CENTER]), size=20, face_color='orange', edge_color='white', parent=view.scene)
tip_marker.visible = False

axis = visuals.XYZAxis(parent=view.scene, width=3)
label_x = visuals.Text('X (Left)', pos=[1.1, 0, 0], color='red', font_size=14, bold=True, parent=view.scene)
label_y = visuals.Text('Y (Depth Outside)',   pos=[0, 1.1, 0], color='green', font_size=14, bold=True, parent=view.scene)
label_z = visuals.Text('Z (Up)', pos=[0, 0, 1.1], color='blue', font_size=14, bold=True, parent=view.scene)  

# 3. Message Text (Exactly at Camera Center)
message_text = visuals.Text("", 
                            pos=CAMERA_CENTER, 
                            color='white', 
                            font_size=28, 
                            bold=True, 
                            anchor_x='center', 
                            anchor_y='center',
                            parent=view.scene)

# --- HELPER FUNCTIONS ---

def rotate_vector(q, v):
    """ Rotates vector v by quaternion q """
    w, x, y, z = q
    x_val = 1.0 - 2.0*(y**2 + z**2)
    y_val = 2.0*(x*y + w*z)
    z_val = 2.0*(x*z - w*y)
    return np.array([x_val, y_val, z_val], dtype=np.float32)

def restart_process():
    """ Resets the state machine to the beginning """
    global app_state, state_timer
    print("-> Restarting Calibration Process...")
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
    global Q, pos_data, app_state, state_timer
    
    # --- 1. ALWAYS Process UDP packets ---
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            line = data.decode('utf-8', errors='ignore').strip()
            if line.startswith("DATA,"):
                parts = line.split(',')
                if len(parts) == 7:
                    vals = [float(x) for x in parts[1:]]
                    acc = np.array(vals[0:3])
                    acc_norm = np.linalg.norm(acc)
                    if acc_norm > 0: acc /= acc_norm
                    gyro = np.array(vals[3:6]) * (np.pi / 180.0)
                    Q = madgwick.updateIMU(Q, gyr=gyro, acc=acc)
        except BlockingIOError:
            break
        except Exception:
            continue

    # --- 2. STATE MACHINE LOGIC ---
    current_time = time.time()
    elapsed = current_time - state_timer
    
    if app_state == 0: # STATE: Countdown
        remaining = CALIB_COUNTDOWN_DURATION - elapsed
        if remaining > 0:
            message_text.text = f"Hold Wand Steady...\nCalibration in {remaining:.1f}"
            message_text.color = 'yellow'
        else:
            # Time to calibrate!
            print("-> PERFORMING CALIBRATION")
            Q = np.array([1.0, 0.0, 0.0, 0.0]) 
            
            # Reset trail to start exactly at center (prevent visual glitches)
            pos_data[:] = CAMERA_CENTER 
            
            app_state = 1
            state_timer = current_time

    elif app_state == 1: # STATE: Success Message
        remaining = SUCCESS_MSG_DURATION - elapsed
        if remaining > 0:
            message_text.text = "CALIBRATION DONE!\nStart Playing"
            message_text.color = '#00ff00' 
        else:
            app_state = 2
            message_text.visible = False
            trail_line.visible = True
            tip_marker.visible = True

    elif app_state == 2: # STATE: Running
        
        # 1. Get Rotation relative to (0,0,0)
        raw_tip = rotate_vector(Q, np.array([1.0, 0.0, 0.0]))
        # 2. Map axes
        mapped_tip = np.array([-raw_tip[1], raw_tip[0], -raw_tip[2]], dtype=np.float32)
        # 3. Shift to Camera Center
        final_tip = mapped_tip + CAMERA_CENTER
        
        # Update Trail
        pos_data = np.roll(pos_data, -1, axis=0)
        pos_data[-1] = final_tip
        trail_line.set_data(pos=pos_data)
        
        # Update Tip Marker
        tip_marker.set_data(pos=np.array([final_tip]))

timer = app.Timer(interval=0, connect=update, start=True)

if __name__ == '__main__':
    print("Visualizer Started.")
    print("Use 'R' to restart calibration process.")
    app.run()