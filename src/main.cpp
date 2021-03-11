#include <Arduino.h>
#include <SAMDTimerInterrupt.h>
#include <Adafruit_NeoPixel.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>
#include "Adafruit_TinyUSB.h"

#pragma region flash

// On-board external flash (QSPI or SPI) macros should already
// defined in your board variant if supported
// - EXTERNAL_FLASH_USE_QSPI
// - EXTERNAL_FLASH_USE_CS/EXTERNAL_FLASH_USE_SPI
#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;

#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS, EXTERNAL_FLASH_USE_SPI);

#else
#error No QSPI/SPI flash are defined on your board variant.h !
#endif

Adafruit_SPIFlash onboardFlash(&flashTransport);
FatFileSystem fatfs;

#pragma endregion

#pragma region LED

#define NUM_LEDS 12
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN_A0, NEO_GRB + NEO_KHZ800);

#pragma endregion

#pragma region CameraWatcher

/*
Signal taken from the top of the bottom-right LED on the Logitech C920. 
With a pullup, we see LOW when off.
When on, it appears to be a PWM, so we use the below hacked together detector.
*/
unsigned long last_on = 0;
unsigned long last_off = 0;

SAMDTimer ITimer0(TIMER_TC3);

bool CamOn = false;
bool ManualOverride = false;

// Timer periodically checks the difference between on/off
void TimerHandler0(void)
{
  // Experimentally derived - if last_on - last_off < 150, cam is off. Above 1500, on.
  // 1000 seems to work as a check point
  CamOn = (last_on - last_off) > 1000;
  //Reset to make sure we get no false/partial readings
  last_on = 0;
  last_off = 0;
}

//Don't need to be thorough here, just means there's a max 100ms delay between cam on/off and light on/off.
#define TIMER0_INTERVAL_MS  100

// ISR triggered on pin change, record millis for appropriate change
void lineChange ()
{
  if(digitalRead(PIN_A1) == HIGH){
    last_on = micros();
  }else{
    last_off = micros();
  }
} 

#pragma endregion

#pragma region DataSettings
#define SETTINGS_FILE      "settings.bin"

struct Settings {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t brightness;
  uint8_t mode;
  uint8_t padding[58]; // Pad to 63
};

struct RGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t brightness;
};

struct Control {
  uint8_t command;
  uint8_t pattern;
  RGB all;
  RGB leds[NUM_LEDS]; 
  uint8_t padding[63 - 1 - 1 - sizeof(RGB) - (NUM_LEDS*sizeof(RGB))]; // Pad to 64
};

union Data {
  struct Settings settings;
  struct Control control;
  byte mode;
};

struct HIDData {
  uint8_t instruction;
  union Data data;
};

Settings settings = { .r = 255, .g = 255, .b = 255, .brightness = 255, .mode = 0, NULL};

void writeSettings(){
  Serial.println("Writing Settings");
  File dataFile = fatfs.open(SETTINGS_FILE, FILE_WRITE);
  uint8_t buffer[64];
  buffer[0] = settings.r;
  buffer[1] = settings.g;
  buffer[2] = settings.b;
  buffer[3] = settings.brightness;
  buffer[4] = settings.mode;

  
  for(int i = 5; i < 64; i++){
    buffer[i] = 0;
  }

  Serial.println("Writing:");
  for(int i = 0; i < 64; i++){
    Serial.printf("%#02x ", buffer[i]);
  }

  dataFile.seek(0);

  dataFile.write((uint8_t *)&buffer, sizeof(buffer));
  dataFile.flush();
  dataFile.close();
}

void readSettings(){
  Serial.println("Reading Settings");
  File dataFile = fatfs.open(SETTINGS_FILE, FILE_READ);
  if(!dataFile){
    writeSettings();
    dataFile = fatfs.open(SETTINGS_FILE, FILE_READ);
  }

  uint8_t buffer[64];

  dataFile.readBytes((uint8_t *)&buffer, sizeof(buffer));

  Serial.println("Reading:");
  for(int i = 0; i < 64; i++){
    Serial.printf("%#02x ", buffer[i]);
  }

  settings.r = buffer[0];
  settings.g = buffer[1];
  settings.b = buffer[2];
  settings.brightness = buffer[3];
  settings.mode = buffer[4];
  dataFile.close();
}

void deserialize(HIDData *data, uint8_t const* buffer, uint16_t bufsize){
  for(int i = 0; i < bufsize; i++){
    Serial.printf("%#02x ", buffer[i]);
  }
  Serial.println();
  data->instruction = buffer[0];

  switch(data->instruction){
    case 0: //settings
      Serial.println("Decoding Settings");
      data->data.settings.r = buffer[1];
      data->data.settings.g = buffer[2];
      data->data.settings.b = buffer[3];
      data->data.settings.brightness = buffer[4];
      data->data.settings.mode = buffer[5];
      break;
    case 1: //Control
      Serial.println("Decoding Control");
      data->data.control.command = buffer[1];
      data->data.control.pattern = buffer[2];
      data->data.control.all.r = buffer[3];
      data->data.control.all.g = buffer[4];
      data->data.control.all.b = buffer[5];
      data->data.control.all.brightness = buffer[6];

      for(uint8_t i = 0; i < NUM_LEDS; i++){
        data->data.control.leds[i].r = buffer[i+7];
        data->data.control.leds[i].g = buffer[i+8];
        data->data.control.leds[i].b = buffer[i+9];
        data->data.control.leds[i].brightness = buffer[i+10];
      }

      break;
    case 2: //Mode
      Serial.println("Decoding Control");
      data->data.mode = buffer[1];
      break;
    default:
      Serial.println("Unknown!");
      return;
  }
}

#pragma endregion

#pragma region USBCode
// HID report descriptor using TinyUSB's template
// Generic In Out with 64 bytes report (max)
uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_GENERIC_INOUT(64),
};

Adafruit_USBD_HID usb_hid;

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t get_report_callback (uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // not used in this example
  return 0;
}

void set_leds(Control c){
  Serial.println("set_leds");
  Serial.println("Command: "+ String(c.command));

  if (c.command == 255){
    Serial.println("Reverting to camera control");
    ManualOverride = false;
  }else{
    Serial.println("Manual control");
    ManualOverride = true;
  }

  if(c.command == 0){
    for(uint8_t i=0; i< strip.numPixels(); i++) {
    Serial.println("Set [" + String(i) + "]: R:" + String(c.all.r) + " G:" + String(c.all.b) + " B:" + String(c.all.b));  
      strip.setPixelColor(i, c.leds[i].r, c.leds[i].g, c.leds[i].b);
    }
  }else if(c.command == 1){    
    Serial.println("Set All: R:" + String(c.all.r) + " G:" + String(c.all.b) + " B:" + String(c.all.b));  
    for(uint8_t i=0; i< strip.numPixels(); i++) {
      strip.setPixelColor(i, c.all.r, c.all.g, c.all.b);
    }
    //strip.setBrightness(c.all.brightness);
  }
  strip.show();
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void set_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{ 
  (void) report_id;
  (void) report_type;

  HIDData *data = (HIDData *) malloc(sizeof(HIDData));
  deserialize(data, buffer, bufsize);
  
  Serial.println("Instruction: " + String(data->instruction));

  switch(data->instruction){
    case 0:
      //settings
      usb_hid.sendReport(report_id, (void*)1, 1);
      //Settings s = data->data.settings;
      settings = data->data.settings;
      writeSettings();
      readSettings();
      break;
    case 1:
      //control
      usb_hid.sendReport(report_id, (void*)1, 1);
      set_leds(data->data.control);
      break;
    case 2:
      settings.mode = data->data.mode;
      break;
    default:
      Serial.println("Unknown Command, ignoring.");
      usb_hid.sendReport(report_id, 0, 1);
      break;
  }

  free(data);
}

#pragma endregion

void setup() {
  usb_hid.enableOutEndpoint(true);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setReportCallback(get_report_callback, set_report_callback);
  usb_hid.setStringDescriptor("CamRing");

  usb_hid.begin();
  Serial.begin(115200);

  // wait until device mounted
  while( !USBDevice.mounted() ) delay(1);
  // wait for native usb
  while (!Serial) delay(1);

  Serial.print("Starting up onboard QSPI Flash...");
  onboardFlash.begin();
  Serial.println("Done");
  Serial.println("Onboard Flash information");
  Serial.print("JEDEC ID: 0x");
  Serial.println(onboardFlash.getJEDECID(), HEX);
  Serial.print("Flash size: ");
  Serial.print(onboardFlash.size() / 1024);
  Serial.println(" KB");

  // First call begin to mount the filesystem.  Check that it returns true
  // to make sure the filesystem was mounted.
  if (!fatfs.begin(&onboardFlash)) {
    Serial.println("Error, failed to mount newly formatted filesystem!");
    Serial.println("Was the flash chip formatted with the fatfs_format example?");
    while(1);
  }
  Serial.println("Mounted filesystem!");
    
  readSettings();
  
  // put your setup code here, to run once:
  pinMode(PIN_A1, INPUT_PULLUP); 
  attachInterrupt (digitalPinToInterrupt (PIN_A1), lineChange, CHANGE);  // attach interrupt handler

    // Interval in microsecs
  if (ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS * 1000, TimerHandler0)){
    Serial.println("Starting  ITimer0 OK, millis() = " + String(millis()));
  }else{
    Serial.println("Can't set ITimer0. Select another freq. or timer");
  }  

  strip.begin();
  strip.setBrightness(255);
  strip.show(); // Initialize all pixels to 'off'
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

uint8_t cycle = 0;

void loop() {
  // put your main code here, to run repeatedly:
  if(!ManualOverride){
    if(CamOn){
      //Serial.printf("Mode:%#02x R:%#02x G:%#02x B:%#02x\n", settings.mode, settings.r, settings.g, settings.b);
      if(settings.mode == 0){
        for(uint8_t i=0; i< strip.numPixels(); i++) {
          strip.setPixelColor(i,settings.r, settings.g, settings.b);
        }
      }else if (settings.mode == 1)
      {
        for(uint8_t i=0; i< strip.numPixels(); i++) {
          strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + cycle++) & 255));
        }
      }
      strip.show();
      delay(20);
    }else{
      for(uint8_t i=0; i< strip.numPixels(); i++) {
        strip.setPixelColor(i,0);
      }
      strip.show();
    }
  }
  delay(1);

}