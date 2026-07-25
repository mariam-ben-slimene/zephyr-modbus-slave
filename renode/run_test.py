#!/usr/bin/env python3
import subprocess
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESC_FILE = os.path.join(SCRIPT_DIR, "modbus_slave_test.resc")
UART_LOG = os.path.join(SCRIPT_DIR, "uart_output.log")

EXPECTED_LINES = [
    "FC06 Write",
    "FC03 Read",
    "Temp=25.0",
]

def main():
    if os.path.exists(UART_LOG):
        os.remove(UART_LOG)

    print("Running Renode emulation...")
    result = subprocess.run(
        ["renode", "--disable-xwt", "--console", RESC_FILE],
        capture_output=True,
        text=True,
        timeout=60,
    )

    if not os.path.exists(UART_LOG):
        print("FAIL: UART log file was not created.")
        print(result.stdout)
        print(result.stderr)
        sys.exit(1)

    with open(UART_LOG, "r", errors="ignore") as f:
        uart_output = f.read()

    print("--- UART Output ---")
    print(uart_output)
    print("--------------------")

    missing = [line for line in EXPECTED_LINES if line not in uart_output]

    if missing:
        print(f"FAIL: Missing expected output: {missing}")
        sys.exit(1)

    print("PASS: All expected Modbus transactions verified.")
    sys.exit(0)

if __name__ == "__main__":
    main()
