# Software Quality Projekt

## Übersicht

Dieses Repository bildet einen typischen Embedded-Software-Workflow nach.

Das Projekt besteht aus drei unabhängigen Teilprojekten, die über ein gemeinsames Build-System sowie eine Continuous-Integration-Pipeline miteinander verbunden sind.

- **Sensor_ESP32** – Firmware für einen ESP32, entwickelt mit ESP-IDF
- **MQTT_Broker_Pi** – C-Anwendung für einen Raspberry Pi zur Verarbeitung von MQTT-Nachrichten
- **NiceGui_PC** – Python/NiceGUI-Anwendung zur Visualisierung der empfangenen Sensordaten

Alle Projekte werden automatisch mittels **GitHub Actions** gebaut, getestet und statisch analysiert.

---

# Projektstruktur

```text
SQ_Projekt/
│
├── .github/
│   └── workflows/
│       ├── esp32-build.yml
│       ├── raspberry-build.yml
│       ├── python-test.yml
│       └── static-analysis.yml
│
├── Sensor_ESP32/
│
├── MQTT_Broker_Pi/
│
├── NiceGui_PC/
│   ├── services/
│   ├── view/
│   ├── tests/
│   └── requirements.txt
│
├── Makefile
└── README.md
```

---

# Voraussetzungen

## ESP32

- ESP-IDF 5.x
- Python
- CMake
- Ninja

## Raspberry Pi

Benötigte Pakete:

```bash
sudo apt update
sudo apt install \
    build-essential \
    cmake \
    pkg-config \
    libmosquitto-dev \
    libcjson-dev \
    libssl-dev
```

## Python

Python 3.11 oder neuer

Installation der benötigten Bibliotheken:

```bash
pip install -r NiceGui_PC/requirements.txt
```

---

# Projekt lokal bauen

## Gesamtes Projekt

```bash
make all
```

---

## ESP32

```bash
make esp32
```

oder

```bash
cd Sensor_ESP32
idf.py build
```

---

## Raspberry Pi

```bash
make pi
```

oder

```bash
cmake -S MQTT_Broker_Pi -B build/MQTT_Broker_Pi
cmake --build build/MQTT_Broker_Pi
```

---

## Python

Syntaxprüfung

```bash
make pc
```

oder

```bash
python -m compileall NiceGui_PC
```

---

# Lokale Tests

## Python Unit Tests

```bash
PYTHONPATH=NiceGui_PC python -m pytest NiceGui_PC/tests
```

Beispielausgabe

```text
=====================
4 passed
=====================
```

---

## Python Linter

```bash
ruff check NiceGui_PC
```

---

## C Static Analysis

```bash
cppcheck \
    --enable=warning,performance,portability \
    --std=c11 \
    MQTT_Broker_Pi
```

---

# GitHub Actions

Die Continuous Integration besteht aus mehreren Workflows.

## ESP32 Build

- ESP-IDF Build
- Firmware wird kompiliert

---

## Raspberry Pi Build

- Installation der benötigten Bibliotheken
- CMake Konfiguration
- Build der C-Anwendung

---

## Python Tests

- Installation der Python-Abhängigkeiten
- Syntaxprüfung
- Unit-Tests mittels pytest

---

## Static Analysis

### C

- cppcheck

### Python

- Ruff

---

# Makefile

Das Projekt besitzt ein gemeinsames Makefile.

## Alle Projekte bauen

```bash
make all
```

## ESP32

```bash
make esp32
```

## Raspberry Pi

```bash
make pi
```

## Python

```bash
make pc
```

## Bereinigen

```bash
make clean
```

---

# Raspberry Pi ausführen

Der Raspberry Pi kann direkt aus dem Makefile gestartet werden.

Beispiel:

```bash
make run PI_HOST=pi@192.168.178.50
```

Standardmäßig wird folgende Variable verwendet:

```make
PI_HOST ?= pi@raspberrypi.local
```

Der Build wird anschließend per SSH auf den Raspberry Pi kopiert und dort gestartet.

---

# Tests

Für das Python-Projekt existieren Unit-Tests.

Es werden unter anderem getestet:

- MQTT Publish
- MQTT Receive
- Verarbeitung eingehender JSON-Daten
- Mocking des MQTT-Clients

Die Tests verwenden **pytest** sowie **unittest.mock**, wodurch kein echter MQTT-Broker benötigt wird.

---

# Continuous Integration

Die CI prüft automatisch bei jedem Pull Request:

- erfolgreicher ESP32 Build
- erfolgreicher Raspberry Pi Build
- Python Unit Tests
- Python Syntaxprüfung
- statische Analyse mittels Ruff
- statische Analyse mittels cppcheck

Ein Merge in den Hauptbranch ist nur bei erfolgreicher CI möglich.

---

# Repository

GitHub Repository

> https://github.com/<USERNAME>/SQ_Projekt

---

# Pull Requests

## Erfolgreicher Pull Request

Link:

ESP32-Build: https://github.com/Frosti21/SQ_Projekt/actions/runs/28462517805
Pyton-Test: https://github.com/Frosti21/SQ_Projekt/actions/runs/28462517799
RasPi: https://github.com/Frosti21/SQ_Projekt/actions/runs/28462517802
Statische Analyse: https://github.com/Frosti21/SQ_Projekt/actions/runs/28462517949
---

## Fehlgeschlagener Pull Request

Dieser Pull Request enthält absichtlich einen Fehler

Link:
ESP32-Build: https://github.com/Frosti21/SQ_Projekt/actions/runs/28465418380
RasPi: https://github.com/Frosti21/SQ_Projekt/actions/runs/28465418407
Statische Analyse: https://github.com/Frosti21/SQ_Projekt/actions/workflows/static-analysis.yml

---

# Verwendete Technologien

- C
- Python
- ESP-IDF
- MQTT
- Mosquitto
- cJSON
- NiceGUI
- CMake
- Make
- GitHub Actions
- pytest
- Ruff
- cppcheck

---

# Autor

Markus Böckle

Sommersemester 2026

Softwarequalität
