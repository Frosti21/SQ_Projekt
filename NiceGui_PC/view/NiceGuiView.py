import asyncio
from nicegui import ui

class DashboardView:
    def __init__(self, receiver):
        self.receiver = receiver

        self.status_label = None
        self.update_label = None
        self.dashboard_container = None
        self.last_rendered_update = None

    @staticmethod
    def latest(entries, key):
        for entry in reversed(entries):
            if key in entry:
                return entry[key]
        return None

    @staticmethod
    def timeseries(entries, key):
        result = []
        for entry in entries:
            timestamp = entry.get("timestamp", "")
            value = entry.get(key)
            if value is not None and timestamp:
                result.append((timestamp, value))
        return result

    def make_stat_card(self, label, value, unit, color):
        with ui.card().classes("p-4 rounded-2xl shadow-md flex flex-col items-center gap-1").style(
            f"background:{color}10; border:1.5px solid {color}40; min-width:140px"
        ):
            ui.label(label).classes("text-xs font-semibold uppercase tracking-widest").style("color:#888")
            with ui.row().classes("items-end gap-1"):
                ui.label(str(value) if value is not None else "–").classes("text-3xl font-bold").style(f"color:{color}")
                ui.label(unit).classes("text-sm pb-1").style("color:#aaa")

    def make_line_chart(self, title, series_dict, y_label):
        if not series_dict:
            return

        with ui.card().classes("p-4 rounded-2xl shadow-md w-full"):
            ui.label(title).classes("text-sm font-semibold uppercase tracking-wider mb-2").style("color:#666")

            all_timestamps = sorted(
                set(timestamp for points in series_dict.values() for timestamp, _ in points)
            )

            chart_data = {
                "chart": {"type": "line", "backgroundColor": "transparent", "height": 220},
                "title": {"text": ""},
                "xAxis": {
                    "categories": [timestamp[11:16] for timestamp in all_timestamps],
                    "labels": {"style": {"fontSize": "10px"}, "rotation": -30},
                },
                "yAxis": {"title": {"text": y_label}},
                "legend": {"enabled": len(series_dict) > 1},
                "credits": {"enabled": False},
                "series": [],
            }

            for name, points in series_dict.items():
                values = dict(points)
                chart_data["series"].append({
                    "name": name,
                    "data": [values.get(timestamp) for timestamp in all_timestamps],
                })

            ui.highchart(chart_data).classes("w-full")

    def render_shelly(self, topic, device):
        entries = device.get("data", [])
        short = topic.split("/")[0]

        with ui.card().classes("w-full p-4 rounded-2xl").style(
            "background:#1a1f2e; border:1px solid #f59e0b30"
        ):
            ui.label(short).classes("text-lg font-bold").style("color:#f59e0b")
            ui.label(topic).classes("text-xs").style("color:#475569")

            with ui.row().classes("flex-wrap gap-3 mb-3"):
                self.make_stat_card("Leistung", self.latest(entries, "apower"), "W", "#f59e0b")
                self.make_stat_card("Spannung", self.latest(entries, "voltage"), "V", "#3b82f6")
                self.make_stat_card("Strom", self.latest(entries, "current"), "A", "#8b5cf6")

            self.make_line_chart(
                f"{short} – Leistungsverlauf",
                {short: self.timeseries(entries, "apower")},
                "Watt",
            )

    def render_esp32(self, topic, device):
        entries = device.get("data", [])
        parts = topic.split("/")
        device_name = parts[1] if len(parts) > 1 else topic
        sensor = parts[2] if len(parts) > 2 else "Sensor"

        fields = {
            "temp": ("Temperatur", "°C", "#ef4444"),
            "humidity": ("Luftfeuchte", "%", "#3b82f6"),
            "value": (sensor, "", "#6b7280"),
        }

        with ui.card().classes("w-full p-4 rounded-2xl").style(
            "background:#1a1f2e; border:1px solid #10b98130"
        ):
            ui.label(f"{device_name} / {sensor}").classes("text-lg font-bold").style("color:#10b981")
            ui.label(topic).classes("text-xs").style("color:#475569")

            with ui.row().classes("flex-wrap gap-3 mb-3"):
                for field, (label, unit, color) in fields.items():
                    if any(field in entry for entry in entries):
                        self.make_stat_card(label, self.latest(entries, field), unit, color)

            for field, (label, unit, color) in fields.items():
                points = self.timeseries(entries, field)
                if points:
                    self.make_line_chart(f"{device_name} – {label}", {label: points}, unit or "Wert")

    def build_dashboard(self):
        self.dashboard_container.clear()

        subscribers = self.receiver.subscribers
        device_data = self.receiver.device_data

        if not subscribers:
            with self.dashboard_container:
                ui.label("Erst «Geräte laden» klicken").style("color:#64748b")
            return

        with self.dashboard_container:
            for subscriber in subscribers:
                topic = subscriber["topic"]
                device = device_data.get(topic)

                with ui.expansion(topic).classes("w-full rounded-xl border mb-2"):
                    if not device:
                        ui.label("Noch keine Daten – «Alle Daten abrufen» klicken")
                        continue

                    if subscriber.get("device_type") == "shelly_plug_s":
                        self.render_shelly(topic, device)
                    elif subscriber.get("device_type") == "esp32":
                        self.render_esp32(topic, device)

    async def load_subscribers(self):
        if self.receiver.request_subscribers():
            ui.notify("Geräteliste wird abgerufen…", type="info")
            await asyncio.sleep(5)
            self.build_dashboard()

    async def fetch_all_data(self):
        if self.receiver.request_all_data():
            ui.notify("Daten werden abgerufen…", type="info")
            await asyncio.sleep(8)
            self.build_dashboard()

    def clear_dashboard(self):
        self.receiver.clear_data()
        self.dashboard_container.clear()
        self.update_label.set_text("")

    def build_ui(self):
        ui.add_head_html("""
        <script src="https://code.highcharts.com/highcharts.js"></script>
        <style>
        body { background: #0f1117; }
        .nicegui-content { background: #0f1117 !important; }
        </style>
        """)

        with ui.column().classes("w-full max-w-5xl mx-auto px-4 py-6 gap-4"):
            ui.label("MQTT Dashboard").style("font-size:2rem; font-weight:800; color:#f1f5f9")
            ui.label("Shelly Plug S + ESP32 Datenvisualisierung").style("color:#64748b")

            with ui.card().classes("w-full p-3 rounded-xl").style(
                "background:#1e2330; border:1px solid #2d3748"
            ):
                self.status_label = ui.label(self.receiver.status).style("color:#94a3b8")
                self.update_label = ui.label("").style("color:#475569")

            with ui.row().classes("gap-3 flex-wrap"):
                ui.button("🔍 Geräte laden", color="blue").on("click", self.load_subscribers)
                ui.button("📡 Alle Daten abrufen", color="amber").on("click", self.fetch_all_data)
                ui.button("🗑 Ansicht leeren").on("click", self.clear_dashboard)

            self.dashboard_container = ui.column().classes("w-full gap-4")
            ui.label(f"Pi: {self.receiver.host}:{self.receiver.port}").style("color:#334155")
        ui.timer(0.5, self.refresh_ui)

    def refresh_ui(self):
        if self.status_label:
            self.status_label.set_text(self.receiver.status)

        if self.update_label and self.receiver.last_update:
            self.update_label.set_text(f"Letztes Update: {self.receiver.last_update}")

        if self.receiver.last_update != self.last_rendered_update:
            self.last_rendered_update = self.receiver.last_update
            if self.dashboard_container:
                self.build_dashboard()