cmake_minimum_required(VERSION 3.16)

project(MQTT_Broker_Pi LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
pkg_check_modules(CJSON REQUIRED libcjson)

add_executable(mqtt_broker
    mqtt_data_collector.c
)

target_include_directories(mqtt_broker PRIVATE
    ${MOSQUITTO_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

target_link_directories(mqtt_broker PRIVATE
    ${MOSQUITTO_LIBRARY_DIRS}
    ${CJSON_LIBRARY_DIRS}
)

target_link_libraries(mqtt_broker PRIVATE
    ${MOSQUITTO_LIBRARIES}
    ${CJSON_LIBRARIES}
)