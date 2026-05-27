# Wi-Fi Connection Setup

## First Connection
For full operation, the device must connect to your local Wi-Fi network. On the first run, you need to provide the connection parameters: the network name (SSID) and password. If the credentials have not yet been provided to the device, you will see this message on the screen:

```
wifi.cfg created.
Please fill and reboot
```

To do this, connect the device to your PC. After the system detects it as a removable drive, you will find a text file named `wifi.cfg` on it.

The file contents must be:

```
ssid = your Wi-Fi network name
pass = access point password
```

Replace the text after `=` with your access point name and password respectively, then **save the file**.

> Note that the device works only in the 2.4 GHz band. 5 GHz networks are not supported. If you have a dual-band router and the access points use different names, make sure you specify the correct one.

After saving the file, eject and reconnect the device. It will try to connect using the specified parameters. If the connection succeeds, the device will continue booting normally and display an IP address on the screen. Open that address to interact with the device. The `wifi.cfg` file will be deleted, but its data will be saved to the device's internal memory.

If the connection fails, the LED will turn red. Check that the entered credentials are correct and that the specified access point is available.

## Changing or Resetting Connection Data
If you want to change the password or access point previously saved on the device, create a `wifi.cfg` file in the device root manually and fill it in as described in the first section.
