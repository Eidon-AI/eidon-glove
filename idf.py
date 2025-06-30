#!/usr/bin/env python3
"""
ESP-IDF shim script for Eidon Glove project
Forwards idf.py commands to the firmware directory
"""

import os
import sys
import subprocess

def main():
    # Get the directory where this script is located (project root)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    firmware_dir = os.path.join(script_dir, "firmware")
    
    # Check if firmware directory exists
    if not os.path.exists(firmware_dir):
        print("Error: firmware directory not found!")
        sys.exit(1)
    
    # Change to firmware directory
    os.chdir(firmware_dir)
    
    # Source ESP-IDF environment and run idf.py with all arguments
    esp_idf_path = os.path.expanduser("~/esp-idf")
    
    # Construct the command
    if sys.platform == "darwin" or sys.platform == "linux":
        # On macOS/Linux, we need to source the export script first
        cmd = f'source {esp_idf_path}/export.sh && idf.py {" ".join(sys.argv[1:])}'
        # Use bash to execute the command
        result = subprocess.run(["/bin/bash", "-c", cmd])
    else:
        # On Windows, use export.bat
        cmd = [os.path.join(esp_idf_path, "export.bat"), "&&", "idf.py"] + sys.argv[1:]
        result = subprocess.run(cmd, shell=True)
    
    # Exit with the same code as the subprocess
    sys.exit(result.returncode)

if __name__ == "__main__":
    main() 