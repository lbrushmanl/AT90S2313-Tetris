#ifndef UTILS
#define UTILS

#define CONFIG_PIN(data_dir, pin)  (data_dir |= pin)
#define TOGGLE_PIN(port, pin)      (port ^= pin)
#define READ_PIN(pins, pin_num)     ((pins & _BV(pin_num)) >> pin_num) 
#define SET_OUTPUT_HIGH(port, pin) (port |= pin)
#define SET_OUTPUT_LOW(port, pin)  (port &= ~(pin))
#define SET_PIN(port, state)       (port = (port & state))
#define SET_PORT(port, data)       (port = data)
#define TO_BYTE16(data, lsb)       (((uint16_t) data[lsb + 1] << 8) | (uint16_t) data[lsb])
#define ARRAY_LEN(array)             (sizeof(array) / sizeof(array[0]))

#endif