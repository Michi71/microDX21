#include "tusb.h"

// --------------------------------------------------------------------
// Device Descriptor
// --------------------------------------------------------------------

tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0x1209,   // Community VID
    .idProduct          = 0xB000,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

// --------------------------------------------------------------------
// Configuration Descriptor
// --------------------------------------------------------------------

enum
{
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
#define EPNUM_MIDI        0x01

uint8_t const desc_fs_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64)
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_fs_configuration;
}

// --------------------------------------------------------------------
// String Descriptors
// --------------------------------------------------------------------

char const* string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },     // 0: English
    "VelvetKeys",                      // 1: Manufacturer
    "VelvetKeys USB-MIDI Bridge",      // 2: Product
    "VK-ADAPTER-001",                  // 3: Serial number
    "VelvetKeys MIDI Port",            // 4: Interface name
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    size_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (!(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])))
            return NULL;

        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++)
            _desc_str[1+i] = str[i];
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2*chr_count + 2);

    return _desc_str;
}
