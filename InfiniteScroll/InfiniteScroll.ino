#include <ILI9341_t3n.h>
#include <SPI.h>
#include <SD.h>
#include "Adafruit_seesaw.h"

// TFT display and SD card will share the hardware SPI interface.
// Hardware SPI pins are specific to the Arduino board type and
// cannot be remapped to alternate pins.  For Arduino Uno,
// Duemilanove, etc., pin 11 = MOSI, pin 12 = MISO, pin 13 = SCK.

// DEFAULT WIRING USING SPI 0 ON TEENSY 4/4.1
//
#define PIN_SCK 13   // mandatory
#define PIN_MISO 12  // mandatory (if the display has no MISO line, set this to 255 but then VSync will be disabled)
#define PIN_MOSI 11  // mandatory
#define PIN_DC 10    // mandatory, can be any pin but using pin 10 (or 36 or 37 on T4.1) provides greater performance

#define PIN_CS 9     // optional (but recommended), can be any pin.
#define PIN_RESET 6  // optional (but recommended), can be any pin.
ILI9341_t3n tft = ILI9341_t3n(PIN_CS, PIN_DC);

#define SD_CS 4

#define SS_SWITCH_SELECT 1
#define SS_SWITCH_UP 2
#define SS_SWITCH_LEFT 3
#define SS_SWITCH_DOWN 4
#define SS_SWITCH_RIGHT 5

Adafruit_seesaw ss;
int32_t encoder_position;

#define SEESAW_ADDR 0x49

void setup(void) {
  // Keep the SD card inactive while working the display.
  pinMode(SD_CS, INPUT_PULLUP);
  delay(200);

  tft.begin(40000000);
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLUE);

  Serial.begin(9600);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.println(F("Waiting for Arduino Serial Monitor..."));
  while (!Serial) {
    if (millis() > 8000) break;
  }

  Serial.print(F("Initializing SD card..."));
  tft.println(F("Init SD card..."));
  while (!SD.begin(SD_CS)) {
    Serial.println(F("failed to access SD card!"));
    tft.println(F("failed to access SD card!"));
    delay(2000);
  }
  Serial.println("OK!");

  if (!ss.begin(SEESAW_ADDR)) {
    Serial.println("Couldn't find seesaw on default address");
    while (1) delay(10);
  }
  Serial.println("seesaw started");
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version != 5740) {
    Serial.print("Wrong firmware loaded? ");
    Serial.println(version);
    while (1) delay(10);
  }
  Serial.println("Found Product 5740");

  ss.pinMode(SS_SWITCH_UP, INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_DOWN, INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_LEFT, INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_RIGHT, INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_SELECT, INPUT_PULLUP);

  // get starting position
  encoder_position = ss.getEncoderPosition();

  Serial.println("Turning on interrupts");
  ss.enableEncoderInterrupt();
  ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH_UP, 1);
}

bool gif = 1;
int length = 0;
int step = 3;
int i = 1;

void loop() {

  if (!ss.digitalRead(SS_SWITCH_UP)) {
    gif = !gif;
  }
  if (!ss.digitalRead(SS_SWITCH_DOWN)) {
    gif = !gif;
  }
  if (!ss.digitalRead(SS_SWITCH_SELECT)) {
    Serial.println("SELECT pressed!");
  }
  if (!ss.digitalRead(SS_SWITCH_LEFT)) {
    step = step - 1;
    if (step < 1) {
      step = 1;
    }
  }
  if (!ss.digitalRead(SS_SWITCH_RIGHT)) {
    step = step + 1;
  }

  int32_t new_position = ss.getEncoderPosition();
  // did we move around?
  if (encoder_position != new_position) {
    if (new_position - encoder_position > 0) {
      i += step;
    } else if (new_position - encoder_position < 0) {
      i -= step;
    }
    if (i > length) {
      i = i - length;
    }
    if (i < 0) {
      i = i + length;
    }
    encoder_position = new_position;  // and save for next round
  }

  if (gif) {
    length = 50;
  } else {
    length = 60;
  }
  char filename[24];
  if (gif) {
    sprintf(filename, "/beach-%02i.bmp", i);
  } else {
    sprintf(filename, "/yang-%02i.bmp", i);
  }
  bmpDraw(filename, 0, 0);
}


// This function opens a Windows Bitmap (BMP) file and
// displays it at the given coordinates.  It's sped up
// by reading many pixels worth of data at a time
// (rather than pixel by pixel).  Increasing the buffer
// size takes more of the Arduino's precious RAM but
// makes loading a little faster.  20 pixels seems a
// good balance for tiny AVR chips.

// Larger buffers are slightly more efficient, but if
// the buffer is too large, extra data is read unnecessarily.
// For example, if the image is 240 pixels wide, a 100
// pixel buffer will read 3 groups of 100 pixels.  The
// last 60 pixels from the 3rd read may not be used.

#define BUFFPIXEL 320


//===========================================================
// Try Draw using writeRect
void bmpDraw(const char *filename, uint8_t x, uint16_t y) {

  File bmpFile;
  int bmpWidth, bmpHeight;              // W+H in pixels
  uint8_t bmpDepth;                     // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;              // Start of image data in file
  uint32_t rowSize;                     // Not always = bmpWidth; may have padding
  uint8_t sdbuffer[3 * BUFFPIXEL];      // pixel buffer (R+G+B per pixel)
  uint16_t buffidx = sizeof(sdbuffer);  // Current position in sdbuffer
  boolean goodBmp = false;              // Set to true on valid header parse
  boolean flip = true;                  // BMP is stored bottom-to-top
  int w, h, row, col;
  uint8_t r, g, b;
  uint32_t pos = 0, startTime = millis();

  uint16_t awColors[320];  // hold colors for one row at a time...

  if ((x >= tft.width()) || (y >= tft.height())) return;

  Serial.println();
  Serial.print(F("Loading image '"));
  Serial.print(filename);
  Serial.println('\'');

  // Open requested file on SD card
  if (!(bmpFile = SD.open(filename))) {
    Serial.print(F("File not found"));
    return;
  }

  // Parse BMP header
  if (read16(bmpFile) == 0x4D42) {  // BMP signature
    Serial.print(F("File size: "));
    Serial.println(read32(bmpFile));
    (void)read32(bmpFile);             // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile);  // Start of image data
    Serial.print(F("Image Offset: "));
    Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    Serial.print(F("Header size: "));
    Serial.println(read32(bmpFile));
    bmpWidth = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if (read16(bmpFile) == 1) {    // # planes -- must be '1'
      bmpDepth = read16(bmpFile);  // bits per pixel
      Serial.print(F("Bit Depth: "));
      Serial.println(bmpDepth);
      if ((bmpDepth == 24) && (read32(bmpFile) == 0)) {  // 0 = uncompressed

        goodBmp = true;  // Supported BMP format -- proceed!
        // Serial.print(F("Image size: "));
        // Serial.print(bmpWidth);
        // Serial.print('x');
        // Serial.println(bmpHeight);

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if (bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if ((x + w - 1) >= tft.width()) w = tft.width() - x;
        if ((y + h - 1) >= tft.height()) h = tft.height() - y;

        for (row = 0; row < h; row++) {  // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if (flip)  // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else  // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if (bmpFile.position() != pos) {  // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer);  // Force buffer reload
          }

          for (col = 0; col < w; col++) {  // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) {  // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0;  // Set index to beginning
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            awColors[col] = tft.color565(r, g, b);
          }  // end pixel
          tft.writeRect(0, row, w, 1, awColors);
        }  // end scanline
        Serial.print(F("Loaded in "));
        Serial.print(millis() - startTime);
        Serial.println(" ms");
      }  // end goodBmp
    }
  }

  bmpFile.close();
  if (!goodBmp) Serial.println(F("BMP format not recognized."));
}



// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read();  // LSB
  ((uint8_t *)&result)[1] = f.read();  // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read();  // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read();  // MSB
  return result;
}
