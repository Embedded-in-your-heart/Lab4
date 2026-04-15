# BLE Sensor Dashboard — Design Spec

**Date:** 2026-04-16  
**Project:** `rpi_central/` — STM32 B-L475E-IOT01A BLE Central (Raspberry Pi)

---

## Overview

Add a real-time HTML dashboard to `rpi_central/` that displays sensor data received over BLE from the STM32 board. The dashboard runs as a single Python process alongside the existing BLE central logic, using Flask-SocketIO to push data to the browser as it arrives.

---

## Architecture

### Single-Process Model

```
ble_central.py (one process)
│
├── Thread 1: BLE Loop
│     bluepy.waitForNotifications()
│       → parse data
│       → socketio.emit("env" | "imu" | "quat", {...})
│
└── Thread 2: Flask-SocketIO Server  (main thread)
      GET /          → serve templates/index.html
      WebSocket /socket.io  → push sensor events to browser
```

The BLE loop runs in a daemon `threading.Thread`. The main thread runs `socketio.run(app, host="0.0.0.0", port=5000)`. Flask-SocketIO operates in `threading` async mode, which is compatible with bluepy's blocking loop.

### WebSocket Events (server → client)

| Event | Payload | Trigger |
|-------|---------|---------|
| `"env"` | `{ temperature_C, pressure_hPa, timestamp }` | Environmental characteristic notification |
| `"imu"` | `{ acc_x, acc_y, acc_z, gyro_x, gyro_y, gyro_z, mag_x, mag_y, mag_z, timestamp }` | AccGyroMag characteristic notification |
| `"quat"` | `{ q_x, q_y, q_z, timestamp }` | Quaternions characteristic notification |
| `"status"` | `{ connected: bool, addr: str \| null }` | BLE connect / disconnect |

---

## File Structure

```
rpi_central/
├── ble_central.py        # Modified: add Flask-SocketIO server
├── templates/
│   └── index.html        # New: frontend dashboard (single file)
└── pyproject.toml        # Add flask, flask-socketio, simple-websocket
```

---

## Backend Changes (`ble_central.py`)

### New dependencies

```toml
# pyproject.toml — add to [project] dependencies
"flask>=3.0",
"flask-socketio>=5.0",
"simple-websocket>=1.0",
```

### Structural changes

1. **Initialize Flask + SocketIO** at module level:
   ```python
   from flask import Flask, render_template
   from flask_socketio import SocketIO
   app = Flask(__name__)
   socketio = SocketIO(app, async_mode="threading", cors_allowed_origins="*")
   ```

2. **Flask route** — serve the dashboard:
   ```python
   @app.route("/")
   def index():
       return render_template("index.html")
   ```

3. **`NotificationDelegate.handleNotification()`** — emit parsed data via SocketIO instead of (or in addition to) `print()`. Each characteristic emits its own named event.

4. **`main()`** — restructured startup:
   - Scan + select device (existing logic, unchanged)
   - Connect + subscribe (existing logic, unchanged)
   - Start BLE notification loop in a daemon `threading.Thread`
   - Emit `status` event on connect/disconnect
   - Call `socketio.run(app, host="0.0.0.0", port=5000)` on the main thread

5. **Signal handling** — `cleanup()` remains, also emits `status { connected: false }` before disconnecting.

---

## Frontend (`templates/index.html`)

### Visual Style

Dark industrial: `#0d1117` background, `#161b22` cards, `#30363d` borders, monospace font. Accent colours: blue (`#58a6ff`) for environmental, green (`#3fb950`) for accelerometer, orange-red (`#f78166`) for gyroscope, purple (`#d2a8ff`) for quaternions / magnetometer.

### Layout — Three Vertical Sections

```
┌─────────────────────────────────────────┐
│  ⬤ BLE SENSOR DASHBOARD     ● CONNECTED │  ← status bar
│  AB:CD:EF:12:34:56   ts: 12453          │
├─────────────────────────────────────────┤
│  ── ENVIRONMENTAL ───────────────────── │
│  ┌─────────────┐  ┌─────────────┐       │
│  │ 26.4 °C     │  │ 1013.2 hPa  │       │
│  │ [sparkline] │  │ [sparkline] │       │
│  └─────────────┘  └─────────────┘       │
├─────────────────────────────────────────┤
│  ── IMU ─────────────────────────────── │
│  ACCELEROMETER (mg)                     │
│  X +128  Y -056  Z +980                 │
│  [3-line sparkline: X=green Y=blue Z=red]│
│                                         │
│  GYROSCOPE (mdps)                       │
│  X +320  Y -120  Z +080                 │
│  [3-line sparkline]                     │
│                                         │
│  MAGNETOMETER                           │
│  X +012  Y -005  Z +034                 │
│  [3-line sparkline]                     │
├─────────────────────────────────────────┤
│  ── QUATERNIONS ─────────────────────── │
│  qX +100  qY -200  qZ +050             │
│  [3-line sparkline]                     │
└─────────────────────────────────────────┘
```

### Sparkline Implementation

- Pure SVG, no external chart library
- Each sensor axis maintains a ring buffer of the **last 60 samples** in JavaScript
- On each WebSocket event, append new values and re-render the SVG `<polyline>` points
- Y-axis auto-scales to min/max of the current 60-sample window per chart
- Multi-axis charts draw one `<polyline>` per axis with distinct colours

### Dependencies (frontend)

- `socket.io.js` — loaded from CDN (`https://cdn.socket.io/4.x/socket.io.min.js`)
- No other external JS libraries

### Connection State

- Status bar shows `● CONNECTED` (green) or `◯ DISCONNECTED` (red)
- On disconnect, all value displays freeze (show last known value with reduced opacity)
- On reconnect (new `status` event with `connected: true`), normal display resumes

---

## Error Handling

| Scenario | Behaviour |
|----------|-----------|
| BLE scan finds no devices | Existing CLI prompt unchanged; Flask server not started |
| BLE disconnects mid-session | `cleanup()` emits `status { connected: false }`; frontend shows disconnected state; process exits |
| Browser cannot reach server | User accesses `http://<rpi-ip>:5000` — standard network troubleshooting |
| Malformed BLE packet | Existing `parse_*()` guards return `None`; notification skipped silently |

---

## Out of Scope

- Authentication / access control on the web server
- Historical data persistence (no database)
- 3D quaternion visualisation (numbers only)
- Mobile-responsive layout
- Multiple simultaneous BLE devices
