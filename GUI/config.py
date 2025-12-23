# config.py

# --- Flask App ---
UPLOAD_FOLDER = 'uploads'
LIVE_INPUT_FILENAME = 'live_input.mid'

# --- Server ---
FLASK_SERVER_URL = "http://127.0.0.1:5000"

# --- UDP ---
UDP_HOST = "127.0.0.1"
UDP_PORT = 5005
UDP_PORT_VIS = 5006

# --- Serial ---
SERIAL_PORT = 'COM8'   
BAUD_RATE = 921600

# --- Playback ---
DEFAULT_BPM = 120.0
MAX_BPM = 240.0
MIN_BPM = 0.0

# --- Wand ---
WAND_HEARTBEAT_TIMEOUT = 3.0
LISTENER_HEARTBEAT_INTERVAL = 2.0

# --- Visualizer ---
TRAIL_LENGTH = 200
MADGWICK_FREQUENCY = 100.0
MADGWICK_GAIN = 0.05
PLAYBACK_SPEED = 0.03

# --- Simulator ---
CSV_FILE = "wand_data_2_50bpm.csv"
SIMULATOR_PLAYBACK_RATE = 0.01
DEBUG = False
