#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ESP32Servo.h>

// https://www.uuidgenerator.net/
const char* SERVICE_UUID = "67d18b12-0631-41e3-997d-c9c422a28c42";
const char* RX_CHARACTERISTIC_UUID = "94734d5d-30cb-4dd0-aa7a-a5d75571469c";
const char* TX_CHARACTERISTIC_UUID = "524cb886-e926-4b96-8d34-bb7eb668ef7d";

const int RESET_BUTTON_PIN = 15;
const int BOX_SERVO_PIN = 9;
const int LAUNCH_SERVO_PIN = 18;


enum class STATE{
    NONE = 0,
    LOADED = 1,
    SHOOTING = 2,
    EMPTY = 3
};

const char* STATE_STRINGS[4] = {"NONE", "LOADED", "SHOOTING", "EMPTY"};

STATE catapultState = STATE::EMPTY;

BLECharacteristic *pTxCharacteristic = nullptr;
Servo boxServo, launchServo;

class CatapultServerCallbacks : public BLEServerCallbacks{
    void onConnect(BLEServer *pServer){
        Serial.println("Connected.");
    }
    void onDisconnect(BLEServer *pServer){
        pServer->startAdvertising();
        Serial.println("Disconnected, restarting advertising.");
    }
};

void setState(STATE newState){
    catapultState = newState;
    if(pTxCharacteristic != nullptr){
        pTxCharacteristic->setValue(STATE_STRINGS[(int)catapultState]);
        pTxCharacteristic->indicate();
    }
}

void resetServos(){
    boxServo.write(180);
    launchServo.write(30);
}

void shootingTask(void *pvParameters){
    Serial.println("Shooting started.");
    setState(STATE::SHOOTING);

    for(int i = 180; i > 80; i -= 5){
        boxServo.write(i);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    launchServo.write(0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    resetServos();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    Serial.println("Shooting done, empty.");
    setState(STATE::EMPTY);

    vTaskDelete(NULL);
}

class CatapultRxCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pRxCharacteristic){
        String rxValue = pRxCharacteristic->getValue();
            Serial.print("Received value \"");
            for(int i = 0; i < rxValue.length(); i++){ Serial.print(rxValue[i]); }
            Serial.println("\".");
        if(catapultState == STATE::LOADED && rxValue == "shoot"){
            /// start FreeRTOS task for shooting slowly
            xTaskCreate(shootingTask, "SHOOTING", 2000, NULL, 2, nullptr);
        }
    }
};

void setup() {
    Serial.begin(115200);
    
    /// SETUP BLE
    BLEDevice::init("Catapult (ESP32)");

    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new CatapultServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(RX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new CatapultRxCharacteristicCallbacks());

    pTxCharacteristic = pService->createCharacteristic(TX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_INDICATE);
    pTxCharacteristic->setValue(STATE_STRINGS[(int)catapultState]);
    
    pService->start();

    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->start();

    /// RESET BUTTON
    pinMode(RESET_BUTTON_PIN, INPUT);

    /// SETUP SERVOS FOR CATAPULT
    boxServo.attach(BOX_SERVO_PIN);
    launchServo.attach(LAUNCH_SERVO_PIN);
    resetServos();

    Serial.println("Setup done!");

    /// CREATE FREERTOS TASK FOR PHYSICAL IO (BUTTON AND LIGHTS)
    xTaskCreate(physicalIOTask, "PHYSICAL_IO", 2000, NULL, 1, nullptr);
}

void loop(){}

void physicalIOTask(void *pvParams) {
    while(true){
        // int btn = (int)digitalRead(RESET_BUTTON_PIN);
        // int red = RGB_BRIGHTNESS * (!btn);
        // int green = RGB_BRIGHTNESS * btn;
        // int blue = 0;
        // rgbLedWrite(RGB_BUILTIN, red, green, blue);
        if(catapultState == STATE::EMPTY){
            if(digitalRead(RESET_BUTTON_PIN)){
                Serial.println("Loaded.");
                setState(STATE::LOADED);
            }
        }
        switch(catapultState){
            case STATE::LOADED:   rgbLedWrite(RGB_BUILTIN, RGB_BRIGHTNESS/6, RGB_BRIGHTNESS/6, 0); break;
            case STATE::EMPTY:    rgbLedWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS/12, RGB_BRIGHTNESS/12); break;
            case STATE::SHOOTING: rgbLedWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0); break;
            default:              rgbLedWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, RGB_BRIGHTNESS); break;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}


