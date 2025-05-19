#!/usr/bin/env python3
import os
from flask import Flask, request, jsonify
from werkzeug.utils import secure_filename

UPLOAD_DIR = 'uploads'
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = Flask(__name__)
app.config['UPLOAD_FOLDER'] = UPLOAD_DIR

@app.route('/mic', methods=['POST'])
def upload_file():
    if 'file' not in request.files:
        return jsonify({'error': "No 'file' field in form"}), 400

    file = request.files['file']
    if file.filename == '':
        return jsonify({'error': 'Empty filename'}), 400

    filename = secure_filename(file.filename)
    save_path = os.path.join(app.config['UPLOAD_FOLDER'], filename)
    file.save(save_path)

    return jsonify({'message': f'File "{filename}" uploaded successfully'}), 200

if __name__ == '__main__':
    PORT = 8888
    print(f"Starting Flask server on port {PORT}, use <Ctrl-C> to stop")
    app.run(host='0.0.0.0', port=PORT)
