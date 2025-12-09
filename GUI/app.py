import os
import threading
import time
import mido
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)

# --- GLOBAL VARIABLES (Shared between Web & Playback) ---
playback_state = {
    "bpm": 120.0,
    "is_playing": False,
    "filename": None,
    "thread": None
}

UPLOAD_FOLDER = 'uploads'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# --- THE PLAYBACK ENGINE (Runs in Background) ---
def playback_engine():
    global playback_state
    
    try:
        mid = mido.MidiFile(playback_state["filename"])
        # Merge tracks to a single stream
        messages = mido.merge_tracks(mid.tracks)
        
        # Open Output (Your computer's speakers)
        with mido.open_output() as port:
            for msg in messages:
                # Check if user clicked Stop
                if not playback_state["is_playing"]:
                    break
                
                # Time Calculation (The "Live BPM" logic)
                if msg.time > 0:
                    current_bpm = playback_state["bpm"]
                    seconds_per_beat = 60.0 / current_bpm
                    seconds_per_tick = seconds_per_beat / mid.ticks_per_beat
                    sleep_time = msg.time * seconds_per_tick
                    
                    time.sleep(sleep_time)

                if not msg.is_meta:
                    port.send(msg)
                    
    except Exception as e:
        print(f"Playback Error: {e}")
    
    # Reset when song finishes
    playback_state["is_playing"] = False

# --- WEB ROUTES ---

@app.route('/')
def index():
    return render_template('index.html') # We will create this next

@app.route('/upload_and_play', methods=['POST'])
def upload_and_play():
    if 'midiFile' not in request.files:
        return jsonify({"status": "error", "message": "No file uploaded"}), 400
    
    file = request.files['midiFile']
    if file.filename == '':
        return jsonify({"status": "error", "message": "No file selected"}), 400

    # Save File
    filepath = os.path.join(UPLOAD_FOLDER, 'live_input.mid')
    file.save(filepath)
    
    # Update State
    playback_state["filename"] = filepath
    playback_state["is_playing"] = True
    playback_state["bpm"] = float(request.form.get("bpm", 120))
    
    # Start the Background Thread
    # We check if a thread is already running to avoid double-playing
    if playback_state["thread"] is None or not playback_state["thread"].is_alive():
        playback_state["thread"] = threading.Thread(target=playback_engine)
        playback_state["thread"].daemon = True
        playback_state["thread"].start()

    return jsonify({"status": "success", "message": "Playing started!"})

@app.route('/set_bpm', methods=['POST'])
def set_bpm():
    try:
        new_bpm = float(request.json['bpm'])
        playback_state["bpm"] = new_bpm
        print(f"--> Live BPM Update: {new_bpm}")
        return jsonify({"status": "success", "bpm": new_bpm})
    except:
        return jsonify({"status": "error"}), 400

@app.route('/stop', methods=['POST'])
def stop():
    playback_state["is_playing"] = False
    return jsonify({"status": "stopped"})

if __name__ == '__main__':
    # Threaded=True is important so the server handles requests while music plays
    app.run(debug=True, threaded=True)