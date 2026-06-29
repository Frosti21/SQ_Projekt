.PHONY: all pi pc esp32 clean

all: pi pc esp32

pi:
	cmake -S MQTT_Broker_Pi -B build/MQTT_Broker_Pi
	cmake --build build/MQTT_Broker_Pi

pc:
	python -m compileall NiceGui_PC

esp32:
	cd Sensor_ESP32 && idf.py build

clean:
	rm -rf build
	cd Sensor_ESP32 && idf.py fullclean