import serial
import socket
import time

# --- CONFIG ---
SERIAL_PORT = 'COM4'   
BAUD_RATE = 921600     
IP = "127.0.0.1"
PORT_MUSIC = 5005      # Port for app.py (BPM)
PORT_VIS = 5006        # Port for visualizer.py (3D Wand)
PORT_CMD = 5007        # Listening for commands from app.py

def listen():
    # 1. Setup UDP Socket for incoming commands (Non-blocking)
    cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cmd_sock.bind((IP, PORT_CMD))
    cmd_sock.setblocking(False) 

    print(f"--- HUB: Connecting to {SERIAL_PORT}... ---")

    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1) as ser:
                print("--- HUB ACTIVE: Forwarding Data... ---")
                ser.reset_input_buffer()
                
                while True:
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

        except Exception as e:
            print(f"Hub Error: {e}")
            time.sleep(2)

if __name__ == "__main__":
    listen()

