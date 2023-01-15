| Supported Targets | ESP32 | ESP32-C3 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

# Remote UI for controlling home


## Overview

Uses switchbot to control devices. Soon adding support for philips Hue, Xbox, RS232, direct IR integration.


### About mDNS

The IP address of an IoT device may vary from time to time, so it’s impracticable to hard code the IP address in the webpage. In this example, we use the `mDNS` to parse the domain name `esp-home.local`, so that we can alway get access to the web server by this URL no matter what the real IP address behind it. See [here](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/protocols/mdns.html) for more information about mDNS.

**Notes: mDNS is installed by default on most operating systems or is available as separate package.**


## Hardware Required

To run this example, you need an ESP32 dev board (e.g. ESP32-WROVER Kit, ESP32-Ethernet-Kit) or ESP32 core board (e.g. ESP32-DevKitC).

### Configure the project

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Connection Configuration` menu:

* Choose the network interface in `Connect using`  option based on your board. Currently we support both Wi-Fi and Ethernet.
* If you select the Wi-Fi interface, you also have to set:
  * Wi-Fi SSID and Wi-Fi password that your esp32 will connect to.
* If you select the Ethernet interface, you also have to set:
  * PHY model in `Ethernet PHY` option, e.g. IP101.
  * PHY address in `PHY Address` option, which should be determined by your board schematic.
  * EMAC Clock mode, GPIO used by SMI.

In the `Remote Configuration` menu:

* Set the domain name in `mDNS Host Name` option.
* SWITCH

### Build and Flash


After a while, you will see a `dist` directory which contains all the website files (e.g. html, js, css, images).

Run `idf.py -p PORT flash monitor` to build and flash the project..

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.


## Example Output

### Render webpage in browser

In your browser, enter the URL where the website located (e.g. `http://remote-home.local`). You can also enter the IP address that ESP32 obtained if your operating system currently don't have support for mDNS service.

Besides that, this example also enables the NetBIOS feature with the domain name `remote-home`. If your OS supports NetBIOS and has enabled it (e.g. Windows has native support for NetBIOS), then the URL `http://remote-home` should also work.

### ESP monitor output

In the *Light* page, after we set up the light color and click on the check button, the browser will send a post request to ESP32, and in the console, we just print the color value.

```bash
I (6115) example_connect: Connected to Ethernet
I (6115) example_connect: IPv4 address: 192.168.2.151
I (6325) esp-home: Partition size: total: 1920401, used: 1587575
I (6325) esp-rest: Starting HTTP Server
I (128305) esp-rest: File sending complete
I (128565) esp-rest: File sending complete
I (128855) esp-rest: File sending complete
I (129525) esp-rest: File sending complete
I (129855) esp-rest: File sending complete
```
