import os
import threading
import time
import mido
import serial
from mido import tempo2bpm
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)

# --- CONFIGURATION ---
SERIAL_PORT = 'COM6'  # Ensure this matches your Arduino
BAUD_RATE = 115200

# --- GLOBAL STATE ---
playback_state = {
    "bpm": 120.0,
    "is_playing": False,
    "is_paused": False,
    "wand_enabled": False, # <--- NEW FLAG
    "filename": None,
    "thread": None,
    "current_ticks": 0,
    "total_ticks": 0,
    "original_duration": 0.0
}

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

# --- BACKGROUND THREAD: WAND LISTENER ---
def wand_listener():
    print(f"--- WAND THREAD: Searching for Arduino on {SERIAL_PORT}... ---")
    
    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as ser:
                print(f"--- WAND CONNECTED ---")
                
                while True:
                    # --- NEW: CHECK IF ENABLED ---
                    if not playback_state["wand_enabled"]:
                        time.sleep(0.5) # Sleep to save CPU
                        ser.reset_input_buffer() # Throw away old data so we don't process lag later
                        continue
                    # -----------------------------

                    if ser.in_waiting > 0:
                        try:
                            line = ser.readline().decode('utf-8').strip()
                            if line.startswith("BPM: "):
                                raw_val = float(line.split(":")[1].strip())
                                apply_bpm_logic(raw_val)
                        except ValueError:
                            pass
                        except Exception as e:
                            print(f"Wand Parse Error: {e}")
                            
                    time.sleep(0.01) 

        except serial.SerialException:
            time.sleep(2)
        except Exception as e:
            print(f"Serial Error: {e}")
            time.sleep(2)

listener_thread = threading.Thread(target=wand_listener, daemon=True)
listener_thread.start()

# --- PLAYBACK ENGINE ---
def playback_engine():
    global playback_state
    try:
        mid = mido.MidiFile(playback_state["filename"])
        playback_state["total_ticks"] = max(sum(msg.time for msg in track) for track in mid.tracks)
        playback_state["original_duration"] = mid.length
        playback_state["current_ticks"] = 0
        messages = mido.merge_tracks(mid.tracks)
        
        with mido.open_output() as port:
            for msg in messages:
                if not playback_state["is_playing"]: break
                while (playback_state["is_paused"] or playback_state["bpm"] <= 0) and playback_state["is_playing"]:
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
    
@app.route('/')
def index():
    return render_template('index.html')

# --- NEW ROUTE: TOGGLE WAND ---
@app.route('/set_wand_mode', methods=['POST'])
def set_wand_mode():
    data = request.json
    playback_state["wand_enabled"] = data.get('enabled', False)
    print(f"Wand Mode set to: {playback_state['wand_enabled']}")
    return jsonify({"status": "success", "enabled": playback_state["wand_enabled"]})
# ------------------------------

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():
    if 'midiFile' not in request.files: return jsonify({"status": "error"}), 400
    file = request.files['midiFile']
    # We update the global flag here too just in case
    is_wand_mode = request.form.get('wand_mode') == 'true'
    print(f"Upload: Wand Mode = {is_wand_mode}")
    playback_state["wand_enabled"] = is_wand_mode 
    
    if file.filename == '': return jsonify({"status": "error"}), 400

    filepath = os.path.join(UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)
    
    detected_bpm = 120.0
    track_name = "Unknown Track"
    copyright_info = ""

    try:
        temp_mid = mido.MidiFile(filepath)
        for track in temp_mid.tracks:
            for msg in track:
                if msg.type == 'set_tempo': detected_bpm = tempo2bpm(msg.tempo)
                if msg.type == 'track_name' and track_name == "Unknown Track": track_name = msg.name
                if msg.type == 'copyright' and copyright_info == "": copyright_info = msg.text
    except Exception as e:
        print(f"BPM Error: {e}")

    start_bpm = 0.0 if is_wand_mode else detected_bpm
    
    playback_state["filename"] = filepath
    playback_state["is_playing"] = True
    playback_state["is_paused"] = False
    playback_state["bpm"] = start_bpm
    playback_state["current_ticks"] = 0
    
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()

    return jsonify({"status": "success", "detected_bpm": detected_bpm, "start_bpm": start_bpm, "track_name": track_name, "copyright": copyright_info})

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
        return jsonify({"status": "success", "bpm": final_bpm, "paused": playback_state["is_paused"]})
    except:
        return jsonify({"status": "error"}), 400

@app.route('/stop', methods=['POST'])
def stop():
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    return jsonify({"status": "stopped"})

@app.route('/reset', methods=['POST'])
def reset():
    global playback_state
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    time.sleep(0.1)
    playback_state["filename"] = None
    playback_state["bpm"] = 120.0
    playback_state["current_ticks"] = 0
    playback_state["total_ticks"] = 0
    playback_state["original_duration"] = 0.0
    print("--- SERVER RESET ---")
    return jsonify({"status": "reset_complete"})

if __name__ == '__main__':
    app.run(debug=True, threaded=True, use_reloader=False)