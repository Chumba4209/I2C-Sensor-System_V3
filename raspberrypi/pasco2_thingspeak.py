import smbus2
import requests
import time

SLAVE_ADDR   = 0x11
TS_WRITE_KEY = "YOUR_PASCO2_WRITE_KEY"
TS_URL       = "https://api.thingspeak.com/update"

def read_co2():
    try:
        bus   = smbus2.SMBus(1)
        write = smbus2.i2c_msg.write(SLAVE_ADDR, [0x01])
        read  = smbus2.i2c_msg.read(SLAVE_ADDR, 2)
        bus.i2c_rdwr(write, read)
        data  = list(read)
        bus.close()
        return (data[0] << 8) | data[1]
    except Exception as e:
        print(f"[I2C] Error: {e}")
        return None

def push_to_thingspeak(co2):
    try:
        res = requests.get(TS_URL, params={
            "api_key": TS_WRITE_KEY,
            "field1":  co2
        }, timeout=10)
        if res.status_code == 200 and res.text != "0":
            print(f"[ThingSpeak] Pushed CO2={co2} ppm (entry {res.text})")
        else:
            print(f"[ThingSpeak] Push failed: {res.text}")
    except Exception as e:
        print(f"[ThingSpeak] Error: {e}")

print("PAS CO2 → ThingSpeak pipeline running...\n")

while True:
    co2 = read_co2()
    if co2 is not None:
        print(f"[I2C] CO2: {co2} ppm")
        push_to_thingspeak(co2)
    time.sleep(15)