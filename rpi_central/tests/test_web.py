import sys
import os

# Mock bluepy since it only builds on Linux/Raspberry Pi
from unittest.mock import MagicMock
sys.modules['bluepy'] = MagicMock()
sys.modules['bluepy.btle'] = MagicMock()

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
