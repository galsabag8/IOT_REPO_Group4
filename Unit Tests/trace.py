import serial
import numpy as np
from vispy import app, scene
from vispy.scene import visuals
from ahrs.filters import Madgwick
import sys

DEBUG = False
# Properties
SERIAL_PORT = 'COM4'       
BAUD_RATE = 921600         
TRAIL_LENGTH = 100         

# Serial Connection
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.01)
    ser.set_buffer_size(rx_size=8192, tx_size=8192)
    print(f"Connected to {SERIAL_PORT}...")
    ser.reset_input_buffer()
except Exception as e:
    print(f"Error: {e}")
    sys.exit()

# Madgwick Filter and Data Storage
madgwick = Madgwick(frequency=100.0, gain=0.05) 
Q = np.array([1.0, 0.0, 0.0, 0.0])
Q_offset = np.array([1.0, 0.0, 0.0, 0.0])
pos_data = np.zeros((TRAIL_LENGTH, 3), dtype=np.float32)

# --- Vispy Setup ---
canvas = scene.SceneCanvas(keys='interactive', show=True, size=(1200, 900), title='Conductor Wand - Calibration Mode')
view = canvas.central_widget.add_view()
view.camera = 'turntable'

view.camera.distance = 2.21
view.camera.fov = 40.00
view.camera.azimuth = 8.00
view.camera.elevation = -0.50
view.camera.center = (-0.6044126729617566, 0.41839053446625507, 0.17701096816630013)


# Graphics Elements
trail_line = visuals.Line(pos=pos_data, color='cyan', width=3, parent=view.scene)
wand_stick = visuals.Line(pos=np.array([[0,0,0], [0,1,0]]), color='white', width=5, connect='segments', parent=view.scene)
tip_marker = visuals.Markers(pos=np.array([[0, 0, 0]]), size=20, face_color='orange', edge_color='white', parent=view.scene)
if DEBUG:
    axis = visuals.XYZAxis(parent=view.scene)

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
        global Q, pos_data
        
        # איפוס אוריינטציה
        if event.text.lower() == 'r':
            print("Resetting Orientation...")
            Q = np.array([1.0, 0.0, 0.0, 0.0])
            pos_data = np.zeros((TRAIL_LENGTH, 3), dtype=np.float32)
            
        # הדפסת פרמטרים של המצלמה
        elif event.text.lower() == 'p':
            cam = view.camera
            print("\n" + "="*40)
            print("COPY AND PASTE THESE LINES INTO YOUR CODE:")
            print("="*40)
            print(f"view.camera.distance = {cam.distance:.2f}")
            print(f"view.camera.fov = {cam.fov:.2f}")
            print(f"view.camera.azimuth = {cam.azimuth:.2f}")
            print(f"view.camera.elevation = {cam.elevation:.2f}")
            # במקרה שהזזת את המרכז (Pan) עם מקש Shift+עכבר:
            print(f"view.camera.center = {cam.center}") 
            print("="*40 + "\n")

def update(event):
    global Q, pos_data
    
    lines_processed = 0
    while ser.in_waiting:
        try:
            line = ser.readline()
            lines_processed += 1
            text_line = line.decode('utf-8', errors='ignore').strip()
            if not text_line: continue
            
            parts = text_line.split(',')
            if len(parts) != 6: continue
            
            vals = [float(x) for x in parts]
            acc = np.array(vals[0:3])
            acc_norm = np.linalg.norm(acc)
            if acc_norm > 0: acc /= acc_norm
            gyro = np.array(vals[3:6]) * (np.pi / 180.0)
            
            Q = madgwick.updateIMU(Q, gyr=gyro, acc=acc)
            
        except ValueError:
            continue
    
    if lines_processed == 0:
        return

    # Calculate tip position
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

timer = app.Timer(interval=0, connect=update, start=True)

if __name__ == '__main__':
    print("Visualizer Started.")
    app.run()