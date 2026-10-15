# Datasheets & documentation

Reference material for the TrapLineLoRa hardware and software stack.

PDFs are **not** committed to git (see `.gitignore`) — run the fetch script to
pull local copies:

```bash
cd docs/datasheets
./fetch_datasheets.sh
```

The script reports anything it could not download; vendors move URLs around, so
expect to fix one or two by hand over time. The links below are the canonical
landing pages and are the thing to trust if a direct PDF link rots.

---

## The kit

| Document | Where |
|---|---|
| XIAO nRF52840 & Wio-SX1262 Kit — product datasheet (PDF) | <https://files.seeedstudio.com/Bazaar/product_pdf/102010710.pdf> |
| Kit user guide / wiki | <https://wiki.seeedstudio.com/xiao_nrf52840&_wio_SX1262_kit_for_meshtastic/> |
| Product page (SKU 102010710) | <https://www.seeedstudio.com/XIAO-nRF52840-Wio-SX1262-Kit-for-Meshtastic-p-6400.html> |
| Meshtastic device page (pin maps, images) | <https://meshtastic.org/docs/hardware/devices/seeed-studio/wio-series/wio-sx1262s/> |

## Wio-SX1262 module

| Document | Where |
|---|---|
| Wio-SX1262 introduction & pinout | <https://wiki.seeedstudio.com/wio_sx1262/> |
| Wio-SX1262 with XIAO course pages | <https://wiki.seeedstudio.com/wio_sx1262_with_xiao_esp32s3_kit_class/> |

## Semtech SX1262 transceiver

| Document | Where |
|---|---|
| SX1261/2 datasheet, Rev 2.2 (Dec 2024) | <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DS_SX1261_2_V2-2.pdf> |
| SX1261/2 datasheet, Rev 1.2 (mirror) | <https://cdn.sparkfun.com/assets/6/b/5/1/4/SX1262_datasheet.pdf> |
| Semtech product page (registration-gated app notes) | <https://www.semtech.com/products/wireless-rf/lora-connect/sx1262> |

Chapters worth bookmarking: §13 (LoRa modem operation), §9.6 (RF switch and
DIO2 control), §13.1.14 `SetDIO3AsTCXOCtrl`, §5.1 (power consumption tables),
§6.1 (PA optimal settings for +22 dBm).

## Nordic nRF52840

| Document | Where |
|---|---|
| nRF52840 Product Specification | <https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html> |
| nRF52840 product page | <https://www.nordicsemi.com/Products/nRF52840> |
| S140 SoftDevice specification | <https://docs.nordicsemi.com/bundle/sds_s140/page/SDS/s1xx/keyfeatures_svc_call.html> |

## XIAO nRF52840 board

| Document | Where |
|---|---|
| XIAO nRF52840 wiki (pinout, schematic, KiCad footprint) | <https://wiki.seeedstudio.com/XIAO_BLE/> |
| Seeed nRF52 Arduino core (fork of Adafruit BSP) | <https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino> |

## Software

| Document | Where |
|---|---|
| RadioLib API documentation | <https://jgromes.github.io/RadioLib/> |
| RadioLib repository & examples | <https://github.com/jgromes/RadioLib> |
| RadioLib SX126x examples | <https://github.com/jgromes/RadioLib/tree/master/examples/SX126x> |
| Adafruit Bluefruit nRF52 API (BLE UART, BLEDis, BLEBas) | <https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide> |
| PlatformIO `nordicnrf52` platform | <https://docs.platformio.org/en/latest/platforms/nordicnrf52.html> |
| Meshtastic firmware (reference variant for this kit) | <https://github.com/meshtastic/firmware> |

## Regulatory — New Zealand

| Document | Where |
|---|---|
| RSM General User Radio Licences (incl. short range devices) | <https://www.rsm.govt.nz/licensing/spectrum-and-licensing/general-user-licences/> |
| NZ radio spectrum band plans | <https://www.rsm.govt.nz/about/spectrum-in-new-zealand/> |

The 915–928 MHz band is the relevant one for this project. Confirm the current
power and duty-cycle conditions before deploying a fixed installation.

---

## Adding to this folder

Drop new PDFs straight in here and add a row to the table above plus a line to
`fetch_datasheets.sh` so a fresh clone can rebuild the folder. Anything you
cannot redistribute (registration-gated Semtech app notes, for example) should
be listed as a link only.
