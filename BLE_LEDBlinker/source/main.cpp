/* mbed Microcontroller Library
 * Copyright (c) 2006-2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <mbed-events/events.h>
#include <mbed.h>
#include "ble/BLE.h"
#include "ble/DiscoveredCharacteristic.h"
#include "ble/DiscoveredService.h"

DigitalOut col1(P0_4, 0);
DigitalOut alivenessLED(P0_13, 1);

#define CHAR_LEN 18
static DiscoveredCharacteristic ledCharacteristic;
static bool triggerLedCharacteristic;
static const char PEER_NAME[] = "Yeelight Blue II";
static char colourString[] = "CLTMP 6500,100,,,,";
//static const char PEER_NAME[] = "LED";

/* Avoid the 'stream' overhead */
RawSerial pc(USBTX, USBRX);
#define printf pc.printf


static EventQueue eventQueue(
    /* event count */ 16 * /* event size */ 32    
);

void periodicCallback(void) {
    alivenessLED = !alivenessLED; /* Do blinky on LED1 while we're waiting for BLE events */
}

void advertisementCallback(const Gap::AdvertisementCallbackParams_t *params) {
    // parse the advertising payload, looking for data type COMPLETE_LOCAL_NAME
    // The advertising payload is a collection of key/value records where
    // byte 0: length of the record excluding this byte
    // byte 1: The key, it is the type of the data
    // byte [2..N] The value. N is equal to byte0 - 1
    for (uint8_t i = 0; i < params->advertisingDataLen; ++i) {

        const uint8_t record_length = params->advertisingData[i];
        if (record_length == 0) {
            continue;
        }
        const uint8_t type = params->advertisingData[i + 1];
        const uint8_t* value = params->advertisingData + i + 2;
        const uint8_t value_length = record_length - 1;

        if(type == GapAdvertisingData::COMPLETE_LOCAL_NAME) {
            printf("Seen Peer: '%s'\r\n", value);
	    printf("compare a: %d:%d \r\n compare b: %d\r\n", value_length, sizeof(PEER_NAME),(memcmp(value, PEER_NAME, value_length)));
            if ((value_length <= sizeof(PEER_NAME)) && (memcmp(value, PEER_NAME, value_length) == 0)) {
                printf(
                    "adv peerAddr[%02x %02x %02x %02x %02x %02x] rssi %d, isScanResponse %u, AdvertisementType %u\r\n",
                    params->peerAddr[5], params->peerAddr[4], params->peerAddr[3], params->peerAddr[2],
                    params->peerAddr[1], params->peerAddr[0], params->rssi, params->isScanResponse, params->type
                );
                BLE::Instance().gap().connect(params->peerAddr, Gap::ADDR_TYPE_PUBLIC, NULL, NULL);
                break;
            }
        }
        i += record_length;
    }
}

void serviceDiscoveryCallback(const DiscoveredService *service) {
    if (service->getUUID().shortOrLong() == UUID::UUID_TYPE_SHORT) {
        printf("S UUID-%x attrs[%u %u]\r\n", service->getUUID().getShortUUID(), service->getStartHandle(), service->getEndHandle());
    } else {
        printf("S UUID-");
        const uint8_t *longUUIDBytes = service->getUUID().getBaseUUID();
        for (unsigned i = 0; i < UUID::LENGTH_OF_LONG_UUID; i++) {
            printf("%02x", longUUIDBytes[i]);
        }
        printf(" attrs[%u %u]\r\n", service->getStartHandle(), service->getEndHandle());
    }
}

void updateLedCharacteristic(void) {
    if (!BLE::Instance().gattClient().isServiceDiscoveryActive()) {

	printf("Writing LED colour\r\n");
        ledCharacteristic.write(18, (const uint8_t *)&colourString);
    }
}

void characteristicDiscoveryCallback(const DiscoveredCharacteristic *characteristicP) {
    printf("  C UUID-%x valueAttr[%u] props[%x]\r\n", characteristicP->getUUID().getShortUUID(), characteristicP->getValueHandle(), (uint8_t)characteristicP->getProperties().broadcast());
    if (characteristicP->getUUID().getShortUUID() == 0xFFF1) { /* !ALERT! Alter this filter to suit your device. */
        ledCharacteristic        = *characteristicP;
        triggerLedCharacteristic = true;
    }
}

void discoveryTerminationCallback(Gap::Handle_t connectionHandle) {
    printf("terminated SD for handle %u\r\n", connectionHandle);
    if (triggerLedCharacteristic) {
        triggerLedCharacteristic = false;
        eventQueue.post(updateLedCharacteristic);
    }
}

void connectionCallback(const Gap::ConnectionCallbackParams_t *params) {
    printf("connectionCallback.\r\n");
    if (params->role == Gap::CENTRAL) {
        printf("Starting service discover\r\n");
        BLE &ble = BLE::Instance();
        ble.gattClient().onServiceDiscoveryTermination(discoveryTerminationCallback);
        ble.gattClient().launchServiceDiscovery(params->handle, serviceDiscoveryCallback, characteristicDiscoveryCallback, 0xFFF0, 0xFFF1);
    }
}

/*
 * copy the char 'padding' into each character in 'buffer' from start to end
 */

void padString(const int start, const int end, const char padding, char* buffer) {
	for (int i = start; i < end; i++) {
		buffer[i] = padding;
	}
}
void formatRGB(const int red, const int green, const int blue, const int brightness, char* buffer) {
	int len = 0;
	/* perplexingly, if we keep brightness at 3 digits, the bulb doesn't
	 * process colour *and* brightness changes together. For example
	 * 000,255,000,100,,, --> green, 100%
	 * 255,000,000,050,,, --> still green, but 50%
	 * whereas if insted "000,255,000,100,,," is followed by "000,255,000,50,,,,"
	 * the colour and brightness change occurs
	 */
	len = sprintf(buffer, "%03d,%03d,%03d,%d,", red, green, blue, brightness);
	padString(len, CHAR_LEN, ',', buffer);

}

/* temp between 1700-6500, brightness 0-100 */
void formatColourTemp(const int temp, const int brightness, char* buffer) {

	int len = 0;
	len = sprintf(buffer, "CLTMP %04d,%d", temp, brightness);
	padString(len, CHAR_LEN, ',', buffer);
}

void triggerToggledWrite(const GattReadCallbackParams *response) {
	static int clrtmp = 1700;
	static int red=0,green=0,blue=0;
    if (response->handle == ledCharacteristic.getValueHandle()) {
        printf("triggerToggledWrite: handle %u, offset %u, len %u\r\n", response->handle, response->offset, response->len);
        for (unsigned index = 0; index < response->len; index++) {
            printf("%c[%02x]", response->data[index], response->data[index]);
        }
        printf("\r\n");
	if (clrtmp > 6500)
		clrtmp=1700;
	if (red > 255)
		red = 0;
	if (green > 255)
		green = 0;
	if (blue > 255)
		blue =0;
	clrtmp+=100;
	red+=11;
	green+=21;
	blue+=41;
	printf("CLTMP %04d,100,,,,\r\n", clrtmp);
//	sprintf((char *)&colourString, "CLTMP %04d,100,,,,", clrtmp);
	formatColourTemp(clrtmp, 100, (char*)&colourString);
	formatRGB(red,green,blue, 100, (char*)&colourString);
	printf("%s\r\n", colourString);
        ledCharacteristic.write(18, (const uint8_t *)&colourString);
    }
}

void triggerRead(const GattWriteCallbackParams *response) {
    if (response->handle == ledCharacteristic.getValueHandle()) {
        ledCharacteristic.read();
    }
}

void disconnectionCallback(const Gap::DisconnectionCallbackParams_t *) {
    printf("disconnected\r\n");
    /* Start scanning and try to connect again */
    BLE::Instance().gap().startScan(advertisementCallback);
}

void onBleInitError(BLE &ble, ble_error_t error)
{
   /* Initialization error handling should go here */
}

void bleInitComplete(BLE::InitializationCompleteCallbackContext *params)
{
    BLE&        ble   = params->ble;
    ble_error_t error = params->error;

    if (error != BLE_ERROR_NONE) {
        /* In case of error, forward the error handling to onBleInitError */
        onBleInitError(ble, error);
        return;
    }

    /* Ensure that it is the default instance of BLE */
    if (ble.getInstanceID() != BLE::DEFAULT_INSTANCE) {
        return;
    }

    ble.gap().onDisconnection(disconnectionCallback);
    ble.gap().onConnection(connectionCallback);

    ble.gattClient().onDataRead(triggerToggledWrite);
    ble.gattClient().onDataWrite(triggerRead);

    // scan interval: 400ms and scan window: 400ms.
    // Every 400ms the device will scan for 400ms
    // This means that the device will scan continuously.
    ble.gap().setScanParams(400, 400);
    ble.gap().startScan(advertisementCallback);
}

void scheduleBleEventsProcessing(BLE::OnEventsToProcessCallbackContext* context) {
    BLE &ble = BLE::Instance();
    eventQueue.post(Callback<void()>(&ble, &BLE::processEvents));
}

int main()
{
    triggerLedCharacteristic = false;
    eventQueue.post_every(500, periodicCallback);

    printf("Hello. Starting\r\n");
    BLE &ble = BLE::Instance();
    ble.onEventsToProcess(scheduleBleEventsProcessing);
    ble.init(bleInitComplete);

    while (true) {
        eventQueue.dispatch();
    }

    return 0;
}
