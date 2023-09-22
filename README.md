# ESP32 I2S MIC VAD

## Dependencies

* platform.io toolkit
* ESP-IDF (ver. 5.1)
* WebRTC-VAD

## Clone

```
$ git clone <url>
$ cd esp32-i2s-mic-vad
$ git submodule update --init --recursive
$ cd components/webrtc-vad
$ git checkout esp32-idf
```

## Build

* Build
```shell
$ pio run -t menuconfig
$ pio run
```
* Upload
```shell
$ pio run -t upload
```
* Connect
```shell
$ pio run -t monitor
```