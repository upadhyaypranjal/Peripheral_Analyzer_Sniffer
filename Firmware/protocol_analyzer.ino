#include <Shrike.h>

#define SDA_PIN 14
#define SCL_PIN 15

ShrikeFlash fpga;

const int UART_BIT_US = 104;
unsigned long programStart;

// ─── Bit capture buffer ───────────────────────────────────────────────────────

#define MAX_BITS 128
uint8_t capturedBits[MAX_BITS];
int capturedCount = 0;

// ─── I2C Capture ─────────────────────────────────────────────────────────────

void captureI2C() {
    capturedCount = 0;

    unsigned long t = micros();
    while (digitalRead(SCL_PIN) == HIGH) {
        if (micros() - t > 2000) return;
    }

    while (capturedCount < MAX_BITS) {

        // Wait for SCL HIGH — SDA is valid here
        t = micros();
        while (digitalRead(SCL_PIN) == LOW) {
            if (micros() - t > 2000) return;
        }

        capturedBits[capturedCount++] = digitalRead(SDA_PIN);

        // Wait for SCL LOW
        t = micros();
        while (digitalRead(SCL_PIN) == HIGH) {
            if (micros() - t > 2000) return;
        }
    }
}

// ─── I2C Decode from buffer ───────────────────────────────────────────────────

void decodeI2C() {
    // Re-capture from current bus state
    captureI2C();

    Serial.println("===== I2C DECODE =====");
    Serial.print("Bits captured: ");
    Serial.println(capturedCount);

    if (capturedCount < 9) {
        Serial.println("Not enough bits");
        Serial.println("======================");
        return;
    }

    int pos = 0;

    // Address byte (7 bits addr + 1 bit R/W)
    uint8_t addrByte = 0;
    for (int i = 7; i >= 0; i--) {
        addrByte |= (capturedBits[pos++] << i);
    }
    uint8_t address = addrByte >> 1;
    bool rw = addrByte & 0x01;

    Serial.print("Address = 0x");
    if (address < 16) Serial.print("0");
    Serial.println(address, HEX);
    Serial.print("Mode    = ");
    Serial.println(rw ? "READ" : "WRITE");

    // ACK for address
    if (pos >= capturedCount) {
        Serial.println("Missing addr ACK");
        Serial.println("======================");
        return;
    }
    bool addrAck = (capturedBits[pos++] == LOW);
    Serial.print("Addr ACK = ");
    Serial.println(addrAck ? "ACK" : "NACK");

    if (!addrAck) {
        Serial.println("No device response");
        Serial.println("======================");
        return;
    }

    // Data bytes
    int byteIndex = 0;
    while (pos + 8 <= capturedCount) {
        uint8_t dataByte = 0;
        for (int i = 7; i >= 0; i--) {
            dataByte |= (capturedBits[pos++] << i);
        }
        Serial.print("Data[");
        Serial.print(byteIndex++);
        Serial.print("] = 0x");
        if (dataByte < 16) Serial.print("0");
        Serial.println(dataByte, HEX);

        if (pos < capturedCount) {
            bool dataAck = (capturedBits[pos++] == LOW);
            Serial.print("Data ACK = ");
            Serial.println(dataAck ? "ACK" : "NACK");
            if (!dataAck) break;
        }
    }

    Serial.println("======================");
}

// ─── UART Decoder ────────────────────────────────

void decodeUART() {
    String lineBuffer = "";
    unsigned long start = millis();

    while ((millis() - start) < 2000) {

        while (digitalRead(SDA_PIN) == HIGH) {
            if ((millis() - start) >= 2000) return;
        }

        delayMicroseconds(UART_BIT_US / 2);
        if (digitalRead(SDA_PIN) != LOW) continue;

        delayMicroseconds(UART_BIT_US);

        uint8_t value = 0;
        for (int i = 0; i < 8; i++) {
            value |= (digitalRead(SDA_PIN) << i);
            delayMicroseconds(UART_BIT_US);
        }

        if (digitalRead(SDA_PIN) != HIGH) continue;

        if (value >= 32 && value <= 126) {
            lineBuffer += (char)value;
        }
        else if (value == '\n') {
            if (lineBuffer.length() > 0) {
                Serial.print("Time = ");
                Serial.print(millis() - programStart);
                Serial.print(" ms | UART = ");
                Serial.println(lineBuffer);
                lineBuffer = "";
            }
        }

        while (digitalRead(SDA_PIN) == LOW);
    }

    // Flush
    if (lineBuffer.length() > 0) {
        Serial.print("Time = ");
        Serial.print(millis() - programStart);
        Serial.print(" ms | UART = ");
        Serial.println(lineBuffer);
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    delay(2000);
    programStart = millis();

    Serial.println("Loading FPGA...");
    if (!fpga.begin()) { Serial.println("FPGA init failed"); while (1); }
    if (!fpga.flash("/FPGA_bitstream_MCU.bin")) { Serial.println("Bitstream flash failed"); while (1); }

    pinMode(SDA_PIN, INPUT_PULLUP);
    pinMode(SCL_PIN, INPUT_PULLUP);

    Serial.println("Protocol Analyzer Started");
}


void loop() {

    int sdaEdges       = 0;
    int sclEdges       = 0;
    int i2cStartCount  = 0;
    int sclRisingCount = 0;
    bool i2cStartSeen  = false;

    int prevSDA = digitalRead(SDA_PIN);
    int prevSCL = digitalRead(SCL_PIN);

    unsigned long windowStart = millis();

    while ((millis() - windowStart) < 100) {

        int curSDA = digitalRead(SDA_PIN);
        int curSCL = digitalRead(SCL_PIN);

        if (curSDA != prevSDA) sdaEdges++;
        if (curSCL != prevSCL) sclEdges++;

        if (prevSDA == HIGH && curSDA == LOW && curSCL == HIGH) {
            i2cStartCount++;
            i2cStartSeen = true;
        }

        if (prevSCL == LOW && curSCL == HIGH) sclRisingCount++;

        prevSDA = curSDA;
        prevSCL = curSCL;

        delayMicroseconds(2);
    }

    // ── Protocol Detection ──────────────────────────────────────

    unsigned long timestamp = millis() - programStart;

    if (i2cStartCount >= 1 && sclEdges >= 8) {

        Serial.println();
        Serial.print("Time = "); Serial.print(timestamp); Serial.println(" ms");
        Serial.println("Protocol = I2C");
        Serial.print("START Conditions = "); Serial.println(i2cStartCount);
        Serial.print("Clock Pulses     = "); Serial.println(sclRisingCount);

        Serial.println("Next frame...");

        unsigned long waitStart = millis();
        bool gotStart = false;

        while (millis() - waitStart < 1500) {
            int sda = digitalRead(SDA_PIN);
            int scl = digitalRead(SCL_PIN);

            static int pSDA = HIGH, pSCL = HIGH;

            if (pSDA == HIGH && sda == LOW && scl == HIGH) {
                gotStart = true;
                pSDA = sda; pSCL = scl;
                break;
            }
            pSDA = sda; pSCL = scl;
        }

        if (gotStart) {
            captureI2C();

            Serial.println("____ I2C Decode ____");
            Serial.print("Bits captured: "); Serial.println(capturedCount);

            if (capturedCount >= 9) {
                int pos = 0;

                uint8_t addrByte = 0;
                for (int i = 7; i >= 0; i--)
                    addrByte |= (capturedBits[pos++] << i);

                uint8_t address = addrByte >> 1;
                bool rw = addrByte & 0x01;

                Serial.print("Address = 0x");
                if (address < 16) Serial.print("0");
                Serial.println(address, HEX);
                Serial.print("Mode    = "); Serial.println(rw ? "Read" : "Write");

                if (pos < capturedCount) {
                    bool addrAck = (capturedBits[pos++] == LOW);
                    Serial.print("Addr ACK = "); Serial.println(addrAck ? "ACK" : "NACK");

                    int byteIndex = 0;
                    while (pos + 8 <= capturedCount) {
                        uint8_t dataByte = 0;
                        for (int i = 7; i >= 0; i--)
                            dataByte |= (capturedBits[pos++] << i);

                        Serial.print("Data["); Serial.print(byteIndex++); Serial.print("] = 0x");
                        if (dataByte < 16) Serial.print("0");
                        Serial.println(dataByte, HEX);

                        if (pos < capturedCount) {
                            bool dAck = (capturedBits[pos++] == LOW);
                            Serial.print("Data ACK = "); Serial.println(dAck ? "ACK" : "NACK");
                            if (!dAck) break;
                        }
                    }
                }
            } else {
                Serial.println("Not enough bits captured");
            }
            Serial.println("--------------------");

        } else {
            Serial.println("No next frame");
        }
    }

    else if (sdaEdges > 20 && sclEdges < 4) {

        Serial.println();
        Serial.print("Time = "); Serial.print(timestamp); Serial.println(" ms");
        Serial.println("Protocol = UART");
        decodeUART();
    }
}
