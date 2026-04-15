#!/usr/bin/env python3
"""
BLE Central for STM32 B-L475E-IOT01A SensorDemo
Connects to the BlueNRG-MS peripheral board, subscribes to sensor
notifications, and prints parsed accelerometer / gyroscope / temperature data.

Reference design: https://github.com/Embedded-in-your-heart/Lab3
Requires: sudo pip install bluepy   (BLE operations need root on RPi)
Usage:    sudo python3 ble_central.py
"""

import struct
import signal
import sys
from bluepy.btle import Scanner, DefaultDelegate, Peripheral, BTLEException

# ---------------------------------------------------------------------------
# GATT UUIDs (must match gatt_db.c on the STM32 side)
# ---------------------------------------------------------------------------
HW_SERVICE_UUID       = "00000000-0001-11e1-9ab4-0002a5d5c51b"
ENVIRONMENTAL_CHAR_UUID = "00140000-0001-11e1-ac36-0002a5d5c51b"
ACC_GYRO_MAG_CHAR_UUID  = "00e00000-0001-11e1-ac36-0002a5d5c51b"

SW_SERVICE_UUID       = "00000000-0002-11e1-9ab4-0002a5d5c51b"
QUATERNIONS_CHAR_UUID = "00000100-0001-11e1-ac36-0002a5d5c51b"

# CCCD descriptor UUID (standard BLE)
CCCD_UUID = 0x2902

# ---------------------------------------------------------------------------
# Data parsing helpers
# ---------------------------------------------------------------------------

def parse_environmental(data):
    """
    Environmental characteristic (8 bytes):
      [timestamp:2][pressure:4][temperature:2]
    - pressure:    int32, unit = 1/100 hPa
    - temperature: int16, unit = 1/10 degC
    """
    if len(data) < 8:
        return None
    ts, press, temp = struct.unpack('<HiH', data[:8])
    return {
        'timestamp': ts,
        'pressure_hPa': press / 100.0,
        'temperature_C': temp / 10.0,
    }


def parse_acc_gyro_mag(data):
    """
    AccGyroMag characteristic (20 bytes):
      [timestamp:2][acc_x:2][acc_y:2][acc_z:2]
                   [gyro_x:2][gyro_y:2][gyro_z:2]
                   [mag_x:2][mag_y:2][mag_z:2]
    - acc:  int16, mg
    - gyro: int16, mdps
    - mag:  int16
    """
    if len(data) < 20:
        return None
    values = struct.unpack('<H9h', data[:20])
    return {
        'timestamp': values[0],
        'acc_x_mg':   values[1],
        'acc_y_mg':   values[2],
        'acc_z_mg':   values[3],
        'gyro_x_mdps': values[4],
        'gyro_y_mdps': values[5],
        'gyro_z_mdps': values[6],
        'mag_x':      values[7],
        'mag_y':      values[8],
        'mag_z':      values[9],
    }


def parse_quaternions(data):
    """
    Quaternions characteristic (8 bytes when SEND_N_QUATERNIONS=1):
      [timestamp:2][q_x:2][q_y:2][q_z:2]
    """
    if len(data) < 8:
        return None
    ts, qx, qy, qz = struct.unpack('<Hhhh', data[:8])
    return {
        'timestamp': ts,
        'q_x': qx,
        'q_y': qy,
        'q_z': qz,
    }


# ---------------------------------------------------------------------------
# BLE Delegates (following Lab3 pattern)
# ---------------------------------------------------------------------------

class ScanDelegate(DefaultDelegate):
    """Callback fired for every advertisement packet received during scan."""
    def handleDiscovery(self, dev, isNewDev, isNewData):
        if isNewDev:
            name = dev.getValueText(0x09) or "(unknown)"
            print(f"  [{dev.addr}] RSSI={dev.rssi} dB  {name}")


class NotificationDelegate(DefaultDelegate):
    """Callback fired when the peripheral sends a notification / indication."""

    def __init__(self, char_handle_map):
        super().__init__()
        # Map from GATT value handle -> characteristic name
        self.handle_map = char_handle_map

    def handleNotification(self, cHandle, data):
        name = self.handle_map.get(cHandle, f"0x{cHandle:04X}")

        if name == "Environmental":
            parsed = parse_environmental(data)
            if parsed:
                print(f"[ENV]  T={parsed['temperature_C']:.1f} C  "
                      f"P={parsed['pressure_hPa']:.2f} hPa")
                return

        elif name == "AccGyroMag":
            parsed = parse_acc_gyro_mag(data)
            if parsed:
                print(f"[IMU]  Acc=({parsed['acc_x_mg']:>6d}, {parsed['acc_y_mg']:>6d}, {parsed['acc_z_mg']:>6d}) mg  "
                      f"Gyro=({parsed['gyro_x_mdps']:>7d}, {parsed['gyro_y_mdps']:>7d}, {parsed['gyro_z_mdps']:>7d}) mdps")
                return

        elif name == "Quaternions":
            parsed = parse_quaternions(data)
            if parsed:
                print(f"[QUAT] X={parsed['q_x']:>6d}  Y={parsed['q_y']:>6d}  Z={parsed['q_z']:>6d}")
                return

        # Fallback: print raw hex
        print(f"[{name}] Raw: {data.hex()}")


# ---------------------------------------------------------------------------
# Scan
# ---------------------------------------------------------------------------

def scan_devices(timeout=10):
    """Scan for BLE peripherals and return the discovered device list."""
    print(f"\nScanning for BLE devices ({timeout} sec) ...")
    scanner = Scanner().withDelegate(ScanDelegate())
    devices = scanner.scan(timeout)
    dev_list = list(devices)

    print(f"\n{'='*60}")
    print(f" Found {len(dev_list)} device(s)")
    print(f"{'='*60}")
    for i, d in enumerate(dev_list):
        name = d.getValueText(0x09) or "(unknown)"
        print(f"  [{i}] {d.addr}  type={d.addrType}  RSSI={d.rssi} dB  {name}")
    return dev_list


def select_device(dev_list):
    """Let the user choose a device by index."""
    while True:
        try:
            idx = int(input("\nSelect device index: "))
            if 0 <= idx < len(dev_list):
                return dev_list[idx]
            print("Index out of range.")
        except ValueError:
            print("Please enter a number.")


# ---------------------------------------------------------------------------
# Service / Characteristic discovery
# ---------------------------------------------------------------------------

def discover_and_subscribe(peripheral):
    """
    Discover HW and SW services, find the target characteristics,
    enable CCCD notifications on each, and return the handle map.
    """
    handle_map = {}
    chars_to_subscribe = []

    target_chars = {
        ENVIRONMENTAL_CHAR_UUID: "Environmental",
        ACC_GYRO_MAG_CHAR_UUID:  "AccGyroMag",
        QUATERNIONS_CHAR_UUID:   "Quaternions",
    }

    print("\nDiscovering services ...")
    for svc in peripheral.getServices():
        print(f"  Service: {svc.uuid}")
        for ch in svc.getCharacteristics():
            uuid_str = str(ch.uuid)
            props = ch.propertiesToString()
            print(f"    Char: {uuid_str}  handle=0x{ch.getHandle():04X}  [{props}]")

            if uuid_str in target_chars:
                name = target_chars[uuid_str]
                val_handle = ch.getHandle()
                # The notification data arrives on the value handle
                handle_map[val_handle] = name
                if "NOTIFY" in props or "INDICATE" in props:
                    chars_to_subscribe.append((ch, name, svc))

    # Enable CCCD for each target characteristic
    for ch, name, svc in chars_to_subscribe:
        try:
            cccd_handle = ch.getHandle() + 2  # CCCD is typically at value_handle + 1 (i.e. char_handle + 2)
            # Try to find CCCD descriptor properly
            descs = ch.getDescriptors()
            for d in descs:
                if d.uuid == CCCD_UUID:
                    cccd_handle = d.handle
                    break

            # Write 0x0001 = Enable Notification
            peripheral.writeCharacteristic(cccd_handle, struct.pack('<H', 0x0001), withResponse=True)
            print(f"  -> Enabled notifications for {name} (CCCD handle=0x{cccd_handle:04X})")
        except BTLEException as e:
            print(f"  !! Failed to enable notifications for {name}: {e}")

    return handle_map


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Scan and select device
    dev_list = scan_devices(timeout=10)
    if not dev_list:
        print("No devices found. Make sure the STM32 board is advertising.")
        sys.exit(1)

    dev = select_device(dev_list)
    print(f"\nConnecting to {dev.addr} (type={dev.addrType}) ...")

    peripheral = Peripheral()

    def cleanup(signum=None, frame=None):
        print("\nDisconnecting ...")
        try:
            peripheral.disconnect()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGINT, cleanup)

    try:
        peripheral.connect(dev.addr, dev.addrType)
        print("Connected!")

        # Discover services and enable notifications
        handle_map = discover_and_subscribe(peripheral)

        if not handle_map:
            print("No target characteristics found. Is this the correct device?")
            cleanup()

        # Set notification delegate
        peripheral.withDelegate(NotificationDelegate(handle_map))

        print(f"\n{'='*60}")
        print(" Listening for sensor data ... (Ctrl+C to stop)")
        print(f"{'='*60}\n")

        while True:
            peripheral.waitForNotification(1.0)

    except BTLEException as e:
        print(f"BLE Error: {e}")
    finally:
        cleanup()


if __name__ == "__main__":
    main()
