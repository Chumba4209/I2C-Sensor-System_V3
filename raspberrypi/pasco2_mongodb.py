import smbus2
import time
from pymongo import MongoClient
from datetime import datetime, timezone

SLAVE_ADDR  = 0x11
MONGO_URI   = "mongodb+srv://pimaster:Cycy12345.@sensorcluster.u4bkfgw.mongodb.net/?appName=SensorCluster"
DB_NAME     = "CO2_Meter"
COLLECTION  = "pasco2"

try:
    client     = MongoClient(MONGO_URI, serverSelectionTimeoutMS=10000)
    db         = client[DB_NAME]
    collection = db[COLLECTION]
    client.server_info()
    print(f"[MongoDB] Connected | DB: {DB_NAME} | Collection: {COLLECTION}")
except Exception as e:
    print(f"[MongoDB] Connection failed: {e}")
    exit(1)

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

def push_to_mongo(co2):
    try:
        doc = {
            "timestamp": datetime.now(timezone.utc),
            "sensor":    "XENSIV_PAS_CO2",
            "node":      "slave_0x11",
            "co2_ppm":   co2
        }
        result = collection.insert_one(doc)
        print(f"[MongoDB] Inserted ID: {result.inserted_id}")
    except Exception as e:
        print(f"[MongoDB] Error: {e}")

print("PAS CO2 → MongoDB Atlas pipeline running...\n")

while True:
    co2 = read_co2()
    if co2 is not None:
        print(f"[I2C] CO2: {co2} ppm")
        push_to_mongo(co2)
    time.sleep(15)