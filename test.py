import requests
import sys

if len(sys.argv) != 2:
    print("Usage: python send_audio.py <audio_file.wav>")
    sys.exit(1)

url = 'http://localhost:8888/upload'
file_path = sys.argv[1]

try:
    with open(file_path, 'rb') as f:
        response = requests.post(url, data=f)
    print(response.text)
except Exception as e:
    print(f"Error sending file: {e}")
