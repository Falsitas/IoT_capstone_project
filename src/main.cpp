#include <Arduino.h>
#include <NewPing.h>
#include <LiquidCrystal_I2C.h>
#include <driver/i2s.h>
#include <Wire.h>

#include "audio_wave.h"
#include "Tune.h"
#include "custom_ble.h"
#include "record_event.h"


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
#define SAMPLES 128

// multicore settings
#define STACK_DEPTH 8192

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

// record
#define MAX_EVENTS 1024
#define RECORD_LED_PIN 36
#define PLAYING_LED_PIN 35


/***********************
 * Function prototypes *
 ***********************/

// I2S and audio functions
void i2sInit();
int16_t generateSample(WaveType type, float phase, int16_t amplitude);
void generateAudio();
void playBackAudio();
void audioTask(void *parameter);

// LCD display functions
void showWelcomeMessage();
void showCurrentWaveType();

// HC-SR04 functions
unsigned int distanceToMidiNote(unsigned int distance);
void pingTask(void *parameter);

// Rotary encoder functions
void rotaryControl();

// recording functions
void startRecording();
void stopRecording();
void startPlayBack();
void stopPlayBack();
void playBackTask(void* parameter);


/*********************
 * Global variables *
 ********************/
// shared state variables for audio generation
volatile float currentFrequency = noteFrequencies[A4]; // A4 note
volatile float phase = 0.0f;
volatile int16_t volume = 3000; // -32768 to 32,767 for 16-bit audio
volatile WaveType currentWaveType = SINE; // Default wave type
int16_t buffer[SAMPLES * 2]; // stereo buffer (left + right)

// note change by HC-SR04
int8_t stableNote = -1;
int8_t candidateNote = -1;
uint8_t sameCount = 0;

// flag to indicate if wave type has changed (for LCD update)
bool isWaveTypeChanged = true; // initialize to true so that LCD shows wave type on startup

// potentiometer
int16_t pot;

// rotary encoder
int lastCLK;

// ble-record
bool isRecording = false;
bool isPlaying = false;
NoteEvent recorded[MAX_EVENTS];
uint16_t eventCount = 0;
uint32_t recordStartTime;
TaskHandle_t playTaskHandler = NULL;

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
  // set LED pins
  pinMode(RECORD_LED_PIN, OUTPUT);
  pinMode(PLAYING_LED_PIN, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH);
  delay(250);
  digitalWrite(BUILTIN_LED, LOW);
  delay(250);
  digitalWrite(RECORD_LED_PIN, HIGH);
  delay(250);
  digitalWrite(RECORD_LED_PIN, LOW);
  delay(250);
  digitalWrite(PLAYING_LED_PIN, HIGH);
  delay(250);
  digitalWrite(PLAYING_LED_PIN, LOW);
  delay(250);
  #if VERBOSE
    Serial.println("BLE initialized");
  #endif

  // Initialize I2S for audio output
  i2sInit();
  // sound test for 1sec
  for(int i = 0; i < (AUDIO_SAMPLE_RATE / SAMPLES); i++) {
    generateAudio();
  }
  #if VERBOSE
    Serial.println("I2S initialized");
  #endif

  // Create audio task on core 0 (parallel to main loop)
  xTaskCreatePinnedToCore(audioTask, "Audio Task", STACK_DEPTH, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(pingTask, "Ping Task", 4096, NULL, 1, NULL, 0);
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
  if(ble.recRequested) {
    ble.recRequested = false;
    if(isRecording) stopRecording();
    else startRecording();
  }
  if(ble.playStopRequested) {
    ble.playStopRequested = false;
    if(isPlaying) stopPlayBack();
    else startPlayBack();
  }
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

  const int samples = SAMPLES; // number of samples per buffer
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

void playBackAudio(uint32_t duration, int8_t note) {
  const int sampleRate = AUDIO_SAMPLE_RATE;
  const int chunk = 256;
  uint32_t samples = sampleRate * duration / 1000;
  int16_t buffer[chunk * 2];
  float phaseStep = 2.0f * PI * noteFrequencies[note] / sampleRate;
  size_t bytesWritten = 0;

  while(samples > 0) {
    int n = (samples > chunk) ? chunk : samples;
    
    for (int i = 0; i < n; i++) {
      // generate sample based on current wave type
      int16_t s = generateSample(currentWaveType, phase, volume);

      // update phase, wrap around at 2*PI
      phase += phaseStep;
      if (phase >= 2.0f * PI) phase -= 2.0f * PI;

      // write sample to both left and right channels
      buffer[i * 2]     = s;  // Left
      buffer[i * 2 + 1] = s;  // Right
    }
    i2s_write(I2S_PORT, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
    samples -= n;
  }
}

void audioTask(void *parameter) {
  while (true) {
    if(isPlaying) {
      vTaskDelay(20);
      continue;
    }
    generateAudio();
    // vTaskDelay(1); // small delay to yield to other tasks (if needed)
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
  return map(distance, MIN_DISTANCE, MAX_DISTANCE, C2, C7); // C2 to C7 (61keys range)
  // return map(distance, MIN_DISTANCE, MAX_DISTANCE, A0, C8); // A0 to C8 (88keys piano range)
}

void pingTask(void *parameter) {
  while(true) {
    // no need to ping while play back
    if(isPlaying) {
      vTaskDelay(20);
      continue;
    }

    int newNote = distanceToMidiNote(sonar.ping_cm());
    if (newNote == candidateNote) {
      sameCount++;
    } else {
      candidateNote = newNote;
      sameCount = 1;
    }

    // stable change for the notes prevent fluctuation
    if (sameCount >= 3 && stableNote != candidateNote) {
      stableNote = candidateNote;
      // if recording
      if(isRecording && eventCount < MAX_EVENTS) {
        recorded[eventCount].timestamp = millis() - recordStartTime;
        recorded[eventCount].note = stableNote;
        eventCount++;
      }
      currentFrequency = noteFrequencies[stableNote];
    }
    
    vTaskDelay(20);
  }
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


/***********************
 * recording functions *
 ***********************/
void startRecording() {
  #if VERBOSE
    Serial.println("Recording started");
  #endif
  // recording first, kill playback and start new recording
  if(isPlaying) {
    stopPlayBack();
  }
  isRecording = true;
  digitalWrite(RECORD_LED_PIN, HIGH);
  eventCount = 0;
  recordStartTime = millis();

  // record first note
  recorded[eventCount++] = {0, stableNote};
}

void stopRecording() {
  #if VERBOSE
    Serial.println("Recording stopped");
  #endif
  isRecording = false;
  digitalWrite(RECORD_LED_PIN, LOW);

  // record finish time, -1 for the note since there is no meaning of recording stable note
  recorded[eventCount++] = {millis() - recordStartTime, -1};
}

void startPlayBack() {
  #if VERBOSE
    Serial.println("Playback started");
  #endif
  // playback is not allowed when recording
  if(isRecording) {
    return;
  }
  isPlaying = true;
  digitalWrite(PLAYING_LED_PIN, HIGH);
  xTaskCreatePinnedToCore(playBackTask, "Play Task", STACK_DEPTH, NULL, 1, &playTaskHandler, 1);
}

void stopPlayBack() {
  #if VERBOSE
    Serial.println("Playback stopped");
  #endif
  isPlaying = false;
  digitalWrite(PLAYING_LED_PIN, LOW);
  vTaskDelete(playTaskHandler);
  playTaskHandler = NULL;
}

void playBackTask(void* parameter) {
  int currentCount = 0;
  while(currentCount < eventCount-1) {
    playBackAudio(recorded[currentCount+1].timestamp - recorded[currentCount].timestamp, recorded[currentCount].note);
    currentCount++;
  }
  // end of playback
  isPlaying = false;
  digitalWrite(PLAYING_LED_PIN, LOW);
  vTaskDelete(NULL);
}