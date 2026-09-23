#ifndef RFIDREADER_H
#define RFIDREADER_H

/** Adapted from : vvvvv
 * ----------------------------------------------------------------------------
 * This is a MFRC522 library example; see https://github.com/miguelbalboa/rfid
 * for further details and other examples.
 *
 * NOTE: The library file MFRC522.h has a lot of useful info. Please read it.
 *
 * Released into the public domain.
 * ----------------------------------------------------------------------------
 * Minimal example how to use the interrupts to read the UID of a MIFARE Classic PICC
 * (= card/tag).
 *
 *
 * Typical pin layout used:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno/101       Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            53        D10        3                10
 * IRQ         ?            ?             ?         ?          2                10
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 *
 */

#include <SPI.h>
#include <MFRC522.h>

/**
 * MFRC522 interrupt serving routine
 */
volatile bool bNewInt = false;
void readCard() {
	Serial.println(String()+"Interrupt call:"+bNewInt+" millis:"+millis());
	bNewInt = true;
}

class RFIDReader {
	byte sspin,rstpin,irqpin;
	String tag;
	MFRC522 mfrc522;   // Create MFRC522 instance.
	MFRC522::MIFARE_Key key;

/*

	void activateRec();
	void clearInt();

*/
public:
	//	RFIDReader(){}

	void init(byte sspin0, byte  rstpin0, byte  irqpin0)/*:sspin(sspin0), rstpin(rstpin0), irqpin(irqpin0)*/{
		sspin=sspin0;
		rstpin=rstpin0;
		irqpin=irqpin0;
		byte regVal = 0x7F;
		SPI.begin();          // Init SPI bus
		mfrc522.PCD_Init(sspin, rstpin); // Init MFRC522 card

		/* read and printout the MFRC522 version (valid values 0x91 & 0x92)*/
		Serial.print(F("MFRC522 Ver: 0x"));
		byte readReg = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
		Serial.println(readReg, HEX);

		/* setup the IRQ pin*/
		pinMode(irqpin, INPUT_PULLUP);


		/* Allow the ... irq to be propagated to the IRQ pin
		 * For test purposes propagate the IdleIrq and loAlert*/
		regVal = 0xA0; //rx irq
		mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, regVal);

		bNewInt = false; //interrupt flag

		/*Activate the interrupt*/
		attachInterrupt(digitalPinToInterrupt(irqpin), readCard, FALLING);
		/*
		  do { //clear a spourious interrupt at start
		    ;
		  } while (!bNewInt);
		  bNewInt = false;
		 */
	}

private:
	/**
	 * Helper routine to dump a byte array as hex values to Serial.
	 */
	String dump_byte_array(byte *buffer, byte bufferSize) {
		String val;
		for (byte i = 0; i < bufferSize; i++) {
			Serial.print(buffer[i] < 0x10 ? " 0" : " ");
			Serial.print(buffer[i], HEX);
			char hexStr[10];  // String to hold the hexadecimal value
			sprintf(hexStr, "%X", buffer[i]);  // Convert integer to hexadecimal string
			val+=buffer[i] < 0x10 ? " 0" : " ";
			val+=String(hexStr);

		}
	//	Serial.println();
//		Serial.println(String()+"Val:"+val);
		return val;
	}
	/**
	 * Helper routine to dump a byte array as hex values to Serial.
	 */
	String byte_arrayToHex(byte *buffer, byte bufferSize) {
		String val;
		for (byte i = 0; i < bufferSize; i++) {
//			Serial.print(buffer[i] < 0x10 ? " 0" : " ");
//			Serial.print(buffer[i], HEX);
			char hexStr[10];  // String to hold the hexadecimal value
			sprintf(hexStr, "%X", buffer[i]);  // Convert integer to hexadecimal string
			val+=buffer[i] < 0x10 ? " 0" : " ";
			val+=String(hexStr);

		}
	//	Serial.println();
//		Serial.println(String()+"Val:"+val);
		return val;
	}

	/*
	 * The function sending to the MFRC522 the needed commands to activate the reception
	 */
	void activateRec() {
		mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
		mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
		mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
	}

	/*
	 * The function to clear the pending interrupt bits after interrupt serving routine
	 */
	void clearInt() {
		mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
	}
public:
	String readTag() {

		if (bNewInt) { //new read interrupt
	//		Serial.print(F("Interrupt. "));
			mfrc522.PICC_ReadCardSerial(); //read the tag data
			// Show some details of the PICC (that is: the tag/card)
//			Serial.print(F("Card UID:"));
			tag=byte_arrayToHex(mfrc522.uid.uidByte, mfrc522.uid.size);
//			Serial.println();
//			Serial.println(String()+"millis:"+millis());

			clearInt();
			mfrc522.PICC_HaltA();
			bNewInt = false;
			return tag;
		}

		// The receiving block needs regular retriggering (tell the tag it should transmit??)
		// (mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg,mfrc522.PICC_CMD_REQA);)
		activateRec();
		return "";
	}

};

#endif
