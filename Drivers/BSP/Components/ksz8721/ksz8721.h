#pragma once

#include <stdint.h>

#define KSZ8721_STATUS_OK 0
#define KSZ8721_STATUS_ERROR -1
#define KSZ8721_STATUS_ADDRESS_ERROR -3
#define KSZ8721_STATUS_WRITE_ERROR -4
#define KSZ8721_STATUS_READ_ERROR -5
#define KSZ8721_STATUS_LINK_DOWN 1
#define KSZ8721_STATUS_100MBITS_FULLDUPLEX 2
#define KSZ8721_STATUS_100MBITS_HALFDUPLEX 3
#define KSZ8721_STATUS_10MBITS_FULLDUPLEX 4
#define KSZ8721_STATUS_10MBITS_HALFDUPLEX 5
#define KSZ8721_STATUS_AUTONEGO_NOTDONE 6

typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*WriteReg)(uint32_t address, uint32_t reg, uint32_t value);
    int32_t (*ReadReg)(uint32_t address, uint32_t reg, uint32_t *value);
    int32_t (*GetTick)(void);
} ksz8721_IOCtx_t;

typedef struct {
    uint32_t DevAddr;
    uint32_t Is_Initialized;
    ksz8721_IOCtx_t IO;
} ksz8721_Object_t;

int32_t KSZ8721_RegisterBusIO(ksz8721_Object_t *phy, const ksz8721_IOCtx_t *io);
int32_t KSZ8721_Init(ksz8721_Object_t *phy);
int32_t KSZ8721_GetLinkState(ksz8721_Object_t *phy);