"""
dashboard_gas.py
Dashboard Smart Gas & Heat Sensor.
Mendengarkan data dari ESP32 (Wokwi) lewat broker publik HiveMQ,
menyimpan ke CSV, lalu menggambar grafik dengan Plotly.

Install sekali:  pip install paho-mqtt pandas plotly
Jalankan:        python dashboard_gas.py
Saat berjalan, ubah ppm gas dan suhu di Wokwi supaya grafik berisi kejadian.
"""
import json
import time

import paho.mqtt.client as mqtt
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots

HOST = "broker.hivemq.com"
PORT = 1883

# Suffix tim. Harus SAMA PERSIS dengan di sketch.ino.
TEAM_ID = "sic8-navya-ah2zi6"

# Topic dibentuk otomatis dari TEAM_ID.
# Hasil: sic8/sic8-navya-ah2zi6/gasmonitor/data
TOPIC_DATA = f"sic8/{TEAM_ID}/gasmonitor/data"
CLIENT_ID = f"py-{TEAM_ID}-dash"

DURASI_DETIK = 120  # lama merekam data

# Ambang batas, sama dengan di sketch.ino
GAS_WARNING_ADC = 1800
GAS_DANGER_ADC = 2600
TEMP_WARNING_C = 35.0
TEMP_DANGER_C = 45.0

STATUS_VALID = {"SAFE", "WARNING", "DANGER", "SENSOR ERROR"}
rows = []


def on_connect(client, userdata, flags, reason_code, properties):
    print("Terhubung ke broker:", reason_code)
    client.subscribe(TOPIC_DATA)
    print("Mendengarkan topic:", TOPIC_DATA)


def on_message(client, userdata, msg):
    # Di broker publik, siapa pun bisa mengirim pesan. Saring dulu.
    try:
        d = json.loads(msg.payload.decode())
    except (ValueError, UnicodeDecodeError):
        return  # bukan JSON, abaikan
    if not isinstance(d, dict):
        return
    if not isinstance(d.get("gas_adc"), (int, float)):
        return
    if d.get("status") not in STATUS_VALID:
        return
    d["waktu"] = pd.Timestamp.now()
    rows.append(d)
    print(d)


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT)
client.loop_start()

print(f"Merekam data selama {DURASI_DETIK} detik...")
time.sleep(DURASI_DETIK)

client.loop_stop()
client.disconnect()

df = pd.DataFrame(rows)
if df.empty:
    raise SystemExit(
        "Tidak ada data diterima. Pastikan simulasi Wokwi berjalan "
        "dan TEAM_ID sama dengan di sketch.ino."
    )

# Ubah kolom angka (nilai null dari ESP32 menjadi kosong)
for kolom in ["gas_adc", "temperature_c", "humidity_pct"]:
    if kolom in df.columns:
        df[kolom] = pd.to_numeric(df[kolom], errors="coerce")

df.to_csv("data_gasmonitor.csv", index=False)
print(f"{len(df)} data tersimpan di data_gasmonitor.csv")

fig = make_subplots(
    rows=2, cols=1, shared_xaxes=True,
    subplot_titles=("Kadar gas (ADC)", "Suhu (°C)"),
)
fig.add_trace(go.Scatter(x=df["waktu"], y=df["gas_adc"], name="Gas ADC"),
              row=1, col=1)
fig.add_trace(go.Scatter(x=df["waktu"], y=df["temperature_c"], name="Suhu"),
              row=2, col=1)

fig.add_hline(y=GAS_WARNING_ADC, line_dash="dash", row=1, col=1)
fig.add_hline(y=GAS_DANGER_ADC, line_dash="dash", row=1, col=1)
fig.add_hline(y=TEMP_WARNING_C, line_dash="dash", row=2, col=1)
fig.add_hline(y=TEMP_DANGER_C, line_dash="dash", row=2, col=1)

bukan_aman = df[df["status"] != "SAFE"]
fig.add_trace(
    go.Scatter(x=bukan_aman["waktu"], y=bukan_aman["gas_adc"],
               mode="markers", marker=dict(size=10, color="red"),
               name="Bukan SAFE"),
    row=1, col=1,
)

fig.update_layout(title="Dashboard Deteksi Gas dan Panas", height=620)
fig.write_html("dashboard_gas.html")
print("Grafik juga tersimpan di dashboard_gas.html")
fig.show()
