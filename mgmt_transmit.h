#ifndef MGMT_TRANSMIT_H_
#define MGMT_TRANSMIT_H_


#include "mgmt_types.h"

extern uint8_t SELFID;
extern uint32_t FREQ_INIT;
extern uint16_t POWER_INIT;
extern uint8_t BW_INIT;
extern uint8_t MCS_INIT;
extern uint16_t MACMODE_INIT;
extern double longitude;
extern double latitude;
//extern stMeshInfo meshinfo;
extern uint8_t NET_WORKMOD_INIT;
extern uint8_t RX_CHANNEL_MODE_INIT;
extern uint8_t SELFIP_s[4];
extern uint8_t DEVICETYPE_INIT;
extern uint32_t HOP_FREQ_TB_INIT[32];
extern uint8_t g_u8SLOTLEN;
extern volatile int g_config_dirty;
Smgmt_phy PHY_MSG;
void mgmt_system_init();
void mgmt_get_msg(void);

#endif