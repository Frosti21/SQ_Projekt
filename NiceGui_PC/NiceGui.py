from nicegui import ui
from view.NiceGuiView import DashboardView
from services.mqtt_service import MqttReceiver


PI_IP = "172.20.10.5"
MQTT_PORT = 1883


def main():
    receiver = MqttReceiver(host=PI_IP, port=MQTT_PORT)
    view = DashboardView(receiver)

    receiver.start()
    view.build_ui()

    ui.run(title="MQTT Dashboard", dark=True, port=8080, reload=False, favicon="📡",)


if __name__ in {"__main__", "__mp_main__"}:
    main()