// Do not remove the include below
//include "NRF_sensors_v2.h"
#include <SPI.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <nRF24L01.h>
#include "RF24.h"
#include "Adafruit_BMP085.h"
#include "BH1750.h"
#include "SHT2x.h"
#include "printf.h"
#include <Sleep_n0m1.h>

extern HardwareSerial Serial;

#define DEBUG_ENABLE 					(1)

#define PAYLOAD_SIZE					(32)
#define LED_PIN 							(13)
#define BATT_CHECK_CNT				(5)
#define MINIMUM_BAT_VOLTAGE   (3300)//mV
#define REFRESH_RATE					(10000)//mV
#define RAIN_EN								(8)
#define RAIN_A								A0
#define RAIN_D								A1


Sleep sleep;
unsigned long sleepTime; //how long you want the arduino to sleep
int rain_a = 0;
int rain_d = 0;
//uint8_t seqnum = 0;

char payload[PAYLOAD_SIZE];

long adc = 0;
uint8_t measure_adc = BATT_CHECK_CNT;

typedef struct {
	char devID; //ID workstation - 1B
	char msgType;
	uint8_t seqNum; //packet number - 1B
	char temp_minus;
	char temperature[4]; //temperature value - 3B
	char humidity[4]; //humidity value - 3B
	char airpressure[6];
	char lux[5];
	char rain[4];
	//uint8_t gas; //air quality adc data - 1B
} nrf_message_t;
nrf_message_t nrf_message;

typedef struct {
	char devID; //ID workstation - 1B
	char msgType;
	uint8_t seqNum; //packet number - 1B
	char vcc[5];
	//uint8_t gas; //air quality adc data - 1B

} nrf_message_sys_t;
nrf_message_sys_t nrf_sys_message;

//
// Hardware configuration
//

// Set up nRF24L01 radio on SPI bus plus pins 9 & 10

RF24 radio(9, 10);

//
// Topology
//

// Radio pipe addresses for the 2 nodes to communicate.
const uint64_t pipes[1] = { 0xE8E8F0F0E2LL };

//
// Role management
//
// Set up role.  This sketch uses the same software for all the nodes
// in this system.  Doing so greatly simplifies testing.
//

// The various roles supported by this sketch
typedef enum {
	role_ping_out = 1, role_pong_back
} role_e;

// The debug-friendly names of those roles
const char* role_friendly_name[] = { "invalid", "Ping out", "Pong back" };

// The role of the current running sketch
role_e role = role_ping_out;

//SENSORS
Adafruit_BMP085 bmp;
BH1750 lightMeter;

long readVcc(void) {
	// Read 1.1V reference against AVcc
	// set the reference to Vcc and the measurement to the internal 1.1V reference
	#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
	ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
	#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
	ADMUX = _BV(MUX5) | _BV(MUX0);
	#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
	ADMUX = _BV(MUX3) | _BV(MUX2);
	#else
	ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
	#endif

	delay(2); // Wait for Vref to settle
	ADCSRA |= _BV(ADSC); // Start conversion
	while (bit_is_set(ADCSRA, ADSC)); // measuring

	uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
	uint8_t high = ADCH; // unlocks both

	long result = (high << 8) | low;

	result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
	return result; // Vcc in millivolts
}



//The setup function is called once at startup of the sketch
void setup() {
	// Add your initialization code here
	Serial.begin(57600);
	printf_begin();
	printf("\n\rRF24/examples/GettingStarted/\n\r");
	printf("ROLE: %s\n\r", role_friendly_name[role]);
	//printf("*** PRESS 'T' to begin transmitting to the other node\n\r");


	//
	// Setup and configure rf radio
	//

	sleepTime = REFRESH_RATE; //set sleep time in ms, max sleep time is 49.7 days

	radio.begin();
	printf("\nRadio init...");

	// optionally, increase the delay between retries & # of retries
	radio.setRetries(15, 15);

	// optionally, reduce the payload size.  seems to
	// improve reliability
	//radio.setPayloadSize(8);

	//
	// Open pipes to other nodes for communication
	//

	// This simple sketch opens two pipes for these two nodes to communicate
	// back and forth.
	// Open 'our' pipe for writing
	// Open the 'other' pipe for reading, in position #1 (we can have up to 5 pipes open for reading)

	if (role == role_ping_out) {
		radio.openWritingPipe(pipes[0]);
		radio.openReadingPipe(1, pipes[1]);
	} else {
		radio.openWritingPipe(pipes[1]);
		radio.openReadingPipe(1, pipes[0]);
	}

	//
	// Start listening
	//

	//radio.startListening();

	//
	// Dump the configuration of the rf unit for debugging
	//

	radio.setPALevel(RF24_PA_HIGH);

	printf("ok!");

	//radio.printDetails();
	//Serial.begin(9600);
	if (!bmp.begin()) {
		printf("\nCould not find BMP085 sensor!");
		while (1) {
		}
	}
	printf("\nBaro done!");

	lightMeter.begin();

	printf("\nLight meter done!");

	pinMode(A0, INPUT);
	pinMode(A1, INPUT);
	pinMode(RAIN_EN, OUTPUT);

	nrf_message.devID = 1;
	nrf_sys_message.devID = 1;
	//
	nrf_message.temp_minus = 0;

	printf("\nInit done!");
}

// The loop function is called in an endless loop
void loop() {
	//Add your repeated code here

	if(nrf_message.seqNum%10){

		nrf_message.msgType = 1;

		digitalWrite(RAIN_EN, HIGH);

		//
		// Sequence number
		//
		nrf_message.seqNum++;// = seqNum;
		nrf_sys_message.seqNum = nrf_message.seqNum;


		//
		// Temperature
		//
		float temp = SHT2x.GetTemperature();	//bmp.readTemperature();

		if (temp > 0)
		nrf_message.temp_minus = 0;
		else
		nrf_message.temp_minus = 1;

		dtostrf(temp, 4, 1, nrf_message.temperature);

		//nrf_message.temperature[2] = nrf_message.temperature[3];
		//nrf_message.temperature[3] = '0';

		//
		// Humidity
		//
		//char temp_hum[] = "55.4";
		float hum = SHT2x.GetHumidity();
		dtostrf(hum, 4, 1, nrf_message.humidity);


		//
		// Pressure
		//
		int32_t press = bmp.readPressure();

		//delay(100);
		//int32t press = bmp.readPressure();
		//sprintf(nrf_message.airpressure, "%06i", (int)bmp.readPressure());
		dtostrf(press, 6, 0, nrf_message.airpressure);
		//Serial.print(nrf_message.airpressure);


		//
		// Lux
		//
		uint16_t lux = lightMeter.readLightLevel();
		//sprintf(nrf_message.lux, "%05u", lux);
		dtostrf(lux, 5, 0, nrf_message.lux);

		//
		// Rain
		//
		rain_a = analogRead(A0);
		dtostrf(rain_a, 4, 0, nrf_message.rain);
		//rain_d = digitalRead(A1);



		#if DEBUG_ENABLE == 1

		printf("\n");
		printf("\nmsg type: %d", nrf_message.msgType);
		printf("\nseq: %d", nrf_message.seqNum);
		printf("\ntemp: %s", nrf_message.temperature);
		printf("\nhum: %s", nrf_message.humidity);
		printf("\nprs: %s", nrf_message.airpressure);
		printf("\nlux: %s", nrf_message.lux);
		printf("\nrain: %s", nrf_message.rain);


		memcpy(&payload, &nrf_message, sizeof(nrf_message));
		#endif
	}else{

		nrf_sys_message.msgType = 2;

		//
		// Sequence number
		//
		nrf_message.seqNum++;// = seqNum;
		nrf_sys_message.seqNum = nrf_message.seqNum;

		//
		// VCC
		//
		measure_adc++;
		if (measure_adc > BATT_CHECK_CNT) {
			adc = readVcc();
			if (adc < MINIMUM_BAT_VOLTAGE) {
				printf("\nLOW BATT!");
				while(1){
					sleep.pwrDownMode(); //set sleep mode
					radio.powerDown();
				}
			}
		}
		dtostrf(adc, 5, 0, nrf_sys_message.vcc);

		memcpy(&payload, &nrf_sys_message, sizeof(nrf_sys_message));

		printf("\nmsg type: %d", nrf_sys_message.msgType);
		printf("\nvcc: %s", nrf_sys_message.vcc);
	}


	//
	// Ping out role.
	//

	if (role == role_ping_out) {
		// First, stop listening so we can talk.
		radio.stopListening();




		//memcpy(&nrf_message, &payload, sizeof(nrf_message));

		bool ok = radio.write(&payload, sizeof(payload));


		#if DEBUG_ENABLE == 1

		printf("\n");
		for (int i = 0; i < PAYLOAD_SIZE; i++) {
			putchar(payload[i]);
		}
		if (ok){
			printf("\nsend ok...");
		}else{
			printf("\nsend failed.\n\r");
		}
		#endif
		// Now, continue listening
		//radio.startListening();

		radio.powerDown();
		delay(100);
		sleep.pwrDownMode(); //set sleep mode
		sleep.sleepDelay(sleepTime); //sleep for: sleepTime
		radio.powerUp();

		digitalWrite(RAIN_EN, LOW);
		// Try again 1s later
		//delay(60000);
	}

	//
	// Pong back role.  Receive each packet, dump it out, and send it back
	//

	// if (role == role_pong_back) {
	// 	// if there is data ready
	// 	if (radio.available()) {
	// 		// Dump the payloads until we've gotten everything
	// 		unsigned long got_time;
	// 		bool done = false;
	// 		while (!done) {
	// 			// Fetch the payload, and see if this was the last one.
	// 			done = radio.read(&got_time, sizeof(unsigned long));
	//
	// 			// Spew it
	// 			printf("Got payload %lu...", got_time);
	//
	// 			// Delay just a little bit to let the other unit
	// 			// make the transition to receiver
	// 			delay(20);
	// 		}
	//
	// 		// First, stop listening so we can talk
	// 		radio.stopListening();
	//
	// 		// Send the final one back.
	// 		radio.write(&got_time, sizeof(unsigned long));
	// 		printf("Sent response.\n\r");
	//
	// 		// Now, resume listening so we catch the next packets.
	// 		radio.startListening();
	// 	}
	// }

	// //
	// // Change roles
	// //
	//
	// if (Serial.available()) {
	// 	char c = toupper(Serial.read());
	// 	if (c == 'T' && role == role_pong_back) {
	// 		printf(
	// 			"*** CHANGING TO TRANSMIT ROLE -- PRESS 'R' TO SWITCH BACK\n\r");
	//
	// 			// Become the primary transmitter (ping out)
	// 			role = role_ping_out;
	// 			radio.openWritingPipe(pipes[0]);
	// 			radio.openReadingPipe(1, pipes[1]);
	// 		} else if (c == 'R' && role == role_ping_out) {
	// 			printf(
	// 				"*** CHANGING TO RECEIVE ROLE -- PRESS 'T' TO SWITCH BACK\n\r");
	//
	// 				// Become the primary receiver (pong back)
	// 				role = role_pong_back;
	// 				radio.openWritingPipe(pipes[1]);
	// 				radio.openReadingPipe(1, pipes[0]);
	// 			}
	// 		}


}
