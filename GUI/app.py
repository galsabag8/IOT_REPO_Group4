import os
import subprocess 
import sys
import threading
import time
import mido
import atexit
from mido import tempo2bpm
from flask import Flask, render_template, request, jsonify
import socket
import csv

# --- IMPORT YOUR LISTENER MODULE ---
import listener 
from listener import IP, PORT_VIS

PORT_CMD = 5007  # Command port for Listener Hub

app = Flask(__name__)

# --- GLOBAL STATE ---
playback_state = {
    "bpm": 120.0,
    "is_playing": False,
    "is_paused": False,
    "wand_enabled": False, 
    "filename": None,
    "thread": None,
    "current_ticks": 0,
    "total_ticks": 0,
    "original_duration": 0.0,
    "weight": 0,
    "last_bpm": 0, # Helper for auto-resume logic
    "in_warmup": False,   # Are we currently waiting for warmup beats?
    "warmup_count": 0,    # How many beats received so far?
    "warmup_target": 0,    # How many beats to wait for (usually 1 bar)
    "record_enabled": False,
    "replay_active": False,
    "wand_connected": False,
    "last_wand_update": 0
}

# --- GUI PROCESS KEEPER ---
gui_process = None

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
    """ Kills the GUI window if app.py crashes or closes """
    close_gui()

atexit.register(cleanup)

# --- UDP LISTENER ---
def udp_music_listener():
    print("--- APP: UDP Music Listener Started on Port 5005 ---")
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind(("127.0.0.1", 5005)) 
    udp_sock.setblocking(False)
    
    while True:
        try:
            data, addr = udp_sock.recvfrom(1024)
            line = data.decode('utf-8').strip()
            if playback_state["replay_active"]:
                continue

            # --- 1. WARMUP LOGIC ---            
            if line == "BEAT_TRIG" and playback_state["in_warmup"]:
                playback_state["warmup_count"] += 1
                print(f"--- WARMUP: {playback_state['warmup_count']} / {playback_state['warmup_target']} ---")
                
                # If we reached the target (e.g., 4 beats), start the music!
                if playback_state["warmup_count"] >= playback_state["warmup_target"]:
                    print("--- WARMUP COMPLETE! STARTING MUSIC ---")
                    playback_state["in_warmup"] = False
                    # The playback_engine thread is waiting for this flag to flip
            
            # Handle Connection Status
            if line == "STATUS: CONNECTED":
                playback_state["wand_connected"] = True
                playback_state["last_wand_update"] = time.time()
                continue
            if line == "STATUS: DISCONNECTED":
                playback_state["wand_connected"] = False
                continue

            if line.startswith("BPM: "):
                playback_state["wand_connected"] = True
                playback_state["last_wand_update"] = time.time()
                if playback_state["wand_enabled"]:
                    try:
                        raw_val = float(line.split(":")[1].strip())
                        apply_bpm_logic(raw_val)
                    except ValueError:
                        pass
            elif line.startswith("Time: ") and playback_state["wand_enabled"]:
                try:
                    raw_val = float(line.split(":")[1].strip())
                    print(f"-> Received Time Signature Update: {raw_val}")
                except ValueError:
                    pass

                    
        except BlockingIOError:
            time.sleep(0.01)
        except Exception as e:
            print(f"UDP Error: {e}")
            time.sleep(0.1)

UPLOAD_FOLDER = 'uploads'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

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

# --- REPLAY DRIVER ---
def replay_driver(csv_path):
    """ Reads CSV and simulates live events for Visuals and BPM """
    print(f"--- REPLAY: Starting driver for {csv_path} ---")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        rows = []
        with open(csv_path, 'r') as f:
            reader = csv.reader(f)
            header = next(reader)
            for row in reader:
                if len(row) >= 5: 
                    rows.append(row)
        
        if not rows: 
            close_gui() # Close if file empty
            return

        start_t = float(rows[0][0]) 
        system_start_time = time.time()

        row_idx = 0
        total_rows = len(rows)

        while playback_state["is_playing"] and row_idx < total_rows:
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
                sock.sendto(sensor_str.encode('utf-8'), (IP, PORT_VIS))
                
                row_idx += 1
            else:
                time.sleep(0.001)

    except Exception as e:
        print(f"Replay Error: {e}")
    
    print("--- REPLAY: Finished ---")
    
    # RESET STATE
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    playback_state["replay_active"] = False
    
    # IMPORTANT: Close Visualization when Replay finishes naturally
    close_gui() 

# --- PLAYBACK ENGINE ---
def playback_engine():
    global playback_state
    try:
        if not playback_state["filename"]: return

        mid = mido.MidiFile(playback_state["filename"])
        playback_state["total_ticks"] = max(sum(msg.time for msg in track) for track in mid.tracks)
        playback_state["original_duration"] = mid.length
        playback_state["current_ticks"] = 0
        messages = mido.merge_tracks(mid.tracks)
        
        with mido.open_output() as port:
            for msg in messages:
                if not playback_state["is_playing"]: break
                if playback_state["is_playing"] and playback_state["wand_enabled"] and not playback_state["wand_connected"]: break
                
                while (playback_state["is_paused"] or playback_state["bpm"] <= 0) and playback_state["is_playing"]:
                    if playback_state["wand_enabled"] and not playback_state["wand_connected"]: break
                    time.sleep(0.05) 

                if msg.time > 0:
                    playback_state["current_ticks"] += msg.time
                    current_bpm = playback_state["bpm"]
                    if current_bpm <= 0: current_bpm = 120 
                    
                    seconds_per_beat = 60.0 / current_bpm
                    sleep_time = msg.time * (seconds_per_beat / mid.ticks_per_beat)
                    time.sleep(sleep_time)

                if not msg.is_meta:
                    port.send(msg)
    except Exception as e:
        print(f"Playback Error: {e}")
    
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    playback_state["current_ticks"] = 0
    playback_state["in_warmup"] = False # Reset just in case 
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

# --- ROUTES ---
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/set_record_mode', methods=['POST'])
def set_record_mode():
    data = request.json
    enabled = data.get('enabled', False)
    playback_state["record_enabled"] = enabled
    return jsonify({"status": "success", "enabled": enabled})

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

    midi_path = os.path.join(UPLOAD_FOLDER, 'replay_temp.mid')
    csv_path = os.path.join(UPLOAD_FOLDER, 'replay_temp.csv')
    midi_file.save(midi_path)
    csv_file.save(csv_path)

    playback_state["filename"] = midi_path
    playback_state["is_playing"] = True
    playback_state["is_paused"] = False
    playback_state["replay_active"] = True
    playback_state["wand_enabled"] = False 
    
    # Start Playback Threads
    playback_state["thread"] = threading.Thread(target=playback_engine)
    playback_state["thread"].daemon = True
    playback_state["thread"].start()

    replay_t = threading.Thread(target=replay_driver, args=(csv_path,))
    replay_t.daemon = True
    replay_t.start()
    
    # REPLAY START -> Open GUI
    open_gui()

    return jsonify({"status": "success", "track_name": midi_file.filename})

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():
    if 'midiFile' not in request.files: return jsonify({"status": "error"}), 400
    file = request.files['midiFile']
    
    is_wand_mode = request.form.get('wand_mode') == 'true'
    playback_state["wand_enabled"] = is_wand_mode 
    playback_state["replay_active"] = False
    
    if file.filename == '': return jsonify({"status": "error"}), 400

    filepath = os.path.join(UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)

    # 1. Calculate Weight
    try:
        mid_object = mido.MidiFile(filepath)
    except Exception as e:
        print(f"Error loading MIDI: {e}")
        return jsonify({"status": "error", "message": "Invalid MIDI file"}), 400

    # 2. Pass the OBJECT to get_weight_count (not the string)
    detected_weight = get_weight_count(mid_object)
    playback_state["weight"] = detected_weight

    # 2. Configure Warmup
    if is_wand_mode:
        playback_state["in_warmup"] = True
        playback_state["warmup_count"] = 0
        playback_state["warmup_target"] = detected_weight # Wait for 1 full bar
        
        # Send Weight to Arduino
        try:
            udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # Command format: "SET_SIG:3"
            msg = f"SET_SIG:{detected_weight}"
            udp_sock.sendto(msg.encode('utf-8'), ("127.0.0.1", PORT_CMD))
            print(f"--- APP: Sent Weight {detected_weight} to Arduino ---")
        except Exception as e:
            print(f"--- APP: Failed to send weight: {e} ---")
    
    detected_bpm = 120.0
    try:
        temp_mid = mido.MidiFile(filepath)
        for track in temp_mid.tracks:
            for msg in track:
                if msg.type == 'set_tempo': detected_bpm = tempo2bpm(msg.tempo)
    except: pass

    start_bpm = 0.0 if is_wand_mode else detected_bpm
    
    playback_state["filename"] = filepath
    playback_state["is_playing"] = True
    playback_state["is_paused"] = not is_wand_mode
    playback_state["bpm"] = start_bpm
    
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()

    # NOTE: We do not force open GUI here. 
    # Wand Mode toggle handles opening/closing. 
    # If we are in Wand Mode, GUI is already open.
    
    return jsonify({"status": "success", "start_bpm": start_bpm})

@app.route('/progress')
def progress():
    current_time_display = 0.0
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
        "replay_active": playback_state["replay_active"]
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
    if playback_state["replay_active"]:
        close_gui()

    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    playback_state["replay_active"] = False
    
    # We do NOT close GUI if we are in Wand Mode (wand_enabled=True)
    # The user might want to load another song while in Wand Mode.
    
    return jsonify({"status": "stopped"})

@app.route('/reset', methods=['POST'])
def reset():
    playback_state["is_playing"] = False
    time.sleep(0.1)
    playback_state["filename"] = None
    playback_state["bpm"] = 120.0
    playback_state["replay_active"] = False
    playback_state["wand_enabled"] = False
    close_gui() # Reset kills everything
    return jsonify({"status": "reset_complete"})

@app.route('/wand_status')
def get_wand_status():
    # If no heartbeat for 3 seconds, assume disconnected
    if time.time() - playback_state["last_wand_update"] > 3.0:
        playback_state["wand_connected"] = False
        
    return jsonify({
        "connected": playback_state["wand_connected"],
        "enabled": playback_state["wand_enabled"]
    })


if __name__ == '__main__':
    print("--- APP: Starting Internal Listener Thread... ---")
    udp_thread = threading.Thread(target=udp_music_listener, daemon=True)
    udp_thread.start()
    t = threading.Thread(target=listener.listen, args=(playback_state,), daemon=True)
    t.start()
    
    app.run(debug=True, threaded=True, use_reloader=False)