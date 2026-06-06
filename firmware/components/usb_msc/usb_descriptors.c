// SPDX-License-Identifier: MIT
//
// USB descriptors for the MSC-only "Wireless Drive" device.

#include <stddef.h>
#include <stdint.h>

#include "tusb.h"

#define USB_VID  0xCAFE
#define USB_PID  0x4001

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL,
};

enum {
    EPNUM_MSC_OUT = 0x01,
    EPNUM_MSC_IN = 0x81,
};

enum {
    CONFIG_TOTAL_LEN = TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN,
};

tusb_desc_device_t const usb_msc_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

char const *usb_msc_string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "MyDevice",
    "Wireless Drive",
    "000000000000",
};

const size_t usb_msc_string_desc_count =
    sizeof(usb_msc_string_desc_arr) / sizeof(usb_msc_string_desc_arr[0]);

uint8_t const usb_msc_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};
