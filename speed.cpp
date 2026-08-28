// Classic Porsche Speedometer Calibrator
// Public Domain
//
// Raw 944/911 transmission sensor signal must be processed
// by an external Schmitt trigger.
//
// Schmitt trigger should bypass filter and pull-up resistor.
//
// Use LM2940-10 with decoupling capacitors per datasheet
// to power Arduino in an automotive setting.
//
// Arduino Pin 7 supplies modified signal for speedometer.

const int debounce = 2500;

const int speedometerPin = 7;
const int sensorPin = 3;

int pulseState = LOW;

volatile unsigned long currentMicros = 0;
volatile unsigned long previousMicros = 0;
volatile unsigned long currentSpeed = 0;
volatile unsigned long previousSpeed = 0;
volatile unsigned long interval = 0;

unsigned long modInterval = 0;

// Decrease to slow down speedometer.
// calFactor = 1.0 makes no change to speedometer.
float calFactor = 0.91;


void setup()
{
    pinMode(13, OUTPUT);
    pinMode(speedometerPin, OUTPUT);
    pinMode(sensorPin, INPUT);

    digitalWrite(sensorPin, HIGH);

    attachInterrupt(1, iSr, FALLING);
}


void loop()
{
    noInterrupts();
    modInterval = interval;
    interrupts();

    currentMicros = micros();

    if (currentMicros - previousSpeed < 1000000)
    {
        if (currentMicros - previousMicros > ((modInterval / 2) / calFactor))
        {
            previousMicros = currentMicros;

            if (pulseState == LOW)
            {
                pulseState = HIGH;
            }
            else
            {
                pulseState = LOW;
            }

            // Blink onboard LED.
            digitalWrite(13, pulseState);

            // Output calibrated signal to speedometer.
            digitalWrite(speedometerPin, pulseState);
        }
    }
}


void iSr()
{
    currentSpeed = micros();

    if (digitalRead(sensorPin) == LOW)
    {
        if ((currentSpeed - previousSpeed) > debounce)
        {
            interval = currentSpeed - previousSpeed;
            previousSpeed = currentSpeed;
        }
    }
}
