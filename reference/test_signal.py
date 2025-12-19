#!/usr/bin/env python3
"""
Test signal handling for repoWatch.
"""

import signal
import time
import os

def signal_handler(signum, frame):
    print(f"Received signal {signum}")
    print("Signal handler working correctly")
    exit(0)

# Set up signal handler
signal.signal(signal.SIGINT, signal_handler)

print("Test script started. PID:", os.getpid())
print("Send SIGINT (Ctrl+C) to test signal handling...")
print("Will auto-exit in 10 seconds if no signal received")

time.sleep(10)
print("Auto-exiting after timeout")

