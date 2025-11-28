/**
 * Bench Press Stillness Detector
 * Device: ESP32 Dev Module + MPU-6050
 * Protocol: Bluetooth Low Energy (BLE)
 */

#include "Wire.h"
#include "MPU6050.h"
#include "math.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLE UUID Constants
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Sensor & Logic Parameters
const float STABLE_THRESHOLD_G = 0.15; // Threshold for stability detection (G)
const float MOTION_THRESHOLD_G = 0.20; // Threshold for motion detection (G)
const int DEFAULT_PRESS_TIME_MS = 600; // Default duration for press command

// Global Objects
MPU6050 mpu;
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// Application State
enum State {
    STATE_TOP_HOLD,
    STATE_DESCENDING,
    STATE_COOLDOWN
};

State currentState = STATE_TOP_HOLD;
unsigned long stableStartTime = 0;
int timeToPressMs = DEFAULT_PRESS_TIME_MS;

// BLE Server Callbacks
class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        // Restart advertising immediately for reconnection
        delay(500); 
        pServer->startAdvertising();
    }
};

// BLE Characteristic Callbacks (Handle Write requests)
class CharCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue();
        if (value.length() > 0) {
            int newTime = value.toInt();
            // Validate range (100ms - 5000ms)
            if (newTime >= 100 && newTime <= 5000) {
                timeToPressMs = newTime;
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Initialize Sensor
    mpu.initialize();
    if (!mpu.testConnection()) {
        Serial.println("Error: MPU6050 connection failed");
        while(1);
    }

    // Initialize BLE
    BLEDevice::init("BenchPressMonitor");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setCallbacks(new CharCallbacks());

    pService->start();

    // Start Advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);
    BLEDevice::startAdvertising();
}

void loop() {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Convert raw data to G
    float accX = ax / 16384.0;
    float accY = ay / 16384.0;
    float accZ = az / 16384.0;

    // Calculate magnitude of acceleration vector (minus gravity)
    float totalVector = sqrt(pow(accX, 2) + pow(accY, 2) + pow(accZ, 2));
    float vibration = abs(totalVector - 1.0);

    // State Machine
    unsigned long currentMillis = millis();

    switch (currentState) {
        case STATE_TOP_HOLD:
            if (vibration > MOTION_THRESHOLD_G) {
                currentState = STATE_DESCENDING;
                stableStartTime = 0;
            }
            break;

        case STATE_DESCENDING:
            if (vibration < STABLE_THRESHOLD_G) {
                if (stableStartTime == 0) stableStartTime = currentMillis;
                
                // Check if stability duration exceeds the target time
                if (currentMillis - stableStartTime > timeToPressMs) {
                    if (deviceConnected) {
                        pCharacteristic->setValue("PRESS");
                        pCharacteristic->notify();
                    }
                    currentState = STATE_COOLDOWN;
                    stableStartTime = currentMillis;
                }
            } else {
                stableStartTime = 0; // Reset timer if motion detected
            }
            break;

        case STATE_COOLDOWN:
            // Prevent double triggering (2 seconds cooldown)
            if (currentMillis - stableStartTime > 2000) {
                currentState = STATE_TOP_HOLD;
            }
            break;
    }
    
    // Low latency delay
    delay(10);
}