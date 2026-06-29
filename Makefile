.PHONY: all pi pc esp32 clean

all: pi pc esp32

pi:
	cmake -S MQTT_Broker_Pi -B build/MQTT_Broker_Pi
	cmake --build build/MQTT_Broker_Pi

pc:
	python3 -m py_compile NiceGui_PC/NiceGui.py

esp32:
	cd Sensor_ESP32 && idf.py build

clean:
	rm -rf build
	cd Sensor_ESP32 && idf.py fullclean