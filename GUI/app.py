import os
import threading
import time
import mido
from mido import tempo2bpm
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)

# --- GLOBAL STATE ---
playback_state = {
    "bpm": 120.0,
    "is_playing": False,
    "is_paused": False,
    "filename": None,
    "thread": None,
    "current_ticks": 0,      # <--- CHANGED: Tracking Ticks
    "total_ticks": 0,        # <--- CHANGED: Total Ticks
    "original_duration": 0.0 # Just for displaying "3:45" at the end
}

UPLOAD_FOLDER = 'uploads'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

def playback_engine():
    global playback_state
    
    try:
        mid = mido.MidiFile(playback_state["filename"])
        
        # 1. CALCULATE TOTAL TICKS (The length of the longest track)
        # We sum up the time (deltas) of every message in every track, and find the max.
        playback_state["total_ticks"] = max(sum(msg.time for msg in track) for track in mid.tracks)
        playback_state["original_duration"] = mid.length
        playback_state["current_ticks"] = 0
        
        messages = mido.merge_tracks(mid.tracks)
        
        with mido.open_output() as port:
            for msg in messages:
                if not playback_state["is_playing"]:
                    break
                
                while playback_state["is_paused"] and playback_state["is_playing"]:
                    time.sleep(0.1)

                if msg.time > 0:
                    # 2. ACCUMULATE PROGRESS (TICKS)
                    playback_state["current_ticks"] += msg.time
                    
                    # 3. PLAYBACK MATH
                    # msg.time here is in 'ticks' because we are using merge_tracks
                    current_bpm = playback_state["bpm"]
                    seconds_per_beat = 60.0 / current_bpm
                    seconds_per_tick = seconds_per_beat / mid.ticks_per_beat
                    sleep_time = msg.time * seconds_per_tick
                    
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

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():
    if 'midiFile' not in request.files: return jsonify({"status": "error"}), 400
    file = request.files['midiFile']
    if file.filename == '': return jsonify({"status": "error"}), 400

    filepath = os.path.join(UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)
    
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
        print(f"BPM Detection Error: {e}")

    # Use the detected BPM instead of the slider's initial value
    playback_state["filename"] = filepath
    playback_state["is_playing"] = True
    playback_state["is_paused"] = False
    playback_state["bpm"] = detected_bpm
    playback_state["current_ticks"] = 0
    
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()

    # Return the detected BPM to the frontend
    return jsonify({
        "status": "success", 
        "detected_bpm": detected_bpm
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
        "current_bpm": playback_state["bpm"]  # <--- NEW: Send BPM back to browser
    })

# ... (Keep /pause, /resume, /stop, /set_bpm exactly the same as before) ...
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
        playback_state["bpm"] = float(request.json['bpm'])
        return jsonify({"status": "success"})
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
    
    # 1. Stop the music
    playback_state["is_playing"] = False
    playback_state["is_paused"] = False
    
    # 2. Give the thread a tiny moment to shut down gracefully
    time.sleep(0.1)
    
    # 3. Wipe the memory
    playback_state["filename"] = None
    playback_state["bpm"] = 120.0
    playback_state["current_ticks"] = 0
    playback_state["total_ticks"] = 0
    playback_state["original_duration"] = 0.0
    
    print("--- SERVER RESET: Ready for new session ---")
    return jsonify({"status": "reset_complete"})

if __name__ == '__main__':
    app.run(debug=True, threaded=True)