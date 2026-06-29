# MQTT Collector – Raspberry Pi 5

Sammelt Daten von **Shelly Plug S** (JSON) und **ESP32** (Rohwerte)
über den lokalen MQTT-Broker und speichert sie als JSON-Dateien.
Ein PC kann die Daten jederzeit per MQTT-Request abrufen.

---

## Voraussetzungen

```bash
sudo apt update
sudo apt install -y libmosquitto-dev libcjson-dev mosquitto mosquitto-clients
```

---

## Kompilieren & Installieren

```bash
# Nur kompilieren (zum Testen)
make

# Als Systemdienst installieren (benötigt root)
sudo make install
sudo systemctl start mqtt_collector
sudo systemctl status mqtt_collector
```

---

## Topic-Struktur

| Gerät        | Topic-Beispiel                          | Format       |
|-------------|------------------------------------------|-------------|
| Shelly 1    | `shellies/shellyplug-s-AABBCC/relay/0`  | JSON         |
| Shelly 2    | `shellies/shellyplug-s-DDEEFF/relay/0`  | JSON         |
| ESP32 #1    | `esp32/sensor1/temperature`             | Rohwert      |
| ESP32 #2    | `esp32/sensor2/humidity`                | Rohwert      |

### Gespeicherte Dateien

Alle Dateien liegen in `/var/lib/mqtt_collector/`:

```
shellies_shellyplug-s-AABBCC_relay_0.json
shellies_shellyplug-s-DDEEFF_relay_0.json
esp32_sensor1_temperature.json
esp32_sensor2_humidity.json
```

### Beispiel einer gespeicherten JSON-Datei (Shelly)

```json
{
  "device_type": "shelly_plug_s",
  "topic": "shellies/shellyplug-s-AABBCC/relay/0",
  "data": [
    {
      "apower": 52.3,
      "voltage": 229.1,
      "current": 0.228,
      "timestamp": "2025-03-15T14:22:05",
      "topic": "shellies/shellyplug-s-AABBCC/relay/0"
    }
  ]
}
```

---

## PC-Datenabruf

Der PC sendet eine Anfrage auf `collector/request`,
der Pi antwortet auf `collector/response`.

### Alle Geräte abrufen

```bash
# Auf dem PC – zuerst subscriben
mosquitto_sub -h 192.168.1.XX -t "collector/response" &

# Dann Request senden (leerer Payload = alles)
mosquitto_pub -h 192.168.1.XX -t "collector/request" -m ""
```

### Nur bestimmte Geräte abrufen (Filter)

```bash
# Nur Shelly-Daten
mosquitto_pub -h 192.168.1.XX -t "collector/request" -m "shellies"

# Nur ESP32-Daten
mosquitto_pub -h 192.168.1.XX -t "collector/request" -m "esp32"
```

---

## Anpassungen im Code

Am Anfang von `mqtt_collector.c` können alle Parameter geändert werden:

```c
#define MQTT_HOST           "localhost"      // IP des Brokers
#define MQTT_PORT           1883
#define DATA_DIR            "/var/lib/mqtt_collector"
#define SHELLY_TOPIC_PREFIX "shellies"       // Shelly Topic erkennen
#define ESP32_TOPIC_PREFIX  "esp32"          // ESP32 Topic erkennen
```

---

## Logs ansehen

```bash
# Live-Log
sudo journalctl -u mqtt_collector -f

# Letzte 100 Zeilen
sudo journalctl -u mqtt_collector -n 100
```

---

## Testen ohne echte Hardware

```bash
# Shelly simulieren
mosquitto_pub -t "shellies/shellyplug-s-AABBCC/status" \
  -m '{"apower":52.3,"voltage":229.1,"current":0.228}'

# ESP32 simulieren
mosquitto_pub -t "esp32/sensor1/temperature" -m "23.5"
mosquitto_pub -t "esp32/sensor1/humidity"    -m "55"
```