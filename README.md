# Neopixel Ringlight control for Logitech C920

Probably no use to anyone, but here it is.

## Requirements
 - Logitech C920
 - Adafruit QT Py with optional flash
 - Soldering iron
 - Wire
 - Screwdrivers

## Steps
Disassemble the webcam. You'll need to solder a wire to the top of the bottom-right LED for the signal, then a wire for ground.
![Board](https://raw.githubusercontent.com/keyz182/camring/master/img/c920.jpg)

Signal should go to A1 on the QT Py. 

A0 on the QT Py should go to the Neopixel ring Data pin, 5V to 5V, GND to GND.

Compile and upload the PlatformIO project. 

Turning the webcam on/off (e.g. opening the windows Camera app) will turn the light on and off.

The C# project in the `RingControl` folder allows control of the light. You can live-preview colours and set them.