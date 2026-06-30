
//  Slave 2 Firmware — Infineon XENSIV PAS CO2 Sensor
//  Board        : Lolin ESP32-C3 Mini
//  I2C Slave    : address 0x11  |  SDA=GPIO6  SCL=GPIO7  (Pi-facing, hardware)
//  I2C Master   : PAS CO2 0x28  |  SDA=GPIO8  SCL=GPIO10 (sensor-facing, SW)
//
//  Protocol:
//    Pi sends command byte 0x01  → slave responds with 2 bytes (CO2 ppm)
//    CO2 value encoded big-endian uint16 (high byte first)
//
//  PAS CO2 I2C registers used:
//    0x00 PROD_ID   — verify sensor is present
//    0x01 SENS_STS  — sensor status (bit0 = data ready)
//    0x05 CO2PPM_H  — CO2 high byte
//    0x06 CO2PPM_L  — CO2 low byte
//    0x04 MEAS_CFG  — measurement configuration


#include <Arduino.h>
#include "driver/i2c.h"
#include <SoftWire.h>
#include <AsyncDelay.h>

//  Pi-facing I2C (hardware slave) 
#define SLAVE_ADDR      0x11          // Slave 2 address (Slave 1 is 0x10)
#define SDA_SLAVE       6
#define SCL_SLAVE       7
#define I2C_SLAVE_PORT  I2C_NUM_0
#define BUF_SIZE        128

//  PAS CO2-facing I2C (software master) 
#define SDA_MASTER      8
#define SCL_MASTER      10
#define PASCO2_ADDR     0x28          // PAS CO2 fixed I2C address

//  PAS CO2 register map 
#define REG_PROD_ID     0x00
#define REG_SENS_STS    0x01
#define REG_MEAS_CFG    0x04
#define REG_CO2PPM_H    0x05
#define REG_CO2PPM_L    0x06
#define REG_MEAS_STS    0x07
#define REG_INT_CFG     0x08
#define REG_ALARM_EN    0x09

// Software I2C instance
SoftWire sw(SDA_MASTER, SCL_MASTER);
char swTxBuf[16];
char swRxBuf[16];

// Latest CO2 reading (ppm) — updated every ~10 seconds
uint16_t co2_ppm = 0;
bool sensor_ready = false;

//  PAS CO2 helper: write one byte to a register 
bool pasco2_write_reg(uint8_t reg, uint8_t value) {
    sw.beginTransmission(PASCO2_ADDR);
    sw.write(reg);
    sw.write(value);
    uint8_t err = sw.endTransmission();
    return (err == 0);
}

//  PAS CO2 helper: read one byte from a register 
uint8_t pasco2_read_reg(uint8_t reg) {
    sw.beginTransmission(PASCO2_ADDR);
    sw.write(reg);
    sw.endTransmission();
    sw.requestFrom((uint8_t)PASCO2_ADDR, (uint8_t)1);
    return sw.read();
}

//  PAS CO2 helper: read two bytes from consecutive registers 
uint16_t pasco2_read16(uint8_t reg) {
    sw.beginTransmission(PASCO2_ADDR);
    sw.write(reg);
    sw.endTransmission();
    sw.requestFrom((uint8_t)PASCO2_ADDR, (uint8_t)2);
    uint8_t high = sw.read();
    uint8_t low  = sw.read();
    return ((uint16_t)high << 8) | low;
}

//  Initialise PAS CO2 
bool pasco2_init() {
    // Check product ID — should be 0x02 for XENSIV PAS CO2
    uint8_t prod_id = pasco2_read_reg(REG_PROD_ID);
    Serial.printf("[PAS CO2] Product ID: 0x%02X (expected 0x64)\n", prod_id);
    if (prod_id != 0x64) {
        Serial.println("[PAS CO2] ERROR: sensor not detected or wrong ID");
        return false;
    }

    // Check sensor status — bit4 = SEN_RDY (sensor ready)
    uint8_t status = pasco2_read_reg(REG_SENS_STS);
    Serial.printf("[PAS CO2] Sensor status: 0x%02X\n", status);
    if (!(status & 0x10)) {
        Serial.println("[PAS CO2] WARNING: SEN_RDY bit not set — check 12V supply");
    }

    // Configure: continuous measurement mode, 10-second interval
    // MEAS_CFG register: OP_MODE=10 (continuous), BOC_CFG=00, PWM_MODE=0
    // Bits [2:1] = OP_MODE → set to 0b10 = continuous mode
    pasco2_write_reg(REG_MEAS_CFG, 0x04);  // continuous mode, 10s interval
    Serial.println("[PAS CO2] Continuous measurement mode configured");

    return true;
}

//  Read CO2 from PAS CO2 
bool pasco2_read_co2(uint16_t &result) {
    // Check measurement status — bit1 = DRDY (data ready)
    uint8_t meas_status = pasco2_read_reg(REG_MEAS_STS);
    if (!(meas_status & 0x02)) {
        return false;   // no new data yet
    }
    // Read 2-byte CO2 value
    result = pasco2_read16(REG_CO2PPM_H);
    return true;
}

// 
void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("[SLAVE2] Booting...");

    //  Hardware I2C slave (Pi-facing) 
    i2c_config_t slave_conf = {};
    slave_conf.mode                = I2C_MODE_SLAVE;
    slave_conf.sda_io_num          = (gpio_num_t)SDA_SLAVE;
    slave_conf.scl_io_num          = (gpio_num_t)SCL_SLAVE;
    slave_conf.sda_pullup_en       = GPIO_PULLUP_ENABLE;
    slave_conf.scl_pullup_en       = GPIO_PULLUP_ENABLE;
    slave_conf.slave.addr_10bit_en = 0;
    slave_conf.slave.slave_addr    = SLAVE_ADDR;

    esp_err_t err;
    err = i2c_param_config(I2C_SLAVE_PORT, &slave_conf);
    Serial.printf("[SLAVE2] i2c_param_config: %s\n", esp_err_to_name(err));

    err = i2c_driver_install(I2C_SLAVE_PORT, I2C_MODE_SLAVE, BUF_SIZE, BUF_SIZE, 0);
    Serial.printf("[SLAVE2] i2c_driver_install: %s\n", esp_err_to_name(err));

    Serial.printf("[SLAVE2] Listening at 0x%02X | SDA=GPIO%d SCL=GPIO%d\n",
                  SLAVE_ADDR, SDA_SLAVE, SCL_SLAVE);

    //  Software I2C master (PAS CO2-facing) 
    sw.setTxBuffer(swTxBuf, sizeof(swTxBuf));
    sw.setRxBuffer(swRxBuf, sizeof(swRxBuf));
    sw.setDelay_us(10);
    sw.begin();
    Serial.println("[SLAVE2] Software I2C master ready | SDA=GPIO8 SCL=GPIO10");

    //  Initialise PAS CO2 
    delay(1000);   // give sensor time to power up
    sensor_ready = pasco2_init();
    if (sensor_ready) {
        Serial.println("[PAS CO2] Initialised successfully");
    } else {
        Serial.println("[PAS CO2] Init failed — check wiring and 5V supply");
    }

    // First measurement takes ~10 seconds in continuous mode
    Serial.println("[PAS CO2] First measurement in ~10 seconds...");
}

// 
void loop() {
    //  Poll PAS CO2 for new reading 
    if (sensor_ready) {
        uint16_t reading;
        if (pasco2_read_co2(reading)) {
            co2_ppm = reading;
            Serial.printf("[PAS CO2] CO2: %d ppm\n", co2_ppm);
        }
    }

    //  Serve Pi requests 
    uint8_t cmd;
    int len = i2c_slave_read_buffer(I2C_SLAVE_PORT, &cmd, 1, pdMS_TO_TICKS(10));
    if (len > 0) {
        Serial.printf("[SLAVE2] Received command: 0x%02X\n", cmd);
        if (cmd == 0x01) {
            // Respond with CO2 ppm as big-endian uint16
            uint8_t response[2] = {
                (uint8_t)(co2_ppm >> 8),
                (uint8_t)(co2_ppm & 0xFF)
            };
            i2c_slave_write_buffer(I2C_SLAVE_PORT, response, 2, pdMS_TO_TICKS(100));
            Serial.printf("[SLAVE2] Sent CO2=%d ppm to Pi\n", co2_ppm);
        }
    }

    delay(1000);   // check for new data every 1 second
}