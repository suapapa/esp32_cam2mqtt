# cam2mqtt

![usecase_of_cam2mqtt](_photo/cam2mqtt.png)

Take snapshot and send it to an mqtt topic using ESP32-CAM.

* Works well with HomeAssistant's [MQTT Camera](https://www.home-assistant.io/integrations/camera.mqtt/)
* Use [esp-idf](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/#).
* It takes one -or two- picture for a day and go to deep sleep.
* Date and time of the photo taken is printed in. [sample](_photo/sample.png).

## requirements

Install [esp-idf](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/#)

Install [ESP32 Camera Driver](https://github.com/espressif/esp32-camera):

    $ cd $IDF_PATH/components
    $ git clone https://github.com/espressif/esp32-camera

## clone, configure and build

Clone poject:

    $ git clone https://github.com/suapapa/esp32_cam2mqtt

Configure wifi-ssid, wifi-pass, mqtt-uri, mqtt-topic and etc...:

    $ cd cam2mqtt
    $ get_idf
    $ idf.py menuconfig

Build, flash, monitor:

    $ idf.py build
    $ idf.py -p /dev/ttyUSB0 flash
    $ idf.py -p /dev/ttyUSB0 monitor

## beautify code

    indent -linux main/main.c
