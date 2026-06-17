# ESP32 C6 Storage Box

Browser-based firmware installer for the **ESP32-C6 Waveshare** board.

No Arduino IDE, PlatformIO, or desktop flashing software is required. Firmware can be installed directly from a supported web browser using ESP Web Tools.

---

## Created By

**SUCHIR KAUSHIK**

---

## Live Web Installer

### Flash Firmware

https://suchir2004.github.io/ESP32C6-STORAGE-BOX/

---

## Supported Hardware

- ESP32-C6 Waveshare
- Chip Family: ESP32-C6

---

## Features

- Browser-based firmware flashing
- No software installation required
- Uses ESP Web Tools
- Works over HTTPS
- Beginner friendly
- Lightweight and responsive interface
- One-click firmware installation

---

## Repository Structure

```text
ESP32-C6-Storage-Box/
│
├── index.html
├── manifest.json
│
└── firmware/
    ├── main.ino.bootloader.bin
    ├── main.ino.partitions.bin
    ├── boot_app0.bin
    └── main.ino.bin
```

---

## Firmware Files

| File | Offset |
|--------|--------|
| main.ino.bootloader.bin | 0x0000 |
| main.ino.partitions.bin | 0x8000 |
| boot_app0.bin | 0xe000 |
| main.ino.bin | 0x10000 |

---

## Installation Instructions

### Step 1

Connect your ESP32-C6 board to the computer using a USB data cable.

### Step 2

Open the web installer:

https://suchir2004.github.io/ESP32C6-STORAGE-BOX/

### Step 3

Click:

**Flash Firmware**

### Step 4

Choose the serial port.

### Step 5

Wait until flashing is completed.

### Step 6

Enjoy your ESP32 C6 Storage Box.

---

## Supported Browsers

✔ Google Chrome

✔ Microsoft Edge

---

## Browser Requirements

ESP Web Tools requires:

- HTTPS connection
- Web Serial support

Firefox and Safari are currently not supported.

---

## USB Drivers

Depending on the USB chip on your board, you may need one of the following drivers:

### CP210x Driver

https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

### CH340 Driver

https://sparks.gogo.co.nz/ch340.html

---

## Troubleshooting

### Device Not Detected

- Use a USB data cable.
- Try another USB port.
- Disconnect other serial devices.
- Close Arduino Serial Monitor.

---

### Failed to Connect

Hold the **BOOT** button while pressing **RESET**, then try flashing again.

---

### Failed to Download Manifest

Verify:

```text
https://suchir2004.github.io/ESP32C6-STORAGE-BOX/manifest.json
```

opens correctly.

---

### GitHub Pages 404 Error

Check:

- Repository name is correct.
- GitHub Pages is enabled.
- Files are uploaded to the main branch.
- Wait several minutes after enabling Pages.

---

### BIN File Path Verification

The following URLs should open directly:

Bootloader:

```text
https://suchir2004.github.io/ESP32C6-STORAGE-BOX/firmware/main.ino.bootloader.bin
```

Partitions:

```text
https://suchir2004.github.io/ESP32C6-STORAGE-BOX/firmware/main.ino.partitions.bin
```

Boot App:

```text
https://suchir2004.github.io/ESP32C6-STORAGE-BOX/firmware/boot_app0.bin
```

Application:

```text
https://suchir2004.github.io/ESP32C6-STORAGE-BOX/firmware/main.ino.bin
```

---

## Manual Bootloader Mode

If flashing fails:

1. Hold the BOOT button.
2. Press RESET.
3. Release RESET.
4. Release BOOT.
5. Retry flashing.

---

## Powered By

- ESP32-C6
- ESP Web Tools
- GitHub Pages

---

## License

This project is provided for educational and personal use.

---

# ESP32 C6 Storage Box

### Designed and developed by

# SUCHIR KAUSHIK