# esp32-homekit-demo
Extended demo of [Apple HomeKit accessory server library](https://github.com/maximkulkin/esp-homekit).

# HomeKit Accessory Development Kit (ADK)

The HomeKit ADK is used by silicon vendors and accessory manufacturers to build HomeKit compatible devices.

The HomeKit ADK implements key components of the HomeKit Accessory Protocol (HAP), which embodies the core principles Apple brings to smart home technology: security, privacy, and reliability.

The HomeKit Open Source ADK is an open-source version of the HomeKit Accessory Development Kit. It can be used by any developer to prototype non-commercial smart home accessories. For commercial accessories, accessory developers must continue to use the commercial version of the HomeKit ADK available through the MFi Program.

Go to the Apple Developer Site if you like to learn more about developing HomeKit-enabled accessories and apps.

## Getting Started

Please go through Getting Started Guide before using HomeKit ADK.

Initialize and sync all submodules (recursively):
```shell
git submodule update --init --recursive
```
Copy wifi.h.sample -> wifi.h and edit it with correct WiFi SSID and password.

Install esp-idf by following instructions on my webpage. https://www.studiopieters.nl/idf/

## Configure project:
Next you execute the following command, where the path is the place, where in macOS, we stored our github repo. Type the following line in your terminal window en press enter.

```shell
docker run -it -v ~/ESP32:/project -w /project espressif/idf:v4.3.2
```
Note: `idf:v4.3.2` can change if you installed another version!

Now you can change into the homekit-led directory. type cd homekit-led and press enter.
```shell
idf.py set-target esp32
```

Open a terminal-based project configuration menu by typing 
```shell
idf.py menuconfig
```
Go to ``StudioPieters`` in the menu and change the options to your choice, or leave it as it is.

Now we start the compiling by typing 
```shell
idf.py build
```
and then press enter, The result will be in a directory build and the process ends with an instruction.
<br>
<br>
#

<img  style="float: right;" src="https://github.com/AchimPieters/esp32-homekit-demo/blob/main/apple_logo.png" width="10"> <sub><sup>HomeKit Accessory Protocol (HAP) is Apple’s proprietary protocol that enables third-party accessories in the home (e.g., lights, thermostats and door locks) and Apple products to communicate with each other. HAP supports two transports, IP and Bluetooth LE. The information provided in the HomeKit Accessory Protocol Specification (Non-Commercial Version) describes how to implement HAP in an accessory that you create for non-commercial use and that will not be distributed or sold.</sup></sub>

<sub><sup> The HomeKit Accessory Protocol Specification (Non-Commercial Version) can be downloaded from the HomeKit Apple Developer page.</sup></sub>

<sub><sup> Copyright © 2019 Apple Inc. All rights reserved. </sup></sub>


#
<br>
<br>
<img src="https://github.com/AchimPieters/esp32-homekit-demo/blob/main/MIT-Software-license_BW.png" width="300">
