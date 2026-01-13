import serial
import socket
import time
import csv
import os
from datetime import datetime  # Importing your shared state
import config

# --- CONFIG ---  


# CSV CONFIG
if not os.path.exists(config.LOG_DIR):
    os.makedirs(config.LOG_DIR)
    
def listen(playback_state):
    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cmd_sock.bind((config.IP, config.PORT_CMD))
    cmd_sock.setblocking(False) 

    out_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    last_bpm = 60.0
    is_recording_active = False 
    csv_file = None
    writer = None
    was_playing_previously = False 

    while True:
        try:
            with serial.Serial(config.SERIAL_PORT, config.BAUD_RATE, timeout=0.1) as ser:
                ser.reset_input_buffer()

                while True:
                    # --- HANDLE INCOMING COMMANDS FROM GUI ---
                    try:
                        data, _ = cmd_sock.recvfrom(128) 
                        if data:
                            ser.write(data)
                            ser.write(b'\n')
                    except BlockingIOError:
                        pass
                    except Exception as e:
                        print(f"CMD Error: {e}")

                    # Wait during replay mode
                    while playback_state.get('replay_active', False):
                        time.sleep(0.1)
                    
                    # --- CHECK PLAYBACK STATE FOR RECORDING ---
                    is_now_playing = playback_state.get('is_playing', False) and playback_state.get('wand_enabled', False)
                    user_wants_record = playback_state.get('record_enabled', False)
                    
                    if is_now_playing and not was_playing_previously:
                        if user_wants_record:
                            timestamp_str = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                            filename = f"{config.LOG_DIR}/track_rec_{timestamp_str}.csv"
                            csv_file = open(filename, mode='w', newline='')
                            writer = csv.writer(csv_file)
                            writer.writerow(["Timestamp", "X", "Y", "Z", "bpm"])

                            # Embed correction matrix as a special comment row
                            correction_matrix = playback_state.get('correction_matrix', None)
                            if correction_matrix is not None:
                                # Flatten the 3x3 matrix to 9 values
                                if hasattr(correction_matrix, 'flatten'):
                                    matrix_values = correction_matrix.flatten().tolist()
                                else:
                                    # If it's already a list of lists, flatten manually
                                    matrix_values = [val for row in correction_matrix for val in row]
                                writer.writerow(["#MATRIX"] + matrix_values)
                            
                            is_recording_active = True
                        else:
                            is_recording_active = False

                    elif not is_now_playing and was_playing_previously:
                        if is_recording_active:
                            if csv_file:
                                csv_file.close()
                                csv_file = None
                                writer = None
                            is_recording_active = False

                    was_playing_previously = is_now_playing

                    # --- SINGLE SERIAL READ POINT ---
                    if ser.in_waiting:
                        try:
                            line = ser.readline()
                            decoded_line = line.decode('utf-8', errors='ignore').strip()

                            # Send raw data to both endpoints
                            out_sock.sendto(line, (config.IP, config.PORT_VIS))
                            out_sock.sendto(line, (config.IP, config.PORT_MUSIC))

                            # --- PARSE MESSAGE TYPES ---
                            
                            # Connection Status
                            if decoded_line == "STATUS: CONNECTED" and not playback_state.get("wand_connected", False):
                                playback_state["wand_connected"] = True
                                playback_state["last_wand_update"] = time.time()
                                data = b'WAND STATUS: ACK'
                                ser.write(data)
                                ser.write(b'\n')
                                continue
                            
                            # Button State (CRITICAL FIX)
                            if decoded_line.startswith("Button: "):
                                button_action = decoded_line.split(":")[1].strip()
                                if button_action == "PLAY":
                                    playback_state["button_state"] = True
                                elif button_action == "STOP":
                                    playback_state["button_state"] = False

                            # Beat Detection
                            if decoded_line.startswith("BEAT:"):
                                try:
                                    beat_val = int(decoded_line.split(":")[1].strip())
                                    playback_state["last_beat_received"] = beat_val
                                except ValueError:
                                    pass

                            # Debug Logs
                            if decoded_line.startswith("LOG:"):
                                # --- NEW: Append to Queue ---
                                # Only keep the last 50 to prevent memory issues if frontend disconnects
                                if len(playback_state["debug_queue"]) < 50:
                                    playback_state["debug_queue"].append(decoded_line)

                            # Recording Data
                            if is_recording_active and writer:
                                if decoded_line.startswith("DATA,"):
                                    parts = decoded_line.split(',')
                                    if len(parts) == 4 and last_bpm > 0:
                                        vals = [float(x) for x in parts[1:]]
                                        writer.writerow([time.time()] + vals + [last_bpm])
                                
                            # BPM Updates
                            if decoded_line.startswith("BPM: "):
                                try:
                                    last_bpm = float(decoded_line.split(":")[1].strip())
                                except: 
                                    pass

                        except Exception as e:
                            print(f"Packet Error: {e}")
                    else:
                        time.sleep(0.001)

        except Exception as e:
            print(f"Hub Error: {e}")
            playback_state["wand_connected"] = False
            if csv_file:
                csv_file.close()
            time.sleep(2)


