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

# --- IMPORT YOUR LISTENER MODULE ---
import listener 

app = Flask(__name__)

# --- GLOBAL STATE ---
# This dictionary is shared between the Web Server and the Listener Thread
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
    "last_bpm": 0 # Helper for auto-resume logic
}

# --- GUI PROCESS KEEPER ---
gui_process = None

# --- UDP LISTENER (Restored) ---
def udp_music_listener():
    """ Listens for UDP packets on Port 5005 (from Simulator or Listener) """
    print("--- APP: UDP Music Listener Started on Port 5005 ---")
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind(("127.0.0.1", 5005)) 
    udp_sock.setblocking(False)
    
    while True:
        try:
            data, addr = udp_sock.recvfrom(1024)
            line = data.decode('utf-8').strip()
            
            # Update BPM if we are in Wand Mode
            if line.startswith("BPM: ") and playback_state["wand_enabled"]:
                try:
                    raw_val = float(line.split(":")[1].strip())
                    apply_bpm_logic(raw_val)
                except ValueError:
                    pass
                    
        except BlockingIOError:
            time.sleep(0.01) # No data waiting
        except Exception as e:
            print(f"UDP Error: {e}")
            time.sleep(0.1)

def cleanup():
    """ Kills the GUI window if app.py crashes or closes """
    if gui_process:
        print("--- APP: Closing GUI... ---")
        gui_process.terminate()

atexit.register(cleanup)

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

# --- PLAYBACK ENGINE ---
def playback_engine():
    global playback_state
    try:
        if not playback_state["filename"]: return

        mid = mido.MidiFile(playback_state["filename"])
        playback_state["weight"] = get_weight_count(mid)
        playback_state["total_ticks"] = max(sum(msg.time for msg in track) for track in mid.tracks)
        playback_state["original_duration"] = mid.length
        playback_state["current_ticks"] = 0
        messages = mido.merge_tracks(mid.tracks)
        
        with mido.open_output() as port:
            for msg in messages:
                if not playback_state["is_playing"]: break
                
                # Pause Logic
                while (playback_state["is_paused"] or playback_state["bpm"] <= 0) and playback_state["is_playing"]:
                    time.sleep(0.05) 

                if msg.time > 0:
                    playback_state["current_ticks"] += msg.time
                    current_bpm = playback_state["bpm"]
                    if current_bpm <= 0: current_bpm = 120 
                    
                    # Dynamic Sleep based on current BPM
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

@app.route('/set_wand_mode', methods=['POST'])
def set_wand_mode():
    global gui_process
    data = request.json
    enabled = data.get('enabled', False)
    playback_state["wand_enabled"] = enabled
    
    if enabled:
        # LAUNCH THE GUI WINDOW
        if gui_process is None:
            print("--- APP: Launching GUI Window... ---")
            # Make sure the file is named 'gui.py' (the visualizer code I gave you)
            gui_process = subprocess.Popen([sys.executable, 'trace.py'])
    else:
        # KILL THE GUI WINDOW
        if gui_process:
            print("--- APP: Closing GUI Window... ---")
            gui_process.terminate()
            gui_process = None

    return jsonify({"status": "success", "enabled": enabled})

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():
    if 'midiFile' not in request.files: return jsonify({"status": "error"}), 400
    file = request.files['midiFile']
    
    is_wand_mode = request.form.get('wand_mode') == 'true'
    playback_state["wand_enabled"] = is_wand_mode 
    
    if file.filename == '': return jsonify({"status": "error"}), 400

    filepath = os.path.join(UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)
    
    # Auto-detect BPM from file metadata as a fallback
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
    playback_state["is_paused"] = False
    playback_state["bpm"] = start_bpm
    
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()

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
        "current_bpm": playback_state["bpm"]
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
    try:
        raw_bpm = float(request.json['bpm'])
        final_bpm = apply_bpm_logic(raw_bpm)
        return jsonify({"status": "success", "bpm": final_bpm})
    except:
        return jsonify({"status": "error"}), 400

@app.route('/stop', methods=['POST'])
def stop():
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    return jsonify({"status": "stopped"})

@app.route('/reset', methods=['POST'])
def reset():
    playback_state["is_playing"] = False
    time.sleep(0.1)
    playback_state["filename"] = None
    playback_state["bpm"] = 120.0
    return jsonify({"status": "reset_complete"})

# --- MAIN ENTRY POINT ---
if __name__ == '__main__':
    # 1. Start the Listener Thread
    # We pass 'playback_state' so the listener can update BPM directly
    print("--- APP: Starting Internal Listener Thread... ---")
    udp_thread = threading.Thread(target=udp_music_listener, daemon=True)
    udp_thread.start()
    t = threading.Thread(target=listener.listen, daemon=True)
    t.start()
    
    # 2. Start Flask Server
    app.run(debug=True, threaded=True, use_reloader=False)