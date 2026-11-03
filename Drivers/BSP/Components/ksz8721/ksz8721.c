#include "ksz8721.h"

#ifndef ETH_PHY_ADDRESS
#define ETH_PHY_ADDRESS 1
#endif

#define KSZ8721_BCR 0
#define KSZ8721_BSR 1
#define KSZ8721_PHYID1 2
#define KSZ8721_PHYID2 3
#define KSZ8721_ANAR 4
#define KSZ8721_ANLPAR 5
#define KSZ8721_BCR_AUTONEGO 0x1000U
#define KSZ8721_BCR_RESTART 0x0200U
#define KSZ8721_BCR_POWER_DOWN 0x0800U
#define KSZ8721_BCR_ISOLATE 0x0400U
#define KSZ8721_BCR_100M 0x2000U
#define KSZ8721_BCR_FULL_DUPLEX 0x0100U
#define KSZ8721_BSR_LINK 0x0004U
#define KSZ8721_BSR_AUTONEGO_DONE 0x0020U
#define KSZ8721_ABILITY_100FD 0x0100U
#define KSZ8721_ABILITY_100HD 0x0080U
#define KSZ8721_ABILITY_10FD 0x0040U
#define KSZ8721_ABILITY_10HD 0x0020U

int32_t KSZ8721_RegisterBusIO(ksz8721_Object_t *phy, const ksz8721_IOCtx_t *io)
{
    if(!phy || !io || !io->ReadReg || !io->WriteReg)
        return KSZ8721_STATUS_ERROR;

    phy->IO = *io;
    return KSZ8721_STATUS_OK;
}

int32_t KSZ8721_Init(ksz8721_Object_t *phy)
{
    uint32_t id1, id2, control;

    if(phy->Is_Initialized)
        return KSZ8721_STATUS_OK;

    if(phy->IO.Init && phy->IO.Init() < 0)
        return KSZ8721_STATUS_ERROR;

    phy->DevAddr = ETH_PHY_ADDRESS;

    if(phy->IO.ReadReg(phy->DevAddr, KSZ8721_PHYID1, &id1) < 0 ||
       phy->IO.ReadReg(phy->DevAddr, KSZ8721_PHYID2, &id2) < 0)
        return KSZ8721_STATUS_READ_ERROR;

    if(id1 != 0x0022U || (id2 & 0xFFFCU) != 0x1618U)
        return KSZ8721_STATUS_ADDRESS_ERROR;

    if(phy->IO.ReadReg(phy->DevAddr, KSZ8721_BCR, &control) < 0)
        return KSZ8721_STATUS_READ_ERROR;

    control &= ~(KSZ8721_BCR_POWER_DOWN | KSZ8721_BCR_ISOLATE);
    control |= KSZ8721_BCR_AUTONEGO | KSZ8721_BCR_RESTART;

    if(phy->IO.WriteReg(phy->DevAddr, KSZ8721_BCR, control) < 0)
        return KSZ8721_STATUS_WRITE_ERROR;

    phy->Is_Initialized = 1;
    return KSZ8721_STATUS_OK;
}

int32_t KSZ8721_GetLinkState(ksz8721_Object_t *phy)
{
    uint32_t status, control, advertised, partner;

    if(!phy->Is_Initialized)
        return KSZ8721_STATUS_ERROR;

    if(phy->IO.ReadReg(phy->DevAddr, KSZ8721_BSR, &status) < 0 ||
       phy->IO.ReadReg(phy->DevAddr, KSZ8721_BSR, &status) < 0 ||
       phy->IO.ReadReg(phy->DevAddr, KSZ8721_BCR, &control) < 0)
        return KSZ8721_STATUS_READ_ERROR;

    if(!(status & KSZ8721_BSR_LINK))
        return KSZ8721_STATUS_LINK_DOWN;

    if(!(control & KSZ8721_BCR_AUTONEGO)) {
        if(control & KSZ8721_BCR_100M)
            return control & KSZ8721_BCR_FULL_DUPLEX ? KSZ8721_STATUS_100MBITS_FULLDUPLEX : KSZ8721_STATUS_100MBITS_HALFDUPLEX;
        return control & KSZ8721_BCR_FULL_DUPLEX ? KSZ8721_STATUS_10MBITS_FULLDUPLEX : KSZ8721_STATUS_10MBITS_HALFDUPLEX;
    }

    if(!(status & KSZ8721_BSR_AUTONEGO_DONE))
        return KSZ8721_STATUS_AUTONEGO_NOTDONE;

    if(phy->IO.ReadReg(phy->DevAddr, KSZ8721_ANAR, &advertised) < 0 ||
       phy->IO.ReadReg(phy->DevAddr, KSZ8721_ANLPAR, &partner) < 0)
        return KSZ8721_STATUS_READ_ERROR;

    uint32_t common = advertised & partner;

    if(common & KSZ8721_ABILITY_100FD)
        return KSZ8721_STATUS_100MBITS_FULLDUPLEX;
    if(common & KSZ8721_ABILITY_100HD)
        return KSZ8721_STATUS_100MBITS_HALFDUPLEX;
    if(common & KSZ8721_ABILITY_10FD)
        return KSZ8721_STATUS_10MBITS_FULLDUPLEX;
    if(common & KSZ8721_ABILITY_10HD)
        return KSZ8721_STATUS_10MBITS_HALFDUPLEX;

    return KSZ8721_STATUS_LINK_DOWN;
}