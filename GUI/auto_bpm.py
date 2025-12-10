import requests
import time

# Configuration
SERVER_URL = "http://127.0.0.1:5000/set_bpm"

def send_bpm(bpm):
    try:
        # This matches the structure expected by your Flask app
        response = requests.post(SERVER_URL, json={"bpm": int(bpm)})
        if response.status_code == 200:
            print(f"--> Success: BPM set to {bpm}")
        else:
            print(f"--> Error: Server rejected request ({response.text})")
    except requests.exceptions.ConnectionError:
        print("--> Error: Could not connect to Flask. Is 'app.py' running?")

def main():
    print("--- Arduino BPM Simulator ---")
    print(f"Targeting: {SERVER_URL}")
    print("Enter a number to change BPM instantly. Press 'q' to quit.")
    print("-----------------------------")

    while True:
        # LATER: This line will be replaced by: bpm_input = serial_port.readline()
        user_input = input("Enter BPM: ").strip()

        if user_input.lower() == 'q':
            print("Exiting simulator.")
            break

        if not user_input.isdigit():
            print("Please enter a valid number.")
            continue

        bpm_val = int(user_input)
        
        # Sanity check (Arduino sensors can sometimes give weird spikes)
        if 20 <= bpm_val <= 300:
            send_bpm(bpm_val)
        else:
            print("Value out of safe range (20-300). Ignoring.")

if __name__ == "__main__":
    main()