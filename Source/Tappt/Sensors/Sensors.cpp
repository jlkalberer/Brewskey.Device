#include "Sensors.h"

Sensors::Sensors(Tap taps[], uint8_t tapCount) {
  this->taps = taps;
  this->tapCount = tapCount;

  this->temperatureSensor = new Temperature();

  pinMode(FLOW_PIN, INPUT);
	digitalWrite(FLOW_PIN, HIGH);
#if USE_INTERRUPT == 1
	attachInterrupt(FLOW_PIN, &Sensors::SingleFlowCounter, this, FALLING, 0);
#endif
  pinMode(SOLENOID_PIN, OUTPUT);

#ifdef EXPANSION_BOX_PIN
  /*RS485 direction pin*/
	pinMode(EXPANSION_BOX_PIN, OUTPUT);

  this->PrepareDataPacket();
#endif

  this->OpenSolenoids();
  delay(10);
  this->CloseSolenoids();
}

int Sensors::Tick() {
  this->temperatureSensor->Tick();
  // TODO: Maybe we can support 5 taps...?
#if USE_INTERRUPT == 0
  this->SingleFlowCounter();
#endif
#ifdef EXPANSION_BOX_PIN
  this->ReadMultitap();
#endif
  return 0;
}

void Sensors::SingleFlowCounter()
{
	uint8_t pin = digitalRead(FLOW_PIN);
#if USE_INTERRUPT == 1
	// delayMicroseconds(1200);
	if (pin == 0) {
    this->taps[0].AddToFlowCount(1);
	}
#else
	static uint8_t buffer = 0;

	/*shift buffer byte by 1 */
	buffer <<= 1;

	/*SENSOR_PIN represents the pin status, if high set last bit in buffer to 1
  else it will remain 0*/
	if(pin != 0)
	{
		buffer |= 0x01;
	}

	/*check for 0x07 pattern (mask upper 2 bits), representing a low to high
  transition verified by 3 low samples followed by 3 high samples*/
	if((buffer & 0x3F) == 0x07)
	{
		this->taps[0].AddToFlowCount(1);
	}
#endif
}

// If this gets called it means the pour stopped or the user changed the state
// of the device (cleaning mode/disabled/enabled)
void Sensors::CloseSolenoids() {
  for (int i = 0; i < this->tapCount; i++) {
    this->CloseSolenoid(i);
  }
}

void Sensors::CloseSolenoid(uint8_t solenoid) {
  if (solenoid == 0) {
    digitalWrite(SOLENOID_PIN, LOW);
  }

#ifdef EXPANSION_BOX_PIN
  switch(solenoid) {
    case 0: {
      this->dataPacket[4] |= 0x03;
      break;
    }
    case 1: {
      this->dataPacket[4] |= 0x0C;
      break;
    }
    case 2: {
      this->dataPacket[4] |= 0x30;
      break;
    }
    case 3: {
      this->dataPacket[4] |= 0xC0;
      break;
    }
  }
#endif
}

void Sensors::OpenSolenoids() {
  digitalWrite(SOLENOID_PIN, HIGH);

  #ifdef EXPANSION_BOX_PIN
    /*switch(solenoid) {
      case 0: {
        this->dataPacket[3] |= 0x03;
        break;
      }
      case 1: {
        this->dataPacket[3] |= 0x0C;
        break;
      }
      case 2: {
        this->dataPacket[3] |= 0x30;
        break;
      }
      case 3: {
        this->dataPacket[3] |= 0xC0;
        break;
      }
    }*/
  #endif
}

#ifdef EXPANSION_BOX_PIN
void Sensors::ReadMultitap(void)
{
	uint8_t ii,
    count = 0,
    checksum = 0,
    data = 0,
    esc_flag = 0,
    isValid =0;

  /*set RS485 direction pin HIGH: transmitter*/
	digitalWrite(EXPANSION_BOX_PIN, HIGH);
	/*transmit packet*/
  this->UartSendPacket(this->dataPacket, 6);
  /*reset packet to the original values*/
  this->PrepareDataPacket();
  /*set RS485 direction pin LOW: receiver*/
	digitalWrite(EXPANSION_BOX_PIN, LOW);

  delay(100);

	/*read all received bytes*/
	while (Serial1.available() > 0) {
		data = Serial1.read();			/* Get data */
    Serial.print(data, HEX);
		if(data == '#' && !esc_flag)				/* If finding first escape byte */
		{
			esc_flag = 1;							/* Set escape byte flag */
		}
		else
		{
      /* Escape byte not set */
			if(!esc_flag)
			{
        /* Getting sync byte of packet, since no escape byte beore it */
				if(data == '+')
				{
					count = 0;						/* Reset Counter - since start of packet */
					for(ii = 0; ii < PACKET_BUFFER; ii++)
					{
						incomingBuffer[ii] = 0;	/* Clearing packet buffer */
					}

					continue;
				}

				if(data == '-')						/* End of packet */
				{
					checksum = 0;					/* Reset checksum */

					for(ii = 0; ii < 20; ii++)		/* Calculating checksum of packet */
					{
						checksum ^= incomingBuffer[ii];
					}

					checksum = 255 - checksum;

					if(checksum == incomingBuffer[count - 1])
					{
						isValid = 1;	/*packet is valid*/
					}
					else
					{
						isValid = 0;			/* packet invalid */
					}
				}
			}
			else
			{
				esc_flag = 0;
			}

      /* If count still less than packet buffer size */
			if(count < PACKET_BUFFER)
			{
				incomingBuffer[count] = data;	/* Store data in buffer */
				count++;									/* Increment counter */
			}
		}
	}
  Serial.println();
  Serial.println();

	if(isValid)
	{
    Serial.println();
    Serial.println(System.freeMemory());
    Serial.println();

		/*print received data to USB*/
		if(incomingBuffer[0] == 0x00 && incomingBuffer[1] == 0x01 && incomingBuffer[2] == 0x33)
		{
      for (ii = 1; ii <= this->tapCount; ii++) {
        unsigned long pulses =
          (incomingBuffer[4 * ii]<<24) |
          (incomingBuffer[4 * ii + 1]<<16) |
          (incomingBuffer[4 * ii + 2]<<8) |
          (incomingBuffer[4 * ii + 1]);

        Serial.print("Pulses: ");
        Serial.println(pulses);
        // Get difference to determine if it is still pouring
        pulses -= this->taps[ii - 1].GetTotalPulses();
        if (pulses <= 0) {
          continue;
        }

        this->taps[ii - 1].AddToFlowCount(pulses);
      }

			Serial.printf("SOL1: %s, PULSES1: %d, SOL2: %s, PULSES2: %d, SOL3: %s, PULSES3: %d, SOL4: %s, PULSES4: %d\n",
				(incomingBuffer[3] & 0x01)?"ON":"OFF",	/*solenoid 1*/
				(incomingBuffer[4]<<24) | (incomingBuffer[5]<<16) |	(incomingBuffer[6]<<8) | (incomingBuffer[7]), /*flow 1*/
				(incomingBuffer[3] & 0x02)?"ON":"OFF",	/*solenoid 2*/
				(incomingBuffer[8]<<24) | (incomingBuffer[9]<<16) |	(incomingBuffer[10]<<8) | (incomingBuffer[11]), /*flow 2*/
				(incomingBuffer[3] & 0x04)?"ON":"OFF",	/*solenoid 3*/
				(incomingBuffer[12]<<24) | (incomingBuffer[13]<<16) |	(incomingBuffer[14]<<8) | (incomingBuffer[15]), /*flow 3*/
				(incomingBuffer[3] & 0x08)?"ON":"OFF",	/*solenoid 4*/
				(incomingBuffer[16]<<24) | (incomingBuffer[17]<<16) |	(incomingBuffer[18]<<8) | (incomingBuffer[19])); /*flow 4*/
      Serial.println();

		}
	}
}

/*process and send packet*/
void Sensors::UartSendPacket(uint8_t* pstr, int length)
{
	uint8_t * str = pstr;  //.. str = pstr
	int i = 0;

	/*packet needs to start with the start-of-packet byte (ASCII '+' value)*/
	Serial1.write('+');

	while(i < length) /* Loop through the packet data bytes */
	{

		/*if any of the special bytes are found, escape them by sending ASCII '#'
      before the byte*/
		if(*str == '*' || *str == '+' || *str == '-' || *str == '#')
		{
			Serial1.write('#');
		}

		/*send data byte*/
		Serial1.write(*str);

		str++;														/* Point to next char */
		i++;														/* Incr length index */
	}

	/*packet needs to end with the end-of-packet byte (ASCII '-' value)*/
	Serial1.write('-');

	/*wait for serial data to be transfered*/
	Serial1.flush();
}

/*data packet preparation*/
void Sensors::PrepareDataPacket()
{

	uint8_t checksum = 0;
	uint8_t ii;

	this->dataPacket[0] = 0x01;	/*destination - hardcoded 0x01*/
	this->dataPacket[1] = 0x00;	/*source - mainboard*/
	this->dataPacket[2] = 0x22; /*packet type - solenoid control */

	/*dataPacket[3] - turn solenoid ON
	Bits: 0x03 - solendoid 1
	Bits: 0x0C - solendoid 2
	Bits: 0x30 - solendoid 3
	Bits: 0xC0 - solendoid 4

	solendoid will turn OFF automatically when no more flow is detected*/
	this->dataPacket[3] = 0x00;

	/*dataPacket[4] - force solenoid OFF
	Bits: 0x03 - solendoid 1
	Bits: 0x0C - solendoid 2
	Bits: 0x30 - solendoid 3
	Bits: 0xC0 - solendoid 4
	Thus can be used to override the auto-off algorithm, for example in case
  abnormal flow is detected (tap left open or leak)
	*/
	this->dataPacket[4] = 0x00;

	/*calculate checksum*/
	for(ii = 0; ii < 5; ii++)
	{
		checksum ^= dataPacket[ii];
	}

	dataPacket[5] = 255 - checksum;
}

#endif
