import mido
import time
import threading
import sys

# --- GLOBAL SHARED VARIABLES ---
current_bpm = 120.0
is_playing = True
mid_file = None

def input_listener():
    """Waits for user input to change BPM dynamically."""
    global current_bpm, is_playing
    print(f"Type a new BPM (e.g., 140) and press Enter. Type 'q' to quit.")
    
    while is_playing:
        try:
            user_input = input()
            if user_input.lower() == 'q':
                is_playing = False
                break
            
            new_val = float(user_input)
            if 40 <= new_val <= 240:
                current_bpm = new_val
                print(f"--> BPM updated to: {current_bpm}")
            else:
                print("Keep BPM between 40 and 240.")
                
        except ValueError:
            print("Invalid number.")

def play_midi_live(filename):
    global current_bpm, is_playing, mid_file
    
    try:
        mid_file = mido.MidiFile(filename)
        # Merge all tracks into one stream of messages sorted by time
        messages = mido.merge_tracks(mid_file.tracks)
        
        print(f"Playing: {filename}")
        print(f"Original file resolution: {mid_file.ticks_per_beat} ticks/beat")
        
        # Open the default output port (e.g., Microsoft GS Wavetable Synth)
        with mido.open_output() as port:
            
            for msg in messages:
                if not is_playing:
                    break
                
                # --- THE MAGIC MATH ---
                # MIDI files store time in 'ticks'. We must convert ticks to seconds.
                # Formula: Seconds = Ticks * (Seconds per Beat / Ticks per Beat)
                # Seconds per Beat = 60 / BPM
                
                if msg.time > 0:
                    # Calculate how long to wait for this specific note
                    # based on the BPM *right now*
                    seconds_per_beat = 60.0 / current_bpm
                    seconds_per_tick = seconds_per_beat / mid_file.ticks_per_beat
                    sleep_time = msg.time * seconds_per_tick
                    
                    time.sleep(sleep_time)

                # Send the message (play the note)
                if not msg.is_meta:
                    port.send(msg)
                    
        print("\nSong finished.")
        is_playing = False # Stop the input thread too

    except Exception as e:
        print(f"\nError: {e}")
        is_playing = False

# --- MAIN EXECUTION ---
if __name__ == "__main__":
    target_file = input("Enter MIDI filename (e.g., uploads/input.mid): ").strip()
    
    # Start the input listener in a separate background thread
    input_thread = threading.Thread(target=input_listener)
    input_thread.daemon = True # Kills this thread if the main program exits
    input_thread.start()

    # Start the music in the main thread
    play_midi_live(target_file)