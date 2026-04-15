import struct
import sys
import os

# Mock bluepy since it only builds on Linux/Raspberry Pi
from unittest.mock import MagicMock
sys.modules['bluepy'] = MagicMock()
sys.modules['bluepy.btle'] = MagicMock()

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

    def test_large_temperature(self):
        # temperature = 500 => 50.0 C
        data = struct.pack("<HiH", 1, 100000, 500)
        result = parse_environmental(data)
        assert abs(result["temperature_C"] - 50.0) < 0.01


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
