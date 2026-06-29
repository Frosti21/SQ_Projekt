cmake_minimum_required(VERSION 3.16)

project(MQTT_Broker_Pi LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(mqtt_broker
    mqtt_data_collector.c
)

target_link_libraries(mqtt_broker
    mosquitto
    pthread
)