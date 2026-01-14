// bacnet_config.h - Global BACnet stack configuration
#ifndef BACNET_CONFIG_H
#define BACNET_CONFIG_H

// Essential BACnet Stack Sizes (Standard defaults)
#define MAX_APDU                     1476
#define MAX_BITSTRING_BYTES          15
#define MAX_CHARACTER_STRING_BYTES   64
#define MAX_OCTET_STRING_BYTES       64

// Optional: Increase MAX_APDU if you have many objects (try 2048 if needed)
// #define MAX_APDU 2048

#endif // BACNET_CONFIG_H