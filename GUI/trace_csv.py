import numpy as np
from vispy import app, scene
from vispy.scene import visuals
from ahrs.filters import Madgwick
import sys
import csv
import time

# --- CONFIGURATION ---
CSV_FILE = 'wand_data_2_dor.csv'  # <--- Make sure this matches your filename
PLAYBACK_SPEED = 0.001            # 10ms (approx 100Hz) to match real time
TRAIL_LENGTH = 100
CAMERA_CENTER = np.array([-0.6044, 0.4183, 0.1770], dtype=np.float32)
DEBUG = True

# --- BEAT DETECTION CONFIG ---
Z_THRESHOLD = 0.05  # Minimum height change to register a peak/valley (prevents noise)

# --- LOAD DATA ---
try:
    print(f"Loading {CSV_FILE}...")
    data_buffer = []
    with open(CSV_FILE, 'r') as f:
        rownum = 0
        reader = csv.reader(f)
        header = next(reader) # Skip header row
        for row in reader:
            rownum+=1
            # We expect: ax, ay, az, gx, gy, gz (first 6 columns)
            if len(row) >= 6 and rownum > 700:
                # Convert strings to floats
                data_buffer.append([float(x) for x in row[:6]])
    
    print(f"Loaded {len(data_buffer)} frames of motion data.")
    
except FileNotFoundError:
    print(f"Error: Could not find file '{CSV_FILE}'")
    sys.exit()

# Global Playback Index
current_frame_idx = 0

# Madgwick Filter and Data Storage
madgwick = Madgwick(frequency=100.0, gain=0.05) 
Q = np.array([1.0, 0.0, 0.0, 0.0])
Q_offset = np.array([1.0, 0.0, 0.0, 0.0])
pos_data = np.zeros((TRAIL_LENGTH, 3), dtype=np.float32)

# --- GLOBAL VARIABLES FOR BEAT LOGIC ---
# State: 0 = Looking for Min (Descending), 1 = Looking for Max (Ascending)
detection_state = 0 
last_min_z = 100.0  # High initial value
last_min_x = 0.0
curr_max_z = -100.0 # Low initial value
curr_max_x = 0.0
beat_display_timer = 0  # To hide text after a while

# --- Vispy Setup ---
canvas = scene.SceneCanvas(keys='interactive', show=True, size=(1200, 900), title='Conductor Wand - Beat Detection')
view = canvas.central_widget.add_view()
view.camera = 'turntable'
view.camera.distance = 2.94
view.camera.fov = 40.00
view.camera.azimuth = 179.00
view.camera.elevation = 1.00
view.camera.center = (0.0677618467062765, 0.17534370730263985, 0.2212271122316877)

# Graphics Elements
trail_line = visuals.Line(pos=pos_data, color='cyan', width=3, parent=view.scene)
wand_stick = visuals.Line(pos=np.array([[0,0,0], [0,1,0]]), color='white', width=5, connect='segments', parent=view.scene)
tip_marker = visuals.Markers(pos=np.array([[0, 0, 0]]), size=20, face_color='orange', edge_color='white', parent=view.scene)

# Text for Beat Indication
beat_text = visuals.Text('', pos=[0, 0, 0.5], color='yellow', font_size=40, bold=True, parent=view.scene)

if DEBUG:
    axis = visuals.XYZAxis(parent=view.scene, width=3)
    label_x = visuals.Text('X (Left)', pos=[1.1, 0, 0], color='red', font_size=14, bold=True, parent=view.scene)
    label_y = visuals.Text('Y (Depth Outside)',   pos=[0, 1.1, 0], color='green', font_size=14, bold=True, parent=view.scene)
    label_z = visuals.Text('Z (Up)', pos=[0, 0, 1.1], color='blue', font_size=14, bold=True, parent=view.scene)  

# Helper Functions

def rotate_vector(q, v):
    w, x, y, z = q
    x_val = 1.0 - 2.0*(y**2 + z**2)
    y_val = 2.0*(x*y + w*z)
    z_val = 2.0*(x*z - w*y)
    return np.array([x_val, y_val, z_val], dtype=np.float32)

if DEBUG:
    # Key Press Event
    @canvas.events.key_press.connect
    def on_key_press(event):
        global Q, pos_data, detection_state, last_min_z, curr_max_z
        
        # Reset Orientation
        if event.text.lower() == 'r':
            print("Resetting Orientation...")
            Q = np.array([1.0, 0.0, 0.0, 0.0])
            pos_data = np.zeros((TRAIL_LENGTH, 3), dtype=np.float32)
            # Reset detection logic
            detection_state = 0
            last_min_z = 100.0
            curr_max_z = -100.0
            beat_text.text = ''
            
        # Print Camera Params
        elif event.text.lower() == 'p':
            cam = view.camera
            print("\n" + "="*40)
            print("COPY AND PASTE THESE LINES INTO YOUR CODE:")
            print("="*40)
            print(f"view.camera.distance = {cam.distance:.2f}")
            print(f"view.camera.fov = {cam.fov:.2f}")
            print(f"view.camera.azimuth = {cam.azimuth:.2f}")
            print(f"view.camera.elevation = {cam.elevation:.2f}")
            print(f"view.camera.center = {cam.center}") 
            print("="*40 + "\n")

def update(event):
    global Q, pos_data, current_frame_idx, beat_display_timer
    global detection_state, last_min_z, last_min_x, curr_max_z, curr_max_x

    # 1. Get Next Frame Data
    if current_frame_idx >= len(data_buffer):
        current_frame_idx = 0 # Loop back to start
        Q = np.array([1.0, 0.0, 0.0, 0.0]) 
        detection_state = 0 # Reset logic on loop

    vals = data_buffer[current_frame_idx]
    current_frame_idx += 1

    # 2. Process Math
    try:
        acc = np.array(vals[0:3])
        acc_norm = np.linalg.norm(acc)
        if acc_norm > 0: acc /= acc_norm
        
        # Convert Gyro from Deg/s to Rad/s
        gyro = np.array(vals[3:6]) * (np.pi / 180.0)
        
        Q = madgwick.updateIMU(Q, gyr=gyro, acc=acc)

    except Exception as e:
        print(f"Math Error: {e}")
        return

    # 3. Update Graphics
    raw_tip = rotate_vector(Q, np.array([1.0, 0.0, 0.0]))
    
    screen_x = -raw_tip[1] 
    screen_y = raw_tip[0]
    screen_z = -raw_tip[2]
    
    final_tip = np.array([screen_x, screen_y, screen_z])
    
    pos_data = np.roll(pos_data, -1, axis=0)
    pos_data[-1] = final_tip
    
    trail_line.set_data(pos=pos_data)
    wand_stick.set_data(pos=np.array([[0,0,0], final_tip]))
    tip_marker.set_data(pos=np.array([final_tip]), face_color='orange', size=20)

    # --- 4. BEAT DETECTION LOGIC ---
    # Current Z and X from the screen coordinates
    curr_z = screen_z
    curr_x = screen_x

    # State 0: LOOKING FOR MINIMUM (The valley between beats)
    if detection_state == 0:
        if curr_z < last_min_z:
            last_min_z = curr_z
            last_min_x = curr_x # Capture the X at the lowest Z
        
        # If signal rises significantly above the min, we found the valley
        # Switch to looking for Max
        if curr_z > last_min_z + Z_THRESHOLD:
            detection_state = 1
            curr_max_z = -100.0 # Reset max tracker

    # State 1: LOOKING FOR MAXIMUM (The Beat)
    elif detection_state == 1:
        if curr_z > curr_max_z:
            curr_max_z = curr_z
            curr_max_x = curr_x # Capture the X at the highest Z
        
        # If signal drops significantly below the max, a Beat just happened
        if curr_z < curr_max_z - Z_THRESHOLD:
            # BEAT DETECTED!
            
            # Logic: 
            # If Beat X > Min X -> Beat 1
            # If Beat X < Min X -> Beat 2
            
            beat_type = ""
            if curr_max_x > last_min_x:
                beat_type = "BEAT 1"
                beat_text.color = 'lime'
            else:
                beat_type = "BEAT 2"
                beat_text.color = 'magenta'
            
            print(f"Detected: {beat_type} (Max X: {curr_max_x:.3f}, Min X: {last_min_x:.3f})")
            
            # Show on screen
            beat_text.text = beat_type
            beat_text.pos = [0, 0, 0.8] # Position slightly above center
            beat_display_timer = 20 # Show for 20 frames

            # Reset State -> Look for next Min
            detection_state = 0
            last_min_z = 100.0

    # Handle text fade out
    if beat_display_timer > 0:
        beat_display_timer -= 1
    else:
        beat_text.text = ''

# Set timer to match roughly 100Hz playback speed
timer = app.Timer(interval=PLAYBACK_SPEED, connect=update, start=True)

if __name__ == '__main__':
    print("Visualizer Started. Playing CSV with Beat Detection...")
    app.run()