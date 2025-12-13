import os
import threading
import time
import mido
from mido import tempo2bpm
from flask import Flask, render_template, request, jsonify
from serial_reader import ArduinoSerialReader
from datetime import datetime

app = Flask(__name__)

# --- SYSTEM LOG ---
frontend_logs = []
system_log = []
LOG_MAX_SIZE = 100  # Keep only last 100 messages

def add_log(message, level="info"):
    """Add message to both backend and frontend logs"""
    global system_log, frontend_logs
    timestamp = datetime.now().strftime("%H:%M:%S")
    log_entry = {
        "timestamp": timestamp,
        "level": level,
        "message": message
    }
    system_log.append(log_entry)
    frontend_logs.append(log_entry)  # ← ADD THIS LINE
    
    if len(system_log) > LOG_MAX_SIZE:
        system_log.pop(0)
    if len(frontend_logs) > LOG_MAX_SIZE:
        frontend_logs.pop(0)

    print(f"[{timestamp}] {level.upper()}: {message}")

# Add this NEW endpoint after the /logs endpoint:
@app.route('/get_frontend_logs')
def get_frontend_logs():
    """Return pending logs for frontend and clear the queue"""
    global frontend_logs
    logs = frontend_logs.copy()
    frontend_logs = []  # Clear after sending
    return jsonify(logs)


# --- GLOBAL STATE ---
playback_state = {
    "bpm": 120.0,
    "is_playing": False,
    "is_paused": False,
    "filename": None,
    "thread": None,
    "current_ticks": 0,      # <--- CHANGED: Tracking Ticks
    "total_ticks": 0,        # <--- CHANGED: Total Ticks
    "original_duration": 0.0, # Just for displaying "3:45" at the end
    "use_wand_mode": False  # NEW: Track if using Arduino sensor
}

# Arduino Serial Reader
arduino_reader = ArduinoSerialReader(port='COM4', baudrate=115200)   # Adjust COM port as needed
bpm_sync_thread = None

UPLOAD_FOLDER = 'uploads'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

def playback_engine():
    """Unified playback engine that handles both standard and wand mode"""
    global playback_state
    
    try:
        mid = mido.MidiFile(playback_state["filename"])
        playback_state["total_ticks"] = max(sum(msg.time for msg in track) for track in mid.tracks)
        playback_state["original_duration"] = mid.length
        playback_state["current_ticks"] = 0
        
        messages = mido.merge_tracks(mid.tracks)
        
        with mido.open_output() as port:
            for msg in messages:
                # Stop if user clicked stop button
                if not playback_state["is_playing"]:
                    break
                
                # Update BPM from Arduino in wand mode (only if not manually paused)
                if playback_state["use_wand_mode"] and not playback_state["is_paused"]:
                    arduino_bpm = arduino_reader.get_bpm()
                    if arduino_bpm > 0:
                        playback_state["bpm"] = arduino_bpm
                    else:
                        playback_state["bpm"] = 0
                        playback_state["is_paused"] = True
                        add_log("No conductor movement detected - Paused", "warning")
                
                # Wait if paused or no BPM
                while (playback_state["is_paused"] or playback_state["bpm"] <= 0) and playback_state["is_playing"]:
                    time.sleep(0.05)
                    
                    # Check Arduino again while waiting (only if wand mode)
                    if playback_state["use_wand_mode"] and not playback_state["is_paused"]:
                        arduino_bpm = arduino_reader.get_bpm()
                        if arduino_bpm > 0:
                            playback_state["bpm"] = arduino_bpm
                            add_log(f"Movement detected! Resuming at {arduino_bpm} BPM", "success")
                            break

                # Sleep with pause checks (responsive pause)
                if msg.time > 0:
                    playback_state["current_ticks"] += msg.time
                    
                    current_bpm = playback_state["bpm"] if playback_state["bpm"] > 0 else 120
                    seconds_per_beat = 60.0 / current_bpm
                    seconds_per_tick = seconds_per_beat / mid.ticks_per_beat
                    sleep_time = msg.time * seconds_per_tick
                    
                    # Sleep in small chunks to respond to pause quickly
                    elapsed = 0
                    while elapsed < sleep_time and playback_state["is_playing"] and not playback_state["is_paused"]:
                        chunk = min(0.05, sleep_time - elapsed)
                        time.sleep(chunk)
                        elapsed += chunk

                # Send MIDI note
                if not msg.is_meta:
                    port.send(msg)
                    
    except Exception as e:
        add_log(f"Playback Error: {e}", "error")
    
    # Cleanup
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    playback_state["current_ticks"] = 0

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():

    if 'midiFile' not in request.files: return jsonify({"status": "error"}), 400
    file = request.files['midiFile']
    is_wand_mode = request.form.get('wand_mode') == 'true'
    if file.filename == '': return jsonify({"status": "error"}), 400

    filepath = os.path.join(UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)
    add_log(f"MIDI file uploaded: {file.filename}", "info")
    
    # --- DETECT ORIGINAL BPM ---
    detected_bpm = 120.0 # Default fallback
    try:
        temp_mid = mido.MidiFile(filepath)
        for track in temp_mid.tracks:
            for msg in track:
                if msg.type == 'set_tempo':
                    # Convert tempo (microseconds per beat) to BPM
                    detected_bpm = tempo2bpm(msg.tempo)
                    break # Stop at the first tempo we find
            else:
                continue
            break
    except Exception as e:
        add_log(f"BPM Detection Error: {e}", "warning")

    # Use the detected BPM instead of the slider's initial value
    #start_bpm = 0.0 if is_wand_mode else detected_bpm
    playback_state["filename"] = filepath
    playback_state["is_playing"] = True
    playback_state["is_paused"] = False
    #playback_state["bpm"] = start_bpm
    playback_state["use_wand_mode"] = is_wand_mode
    playback_state["current_ticks"] = 0

    if is_wand_mode:
        # Connect to Arduino and start with 0 BPM (wait for first beat)
        arduino_reader.connect()
        playback_state["bpm"] = 0.0
        add_log("Wand mode activated: Waiting for Arduino BPM...", "info")
    else:
        # Use detected MIDI BPM
        playback_state["bpm"] = detected_bpm
        add_log(f"Standard mode: Using detected BPM {detected_bpm}", "info")
    
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()
    

    # Return the detected BPM to the frontend
    return jsonify({
        "status": "success", 
        "detected_bpm": detected_bpm,
        "start_bpm": playback_state["bpm"],
        "wand_mode": is_wand_mode
    })

# --- UPDATED PROGRESS ENDPOINT ---
@app.route('/progress')
def progress():
    # Calculate "Musical Time" for display
    current_time_display = 0.0
    if playback_state["total_ticks"] > 0:
        percent = playback_state["current_ticks"] / playback_state["total_ticks"]
        current_time_display = percent * playback_state["original_duration"]

    return jsonify({
        "progress_percent": (playback_state["current_ticks"] / playback_state["total_ticks"]) * 100 if playback_state["total_ticks"] > 0 else 0,
        "current_time_str": current_time_display,
        "total_time_str": playback_state["original_duration"],
        "is_playing": playback_state["is_playing"],
        "current_bpm": playback_state["bpm"],  # <--- NEW: Send BPM back to browser
        "wand_mode": playback_state["use_wand_mode"]
    })

# ... (Keep /pause, /resume, /stop, /set_bpm exactly the same as before) ...
# --- NEW: SYSTEM LOG ENDPOINT ---
@app.route('/logs')
def get_logs():
    """Return all system logs for the frontend"""
    return jsonify(system_log)

@app.route('/pause', methods=['POST'])
def pause():
    playback_state["is_paused"] = True
    return jsonify({"status": "paused"})

@app.route('/resume', methods=['POST'])
def resume():
    playback_state["is_paused"] = False
    return jsonify({"status": "resumed"})

@app.route('/set_bpm', methods=['POST'])
def set_bpm():
    try:
        raw_bpm = float(request.json['bpm'])
        
        # Don't allow manual BPM changes in wand mode
        if playback_state["use_wand_mode"]:
            return jsonify({
                "status": "warning", 
                "message": "Cannot manually set BPM in Wand Mode. Arduino controls the tempo."
            }), 400
        
        # 1. CLAMP TO MAX 240
        if raw_bpm > 240:
            raw_bpm = 240.0
            
        # 2. HANDLE ZERO -> PAUSE
        if raw_bpm <= 0:
            raw_bpm = 0.0
            playback_state["is_paused"] = True # Force Pause
        
        elif playback_state["bpm"] == 0 and raw_bpm > 0:
            playback_state["is_paused"] = False
        
        playback_state["bpm"] = raw_bpm
        return jsonify({"status": "success", "bpm": raw_bpm, "paused": (raw_bpm == 0)})
        
    except:
        return jsonify({"status": "error"}), 400

@app.route('/stop', methods=['POST'])
def stop():
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    add_log("Playback stopped", "info")
    return jsonify({"status": "stopped"})

@app.route('/reset', methods=['POST'])
def reset():
    global playback_state
    
    # 1. Stop the music
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    playback_state["use_wand_mode"] = False
    
    # 2. Give the thread a tiny moment to shut down gracefully
    time.sleep(0.1)

    # 3. DISCONNECT ARDUINO
    arduino_reader.disconnect()  # <--- ADD THIS LINE
    
    # 4. Wipe the memory
    playback_state["filename"] = None
    playback_state["bpm"] = 120.0
    playback_state["current_ticks"] = 0
    playback_state["total_ticks"] = 0
    playback_state["original_duration"] = 0.0
    
    add_log("System reset", "info")
    print("--- SERVER RESET: Ready for new session ---")
    return jsonify({"status": "reset_complete"})

if __name__ == '__main__':
    app.run(debug=True, threaded=True)