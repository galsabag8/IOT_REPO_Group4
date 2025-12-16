import serial
import socket
import time

# --- CONFIG ---
SERIAL_PORT = 'COM6'   
BAUD_RATE = 921600     
IP = "127.0.0.1"
PORT_MUSIC = 5005      # Port for app.py (BPM)
PORT_VIS = 5006 
       # Port for visualizer.py (3D Wand)
def listen():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"--- HUB: Connecting to {SERIAL_PORT}... ---")

    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1) as ser:
                print("--- HUB ACTIVE: Broadcasting Data... ---")
                ser.reset_input_buffer()
                
                while True:
                    if ser.in_waiting:
                        try:
                            line = ser.readline() # Keep as bytes for speed
                            sock.sendto(line, (IP, PORT_MUSIC))
                            # Broadcast to BOTH ports
                            
                            sock.sendto(line, (IP, PORT_VIS))
                            
                        except Exception:
                            pass
                    else:
                        time.sleep(0.001) 
        except Exception as e:
            print(f"Hub Error: {e}")
            time.sleep(2)

