#include <Arduino.h>
#include <NewPing.h>
#include <LiquidCrystal_I2C.h>
#include <driver/i2s.h>
#include <Wire.h>

#include "audio_wave.h"
#include "Tune.h"
#include "custom_ble.h"


/**********
 * Macros *
 **********/

#define VERBOSE 0

// HC-SR04 ultrasonic sensor
#define TRIGGER_PIN 5
#define ECHO_PIN 4
#define MIN_DISTANCE 5    // Minimum distance to avoid noise (in cm)
#define MAX_DISTANCE 100  // Maximum distance to measure (in cm)

// MAX98357A I2S audio amplifier
#define LRC_PIN 18
#define BCLK_PIN 17
#define DIN_PIN 16  // ESP32 DOUT
#define I2S_PORT I2S_NUM_0

// audio settings
#define AUDIO_SAMPLE_RATE 44100  // Hz

// Potentiometer
#define POTENTIOMETER_PIN 20

// Rotary encoder
#define ROTARY_CLK_PIN 40
#define ROTARY_DT_PIN 41
#define ROTARY_SW_PIN 42

// LCD display
#define LCD_SDA_PIN 1
#define LCD_SCL_PIN 2
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

// Switches
#define RECORD_SWITCH_PIN 35
#define PLAYBACK_SWITCH_PIN 36
#define STOP_SWITCH_PIN 37


/***********************
 * Function prototypes *
 ***********************/

// I2S and audio functions
void i2sInit();
int16_t generateSample(WaveType type, float phase, int16_t amplitude);
void generateAudio();
void audioTask(void *parameter);

// LCD display functions
void showWelcomeMessage();
void showCurrentWaveType();

// HC-SR04 functions
unsigned int distanceToMidiNote(unsigned int distance);

// Rotary encoder functions
void rotaryControl();


/*********************
 * Global variables *
 ********************/
// shared state variables for audio generation
volatile float currentFrequency = noteFrequencies[A4]; // A4 note
volatile float phase = 0.0f;
volatile int16_t volume = 3000; // -32768 to 32,767 for 16-bit audio
volatile WaveType currentWaveType = SINE; // Default wave type

// flag to indicate if wave type has changed (for LCD update)
bool isWaveTypeChanged = true; // initialize to true so that LCD shows wave type on startup

// potentiometer
int16_t pot;

// rotary encoder
int lastCLK;

// ble
bool isRecording = false;
bool isPlaying = false;

// Initialize instances
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
CustomBLE ble;



void setup() {
  Serial.begin(115200);

  #if VERBOSE
    Serial.println("Starting Simple Synthesizer with");
    Serial.print("volume: ");
    Serial.print(volume);
    Serial.print(", wave type: ");
    switch (currentWaveType) {
      case SINE:
        Serial.println("SINE");
        break;
      case SQUARE:
        Serial.println("SQUARE");
        break;
      case SAW:
        Serial.println("SAW");
        break;
    }
    Serial.println("Starting setup...");
  #endif

  // Initialize LCD
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();
  #if VERBOSE
    Serial.println("LCD initialized");
  #endif

  // show welcome message and prepare
  showWelcomeMessage();
  #if VERBOSE
    Serial.println("Welcome message displayed");
  #endif

  // set pin mode for potentiometer
  pinMode(POTENTIOMETER_PIN, INPUT);
  #if VERBOSE
    Serial.print("Potentiometer pin initialized");
  #endif

  // set pin mode for rotary encoder
  pinMode(ROTARY_CLK_PIN, INPUT);
  pinMode(ROTARY_DT_PIN, INPUT);
  pinMode(ROTARY_SW_PIN, INPUT);
  lastCLK = digitalRead(ROTARY_CLK_PIN);
  #if VERBOSE
    Serial.println("Rotary encoder pins initialized");
  #endif

  // setup BLE
  ble.begin();
  #if VERBOSE
    Serial.println("BLE initialized");
  #endif

  // Initialize I2S for audio output
  i2sInit();
  #if VERBOSE
    Serial.println("I2S initialized");
  #endif

  // Create audio task on core 0 (parallel to main loop)
  // xTaskCreatePinnedToCore(audioTask, "Audio Task", 4096, NULL, 2, NULL, 1);
  #if VERBOSE
    Serial.println("Audio task created");
  #endif
}

void loop() {
  // i2c scan code
  // byte error, address;
  // int nDevices = 0;

  // Serial.println("Scanning...");

  // for(address = 1; address < 127; address++) {
  //   Wire.beginTransmission(address);
  //   error = Wire.endTransmission();

  //   if(error == 0) {
  //     Serial.print("I2C device found at 0x");
  //     if(address < 16) Serial.print("0");
  //     Serial.println(address, HEX);
  //     nDevices++;
  //   }
  // }

  // if(nDevices == 0)
  //   Serial.println("No I2C devices found");

  // delay(5000);
 
  // display wave type selected if changed
  // initially display once (default: SINE)
  if (isWaveTypeChanged) {
    #if VERBOSE
      Serial.print("Wave type changed to: ");
      switch (currentWaveType) {
        case SINE:
          Serial.println("SINE");
          break;
        case SQUARE:
          Serial.println("SQUARE");
          break;
        case SAW:
          Serial.println("SAW");
          break;
      }
    #endif
    showCurrentWaveType();
    isWaveTypeChanged = false;
  }

  // set volume
  #if VERBOSE
    pot = analogRead(POTENTIOMETER_PIN);
    Serial.print("current potentiometer: ");
    Serial.print(pot);
    volume = map(pot, 0, 4095, 0, 127) * 258;
    Serial.print(", to volume: ");
    Serial.println(volume);
  #else
    volume = map(analogRead(POTENTIOMETER_PIN), 0, 4095, 0, 127) * 258;
  #endif

  // set wave
  rotaryControl();

  // BLE
  if(ble.recStartRequested) {
    #if VERBOSE
      Serial.println("\nREC_START\n");
    #endif
    isRecording = true;
    ble.recStartRequested = false;
  }
  if(ble.recStopRequested) {
    #if VERBOSE
      Serial.println("\nREC_STOP\n");
    #endif
    isRecording = false;
    ble.recStopRequested = false;
  }
  if(ble.playStopRequested) {
    #if VERBOSE
      Serial.println("\nPLAY_STOP\n");
    #endif
    isPlaying = !isPlaying;
    ble.playStopRequested = false;
  }

  // change frequency based on distance measured by HC-SR04  
  #if VERBOSE
    unsigned int distance = sonar.ping_cm();
    unsigned int midiNote = distanceToMidiNote(distance);
    double frequency = noteFrequencies[midiNote];
    
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" cm, MIDI Note: ");
    Serial.print(midiNote);
    Serial.print(", Frequency: ");
    Serial.print(frequency);
    Serial.println();
    Serial.println();
    currentFrequency = frequency;
  #else
    currentFrequency = noteFrequencies[distanceToMidiNote(sonar.ping_cm())];
  #endif


  // delay(100); // small delay to avoid flooding serial output
}


/***************************
 * I2S and Audio Functions *
 ***************************/
void i2sInit() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = BCLK_PIN,
    .ws_io_num = LRC_PIN,
    .data_out_num = DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

int16_t generateSample(WaveType type, float phase, int16_t amplitude) {
  switch (type) {
    case SINE:
      return (int16_t)(sinf(phase) * amplitude);
    case SQUARE:
      return (phase < PI) ? amplitude : -amplitude;
    case SAW:
      return (int16_t)(((phase / (2.0f * PI)) * 2 - 1) * amplitude);
    default:
      return 0;
  }
}

void generateAudio() {
  const int sampleRate = AUDIO_SAMPLE_RATE;

  const int samples = 32; // number of samples per buffer
  int16_t buffer[samples * 2]; // stereo buffer (left + right)
  float phaseStep = 2.0f * PI * currentFrequency / sampleRate;

  for (int i = 0; i < samples; i++) {
    // generate sample based on current wave type
    int16_t s = generateSample(currentWaveType, phase, volume);

    // update phase, wrap around at 2*PI
    phase += phaseStep;
    if (phase >= 2.0f * PI) phase -= 2.0f * PI;

    // write sample to both left and right channels
    buffer[i * 2]     = s;  // Left
    buffer[i * 2 + 1] = s;  // Right
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
}

void audioTask(void *parameter) {
  while (true) {
    generateAudio();
    vTaskDelay(1); // small delay to yield to other tasks (if needed)
  }
}


/*************************
 * LCD Display Functions *
 *************************/
void showWelcomeMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Simple");
  lcd.setCursor(0, 1);
  lcd.print(" Synthesizer");
}

void showCurrentWaveType() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Current Wave:");
  lcd.setCursor(0, 1);
  switch (currentWaveType) {
    case SINE:
      lcd.print("1. Sine Wave");
      break;
    case SQUARE:
      lcd.print("2. Square Wave");
      break;
    case SAW:
      lcd.print("3. Sawtooth Wave");
      break;
  }
}


/*********************
 * HC-SR04 Functions *
 *********************/
unsigned int distanceToMidiNote(unsigned int distance) {
  if (distance > MAX_DISTANCE) distance = MAX_DISTANCE; // cap at max distance
  else if (distance < MIN_DISTANCE) distance = MIN_DISTANCE; // cap at min distance
  return map(distance, MIN_DISTANCE, MAX_DISTANCE, A0, C8); // A0 to C8 (piano range)
}


/****************************
 * Rotary Encoder Functions *
 ****************************/
void rotaryControl() {
  int clk = digitalRead(ROTARY_CLK_PIN);

  if(clk != lastCLK) {
    if(clk != digitalRead(ROTARY_DT_PIN)) {
      #if VERBOSE
        Serial.println("Encoder moved CW");
      #endif
      currentWaveType = (WaveType)((currentWaveType + 1) % 3); // modulo to avoid overflow
    }
    else {
      #if VERBOSE
        Serial.println("Encoder moved CCW");
      #endif
      currentWaveType = (WaveType)((currentWaveType + 2) % 3); // -1 equals to -1+n. addition to prevent negative.
    }
    isWaveTypeChanged = true;
    lastCLK = clk;
  }
}