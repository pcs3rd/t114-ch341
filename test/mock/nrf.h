#ifndef MOCK_NRF_H
#define MOCK_NRF_H
#include <stdint.h>
typedef struct { uint32_t DEVICEID[2]; } mock_ficr_t;
extern mock_ficr_t mock_ficr;
#define NRF_FICR (&mock_ficr)
#define SPIM_FREQUENCY_FREQUENCY_M8 0x80000000UL
#endif
