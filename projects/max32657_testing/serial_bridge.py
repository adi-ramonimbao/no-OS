#!/usr/bin/env python3
"""
Serial to WebSocket Bridge for ADXL367 Accelerometer Data
Reads X/Y/Z data from /dev/ttyACM0 and broadcasts via WebSocket
"""

import asyncio
import websockets
import serial
import re
import json
import os
import threading
import functools
from http.server import HTTPServer, SimpleHTTPRequestHandler
from typing import Set

# Configuration
SERIAL_PORT = '/dev/ttyACM0'
SERIAL_BAUDRATE = 115200
WEBSOCKET_PORT = 8765
HTTP_PORT = 8000
HTML_FILE = 'adxl367_viewer.html'

# Regex to parse "X=   123 Y=  -456 Z=   789" format
DATA_PATTERN = re.compile(r'X=\s*(-?\d+)\s+Y=\s*(-?\d+)\s+Z=\s*(-?\d+)')

# Connected WebSocket clients
clients: Set[websockets.WebSocketServerProtocol] = set()


async def serial_reader(serial_port: serial.Serial):
    """Read from serial port and broadcast to all WebSocket clients"""
    print(f"📡 Reading from {SERIAL_PORT} at {SERIAL_BAUDRATE} baud")

    loop = asyncio.get_event_loop()

    while True:
        # Read line from serial (non-blocking)
        line = await loop.run_in_executor(None, serial_port.readline)

        if not line:
            await asyncio.sleep(0.01)
            continue

        try:
            line_str = line.decode('utf-8', errors='ignore').strip()

            # Parse accelerometer data
            match = DATA_PATTERN.search(line_str)
            if match:
                x, y, z = map(int, match.groups())

                # Create JSON message
                data = {
                    'x': x,
                    'y': y,
                    'z': z
                }

                message = json.dumps(data)
                print(f"📊 X={x:6d} Y={y:6d} Z={z:6d} → {len(clients)} clients")

                # Broadcast to all connected clients
                if clients:
                    await asyncio.gather(
                        *[client.send(message) for client in clients],
                        return_exceptions=True
                    )
        except Exception as e:
            print(f"⚠️  Error processing line: {e}")


async def websocket_handler(websocket: websockets.WebSocketServerProtocol):
    """Handle WebSocket client connections"""
    clients.add(websocket)
    client_addr = websocket.remote_address
    print(f"✅ Client connected: {client_addr} (total: {len(clients)})")

    try:
        # Keep connection alive and handle incoming messages (if any)
        async for message in websocket:
            pass  # We don't expect messages from client, but this keeps connection alive
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        clients.remove(websocket)
        print(f"❌ Client disconnected: {client_addr} (total: {len(clients)})")


def start_http_server():
    """Serve the current directory over HTTP in a background thread.

    ES modules / importmaps cannot load from file:// origins, so the web app
    must be served over HTTP.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))

    class QuietHandler(SimpleHTTPRequestHandler):
        """SimpleHTTPRequestHandler that keeps serial output readable."""
        def log_message(self, *args, **kwargs):
            pass

    handler = functools.partial(QuietHandler, directory=script_dir)
    httpd = HTTPServer(("localhost", HTTP_PORT), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    print(f"🌐 HTTP server: http://localhost:{HTTP_PORT}/{HTML_FILE}")


async def main():
    """Main entry point"""
    print("🚀 ADXL367 Serial-to-WebSocket Bridge")
    print("=" * 50)

    # Serve the web app over HTTP
    start_http_server()

    # Open serial port
    try:
        ser = serial.Serial(SERIAL_PORT, SERIAL_BAUDRATE, timeout=1)
        print(f"✅ Serial port opened: {SERIAL_PORT}")
    except serial.SerialException as e:
        print(f"❌ Failed to open serial port: {e}")
        print("\nTroubleshooting:")
        print("  1. Check if device is connected: ls -l /dev/ttyACM*")
        print("  2. Check permissions: sudo chmod 666 /dev/ttyACM0")
        print("  3. Add user to dialout group: sudo usermod -a -G dialout $USER")
        return

    # Start WebSocket server
    print(f"🌐 Starting WebSocket server on ws://localhost:{WEBSOCKET_PORT}")
    async with websockets.serve(websocket_handler, "localhost", WEBSOCKET_PORT):
        print(f"✅ WebSocket server running")
        print(f"\n💡 Open in your browser: http://localhost:{HTTP_PORT}/{HTML_FILE}")
        print("=" * 50)

        # Start serial reader
        await serial_reader(ser)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n👋 Shutting down...")
