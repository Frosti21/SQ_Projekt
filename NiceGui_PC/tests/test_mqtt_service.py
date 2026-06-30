import json
from unittest.mock import MagicMock

from services.mqtt_service import MqttReceiver


class FakeMessage:
    def __init__(self, topic, payload):
        self.topic = topic
        self.payload = json.dumps(payload).encode()


def test_request_subscribers_publishes_mqtt_message():
    receiver = MqttReceiver(host="localhost", port=1883)
    receiver.connected = True
    receiver.client = MagicMock()

    result = receiver.request_subscribers()

    assert result is True
    receiver.client.publish.assert_called_once_with(
        receiver.topic_subscribers_request,
        "list",
    )


def test_request_all_data_publishes_mqtt_message():
    receiver = MqttReceiver(host="localhost", port=1883)
    receiver.connected = True
    receiver.subscribers = [{"topic": "esp32/temp", "device_type": "esp32"}]
    receiver.client = MagicMock()

    result = receiver.request_all_data()

    assert result is True
    receiver.client.publish.assert_called_once_with(
        receiver.topic_request,
        "all",
    )


def test_receive_subscribers_response_updates_state():
    receiver = MqttReceiver(host="localhost", port=1883)

    msg = FakeMessage(
        receiver.topic_subscribers_response,
        {
            "subscribers": [
                {
                    "topic": "esp32/sensor/temp",
                    "device_type": "esp32",
                    "last_seen": "2025-01-01 12:00:00",
                }
            ]
        },
    )

    receiver._on_message(None, None, msg)

    assert receiver.subscribers is not None
    assert len(receiver.subscribers) == 1
    assert receiver.subscribers[0]["device_type"] == "esp32"
    assert receiver.waiting_subs is False


def test_receive_device_data_updates_state():
    receiver = MqttReceiver(host="localhost", port=1883)

    msg = FakeMessage(
        receiver.topic_response,
        {
            "devices": [
                {
                    "topic": "esp32/sensor/temp",
                    "data": [
                        {
                            "timestamp": "2025-01-01 12:00:00",
                            "temp": 23.5,
                        }
                    ],
                }
            ]
        },
    )

    receiver._on_message(None, None, msg)

    assert "esp32/sensor/temp" in receiver.device_data
    assert receiver.device_data["esp32/sensor/temp"]["data"][0]["temp"] == 23.5
    assert receiver.last_update is not None
    assert receiver.waiting_data is False