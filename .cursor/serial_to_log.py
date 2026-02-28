"""
Capture Serial output from COM7 to .cursor/debug.log (NDJSON).
Run while the device is connected and you reproduce the issue. Stop with Ctrl+C.
"""
import serial
import json
import os
import time

LOG_PATH = os.path.join(os.path.dirname(__file__), ".cursor", "debug.log")
PORT = "COM7"
BAUD = 115200

def main():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with serial.Serial(PORT, BAUD, timeout=0.5) as ser:
        print(f"Capturing {PORT} -> {LOG_PATH} (Ctrl+C to stop)")
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            while True:
                try:
                    line = ser.readline()
                    if not line:
                        continue
                    raw = line.decode("utf-8", errors="replace").strip()
                    if not raw:
                        continue
                    if raw.startswith("{") and raw.endswith("}"):
                        f.write(raw + "\n")
                    else:
                        entry = {"message": "serial", "data": {"raw": raw}, "timestamp": int(time.time() * 1000)}
                        f.write(json.dumps(entry) + "\n")
                    f.flush()
                except KeyboardInterrupt:
                    break
    print("Stopped.")

if __name__ == "__main__":
    main()
