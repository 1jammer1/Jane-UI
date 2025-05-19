import sys
import requests

if len(sys.argv) != 2:
    print("Usage: python send_audio.py <audio_file.wav>")
    sys.exit(1)

url = 'http://localhost:8887/upload'
file_path = sys.argv[1]

try:
    with open(file_path, 'rb') as f:
        data = f.read()
        headers = {'Content-Type': 'application/octet-stream'}
        response = requests.post(url, data=data, headers=headers)
    print(response.text)
except Exception as e:
    print(f"Error sending file: {e}")
