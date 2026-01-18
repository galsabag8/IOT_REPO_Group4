import os
import subprocess 
import sys
import threading
import time
import mido
import atexit
import json
from mido import tempo2bpm
from flask import Flask, render_template, request, jsonify
import socket
import csv
import config
import numpy as np

# --- IMPORT YOUR LISTENER MODULE ---
import listener 

app = Flask(__name__)

# --- GLOBAL STATE ---
playback_state = {
    "bpm": 67.0,
    "is_playing": False,
    "is_paused": False,
    "wand_enabled": False, 
    "filename": None,
    "thread": None,
    "current_ticks": 0,
    "total_ticks": 0,
    "ticks_per_beat": 480,
    "original_duration": 0.0,
    "weight": 0,
    "record_enabled": False,
    "replay_active": False,
    "wand_connected": False,
    "last_beat_received": 0,
    "button_state": False,
    "correction_matrix": None,
    "debug_enabled": False,
    "debug_queue": []
}

# --- GUI PROCESS KEEPER ---
gui_process = None
is_cleaning_up = False

def matrix_receiver():
    """Listens for correction matrix updates from trace.py"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((config.IP, config.PORT_MATRIX))
    sock.setblocking(False)
    
    while True:
        try:
            data, _ = sock.recvfrom(1024)
            msg = json.loads(data.decode('utf-8'))
            if 'matrix' in msg:
                matrix_values = msg['matrix']
                playback_state['correction_matrix'] = np.array(matrix_values, dtype=np.float32).reshape(3, 3)
        except BlockingIOError:
            time.sleep(0.05)
        except json.JSONDecodeError:
            pass
        except Exception as e:
            print(f"Matrix Receiver Error: {e}")
            time.sleep(0.1)

# --- Helper to send replay matrix to trace.py via UDP ---
def send_replay_matrix_to_trace(matrix):
    """Send a replay matrix to trace.py via UDP with MATRIX: prefix"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if matrix is not None:
            # Format: MATRIX:v1,v2,v3,v4,v5,v6,v7,v8,v9
            matrix_values = matrix.flatten().tolist()
            matrix_str = ",".join([str(v) for v in matrix_values])
            matrix_data = f"MATRIX:{matrix_str}"
        else:
            matrix_data = "MATRIX:CLEAR"
        sock.sendto(matrix_data.encode('utf-8'), (config.IP, config.PORT_VIS))
    except Exception as e:
        print(f"--- APP: Failed to send replay matrix: {e} ---")

# --- HELPER: MANAGE GUI WINDOW ---
def open_gui():
    """ Opens the Visualization Window if not already open """
    global gui_process
    if gui_process is None:
        print("--- APP: Launching GUI Window (trace.py)... ---")
        # Ensure 'trace.py' is in the same directory
        gui_process = subprocess.Popen([sys.executable, 'trace.py'])

def close_gui():
    """ Closes the Visualization Window if open """
    global gui_process
    if gui_process:
        print("--- APP: Closing GUI Window... ---")
        gui_process.terminate()
        gui_process = None

def cleanup():
    """ Kills the GUI and stops all playback immediately """
    global is_cleaning_up
    if is_cleaning_up:
        return
    is_cleaning_up = True
    print("--- APP: Cleaning up resources... ---")
    
    # 1. Flag the system to stop
    playback_state["is_playing"] = False
    playback_state["wand_enabled"] = False
    
    # 2. Silence all MIDI notes (The "Panic" Loop)
    # We open a temporary port just to send the silence commands
    try:
        with mido.open_output() as port:
            for ch in range(16):
                port.send(mido.Message('control_change', channel=ch, control=123, value=0)) # All Notes Off
                port.send(mido.Message('control_change', channel=ch, control=64, value=0))  # Sustain Pedal Off
    except:
        pass

    try:
            udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            msg = "CLOSING APP"
            udp_sock.sendto(msg.encode('utf-8'), ("127.0.0.1", config.PORT_CMD))
    except Exception as e:
            print(f"--- APP: Failed to send reset command: {e} ---")

    # 3. Kill the Visualizer Window

    close_gui()

atexit.register(cleanup)

# --- UDP LISTENER ---
def udp_music_listener():
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind(("127.0.0.1", 5005)) 
    udp_sock.setblocking(False)
    
    while playback_state["is_playing"]:
        try:
            data, addr = udp_sock.recvfrom(1024)
            line = data.decode('utf-8').strip()
            if playback_state["replay_active"]:
                continue

            if line.startswith("BPM: "):
                if playback_state["wand_enabled"]:
                    try:
                        raw_val = float(line.split(":")[1].strip())
                        apply_bpm_logic(raw_val)
                    except ValueError:
                        pass
            
        except BlockingIOError:
            time.sleep(0.01)
        except Exception as e:
            print(f"UDP Error: {e}")
            time.sleep(0.1)
    udp_sock.close()


os.makedirs(config.UPLOAD_FOLDER, exist_ok=True)

# --- HELPER: CENTRALIZED BPM LOGIC ---
def apply_bpm_logic(raw_bpm):
    global playback_state
    if raw_bpm > 240: raw_bpm = 240.0
    if raw_bpm < 0: raw_bpm = 0.0
    
    if raw_bpm == 0:
        playback_state["is_paused"] = True
    elif playback_state["bpm"] == 0 and raw_bpm > 0:
        playback_state["is_paused"] = False
        
    playback_state["bpm"] = raw_bpm
    return raw_bpm

# --- NEW: Load correction matrix from CSV header ---
def load_correction_matrix_from_csv(csv_path):
    """
    Reads the CSV and extracts the correction matrix from a #MATRIX row.
    Returns (matrix, data_rows) where matrix is None if not found.
    """
    matrix = None
    data_rows = []
    
    try:
        with open(csv_path, 'r') as f:
            reader = csv.reader(f)
            header = next(reader)  # Skip header row

            for row in reader:
                if len(row) > 0 and row[0] == "#MATRIX":
                    # Extract the 9 matrix values and reshape to 3x3
                    try:
                        matrix_values = [float(v) for v in row[1:10]]
                        matrix = np.array(matrix_values, dtype=np.float32).reshape(3, 3)
                    except (ValueError, IndexError) as e:
                        print(f"--- APP: Error parsing matrix row: {e} ---")
                elif len(row) >= 5:
                    # Regular data row
                    data_rows.append(row)
                    
    except Exception as e:
        print(f"--- APP: Error reading CSV: {e} ---")
    
    return matrix, data_rows


# --- REPLAY DRIVER ---
def replay_driver(csv_path,wand_mode):
    """ Reads CSV and simulates live events for Visuals and BPM """
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Send command to trace.py to enter replay mode
    try:
        # Load matrix and data rows
        matrix, rows = load_correction_matrix_from_csv(csv_path)

        # Send replay matrix to trace.py via UDP
        if matrix is not None:
            send_replay_matrix_to_trace(matrix)
        else:
            send_replay_matrix_to_trace(np.identity(3, dtype=np.float32))
        if not rows: 
            close_gui() # Close if file empty
            return

        start_t = float(rows[0][0]) 
        system_start_time = time.time()

        row_idx = 0
        total_rows = len(rows)

        while playback_state["is_playing"] and row_idx < total_rows:
            while playback_state["is_paused"] and playback_state["is_playing"]:
                time.sleep(0.05)
            elapsed = time.time() - system_start_time
            row_t = float(rows[row_idx][0]) - start_t
            
            if elapsed >= row_t:
                row_data = rows[row_idx]
                
                try:
                    csv_bpm = float(row_data[4])
                    apply_bpm_logic(csv_bpm)
                except: pass

                # Send Visual Data
                sensor_str = f"DATA,{row_data[1]},{row_data[2]},{row_data[3]}"
                sock.sendto(sensor_str.encode('utf-8'), (config.IP, config.PORT_VIS))
                
                row_idx += 1
            else:
                time.sleep(0.001)

    except Exception as e:
        print(f"Replay Error: {e}")
    
    general_stop()
    if not wand_mode:
        close_gui()
    else:
        playback_state["wand_enabled"] = True  # Keep Wand Mode active


    

# --- PLAYBACK ENGINE ---
def playback_engine():

    global playback_state
    try:
        if not playback_state["filename"]: return

        mid = mido.MidiFile(playback_state["filename"])
        messages = mido.merge_tracks(mid.tracks)
        needed = False

        while not playback_state["button_state"] and playback_state["wand_enabled"]:
            time.sleep(0.1)
        
        with mido.open_output() as port:
            for msg in messages:
                if not playback_state["is_playing"]: break
                if playback_state["is_playing"] and playback_state["wand_enabled"] and (not playback_state["wand_connected"] or (not playback_state["button_state"])): break
                
                while (playback_state["is_paused"] or playback_state["bpm"] <= 0) and playback_state["is_playing"]:
                    if needed and playback_state["wand_enabled"]:
                        next_beat = get_next_beat_number()
                        try:
                                udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                                msag = f"PAUSE: {next_beat}"
                                udp_sock.sendto(msag.encode('utf-8'), ("127.0.0.1", config.PORT_CMD))
                        except Exception as e:
                                print(f"--- APP: Failed to send PAUSE command: {e} ---")
                        needed = False

                    for ch in range(16):
                        try:
                            # CC 123 = All Notes Off (stops ringing notes)
                            port.send(mido.Message('control_change', channel=ch, control=123, value=0))
                            # CC 64 = Sustain Pedal Off (lifts the pedal if it was down)
                            port.send(mido.Message('control_change', channel=ch, control=64, value=0))
                        except:
                            pass
                    if playback_state["wand_enabled"] and (not playback_state["wand_connected"] or not playback_state["button_state"]): break
                    time.sleep(0.05)
                needed = True 
                sleep_time = 0

                if msg.time > 0:

                    playback_state["current_ticks"] += msg.time
                    current_bpm = playback_state["bpm"]
                    if current_bpm <= 0: current_bpm = 120                     
                    seconds_per_beat = 60.0 / current_bpm
                    sleep_time = msg.time * (seconds_per_beat / mid.ticks_per_beat)
                    time.sleep(sleep_time)

                if msg.type == 'set_tempo':
                    # Only apply auto-tempo if we are NOT in Wand Mode and NOT in Replay Mode
                    # (In those modes, the Wand or the CSV should dictate the speed)
                    if not playback_state["wand_enabled"] and not playback_state["replay_active"]:
                        new_bpm = tempo2bpm(msg.tempo)
                        playback_state["bpm"] = new_bpm

                if not msg.is_meta:
                    port.send(msg)
    except Exception as e:
        print(f"Playback Error: {e}")
    
    general_stop()
def get_weight_count(mid_object):
    """
    Returns the numerator (number of beats) of the time signature.
    Defaults to 4 if no time_signature message is found.
    """
    for track in mid_object.tracks:
        for msg in track:
            if msg.type == 'time_signature':
                return msg.numerator
    return 4  # Standard MIDI default

def extract_smart_metadata(mid_obj):
    """
    Scans all tracks for track_name messages to find the best Title and Artist.
    """
    candidates = []
    detected_bpm = 67.0
    
    # 1. Gather all unique, non-empty text names
    for track in mid_obj.tracks:
        for msg in track:
            if msg.type == 'track_name':
                text = msg.name.strip()
                if text and text.lower() not in ['untitled', 'copyright', 'track']:
                    candidates.append(text)
            if msg.type == "set_tempo":
                detected_bpm = tempo2bpm(msg.tempo)

    # Remove duplicates while preserving order
    unique_candidates = []
    [unique_candidates.append(x) for x in candidates if x not in unique_candidates]

    title = "Unknown Track"
    artist = ""

    # 2. Smart Extraction Logic
    for text in unique_candidates:
        lower_text = text.lower()
        
        # DETECT ARTIST: Look for "by..."
        if "by " in lower_text or "composed" in lower_text:
            clean_artist = text.replace("by ", "").replace("By ", "").strip()
            # If we don't have an artist yet, take this one
            if not artist: 
                artist = clean_artist
            continue # Don't treat this as a title
            
        # DETECT TITLE: The longest remaining string is usually the proper title
        # (e.g. "1st Mvmt Sonata..." is better than "Sonata")
        if len(text) > len(title) or title == "Unknown Track":
            title = text

    # 3. Formatting
    full_display = title
    if artist:
        full_display = f"{title} ({artist})"

    playback_state["total_ticks"] = max(sum(msg.time for msg in track) for track in mid_obj.tracks)
    playback_state["original_duration"] = mid_obj.length
    playback_state["current_ticks"] = 0
    playback_state["ticks_per_beat"] = mid_obj.ticks_per_beat
    detected_weight = get_weight_count(mid_obj)
    playback_state["weight"] = detected_weight

        
    return full_display, detected_bpm

def get_next_beat_number():
    """
    Calculates the NEXT beat number (1-based) that will occur.
    Example: If currently playing Beat 1.5, returns 2.
    If currently playing Beat 4.1, returns 1.
    """
    current_ticks = playback_state["current_ticks"]
    ticks_per_beat = playback_state.get("ticks_per_beat", 480) # Default to 480 if missing
    weight = playback_state["weight"]
    if weight == 0: weight = 4 # Safety default

    # 1. Calculate how many full beats have passed
    # e.g., if ticks=720 and tpb=480, absolute_beat_index = 1 (we are inside the 2nd beat)
    absolute_beat_index = int(current_ticks / ticks_per_beat)

    # 2. We want the NEXT beat, so we look ahead by 1
    next_beat_index = absolute_beat_index + 1

    # 3. Wrap around the measure using modulo
    # (next_beat_index % weight) gives 0..3, so we add 1 to get 1..4
    # Logic check:
    # If at Beat 1.5 (index 0), next is 1. (1%4)+1 = 2. Correct.
    # If at Beat 4.5 (index 3), next is 4. (4%4)+1 = 1. Correct.
    next_beat_number = (next_beat_index % weight) + 1
    
    return next_beat_number



def general_stop():
    """ Stops all playback and resets state """
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    playback_state["replay_active"] = False
    playback_state["bpm"] = 67.0
    playback_state["filename"] = None
    playback_state["current_ticks"] = 0
    playback_state["total_ticks"] = 0
    playback_state["original_duration"] = 0.0
    playback_state["last_beat_received"] = 0
    playback_state["weight"] = 0
    playback_state["button_state"] = False


   # Clear replay matrix via UDP
    send_replay_matrix_to_trace(None)
    if playback_state["wand_enabled"]:

        try:
                udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                msg = "RESET"
                udp_sock.sendto(msg.encode('utf-8'), ("127.0.0.1", config.PORT_CMD))
        except Exception as e:
                print(f"--- APP: Failed to send reset command: {e} ---")

    

# --- ROUTES ---
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/set_record_mode', methods=['POST'])
def set_record_mode():
    data = request.json
    enabled = data.get('enabled', False)
    playback_state["record_enabled"] = enabled
    matrix_thread = threading.Thread(target=matrix_receiver, daemon=True)
    matrix_thread.start()
    return jsonify({"status": "success", "enabled": enabled})

@app.route('/toggle_debug', methods=['POST'])
def toggle_debug():
    # Toggle state
    if not playback_state["wand_connected"]:
        return jsonify({"status": "error", "msg": "Wand not connected"}), 400
    playback_state["debug_enabled"] = not playback_state["debug_enabled"]
    new_state = playback_state["debug_enabled"]
    
    # Send Command to Listener (which forwards to ESP)
    cmd = "DEBUG:ON" if new_state else "DEBUG:OFF"
    try:
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.sendto(cmd.encode('utf-8'), ("127.0.0.1", config.PORT_CMD))
    except Exception as e:
        print(f"Error sending debug toggle: {e}")

    return jsonify({"status": "success", "enabled": new_state})
@app.route('/set_wand_mode', methods=['POST'])
def set_wand_mode():
    data = request.json
    enabled = data.get('enabled', False)
    playback_state["wand_enabled"] = enabled
    
    if enabled:
        # Wand Mode ON -> Open GUI
        open_gui()
    else:
        # Wand Mode OFF -> Close GUI
        close_gui()

    return jsonify({"status": "success", "enabled": enabled})

@app.route('/start_replay', methods=['POST'])
def start_replay():
    if 'midiFile' not in request.files or 'csvFile' not in request.files:
        return jsonify({"status": "error", "msg": "Missing files"}), 400
        
    midi_file = request.files['midiFile']
    csv_file = request.files['csvFile']

    midi_path = os.path.join(config.UPLOAD_FOLDER, 'replay_temp.mid')
    csv_path = os.path.join(config.UPLOAD_FOLDER, 'replay_temp.csv')
    midi_file.save(midi_path)
    csv_file.save(csv_path)

    playback_state["filename"] = midi_path
    playback_state["is_playing"] = True
    playback_state["is_paused"] = False
    playback_state["replay_active"] = True
    wand_mode = playback_state["wand_enabled"]
    playback_state["wand_enabled"] = False
    playback_state["bpm"] = 0.0  # Start paused until CSV drives it 
    
    # Start Playback Threads
    playback_state["thread"] = threading.Thread(target=playback_engine)
    playback_state["thread"].daemon = True
    playback_state["thread"].start()

    open_gui()
    time.sleep(5)  # Give GUI time to open

    replay_t = threading.Thread(target=replay_driver, args=(csv_path,wand_mode,))
    replay_t.daemon = True
    replay_t.start()
    

    return jsonify({"status": "success", "track_name": midi_file.filename})

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():
    if 'midiFile' not in request.files: return jsonify({"status": "error"}), 400
    file = request.files['midiFile']
    
    is_wand_mode = request.form.get('wand_mode') == 'true'
    playback_state["wand_enabled"] = is_wand_mode 
    playback_state["replay_active"] = False
    
    if file.filename == '': return jsonify({"status": "error"}), 400

    filepath = os.path.join(config.UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)

    # 1. Calculate Weight
    try:
        mid_object = mido.MidiFile(filepath)
    except Exception as e:
        print(f"Error loading MIDI: {e}")
        return jsonify({"status": "error", "message": "Invalid MIDI file"}), 400
    
    smart_name, detected_bpm = extract_smart_metadata(mid_object)
    start_bpm = 0.0 if is_wand_mode else detected_bpm
    
    playback_state["filename"] = filepath
    playback_state["is_playing"] = True
    playback_state["is_paused"] = not is_wand_mode
    playback_state["bpm"] = start_bpm
    if is_wand_mode:
         # Send Weight to Arduino
        try:
            udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            msg = f"WEIGHT:{playback_state['weight']}"
            udp_sock.sendto(msg.encode('utf-8'), ("127.0.0.1", config.PORT_CMD))
        except Exception as e:
            print(f"--- APP: Failed to send weight: {e} ---")
        playback_state['udp_thread'] = threading.Thread(target=udp_music_listener, daemon=True)
        playback_state['udp_thread'].start()
        
    
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()

    # NOTE: We do not force open GUI here. 
    # Wand Mode toggle handles opening/closing. 
    # If we are in Wand Mode, GUI is already open.
    
    return jsonify({"status": "success", "start_bpm": start_bpm, "track_name": smart_name, "detected_weight": playback_state["weight"]})

@app.route('/progress')
def progress():
    current_time_display = 0.0
    current_logs = list(playback_state["debug_queue"]) 
    playback_state["debug_queue"].clear()
    if playback_state["total_ticks"] > 0:
        percent = playback_state["current_ticks"] / playback_state["total_ticks"]
        current_time_display = percent * playback_state["original_duration"]
    return jsonify({
        "progress_percent": (playback_state["current_ticks"] / playback_state["total_ticks"]) * 100 if playback_state["total_ticks"] > 0 else 0,
        "current_time_str": current_time_display,
        "total_time_str": playback_state["original_duration"],
        "is_playing": playback_state["is_playing"],
        "current_bpm": playback_state["bpm"],
        "record_enabled": playback_state["record_enabled"],
        "replay_active": playback_state["replay_active"],
        "current_beat": playback_state.get("last_beat_received", 0),
        "button_state": playback_state["button_state"],
        "debug_enabled": playback_state["debug_enabled"],
        "debug_logs": current_logs,
        "weight": playback_state["weight"]
    })

@app.route('/pause', methods=['POST'])
def pause():
    playback_state["is_paused"] = True
    return jsonify({"status": "paused"})

@app.route('/resume', methods=['POST'])
def resume():
    if playback_state["bpm"] > 0: playback_state["is_paused"] = False
    return jsonify({"status": "resumed"})

@app.route('/set_bpm', methods=['POST'])
def set_bpm():
    if playback_state["replay_active"]:
        return jsonify({"status": "ignored_replay_active"})
    try:
        raw_bpm = float(request.json['bpm'])
        final_bpm = apply_bpm_logic(raw_bpm)
        return jsonify({"status": "success", "bpm": final_bpm})
    except:
        return jsonify({"status": "error"}), 400

@app.route('/stop', methods=['POST'])
def stop():
    # If we were in Replay Mode, we must close the GUI now
    if(playback_state["is_playing"]):
        general_stop()
    
    return jsonify({"status": "stopped"})

@app.route('/reset', methods=['POST'])
def reset():
    time.sleep(0.1)
    playback_state["wand_enabled"] = False
    playback_state["debug_enabled"] = False
    playback_state["record_enabled"] = False
    general_stop()
    close_gui() # Reset kills everything
    return jsonify({"status": "reset_complete"})

@app.route('/wand_status')
def get_wand_status():
    return jsonify({
        "connected": playback_state["wand_connected"],
        "enabled": playback_state["wand_enabled"]
    })


if __name__ == '__main__':
    t = threading.Thread(target=listener.listen, args=(playback_state,), daemon=True)
    t.start()    
    try:
        app.run(debug=True, threaded=True, use_reloader=False)
    finally:
        # This block runs when you hit Ctrl+C or the app crashes
        cleanup()