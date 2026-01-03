# ------ app.py ------
PORT_CMD = 5007             # Command port for Listener Hub
UPLOAD_FOLDER = 'uploads'

# ------ listener.py ------
SERIAL_PORT = 'COM4'    # com port to which the wand is connected, update as needed
BAUD_RATE = 921600     
IP = "127.0.0.1"
PORT_MUSIC = 5005       # Port for app.py (BPM)
PORT_VIS = 5006         # Port for visualizer.py (3D Wand)
PORT_CMD = 5007         # Listening for commands from app.py
LOG_DIR = "logs"        # CSV CONFIG

# ------ trace.py ------
WS_PORT = 8765
COUNTDOWN_DURATION = 3  # Countdown duration in seconds