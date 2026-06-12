#include <Wire.h>

String lineBuffer = "";

void setup()
{
    Serial.begin(9600);

    // D2 = SDA, D1 = SCL
    Wire.begin(D2, D1);

}

void loop()
{
    while (Serial.available())
    {
        char c = Serial.read();

    
        if (c == '\r' || c == '\n')
        {
            if (lineBuffer.length() > 0)
            {
                Serial.println(lineBuffer);

                lineBuffer = "";
            }
        }
        else
        {
            // Store character
            lineBuffer += c;
        
            Wire.beginTransmission(0x48);
            Wire.write((uint8_t)c);
            Wire.endTransmission();
        }
    }
}
