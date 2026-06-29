"""
mqtt_dashboard.py – NiceGUI Dashboard für MQTT Collector
Visualisiert Shelly Plug S und ESP32 Sensordaten.

Installation:
    pip install nicegui paho-mqtt

Starten:
    python mqtt_dashboard.py
"""


import json
import asyncio
import threading
from datetime import datetime

import paho.mqtt.client as mqtt
from nicegui import ui, app

# ─── Konfiguration ────────────────────────────────────────────────
PI_IP      = "172.20.10.5"
MQTT_PORT  = 1883

TOPIC_REQUEST              = "collector/request"
TOPIC_RESPONSE             = "collector/response"
TOPIC_SUBSCRIBERS_REQUEST  = "collector/subscribers/request"
TOPIC_SUBSCRIBERS_RESPONSE = "collector/subscribers/response"

# ─── Globaler Zustand ─────────────────────────────────────────────
state = {
    "subscribers":   None,
    "device_data":   {},
    "status":        "Bereit",
    "last_update":   None,
    "waiting_subs":  False,
    "waiting_data":  False,
}

# ─── MQTT Client ──────────────────────────────────────────────────
try:
    mqtt_client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="nicegui_dashboard"
    )
except AttributeError:
    mqtt_client = mqtt.Client(client_id="nicegui_dashboard")

mqtt_connected = False

# ─── Thread-sicherer UI-Aufruf ────────────────────────────────────
def run_in_ui(fn):
    """Callable sicher aus einem Nicht-asyncio-Thread im UI-Loop ausführen."""
    async def _wrapper():
        fn()
    try:
        loop = app.loop
        if loop and loop.is_running():
            asyncio.run_coroutine_threadsafe(_wrapper(), loop)
    except Exception:
        pass

# ─── MQTT Callbacks ───────────────────────────────────────────────
def mqtt_on_connect(client, userdata, flags, rc, properties=None):
    global mqtt_connected
    if rc == 0:
        mqtt_connected = True
        client.subscribe(TOPIC_RESPONSE)
        client.subscribe(TOPIC_SUBSCRIBERS_RESPONSE)
    else:
        state["status"] = f"MQTT Fehler (rc={rc})"

def mqtt_on_disconnect(client, userdata, flags=None, rc=0, properties=None):
    global mqtt_connected
    mqtt_connected = False

def mqtt_on_message(client, userdata, msg):
    # ── Nur Daten schreiben, kein UI hier ──
    if msg.topic == TOPIC_SUBSCRIBERS_RESPONSE:
        try:
            data = json.loads(msg.payload.decode())
            state["subscribers"]  = data.get("subscribers", [])
            state["waiting_subs"] = False
            state["status"] = f"✓ {len(state['subscribers'])} Gerät(e) gefunden"
            run_in_ui(lambda: ui.notify(f"{len(state['subscribers'])} Geräte geladen", type="positive"))
        except Exception as e:
            state["status"] = f"Subscribers Parse-Fehler: {e}"
            state["waiting_subs"] = False
        return

    if msg.topic == TOPIC_RESPONSE:
        try:
            data = json.loads(msg.payload.decode())
            for device in data.get("devices", []):
                topic = device.get("topic", "")
                if topic:
                    state["device_data"][topic] = device
            state["last_update"] = datetime.now().strftime("%d.%m.%Y %H:%M:%S")
            state["waiting_data"] = False
            state["status"] = f"✓ Daten empfangen – {state['last_update']}"
            run_in_ui(lambda: ui.notify("Daten empfangen!", type="positive"))
        except Exception as e:
            state["status"] = f"Daten Parse-Fehler: {e}"
            state["waiting_data"] = False

mqtt_client.on_connect    = mqtt_on_connect
mqtt_client.on_disconnect = mqtt_on_disconnect
mqtt_client.on_message    = mqtt_on_message

def connect_mqtt():
    try:
        mqtt_client.connect(PI_IP, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        state["status"] = f"Verbindung fehlgeschlagen: {e}"

threading.Thread(target=connect_mqtt, daemon=True).start()

# ─── Hilfsfunktionen ──────────────────────────────────────────────
def latest(entries, key):
    for e in reversed(entries):
        if key in e:
            return e[key]
    return None

def timeseries(entries, key):
    result = []
    for e in entries:
        ts  = e.get("timestamp", "")
        val = e.get(key)
        if val is not None and ts:
            result.append((ts, val))
    return result

# ─── UI Bausteine ─────────────────────────────────────────────────
def make_stat_card(label, value, unit, color):
    with ui.card().classes("p-4 rounded-2xl shadow-md flex flex-col items-center gap-1").style(
        f"background:{color}10; border:1.5px solid {color}40; min-width:140px"
    ):
        ui.label(label).classes("text-xs font-semibold uppercase tracking-widest").style("color:#888")
        with ui.row().classes("items-end gap-1"):
            ui.label(str(value) if value is not None else "–").classes("text-3xl font-bold").style(f"color:{color}")
            ui.label(unit).classes("text-sm pb-1").style("color:#aaa")

def make_line_chart(title, series_dict, y_label):
    if not series_dict:
        return
    with ui.card().classes("p-4 rounded-2xl shadow-md w-full"):
        ui.label(title).classes("text-sm font-semibold uppercase tracking-wider mb-2").style("color:#666")
        all_ts = sorted(set(ts for pts in series_dict.values() for ts, _ in pts))
        chart_data = {
            "chart": {"type": "line", "backgroundColor": "transparent", "height": 220},
            "title": {"text": ""},
            "xAxis": {"categories": [t[11:16] for t in all_ts],
                      "labels": {"style": {"fontSize": "10px"}, "rotation": -30}},
            "yAxis": {"title": {"text": y_label}, "labels": {"style": {"fontSize": "10px"}}},
            "legend": {"enabled": len(series_dict) > 1},
            "credits": {"enabled": False},
            "series": [],
            "plotOptions": {"line": {"marker": {"radius": 2}}},
        }
        for name, pts in series_dict.items():
            ts_to_val = dict(pts)
            chart_data["series"].append({"name": name, "data": [ts_to_val.get(ts) for ts in all_ts]})
        ui.highchart(chart_data).classes("w-full")

# ─── Geräte-Detail Ansicht ────────────────────────────────────────
def render_shelly(topic, device):
    entries = device.get("data", [])
    short   = topic.split("/")[0]
    with ui.card().classes("w-full p-4 rounded-2xl").style("background:#1a1f2e; border:1px solid #f59e0b30"):
        with ui.row().classes("items-center gap-2 mb-3"):
            ui.icon("power", color="amber").classes("text-xl")
            ui.label(short).classes("text-lg font-bold").style("color:#f59e0b")
            ui.label(topic).classes("text-xs").style("color:#475569")

        with ui.row().classes("flex-wrap gap-3 mb-3"):
            make_stat_card("Leistung",  latest(entries, "apower"),  "W",  "#f59e0b")
            make_stat_card("Spannung",  latest(entries, "voltage"), "V",  "#3b82f6")
            make_stat_card("Strom",     latest(entries, "current"), "A",  "#8b5cf6")
            temp = None
            last = entries[-1] if entries else {}
            tobj = last.get("temperature")
            if isinstance(tobj, dict):
                temp = tobj.get("tC")
            make_stat_card("Temperatur", temp, "°C", "#ef4444")

        make_line_chart(f"{short} – Leistungsverlauf",
                        {short: timeseries(entries, "apower")}, "Watt")
        make_line_chart(f"{short} – Spannung & Strom", {
            "Spannung (V)":  timeseries(entries, "voltage"),
            "Strom (A×100)": [(ts, v*100) for ts, v in timeseries(entries, "current")],
        }, "Wert")

def render_esp32(topic, device):
    entries = device.get("data", [])
    parts   = topic.split("/")
    device_name = parts[1] if len(parts) > 1 else topic
    sensor      = parts[2] if len(parts) > 2 else "Sensor"

    # Bekannte Felder mit Label, Einheit, Farbe
    KNOWN_FIELDS = {
        "temp":     ("Temperatur", "°C",  "#ef4444"),
        "humidity": ("Luftfeuchte", "%",  "#3b82f6"),
        "value":    (sensor,        "",   "#6b7280"),
    }

    # Welche Felder sind tatsächlich in den Daten vorhanden?
    present_fields = []
    for field, (label, unit, color) in KNOWN_FIELDS.items():
        if any(field in e for e in entries):
            present_fields.append((field, label, unit, color))

    # Fallback falls gar nichts bekannt
    if not present_fields:
        present_fields = [("value", sensor, "", "#6b7280")]

    with ui.card().classes("w-full p-4 rounded-2xl").style(
        "background:#1a1f2e; border:1px solid #10b98130"
    ):
        with ui.row().classes("items-center gap-2 mb-3"):
            ui.icon("memory", color="teal").classes("text-xl")
            ui.label(f"{device_name} / {sensor}").classes("text-lg font-bold").style("color:#10b981")
            ui.label(topic).classes("text-xs").style("color:#475569")
            ui.label(f"{len(entries)} Einträge").classes("text-xs ml-2").style("color:#64748b")

        # Stat-Cards für alle vorhandenen Felder
        with ui.row().classes("flex-wrap gap-3 mb-3"):
            for field, label, unit, color in present_fields:
                make_stat_card(label, latest(entries, field), unit, color)

        # Zeitreihen-Charts
        for field, label, unit, color in present_fields:
            pts = timeseries(entries, field)
            if pts:
                make_line_chart(
                    f"{device_name} – {label} Verlauf",
                    {label: pts},
                    unit or "Wert"
                )

# ─── Dashboard aufbauen ───────────────────────────────────────────
def build_dashboard(container):
    container.clear()

    if not state["subscribers"]:
        with container:
            with ui.column().classes("items-center justify-center w-full py-20 gap-4"):
                ui.icon("sensors_off", size="4rem").style("color:#334155")
                ui.label("Erst «Geräte laden» klicken").style("color:#64748b")
        return

    with container:
        shellies = [s for s in state["subscribers"] if s["device_type"] == "shelly_plug_s"]
        esp32s   = [s for s in state["subscribers"] if s["device_type"] == "esp32"]

        if shellies:
            ui.label("⚡ Shelly Plug S").classes("text-lg font-bold mt-2").style("color:#f59e0b")
            ui.separator()
            for sub in shellies:
                topic  = sub["topic"]
                device = state["device_data"].get(topic)
                short  = topic.split("/")[0]
                with ui.expansion(
                    f"{short}  ·  Zuletzt: {sub.get('last_seen','?')[11:16]}",
                    icon="power"
                ).classes("w-full rounded-xl border mb-2").style("border-color:#f59e0b40"):
                    if device:
                        render_shelly(topic, device)
                    else:
                        with ui.row().classes("items-center gap-2 p-3"):
                            ui.icon("info", color="grey")
                            ui.label("Noch keine Daten – «Alle Daten abrufen» klicken").style("color:#64748b")

        if esp32s:
            ui.label("🌡 ESP32 Sensoren").classes("text-lg font-bold mt-4").style("color:#10b981")
            ui.separator()
            for sub in esp32s:
                topic  = sub["topic"]
                device = state["device_data"].get(topic)
                parts  = topic.split("/")
                name   = f"{parts[1]}/{parts[2]}" if len(parts) > 2 else topic
                with ui.expansion(
                    f"{name}  ·  Zuletzt: {sub.get('last_seen','?')[11:16]}",
                    icon="memory"
                ).classes("w-full rounded-xl border mb-2").style("border-color:#10b98140"):
                    if device:
                        render_esp32(topic, device)
                    else:
                        with ui.row().classes("items-center gap-2 p-3"):
                            ui.icon("info", color="grey")
                            ui.label("Noch keine Daten – «Alle Daten abrufen» klicken").style("color:#64748b")

# ─── UI Layout ────────────────────────────────────────────────────
ui.add_head_html("""
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=DM+Mono:wght@400;500&family=Syne:wght@600;800&display=swap" rel="stylesheet">
<script src="https://code.highcharts.com/highcharts.js"></script>
<style>
  body { font-family: 'DM Mono', monospace; background: #0f1117; }
  .nicegui-content { background: #0f1117 !important; }
</style>
""")

with ui.column().classes("w-full max-w-5xl mx-auto px-4 py-6 gap-4"):

    # Header
    with ui.row().classes("w-full items-center justify-between"):
        with ui.column().classes("gap-0"):
            ui.label("MQTT Dashboard").style(
                "font-family:'Syne',sans-serif; font-size:2rem; font-weight:800; color:#f1f5f9"
            )
            ui.label("Shelly Plug S + ESP32 Datenvisualisierung").style("color:#64748b; font-size:0.8rem")
        ui.icon("hub", size="3rem").style("color:#334155")

    # Statusleiste
    with ui.card().classes("w-full p-3 rounded-xl").style("background:#1e2330; border:1px solid #2d3748"):
        with ui.row().classes("items-center justify-between w-full"):
            with ui.row().classes("items-center gap-3"):
                ui.icon("circle", size="sm").style("color:#10b981")
                status_label = ui.label(state["status"]).style("color:#94a3b8; font-size:0.85rem")
            update_label = ui.label("").style("color:#475569; font-size:0.75rem")

    # Buttons
    with ui.row().classes("gap-3 flex-wrap"):
        subs_btn = ui.button("🔍 Geräte laden", color="blue").classes("font-bold")
        data_btn = ui.button("📡 Alle Daten abrufen", color="amber").classes("font-bold")
        ui.button("🗑 Ansicht leeren", color=None).classes("font-bold").style(
            "background:#1e2330; color:#94a3b8; border:1px solid #2d3748"
        ).on("click", lambda: (dashboard_container.clear(), state.update({"subscribers": None, "device_data": {}})))

    # Dashboard Container
    dashboard_container = ui.column().classes("w-full gap-4")

    ui.label(f"Pi: {PI_IP}:{MQTT_PORT}").style("color:#334155; font-size:0.7rem; margin-top:2rem")

# ─── Button-Logik ─────────────────────────────────────────────────
async def load_subscribers():
    if not mqtt_connected:
        ui.notify("Nicht mit MQTT verbunden!", type="negative")
        return
    if state["waiting_subs"]:
        ui.notify("Warte bereits…", type="warning")
        return
    state["waiting_subs"] = True
    state["status"] = "⏳ Lade Geräteliste…"
    status_label.set_text(state["status"])
    mqtt_client.publish(TOPIC_SUBSCRIBERS_REQUEST, "list")
    ui.notify("Geräteliste wird abgerufen…", type="info")

    # Warten im asyncio-Event-Loop – kein Thread nötig
    await asyncio.sleep(5)
    build_dashboard(dashboard_container)
    status_label.set_text(state["status"])

async def fetch_all_data():
    if not mqtt_connected:
        ui.notify("Ups, da hat was nicht geklappt!", type="negative")
        return
    if not state["subscribers"]:
        ui.notify("Erst «Geräte laden» klicken!", type="warning")
        return
    if state["waiting_data"]:
        ui.notify("Warte bereits…", type="warning")
        return
    state["waiting_data"] = True
    state["status"] = "⏳ Lade alle Daten…"
    status_label.set_text(state["status"])
    mqtt_client.publish(TOPIC_REQUEST, "all")
    ui.notify("Daten werden abgerufen…", type="info")

    await asyncio.sleep(8)
    build_dashboard(dashboard_container)
    status_label.set_text(state["status"])
    if state["last_update"]:
        update_label.set_text(f"Letztes Update: {state['last_update']}")

subs_btn.on("click", load_subscribers)
data_btn.on("click", fetch_all_data)

def main():
    ui.run(title="MQTT Dashboard", dark=True, port=8080, reload=False, favicon="📡")

# ─── Start ────────────────────────────────────────────────────────
if __name__ in {"__main__", "__mp_main__"}:
    main()
