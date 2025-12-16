import pandas as pd
import numpy as np
from vispy import app, scene
from vispy.scene import visuals
from ahrs.filters import Madgwick
import sys

# --- הגדרות קובץ והרצה ---
CSV_FILENAME = 'wand_data_4_80bpm.csv'
SAMPLE_RATE = 100.0
PLAYBACK_SPEED = 1.0 
TRAIL_LENGTH = 200

# הגדרות המצלמה שלך
INIT_ELEVATION = 0.0
INIT_AZIMUTH = -180.0
INIT_DISTANCE = 3.0

# --- טעינת נתונים ---
try:
    full_df = pd.read_csv(CSV_FILENAME)
    START_ROW = 200
    if len(full_df) > START_ROW:
        df = full_df.iloc[START_ROW:].reset_index(drop=True)
        print(f"Skipped first {START_ROW} rows. Playing {len(df)} samples.")
    else:
        df = full_df
    
    required_cols = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']
    if not all(col in df.columns for col in required_cols):
        sys.exit()
        
except Exception as e:
    print(f"Error: {e}")
    sys.exit()

data_acc = df[['ax', 'ay', 'az']].values
data_gyro = df[['gx', 'gy', 'gz']].values
total_samples = len(df)
current_idx = 0
is_paused = False

# --- אתחול אלגוריתם ---
madgwick = Madgwick(frequency=SAMPLE_RATE, gain=0.05)
Q = np.array([1.0, 0.0, 0.0, 0.0])
pos_data = np.zeros((TRAIL_LENGTH, 3), dtype=np.float32)

# --- Vispy Setup ---
canvas = scene.SceneCanvas(keys='interactive', show=True, size=(1200, 900), title=f'Replay & Beat Detection')
view = canvas.central_widget.add_view()
view.camera = 'turntable'
view.camera.elevation = INIT_ELEVATION
view.camera.azimuth = INIT_AZIMUTH
view.camera.distance = INIT_DISTANCE
view.camera.fov = 45 

# טקסטים
status_text = visuals.Text("Press 'S' to Start/Stop", pos=(0, 0.9, 0), color='white', font_size=12, parent=view.scene)
beat_text = visuals.Text("", pos=(0, 0.7, 0), color='yellow', bold=True, font_size=24, parent=view.scene) # טקסט לפעמות

trail_line = visuals.Line(pos=pos_data, color='cyan', width=3, parent=view.scene)
wand_stick = visuals.Line(pos=np.array([[0,0,0], [0,1,0]]), color='white', width=5, connect='segments', parent=view.scene)
tip_marker = visuals.Markers(pos=np.array([[0, 0, 0]]), size=20, face_color='orange', edge_color='white', parent=view.scene)

# צירים
axis = visuals.XYZAxis(parent=view.scene, width=4) 
axis_labels = visuals.Text(text=['X', 'Y', 'Z'], pos=np.array([[1.2, 0, 0], [0, 1.2, 0], [0, 0, 1.2]]), color=['red', 'green', 'blue'], bold=True, font_size=20, parent=view.scene)

# --- פונקציות ---

def rotate_vector(q, v):
    w, x, y, z = q
    x_val = 1.0 - 2.0*(y**2 + z**2)
    y_val = 2.0*(x*y + w*z)
    z_val = 2.0*(x*z - w*y)
    return np.array([x_val, y_val, z_val], dtype=np.float32)

def update(event):
    global current_idx, Q, pos_data, is_paused

    if is_paused: return 
    if current_idx >= total_samples:
        status_text.text = "Finished. Press 'R' to restart."
        return

    raw_acc = data_acc[current_idx]
    raw_gyro = data_gyro[current_idx]

    acc_norm = np.linalg.norm(raw_acc)
    acc = raw_acc / acc_norm if acc_norm > 0 else raw_acc
    gyro = raw_gyro * (np.pi / 180.0)
    
    Q = madgwick.updateIMU(Q, gyr=gyro, acc=acc)
    raw_tip = rotate_vector(Q, np.array([1.0, 0.0, 0.0]))

    screen_x = -raw_tip[1] 
    screen_y = raw_tip[0]
    screen_z = -raw_tip[2]
    final_tip = np.array([screen_x, screen_y, screen_z])


    pos_data = np.roll(pos_data, -1, axis=0)
    pos_data[-1] = final_tip

    trail_line.set_data(pos=pos_data)
    wand_stick.set_data(pos=np.array([[0,0,0], final_tip]))
    tip_marker.set_data(pos=np.array([final_tip]))
    
    progress = (current_idx / total_samples) * 100
    status_text.text = f"Playing... {progress:.1f}%"
    current_idx += 1

@canvas.events.key_press.connect
def on_key_press(event):
    global current_idx, Q, pos_data, is_paused, last_min_x, tip_history
    
    key = event.text.lower()
    if key == 'r':
        current_idx = 0
        Q = np.array([1.0, 0.0, 0.0, 0.0])
        pos_data = np.zeros((TRAIL_LENGTH, 3), dtype=np.float32)
        last_min_x = None # איפוס לוגיקה
        tip_history = []
        beat_text.text = ""
        is_paused = False 
    elif key == 's':
        is_paused = not is_paused
        status_text.color = 'red' if is_paused else 'white'

interval = (1.0 / SAMPLE_RATE) / PLAYBACK_SPEED
timer = app.Timer(interval=interval, connect=update, start=True)

if __name__ == '__main__':
    app.run()