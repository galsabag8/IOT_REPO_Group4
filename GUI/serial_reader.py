import serial
import threading
import time

class ArduinoSerialReader:
    def __init__(self, port='COM4', baudrate=115200, log_callback=None): # check for your port
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.is_connected = False
        self.latest_bpm = 0
        self.thread = None
        self.running = False
        self.log_callback = log_callback  # Callback function to send logs
    
    def connect(self):
        """Connect to Arduino"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Wait for Arduino to initialize
            self.is_connected = True
            self.running = True
            
            # Start background thread to read data
            self.thread = threading.Thread(target=self._read_loop, daemon=True)
            self.thread.start()
            
            msg = f"✓ Connected to Arduino on {self.port}"
            print(msg)
            self._log(msg, "success")
            return True
        except Exception as e:
            msg = f"✗ Failed to connect to {self.port}: {e}"
            print(msg)
            self._log(msg, "error")
            self.is_connected = False
            return False
    
    def _read_loop(self):
        """Background thread that reads from serial"""
        while self.running and self.is_connected:
            try:
                if self.ser and self.ser.in_waiting:
                    line = self.ser.readline().decode('utf-8').strip()
                    
                    # Parse format: "BPM:120"
                    if line.startswith("BPM:"):
                        bpm_str = line.split(":")[1]
                        self.latest_bpm = int(bpm_str)
                        print(f"→ Received BPM from Arduino: {self.latest_bpm}")
            
            except ValueError as e:
                msg = f"Serial parse error: Invalid BPM format received"
                self._log(msg, "warning")
            except Exception as e:
                msg = f"Serial read error: {str(e)}"
                self._log(msg, "error")
            
            time.sleep(0.01)  # Check every 10ms

    def _log(self, message, level="info"):
        """Send log message to Flask backend"""
        if self.log_callback:
            try:
                self.log_callback(message, level)
            except:
                pass  # Silently fail if logging doesn't work
    
    def get_bpm(self):
        """Get the latest BPM from Arduino"""
        return self.latest_bpm
    
    def disconnect(self):
        """Close serial connection"""
        self.running = False
        if self.ser:
            self.ser.close()
        self.is_connected = False
        print("✓ Disconnected from Arduino")