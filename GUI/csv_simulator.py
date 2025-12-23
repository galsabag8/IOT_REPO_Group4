import socket
import time
import csv
import sys

# --- CONFIGURATION ---
CSV_FILE = "wand_data_2_50bpm.csv"  # Your uploaded file
UDP_IP = "127.0.0.1"
PORT_MUSIC = 5005      # Destination for app.py (BPM)
PORT_VIS = 5006        # Destination for visualizer.py (Motion)

# Playback Speed (The file has ~1670 rows. Assuming 100Hz recording)
PLAYBACK_RATE = 0.01   # 10ms delay between packets

def run_simulation():
    # Setup UDP Socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    print(f"--- CSV SIMULATOR: Replaying {CSV_FILE} ---")
    print(f"--- Broadcasting to Music:{PORT_MUSIC} and Vis:{PORT_VIS} ---")
    print(f"--- Press Ctrl+C to stop ---")

    # Send an initial BPM message to set the music tempo
    # We send it periodically in the loop just to be safe
    bpm_message = "BPM: 100".encode('utf-8')

    try:
        while True:  # Loop the file forever
            with open(CSV_FILE, 'r') as f:
                reader = csv.reader(f)
                header = next(reader) # Skip the header row
                
                print("--- Starting Playback Loop ---")
                
                row_count = 0
                for row in reader:
                    # CSV Row Structure: [ax, ay, az, gx, gy, gz, beat_event]
                    # We need to construct the string: "DATA,ax,ay,az,gx,gy,gz"
                    
                    if len(row) < 6: continue
                    
                    # 1. Create the DATA packet string
                    # We take the first 6 columns (motion data)
                    motion_str = f"DATA,{row[0]},{row[1]},{row[2]},{row[3]},{row[4]},{row[5]}"
                    
                    # 2. Check for Beat Event (Optional visual feedback for you)
                    beat_flag = int(row[6]) if len(row) > 6 else 0
                    if beat_flag == 1:
                        print(f"🥁 BEAT at packet {row_count}")
                    
                    # 3. Broadcast Packets
                    # Send Motion
                    sock.sendto(motion_str.encode('utf-8'), (UDP_IP, PORT_VIS))
                    
                    # Send BPM (Every ~50 packets or 0.5s, just to keep app synced)
                    if row_count % 50 == 0:
                        sock.sendto(bpm_message, (UDP_IP, PORT_MUSIC))
                    
                    # 4. Wait
                    time.sleep(PLAYBACK_RATE)
                    row_count += 1
                    
    except FileNotFoundError:
        print(f"❌ Error: Could not find file '{CSV_FILE}'")
    except KeyboardInterrupt:
        print("\n🛑 Simulation Stopped.")

if __name__ == "__main__":
    run_simulation()