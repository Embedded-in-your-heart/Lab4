# BLE Sensor Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real-time HTML dashboard to `rpi_central/` that displays BLE sensor data (temperature, pressure, accelerometer, gyroscope, magnetometer, quaternions) with live sparkline charts.

**Architecture:** A single Python process runs the existing BLE notification loop in a daemon thread and a Flask-SocketIO server on the main thread. When `NotificationDelegate` receives a BLE notification, it calls `socketio.emit()` to push structured JSON to connected browsers. The frontend is a single `index.html` rendered by Flask, with sparklines drawn in pure SVG (no external chart library). All DOM value updates use `textContent` — never `innerHTML` with data from the network.

**Tech Stack:** Python 3.8+, bluepy, Flask 3.x, Flask-SocketIO 5.x, simple-websocket, socket.io.js (CDN), vanilla JS + SVG

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Modify | `rpi_central/pyproject.toml` | Add flask, flask-socketio, simple-websocket |
| Modify | `rpi_central/ble_central.py` | Add Flask-SocketIO init, route, emit calls, restructure main() |
| Create | `rpi_central/templates/index.html` | Full dashboard HTML (status bar, ENV, IMU, QUAT sections, sparklines) |
| Create | `rpi_central/tests/__init__.py` | Empty — marks tests/ as a package |
| Create | `rpi_central/tests/test_parsers.py` | Unit tests for parse_environmental, parse_acc_gyro_mag, parse_quaternions |
| Create | `rpi_central/tests/test_web.py` | Flask route test (GET / returns 200 with HTML) |

---

## Task 1: Add Dependencies

**Files:**
- Modify: `rpi_central/pyproject.toml`

- [ ] **Step 1: Update pyproject.toml**

Replace the `dependencies` list:

```toml
[project]
name = "rpi-central"
version = "0.1.0"
description = "BLE Central receiver for STM32 B-L475E-IOT01A SensorDemo"
requires-python = ">=3.8"
dependencies = [
    "bluepy>=1.3.0",
    "flask>=3.0",
    "flask-socketio>=5.0",
    "simple-websocket>=1.0",
]

[project.scripts]
ble-central = "ble_central:main"
```

- [ ] **Step 2: Sync dependencies**

```bash
cd rpi_central
uv sync
```

Expected: packages installed with no errors.

- [ ] **Step 3: Commit**

```bash
git add rpi_central/pyproject.toml
git commit -m "feat: add flask and flask-socketio dependencies"
```

---

## Task 2: Unit Tests for Parser Functions

**Files:**
- Create: `rpi_central/tests/__init__.py`
- Create: `rpi_central/tests/test_parsers.py`

- [ ] **Step 1: Create tests package**

Create `rpi_central/tests/__init__.py` as an empty file.

- [ ] **Step 2: Write failing tests**

Create `rpi_central/tests/test_parsers.py`:

```python
import struct
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from ble_central import parse_environmental, parse_acc_gyro_mag, parse_quaternions


class TestParseEnvironmental:
    def test_valid_packet(self):
        # timestamp=100, pressure=101320 (1013.20 hPa), temperature=264 (26.4 C)
        data = struct.pack("<HiH", 100, 101320, 264)
        result = parse_environmental(data)
        assert result is not None
        assert result["timestamp"] == 100
        assert abs(result["pressure_hPa"] - 1013.20) < 0.01
        assert abs(result["temperature_C"] - 26.4) < 0.01

    def test_too_short_returns_none(self):
        assert parse_environmental(b"\x00" * 7) is None

    def test_exact_length(self):
        data = struct.pack("<HiH", 0, 0, 0)
        result = parse_environmental(data)
        assert result is not None

    def test_negative_temperature(self):
        # temperature = -50 => -5.0 C
        data = struct.pack("<HiH", 1, 100000, -50)
        result = parse_environmental(data)
        assert abs(result["temperature_C"] - (-5.0)) < 0.01


class TestParseAccGyroMag:
    def test_valid_packet(self):
        values = [200, 128, -56, 980, 320, -120, 80, 12, -5, 34]
        data = struct.pack("<H9h", *values)
        result = parse_acc_gyro_mag(data)
        assert result is not None
        assert result["timestamp"] == 200
        assert result["acc_x_mg"] == 128
        assert result["acc_y_mg"] == -56
        assert result["acc_z_mg"] == 980
        assert result["gyro_x_mdps"] == 320
        assert result["gyro_y_mdps"] == -120
        assert result["gyro_z_mdps"] == 80
        assert result["mag_x"] == 12
        assert result["mag_y"] == -5
        assert result["mag_z"] == 34

    def test_too_short_returns_none(self):
        assert parse_acc_gyro_mag(b"\x00" * 19) is None

    def test_exact_length(self):
        data = struct.pack("<H9h", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        assert parse_acc_gyro_mag(data) is not None


class TestParseQuaternions:
    def test_valid_packet(self):
        data = struct.pack("<Hhhh", 300, 100, -200, 50)
        result = parse_quaternions(data)
        assert result is not None
        assert result["timestamp"] == 300
        assert result["q_x"] == 100
        assert result["q_y"] == -200
        assert result["q_z"] == 50

    def test_too_short_returns_none(self):
        assert parse_quaternions(b"\x00" * 7) is None

    def test_exact_length(self):
        data = struct.pack("<Hhhh", 0, 0, 0, 0)
        assert parse_quaternions(data) is not None
```

- [ ] **Step 3: Run tests — expect PASS (parsers already exist)**

```bash
cd rpi_central
uv run pytest tests/test_parsers.py -v
```

Expected output:
```
test_parsers.py::TestParseEnvironmental::test_valid_packet PASSED
test_parsers.py::TestParseEnvironmental::test_too_short_returns_none PASSED
test_parsers.py::TestParseEnvironmental::test_exact_length PASSED
test_parsers.py::TestParseEnvironmental::test_negative_temperature PASSED
test_parsers.py::TestParseAccGyroMag::test_valid_packet PASSED
test_parsers.py::TestParseAccGyroMag::test_too_short_returns_none PASSED
test_parsers.py::TestParseAccGyroMag::test_exact_length PASSED
test_parsers.py::TestParseQuaternions::test_valid_packet PASSED
test_parsers.py::TestParseQuaternions::test_too_short_returns_none PASSED
test_parsers.py::TestParseQuaternions::test_exact_length PASSED
10 passed
```

- [ ] **Step 4: Commit**

```bash
git add rpi_central/tests/
git commit -m "test: add unit tests for BLE parser functions"
```

---

## Task 3: Flask Route Test

**Files:**
- Create: `rpi_central/tests/test_web.py`

- [ ] **Step 1: Write failing test**

Create `rpi_central/tests/test_web.py`:

```python
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest


@pytest.fixture
def client():
    import ble_central as bc
    bc.app.config["TESTING"] = True
    with bc.app.test_client() as c:
        yield c


def test_index_returns_200(client):
    response = client.get("/")
    assert response.status_code == 200


def test_index_returns_html(client):
    response = client.get("/")
    assert b"BLE SENSOR DASHBOARD" in response.data
```

- [ ] **Step 2: Run test — expect FAIL (Flask not added yet)**

```bash
cd rpi_central
uv run pytest tests/test_web.py -v
```

Expected: `AttributeError: module 'ble_central' has no attribute 'app'`

- [ ] **Step 3: Commit the test (red state)**

```bash
git add rpi_central/tests/test_web.py
git commit -m "test: add failing Flask route test"
```

---

## Task 4: Add Flask-SocketIO to ble_central.py

**Files:**
- Modify: `rpi_central/ble_central.py`

- [ ] **Step 1: Add imports at the top of ble_central.py**

After the existing imports block, add:

```python
import threading

from flask import Flask, render_template
from flask_socketio import SocketIO
```

- [ ] **Step 2: Initialize Flask and SocketIO at module level**

After the `CCCD_UUID` constant definition (around line 28), add:

```python
# ---------------------------------------------------------------------------
# Flask + SocketIO (web dashboard)
# ---------------------------------------------------------------------------
app = Flask(__name__)
socketio = SocketIO(app, async_mode="threading", cors_allowed_origins="*")


@app.route("/")
def index():
    return render_template("index.html")
```

- [ ] **Step 3: Emit sensor events in NotificationDelegate.handleNotification()**

Replace the body of `handleNotification()` with:

```python
def handleNotification(self, cHandle, data):
    name = self.handle_map.get(cHandle, f"0x{cHandle:04X}")

    if name == "Environmental":
        parsed = parse_environmental(data)
        if parsed:
            print(f"[ENV]  T={parsed['temperature_C']:.1f} C  "
                  f"P={parsed['pressure_hPa']:.2f} hPa")
            socketio.emit("env", {
                "temperature_C": parsed["temperature_C"],
                "pressure_hPa":  parsed["pressure_hPa"],
                "timestamp":     parsed["timestamp"],
            })
            return

    elif name == "AccGyroMag":
        parsed = parse_acc_gyro_mag(data)
        if parsed:
            print(f"[IMU]  Acc=({parsed['acc_x_mg']:>6d}, {parsed['acc_y_mg']:>6d}, {parsed['acc_z_mg']:>6d}) mg  "
                  f"Gyro=({parsed['gyro_x_mdps']:>7d}, {parsed['gyro_y_mdps']:>7d}, {parsed['gyro_z_mdps']:>7d}) mdps")
            socketio.emit("imu", {
                "acc_x":  parsed["acc_x_mg"],
                "acc_y":  parsed["acc_y_mg"],
                "acc_z":  parsed["acc_z_mg"],
                "gyro_x": parsed["gyro_x_mdps"],
                "gyro_y": parsed["gyro_y_mdps"],
                "gyro_z": parsed["gyro_z_mdps"],
                "mag_x":  parsed["mag_x"],
                "mag_y":  parsed["mag_y"],
                "mag_z":  parsed["mag_z"],
                "timestamp": parsed["timestamp"],
            })
            return

    elif name == "Quaternions":
        parsed = parse_quaternions(data)
        if parsed:
            print(f"[QUAT] X={parsed['q_x']:>6d}  Y={parsed['q_y']:>6d}  Z={parsed['q_z']:>6d}")
            socketio.emit("quat", {
                "q_x": parsed["q_x"],
                "q_y": parsed["q_y"],
                "q_z": parsed["q_z"],
                "timestamp": parsed["timestamp"],
            })
            return

    print(f"[{name}] Raw: {data.hex()}")
```

- [ ] **Step 4: Restructure main() to run BLE in a thread**

Replace the entire `main()` function with:

```python
def main():
    dev_list = scan_devices(timeout=10)
    if not dev_list:
        print("No devices found. Make sure the STM32 board is advertising.")
        sys.exit(1)

    dev = select_device(dev_list)
    print(f"\nConnecting to {dev.addr} (type={dev.addrType}) ...")

    peripheral = Peripheral()

    def cleanup(signum=None, frame=None):
        print("\nDisconnecting ...")
        socketio.emit("status", {"connected": False, "addr": None})
        try:
            peripheral.disconnect()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGINT, cleanup)

    def ble_loop():
        try:
            peripheral.connect(dev.addr, dev.addrType)
            print("Connected!")

            handle_map = discover_and_subscribe(peripheral)

            if not handle_map:
                print("No target characteristics found. Is this the correct device?")
                cleanup()
                return

            peripheral.withDelegate(NotificationDelegate(handle_map))
            socketio.emit("status", {"connected": True, "addr": dev.addr})

            print(f"\n{'='*60}")
            print(" Listening for sensor data ... (Ctrl+C to stop)")
            print(f"{'='*60}\n")

            while True:
                peripheral.waitForNotifications(1.0)

        except BTLEException as e:
            print(f"BLE Error: {e}")
        except Exception as e:
            print(f"Unexpected error: {type(e).__name__}: {e}")
        finally:
            cleanup()

    ble_thread = threading.Thread(target=ble_loop, daemon=True)
    ble_thread.start()

    print(f"\nDashboard available at http://0.0.0.0:5000")
    socketio.run(app, host="0.0.0.0", port=5000, use_reloader=False, log_output=True)
```

- [ ] **Step 5: Run route test — expect PASS**

```bash
cd rpi_central
uv run pytest tests/test_web.py -v
```

Expected:
```
tests/test_web.py::test_index_returns_200 FAILED  (templates/index.html not yet created — 500)
tests/test_web.py::test_index_returns_html FAILED
```

Both will turn green after Task 5.

- [ ] **Step 6: Commit**

```bash
git add rpi_central/ble_central.py
git commit -m "feat: integrate Flask-SocketIO server into ble_central"
```

---

## Task 5: Create Dashboard HTML

**Files:**
- Create: `rpi_central/templates/index.html`

**Security note:** All sensor values arriving via WebSocket are numbers. DOM updates use `textContent` exclusively — never `innerHTML` with network data — so XSS is not possible.

- [ ] **Step 1: Create templates/ directory and index.html**

Create `rpi_central/templates/index.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>BLE SENSOR DASHBOARD</title>
  <script src="https://cdn.socket.io/4.7.5/socket.io.min.js"></script>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      background: #0d1117;
      color: #c9d1d9;
      font-family: 'Courier New', Courier, monospace;
      font-size: 13px;
      min-height: 100vh;
      padding: 16px;
    }

    /* ── Status bar ── */
    #status-bar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid #30363d;
      padding-bottom: 10px;
      margin-bottom: 16px;
    }
    .title { color: #58a6ff; font-size: 14px; font-weight: bold; letter-spacing: 1px; }
    #status-indicator { font-size: 11px; }
    #status-indicator.connected    { color: #3fb950; }
    #status-indicator.disconnected { color: #f78166; }

    /* ── Sections ── */
    .sensor-section { margin-bottom: 20px; }
    .section-title {
      color: #8b949e;
      font-size: 10px;
      letter-spacing: 2px;
      margin-bottom: 10px;
    }

    /* ── Card grid ── */
    .card-row          { display: grid; gap: 8px; }
    .card-row.two-col  { grid-template-columns: 1fr 1fr; }
    .card-row.one-col  { grid-template-columns: 1fr; }

    .card {
      background: #161b22;
      border: 1px solid #30363d;
      border-radius: 4px;
      padding: 10px;
      transition: opacity 0.3s;
    }
    .card.stale { opacity: 0.45; }

    .label {
      color: #8b949e;
      font-size: 9px;
      letter-spacing: 1.5px;
      margin-bottom: 4px;
    }

    /* ── ENV big value: number + unit in separate spans ── */
    .big-value { font-size: 22px; font-weight: bold; color: #58a6ff; margin-bottom: 6px; }
    .unit      { font-size: 11px; color: #8b949e; margin-left: 3px; }

    /* ── XYZ row ── */
    .xyz-row             { display: flex; gap: 16px; margin-bottom: 6px; font-size: 12px; }
    .xyz-row .axis       { color: #8b949e; }
    .xyz-row .val        { font-weight: bold; }
    .xyz-row .val.acc    { color: #3fb950; }
    .xyz-row .val.gyro   { color: #f78166; }
    .xyz-row .val.mag    { color: #e3b341; }
    .xyz-row .val.quat   { color: #d2a8ff; }

    /* ── Sparkline ── */
    .sparkline { width: 100%; height: 40px; display: block; overflow: visible; }
  </style>
</head>
<body>

<!-- Status bar -->
<div id="status-bar">
  <span class="title">⬤ BLE SENSOR DASHBOARD</span>
  <span id="status-indicator" class="disconnected">◯ DISCONNECTED</span>
</div>

<!-- ── ENVIRONMENTAL ── -->
<div class="sensor-section">
  <div class="section-title">── ENVIRONMENTAL ─────────────────────────────</div>
  <div class="card-row two-col">

    <div class="card" id="card-temp">
      <div class="label">TEMPERATURE</div>
      <div class="big-value">
        <span id="val-temp">--.-</span><span class="unit">°C</span>
      </div>
      <svg class="sparkline" id="chart-temp"></svg>
    </div>

    <div class="card" id="card-press">
      <div class="label">PRESSURE</div>
      <div class="big-value">
        <span id="val-press">----</span><span class="unit">hPa</span>
      </div>
      <svg class="sparkline" id="chart-press"></svg>
    </div>

  </div>
</div>

<!-- ── IMU ── -->
<div class="sensor-section">
  <div class="section-title">── IMU ───────────────────────────────────────</div>
  <div class="card-row one-col">

    <div class="card" id="card-acc">
      <div class="label">ACCELEROMETER (mg)</div>
      <div class="xyz-row">
        <span><span class="axis">X </span><span class="val acc" id="val-acc-x">----</span></span>
        <span><span class="axis">Y </span><span class="val acc" id="val-acc-y">----</span></span>
        <span><span class="axis">Z </span><span class="val acc" id="val-acc-z">----</span></span>
      </div>
      <svg class="sparkline" id="chart-acc"></svg>
    </div>

    <div class="card" id="card-gyro">
      <div class="label">GYROSCOPE (mdps)</div>
      <div class="xyz-row">
        <span><span class="axis">X </span><span class="val gyro" id="val-gyro-x">----</span></span>
        <span><span class="axis">Y </span><span class="val gyro" id="val-gyro-y">----</span></span>
        <span><span class="axis">Z </span><span class="val gyro" id="val-gyro-z">----</span></span>
      </div>
      <svg class="sparkline" id="chart-gyro"></svg>
    </div>

    <div class="card" id="card-mag">
      <div class="label">MAGNETOMETER</div>
      <div class="xyz-row">
        <span><span class="axis">X </span><span class="val mag" id="val-mag-x">----</span></span>
        <span><span class="axis">Y </span><span class="val mag" id="val-mag-y">----</span></span>
        <span><span class="axis">Z </span><span class="val mag" id="val-mag-z">----</span></span>
      </div>
      <svg class="sparkline" id="chart-mag"></svg>
    </div>

  </div>
</div>

<!-- ── QUATERNIONS ── -->
<div class="sensor-section">
  <div class="section-title">── QUATERNIONS ───────────────────────────────</div>
  <div class="card-row one-col">
    <div class="card" id="card-quat">
      <div class="label">QUATERNIONS</div>
      <div class="xyz-row">
        <span><span class="axis">qX </span><span class="val quat" id="val-qx">----</span></span>
        <span><span class="axis">qY </span><span class="val quat" id="val-qy">----</span></span>
        <span><span class="axis">qZ </span><span class="val quat" id="val-qz">----</span></span>
      </div>
      <svg class="sparkline" id="chart-quat"></svg>
    </div>
  </div>
</div>

<script>
  // ── Sparkline engine ──────────────────────────────────────────────────────
  const HISTORY = 60;

  const buf = {};
  function getBuf(key) {
    if (!buf[key]) buf[key] = [];
    return buf[key];
  }
  function push(key, val) {
    const b = getBuf(key);
    b.push(val);
    if (b.length > HISTORY) b.shift();
  }

  function renderSparkline(svgId, seriesKeys, colors) {
    const svg = document.getElementById(svgId);
    if (!svg) return;
    const W = svg.clientWidth || 200;
    const H = svg.clientHeight || 40;
    const PAD = 2;

    const series = seriesKeys.map(k => getBuf(k));
    const allVals = series.flat();
    if (allVals.length === 0) return;

    let mn = Math.min(...allVals);
    let mx = Math.max(...allVals);
    if (mn === mx) { mn -= 1; mx += 1; }

    const polylines = series.map((data, i) => {
      if (data.length < 2) return '';
      const pts = data.map((v, j) => {
        const x = PAD + (j / (HISTORY - 1)) * (W - PAD * 2);
        const y = (H - PAD) - ((v - mn) / (mx - mn)) * (H - PAD * 2);
        return x.toFixed(1) + ',' + y.toFixed(1);
      }).join(' ');
      const opacity = i === 0 ? '1' : '0.7';
      return '<polyline points="' + pts + '" fill="none" stroke="' + colors[i] + '" stroke-width="1.5" opacity="' + opacity + '"/>';
    }).join('');

    // Safe: polylines is built from numbers only — no user-controlled strings
    svg.innerHTML = polylines;
  }

  // ── Status ────────────────────────────────────────────────────────────────
  const ALL_CARDS = ['card-temp','card-press','card-acc','card-gyro','card-mag','card-quat'];

  function setConnected(connected, addr) {
    const el = document.getElementById('status-indicator');
    if (connected) {
      // addr is a MAC address from the server — use textContent to avoid XSS
      el.textContent = '● CONNECTED  ' + (addr || '');
      el.className = 'connected';
      ALL_CARDS.forEach(id => document.getElementById(id).classList.remove('stale'));
    } else {
      el.textContent = '◯ DISCONNECTED';
      el.className = 'disconnected';
      ALL_CARDS.forEach(id => document.getElementById(id).classList.add('stale'));
    }
  }

  // ── Socket.IO ─────────────────────────────────────────────────────────────
  const socket = io();

  socket.on('connect',    () => setConnected(true,  null));
  socket.on('disconnect', () => setConnected(false, null));
  socket.on('status', (d) => setConnected(d.connected, d.addr));

  socket.on('env', (d) => {
    document.getElementById('val-temp').textContent  = d.temperature_C.toFixed(1);
    document.getElementById('val-press').textContent = d.pressure_hPa.toFixed(1);
    push('temp',  d.temperature_C);
    push('press', d.pressure_hPa);
    renderSparkline('chart-temp',  ['temp'],  ['#58a6ff']);
    renderSparkline('chart-press', ['press'], ['#58a6ff']);
  });

  socket.on('imu', (d) => {
    document.getElementById('val-acc-x').textContent  = fmtInt(d.acc_x);
    document.getElementById('val-acc-y').textContent  = fmtInt(d.acc_y);
    document.getElementById('val-acc-z').textContent  = fmtInt(d.acc_z);
    document.getElementById('val-gyro-x').textContent = fmtInt(d.gyro_x);
    document.getElementById('val-gyro-y').textContent = fmtInt(d.gyro_y);
    document.getElementById('val-gyro-z').textContent = fmtInt(d.gyro_z);
    document.getElementById('val-mag-x').textContent  = fmtInt(d.mag_x);
    document.getElementById('val-mag-y').textContent  = fmtInt(d.mag_y);
    document.getElementById('val-mag-z').textContent  = fmtInt(d.mag_z);

    push('acc_x',  d.acc_x);  push('acc_y',  d.acc_y);  push('acc_z',  d.acc_z);
    push('gyro_x', d.gyro_x); push('gyro_y', d.gyro_y); push('gyro_z', d.gyro_z);
    push('mag_x',  d.mag_x);  push('mag_y',  d.mag_y);  push('mag_z',  d.mag_z);

    renderSparkline('chart-acc',  ['acc_x',  'acc_y',  'acc_z'],  ['#3fb950','#58a6ff','#f78166']);
    renderSparkline('chart-gyro', ['gyro_x', 'gyro_y', 'gyro_z'], ['#f78166','#e3b341','#d2a8ff']);
    renderSparkline('chart-mag',  ['mag_x',  'mag_y',  'mag_z'],  ['#e3b341','#58a6ff','#3fb950']);
  });

  socket.on('quat', (d) => {
    document.getElementById('val-qx').textContent = fmtInt(d.q_x);
    document.getElementById('val-qy').textContent = fmtInt(d.q_y);
    document.getElementById('val-qz').textContent = fmtInt(d.q_z);
    push('q_x', d.q_x); push('q_y', d.q_y); push('q_z', d.q_z);
    renderSparkline('chart-quat', ['q_x','q_y','q_z'], ['#d2a8ff','#58a6ff','#f78166']);
  });

  // Format integer with explicit sign, padded to 4 digits
  function fmtInt(v) {
    const sign = v >= 0 ? '+' : '-';
    return sign + String(Math.abs(v)).padStart(4, '0');
  }
</script>
</body>
</html>
```

- [ ] **Step 2: Run all tests — expect all pass**

```bash
cd rpi_central
uv run pytest tests/ -v
```

Expected:
```
tests/test_parsers.py — 10 passed
tests/test_web.py::test_index_returns_200 PASSED
tests/test_web.py::test_index_returns_html PASSED
12 passed
```

- [ ] **Step 3: Commit**

```bash
git add rpi_central/templates/index.html
git commit -m "feat: add sensor dashboard HTML with sparklines"
```

---

## Task 6: Manual Smoke Test

> BLE hardware is required. Skip if no STM32 board is available; the unit tests already verify parser and route correctness.

- [ ] **Step 1: Run the server on Raspberry Pi**

```bash
cd rpi_central
sudo .venv/bin/python ble_central.py
```

Expected output:
```
Scanning for BLE devices (10 sec) ...
...
Select device index: 0
Connecting to AB:CD:EF:12:34:56 ...
Connected!
Dashboard available at http://0.0.0.0:5000
```

- [ ] **Step 2: Open dashboard in browser**

Navigate to `http://<raspberry-pi-ip>:5000`

Verify:
- Status bar shows `● CONNECTED AB:CD:EF:12:34:56`
- Temperature and pressure values update in real time
- Accelerometer, gyroscope, magnetometer XYZ values update
- Sparkline charts accumulate data points and draw correctly
- Quaternion values update

- [ ] **Step 3: Test disconnect behaviour**

Power off the STM32 board. Verify:
- Status bar changes to `◯ DISCONNECTED`
- All cards dim (reduced opacity)

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat: complete BLE sensor dashboard (Flask-SocketIO + HTML)"
```
