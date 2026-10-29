"""
SEGER - Sistem Evaluasi Gas dan Environmental Room
Edge 2 (HP Android via Termux)

Alur:
  ESP32/Wokwi --(MQTT: seger/<ID>/data)--> script ini
  script ini --(MQTT: seger/<ID>/status)--> ESP32 + aplikasi MQTT di HP

Data processing 2: klasifikasi kondisi ruangan
  NYAMAN / MULAI PENGAP / PENGAP

Install di Termux:
  pkg update && pkg install python
  pip install paho-mqtt
Jalankan:
  python seger_edge2.py
"""

import csv
import json
import os
import time
from datetime import datetime

import paho.mqtt.client as mqtt

# ================== KONFIGURASI ==================
BROKER = "broker.hivemq.com"
PORT = 1883
TOPIC_ID = "kelompokmu"  # HARUS sama dengan TOPIC_ID di sketch.ino

TOPIC_DATA = f"seger/{TOPIC_ID}/data"
TOPIC_STATUS = f"seger/{TOPIC_ID}/status"

LOG_FILE = "seger_log.csv"  # dipakai juga nanti sebagai dataset untuk model ML
# =================================================


def classify(temp, hum, co2):
    """
    Klasifikasi rule-based berbasis skor.

    CO2   : < 800 ppm -> 0 | 800-1200 -> 1 | > 1200 -> 2
    Suhu  : > 30 C    -> +1
    Lembap: > 70 %    -> +1

    Skor 0   -> NYAMAN
    Skor 1   -> MULAI PENGAP
    Skor >=2 -> PENGAP
    """
    score = 0
    if co2 >= 1200:
        score += 2
    elif co2 >= 800:
        score += 1
    if temp > 30:
        score += 1
    if hum > 70:
        score += 1

    if score == 0:
        return "NYAMAN", score, "Udara baik."
    if score == 1:
        return "MULAI PENGAP", score, "Pertimbangkan membuka ventilasi."
    return "PENGAP", score, "Buka jendela/pintu atau nyalakan ventilasi sekarang!"


def log_to_csv(row):
    new_file = not os.path.exists(LOG_FILE)
    with open(LOG_FILE, "a", newline="") as f:
        writer = csv.writer(f)
        if new_file:
            writer.writerow(["waktu", "temp", "hum", "co2", "skor", "status"])
        writer.writerow(row)


def on_connect(client, userdata, *args):
    print(f"Terhubung ke {BROKER}")
    client.subscribe(TOPIC_DATA)
    print(f"Subscribe: {TOPIC_DATA}")
    print(f"Publish  : {TOPIC_STATUS}")
    print("Menunggu data dari ESP32...\n")


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        temp = float(data["temp"])
        hum = float(data["hum"])
        co2 = float(data["co2"])
    except (ValueError, KeyError, json.JSONDecodeError) as e:
        print(f"Data tidak valid, dilewati: {e}")
        return

    status, score, advice = classify(temp, hum, co2)

    result = {
        "status": status,
        "skor": score,
        "saran": advice,
        "temp": temp,
        "hum": hum,
        "co2": co2,
    }
    client.publish(TOPIC_STATUS, json.dumps(result))

    waktu = datetime.now().strftime("%H:%M:%S")
    print(f"[{waktu}] T={temp}C  RH={hum}%  CO2={co2:.0f}ppm  ->  {status}")
    log_to_csv([datetime.now().isoformat(), temp, hum, co2, score, status])


def make_client():
    # Kompatibel dengan paho-mqtt v1 dan v2
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:
        return mqtt.Client()


def main():
    client = make_client()
    client.on_connect = on_connect
    client.on_message = on_message

    while True:
        try:
            client.connect(BROKER, PORT, keepalive=60)
            client.loop_forever()
        except KeyboardInterrupt:
            print("\nBerhenti.")
            break
        except Exception as e:
            print(f"Koneksi bermasalah ({e}), coba lagi 5 detik...")
            time.sleep(5)


if __name__ == "__main__":
    main()
