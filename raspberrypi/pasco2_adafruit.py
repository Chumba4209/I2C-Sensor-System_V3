import smbus2
import requests
import time

SLAVE_ADDR   = 0x11
AIO_USERNAME = " "
AIO_KEY      = " "
AIO_FEED     = "pasco2-level"
AIO_URL      = f"https://io.adafruit.com/api/v2/{AIO_USERNAME}/feeds/{AIO_FEED}/data"

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

def push_to_adafruit(co2):
    try:
        res = requests.post(
            AIO_URL,
            headers={
                "X-AIO-Key":    AIO_KEY,
                "Content-Type": "application/json"
            },
            json={"value": co2},
            timeout=10
        )
        if res.status_code == 200:
            print(f"[AdafruitIO] Pushed CO2={co2} ppm")
        else:
            print(f"[AdafruitIO] Push failed: {res.status_code} — {res.text}")
    except Exception as e:
        print(f"[AdafruitIO] Error: {e}")

print("PAS CO2 → AdafruitIO pipeline running...\n")

while True:
    co2 = read_co2()
    if co2 is not None:
        print(f"[I2C] CO2: {co2} ppm")
        push_to_adafruit(co2)
    time.sleep(15)