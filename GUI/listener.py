import serial
import socket
import time
import csv
import os
from datetime import datetime  # Importing your shared state

# --- CONFIG ---
SERIAL_PORT = 'COM6'   
BAUD_RATE = 921600     
IP = "127.0.0.1"
PORT_MUSIC = 5005      # Port for app.py (BPM)
PORT_VIS = 5006        # Port for visualizer.py (3D Wand)
PORT_CMD = 5007        # Listening for commands from app.py

# CSV CONFIG
LOG_DIR = "logs"
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)
    
def listen(playback_state):
    # 1. Setup UDP Socket for incoming commands (Non-blocking)
    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cmd_sock.bind((IP, PORT_CMD))
    cmd_sock.setblocking(False) 

    # Socket for sending data OUT
    out_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"--- HUB: Connecting to {SERIAL_PORT}... ---")

    # Internal state variables
    last_bpm = 60.0
    
    # This variable tracks if we are CURRENTLY writing to a file
    is_recording_active = False 
    csv_file = None
    writer = None

    # We need to remember the previous state to detect when it *changes*
    was_playing_previously = False 

    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1) as ser:
                print("--- HUB ACTIVE: Ready... ---")
                ser.reset_input_buffer()
                last_heartbeat = 0

                while True:
                    # Send Heartbeat every 2 seconds to confirm connection
                    if time.time() - last_heartbeat > 2.0:
                        try:
                            sock.sendto(b"STATUS: CONNECTED", (IP, PORT_MUSIC))
                            last_heartbeat = time.time()
                        except: pass
                    # --- A. Read from Arduino (Existing Logic) ---
                    if ser.in_waiting:
                        try:
                            line = ser.readline()
                            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            sock.sendto(line, (IP, PORT_MUSIC))
                            sock.sendto(line, (IP, PORT_VIS))
                        except Exception:
                            pass
                    
                    # --- B. Write to Arduino (NEW - No extra thread) ---
                    try:
                        # Check if app.py sent a command
                        data, _ = cmd_sock.recvfrom(128) 
                        if data:
                            print(f"HUB: Sending command -> {data}")
                            ser.write(data) # Forward bytes directly to Serial
                            ser.write(b'\n') # Ensure newline just in case
                    except BlockingIOError:
                        # No data waiting, continue loop
                        pass
                    except Exception as e:
                        print(f"CMD Error: {e}")

                    # Small sleep to prevent 100% CPU usage
                    # (Serial read is fast, but this helps stability)
                    time.sleep(0.001) 
                    while playback_state.get('replay_active', False):
                        time.sleep(0.1)
                    # --- 1. CHECK PLAYBACK STATE ---
                    # Get current status from your app
                    is_now_playing = playback_state.get('is_playing', False) and playback_state.get('wand_enabled', False)
                    user_wants_record = playback_state.get('record_enabled', False) # Your placeholder button
                    # LOGIC: Detect Track START (Rising Edge)
                    if is_now_playing and not was_playing_previously:
                        # The track JUST started. Check the record button NOW.
                        if user_wants_record:
                            print("[REC] Track Started & Recording Requested -> STARTING REC")
                            
                            # Create File
                            timestamp_str = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                            filename = f"{LOG_DIR}/track_rec_{timestamp_str}.csv"
                            csv_file = open(filename, mode='w', newline='')
                            writer = csv.writer(csv_file)
                            writer.writerow(["Timestamp", "X", "Y", "Z", "bpm"])
                            
                            is_recording_active = True
                        else:
                            print("[INFO] Track Started (Recording NOT requested)")
                            is_recording_active = False

                    # LOGIC: Detect Track STOP (Falling Edge)
                    elif not is_now_playing and was_playing_previously:
                        # The track JUST stopped.
                        if is_recording_active:
                            print("[REC] Track Finished -> SAVING FILE")
                            if csv_file:
                                csv_file.close()
                                csv_file = None
                                writer = None
                            is_recording_active = False
                        else:
                            print("[INFO] Track Finished")

                    # Update history for next loop
                    was_playing_previously = is_now_playing


                    if ser.in_waiting:
                        try:
                            # one big msg with all the data needed
                            line = ser.readline()
                            
                            # send everything to both visualizer and music app
                            out_sock.sendto(line, (IP, PORT_VIS))
                            

                            out_sock.sendto(line, (IP, PORT_MUSIC))


                            decoded_line = line.decode('utf-8', errors='ignore').strip()

                            # Terminal Debug Logs from Arduino
                            if decoded_line.startswith("LOG:"):
                                print(f"DEBUG: {decoded_line}")

                            # If we are currently in a recording session, save the data
                            if is_recording_active and writer:
                                if decoded_line.startswith("DATA,"):
                                    parts = decoded_line.split(',')
                                    if len(parts) == 4:
                                        vals = [float(x) for x in parts[1:]]
                                        writer.writerow([time.time()] + vals + [last_bpm])
                                
                            # Update BPM (Global)
                            if decoded_line.startswith("BPM: "):
                                try:
                                    last_bpm = float(decoded_line.split(":")[1].strip())
                                except: pass

                        except Exception as e:
                            print(f"Packet Error: {e}")
                    else:
                        time.sleep(0.001) 

        except Exception as e:
            print(f"Hub Error: {e}")
            try:
                sock.sendto(b"STATUS: DISCONNECTED", (IP, PORT_MUSIC))
            except: pass
            if csv_file:
                csv_file.close()
            time.sleep(2)



