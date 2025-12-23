import numpy as np
from vispy import app, scene
from vispy.scene import visuals
from ahrs.filters import Madgwick
import socket
import sys
import config

# --- UDP SETUP ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((config.UDP_HOST, config.UDP_PORT_VIS))
sock.setblocking(False) # Non-blocking to prevent freezing

# --- MATH SETUP ---
madgwick = Madgwick(frequency=config.MADGWICK_FREQUENCY, gain=config.MADGWICK_GAIN) 
Q = np.array([1.0, 0.0, 0.0, 0.0])
pos_data = np.zeros((config.TRAIL_LENGTH, 3), dtype=np.float32)

# --- VISPY SETUP ---
# (Using the exact camera settings you provided)
canvas = scene.SceneCanvas(keys='interactive', show=True, size=(1200, 900), title='Conductor Wand')
view = canvas.central_widget.add_view()
view.camera = 'turntable'
view.camera.distance = 2.21
view.camera.fov = 40.00
view.camera.azimuth = 8.00
view.camera.elevation = -0.50
view.camera.center = (-0.6044, 0.4183, 0.1770)

# Objects
trail_line = visuals.Line(pos=pos_data, color='cyan', width=3, parent=view.scene)
#wand_stick = visuals.Line(pos=np.array([[0,0,0], [0,1,0]]), color='white', width=5, connect='segments', parent=view.scene)
tip_marker = visuals.Markers(pos=np.array([[0, 0, 0]]), size=20, face_color='orange', edge_color='white', parent=view.scene)

def rotate_vector(q, v):
    w, x, y, z = q
    x_val = 1.0 - 2.0*(y**2 + z**2)
    y_val = 2.0*(x*y + w*z)
    z_val = 2.0*(x*z - w*y)
    return np.array([x_val, y_val, z_val], dtype=np.float32)

def update(event):
    global Q, pos_data
    
    # Process all pending UDP packets
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            line = data.decode('utf-8', errors='ignore').strip()
            # We expect "DATA,ax,ay,az,gx,gy,gz"
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
            break # No more data
        except Exception:
            continue

    # Update Graphics
    raw_tip = rotate_vector(Q, np.array([1.0, 0.0, 0.0]))
    final_tip = np.array([-raw_tip[1], raw_tip[0], -raw_tip[2]])
    
    pos_data = np.roll(pos_data, -1, axis=0)
    pos_data[-1] = final_tip
    
    trail_line.set_data(pos=pos_data)
    #wand_stick.set_data(pos=np.array([[0,0,0], final_tip]))
    tip_marker.set_data(pos=np.array([final_tip]))

timer = app.Timer(interval=0, connect=update, start=True)

if __name__ == '__main__':
    app.run()