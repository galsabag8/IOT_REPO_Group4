import serial
import socket
import time
import config

def listen():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"--- HUB: Connecting to {config.SERIAL_PORT}... ---")

    while True:
        try:
            with serial.Serial(config.SERIAL_PORT, config.BAUD_RATE, timeout=0.1) as ser:
                print("--- HUB ACTIVE: Broadcasting Data... ---")
                ser.reset_input_buffer()
                last_heartbeat = 0
                
                while True:
                    # Send Heartbeat every 2 seconds to confirm connection
                    if time.time() - last_heartbeat > config.LISTENER_HEARTBEAT_INTERVAL:
                        try:
                            sock.sendto(b"STATUS: CONNECTED", (config.UDP_HOST, config.UDP_PORT))
                            last_heartbeat = time.time()
                        except: pass

                    if ser.in_waiting:
                        try:
                            line = ser.readline() # Keep as bytes for speed
                            sock.sendto(line, (config.UDP_HOST, config.UDP_PORT))
                            # Broadcast to BOTH ports
                            
                            sock.sendto(line, (config.UDP_HOST, config.UDP_PORT_VIS))
                            
                        except Exception:
                            pass
                    else:
                        time.sleep(0.001) 
        except Exception as e:
            print(f"Hub Error: {e}")
            try:
                sock.sendto(b"STATUS: DISCONNECTED", (config.UDP_HOST, config.UDP_PORT))
            except: pass
            time.sleep(2)
