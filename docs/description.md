# Remote USB Stick

This firmware lets you use the LilyGo T-Dongle S3 as a flash drive that can be populated over Wi-Fi through a browser interface.

## How It Works
After a successful Wi-Fi connection, the device is controlled through the web interface.
In this interface, you can switch between two modes: remote management and flash drive emulation.

### Remote Management Mode
In this mode, you can manage the memory card contents through the web interface:

1. View contents, navigate directories, see file sizes and names, delete existing files, and upload new files.
2. View drive status: used and free space, or a message that the SD card was not detected.
3. Format the memory as FAT32, which is the only format supported in the current version.
4. Switch the device to the second mode.

### Flash Drive Emulation Mode
In this mode, the device is detected as a regular flash drive.
The web interface remains available, but it allows only one action: switching the device back to the first mode.

## Usage Instructions
1. On first use, configure the device's Wi-Fi connection using the [connection setup guide](./wifi_configuration.md).
2. Connect the T-Dongle to the target device where you want to transfer files.
3. Open any browser and enter the IP address shown on the device screen.
4. Upload the required files.
5. Switch the device to flash drive emulation mode.

After that, the uploaded files will be available on the target device.
