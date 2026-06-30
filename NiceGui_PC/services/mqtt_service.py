import json
import threading
from datetime import datetime
import paho.mqtt.client as mqtt

class MqttReceiver:
    def __init__(self, host, port=1883):
        self.host = host
        self.port = port
        self.connected = False

        self.topic_request = "collector/request"
        self.topic_response = "collector/response"
        self.topic_subscribers_request = "collector/subscribers/request"
        self.topic_subscribers_response = "collector/subscribers/response"

        self.subscribers = None
        self.device_data = {}
        self.status = "Bereit"
        self.last_update = None
        self.waiting_subs = False
        self.waiting_data = False

        self.on_status_changed = None
        self.on_subscribers_received = None
        self.on_data_received = None

        try:
            self.client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                client_id="nicegui_dashboard",
            )
        except AttributeError:
            self.client = mqtt.Client(client_id="nicegui_dashboard")

        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message

    def start(self):
        threading.Thread(target=self._connect, daemon=True).start()

    def _connect(self):
        try:
            self.client.connect(self.host, self.port, 60)
            self.client.loop_start()
        except Exception as error:
            self.set_status(f"Verbindung fehlgeschlagen: {error}")

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        if rc == 0:
            self.connected = True
            client.subscribe(self.topic_response)
            client.subscribe(self.topic_subscribers_response)
            self.set_status("MQTT verbunden")
        else:
            self.connected = False
            self.set_status(f"MQTT Fehler rc={rc}")

    def _on_disconnect(self, client, userdata, flags=None, rc=0, properties=None):
        self.connected = False
        self.set_status("MQTT getrennt")

    def _on_message(self, client, userdata, msg):
        if msg.topic == self.topic_subscribers_response:
            self._handle_subscribers(msg)
        elif msg.topic == self.topic_response:
            self._handle_device_data(msg)

    def _handle_subscribers(self, msg):
        try:
            data = json.loads(msg.payload.decode())
            self.subscribers = data.get("subscribers", [])
            self.waiting_subs = False
            self.set_status(f"✓ {len(self.subscribers)} Gerät(e) gefunden")

            if self.on_subscribers_received:
                self.on_subscribers_received(self.subscribers)

        except Exception as error:
            self.waiting_subs = False
            self.set_status(f"Subscribers Parse-Fehler: {error}")

    def _handle_device_data(self, msg):
        try:
            data = json.loads(msg.payload.decode())

            for device in data.get("devices", []):
                topic = device.get("topic", "")
                if topic:
                    self.device_data[topic] = device

            self.last_update = datetime.now().strftime("%d.%m.%Y %H:%M:%S")
            self.waiting_data = False
            self.set_status(f"✓ Daten empfangen – {self.last_update}")

            if self.on_data_received:
                self.on_data_received(self.device_data)

        except Exception as error:
            self.waiting_data = False
            self.set_status(f"Daten Parse-Fehler: {error}")

    def request_subscribers(self):
        if not self.connected:
            self.set_status("Nicht mit MQTT verbunden")
            return False

        self.waiting_subs = True
        self.set_status("⏳ Lade Geräteliste…")
        self.client.publish(self.topic_subscribers_request, "list")
        return True

    def request_all_data(self):
        if not self.connected:
            self.set_status("Nicht mit MQTT verbunden")
            return False

        if not self.subscribers:
            self.set_status("Erst Geräte laden")
            return False

        self.waiting_data = True
        self.set_status("⏳ Lade alle Daten…")
        self.client.publish(self.topic_request, "all")
        return True

    def clear_data(self):
        self.subscribers = None
        self.device_data = {}
        self.last_update = None
        self.set_status("Ansicht geleert")

    def set_status(self, status):
        self.status = status
        if self.on_status_changed:
            self.on_status_changed(status)