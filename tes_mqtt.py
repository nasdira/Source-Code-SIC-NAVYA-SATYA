"""
tes_mqtt.py
Tes sederhana: dengarkan pesan dari ESP32 (Wokwi) lewat
broker publik HiveMQ dan tampilkan di terminal.

Install sekali:  pip install paho-mqtt
Jalankan:        python tes_mqtt.py
Berhenti:        Ctrl + C
"""
import paho.mqtt.client as mqtt

HOST = "broker.hivemq.com"
PORT = 1883

# Suffix tim. Harus SAMA PERSIS dengan di sketch.ino.
TEAM_ID = "sic8-navya-ah2zi6"

# Topic dibentuk otomatis dari TEAM_ID, tidak perlu diketik manual.
# Hasil: sic8/sic8-navya-ah2zi6/gasmonitor/data
TOPIC_DATA = f"sic8/{TEAM_ID}/gasmonitor/data"
CLIENT_ID = f"py-{TEAM_ID}-tes"


def on_connect(client, userdata, flags, reason_code, properties):
    print("Terhubung ke broker:", reason_code)
    client.subscribe(TOPIC_DATA)
    print("Mendengarkan topic:", TOPIC_DATA)


def on_message(client, userdata, msg):
    print("Pesan masuk:", msg.payload.decode(errors="replace"))


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT)
client.loop_forever()
