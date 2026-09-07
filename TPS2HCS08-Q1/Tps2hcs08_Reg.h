#ifndef TPS2HCS08_REG_H
#define TPS2HCS08_REG_H

#include "ExVioDb_Tps2hcs08.h"
#include "ExVioDb.h"

/* =========================================================================
 * Channel
 * ========================================================================= */
#define TPS2HCS08_CH1                       (0u)
#define TPS2HCS08_CH2                       (1u)
#define TPS2HCS08_CH_MAX                    (2u)

/* =========================================================================
 * Shared register types
 *
 * IMPORTANT:
 * - Register unions and bit-fields are defined in ExVioDb_Tps2hcs08.h.
 * - Logical IC register fields are accessed through the bits member.
 * - Map functions only assign these fields.
 * - Hardware bit positions are handled only by BuildXXXPayload().
 * ========================================================================= */

/* =========================================================================
 * Payload builders
 *
 * No mask macros.
 * Bit shifts are isolated in this one file for static verification.
 * ========================================================================= */

D_STATIC inline uint16 Tps2hcs08_BuildLpmPayload(
    const tTps2hcs08Lpm *reg)
{
    uint16 payload = 0xFFF8u;

    payload |= (uint16)((uint16)reg->bits.AUTO_LPM_EXIT_CH2 << 2u);
    payload |= (uint16)((uint16)reg->bits.AUTO_LPM_EXIT_CH1 << 1u);

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildFaultMaskPayload(
    const tTps2hcs08FaultMask *reg)
{
    uint16 payload = 0xFF80u;

    payload |= (uint16)((uint16)reg->bits.MASK_SHRT_VBB << 5u);
    payload |= (uint16)((uint16)reg->bits.MASK_OL_OFF   << 4u);
    payload |= (uint16)((uint16)reg->bits.MASK_SPI_ERR  << 2u);
    payload |= (uint16)((uint16)reg->bits.MASK_WD_ERR   << 1u);
    payload |= (uint16)reg->bits.MASK_VBB_UVLO;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildSwStatePayload(
    const tTps2hcs08SwState *reg)
{
    uint16 payload = 0xFFFCu;

    payload |= (uint16)((uint16)reg->bits.CH2_ON << 1u);
    payload |= (uint16)reg->bits.CH1_ON;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildDevConfigPayload(
    const tTps2hcs08DevConfig *reg)
{
    uint16 payload = 0xF800u;

    payload |= (uint16)((uint16)reg->bits.CH2_LH_IN      << 9u);
    payload |= (uint16)((uint16)reg->bits.CH1_LH_IN      << 7u);
    payload |= (uint16)((uint16)reg->bits.PWM_SHIFT_DIS  << 6u);
    payload |= (uint16)((uint16)reg->bits.AUTO_LPM_ENTRY << 5u);
    payload |= (uint16)((uint16)reg->bits.PARALLEL_12   << 4u);
    payload |= (uint16)((uint16)reg->bits.WD_EN         << 3u);
    payload |= (uint16)((uint16)reg->bits.WD_TO         << 1u);
    payload |= (uint16)reg->bits.FLT_LTCH_DIS;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildAdcConfigPayload(
    const tTps2hcs08AdcConfig *reg)
{
    uint16 payload = 0xFF00u;

    payload |= (uint16)((uint16)reg->bits.ADC_ISNS_SAMPLE_CONFIG << 6u);
    payload |= (uint16)((uint16)reg->bits.ADC_VDS_DIS           << 5u);
    payload |= (uint16)((uint16)reg->bits.ADC_VSNS_DIS          << 4u);
    payload |= (uint16)((uint16)reg->bits.ADC_TSNS_DIS          << 3u);
    payload |= (uint16)((uint16)reg->bits.ADC_ISNS_DIS          << 2u);
    payload |= (uint16)((uint16)reg->bits.ADC_VBB_DIS           << 1u);
    payload |= (uint16)reg->bits.ADC_DIS;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildPwmPayload(
    const tTps2hcs08PwmCh *reg)
{
    uint16 payload = 0xF000u;

    payload |= (uint16)((uint16)reg->bits.PWM_FREQ_CHx << 9u);
    payload |= (uint16)((uint16)reg->bits.PWM_DTY_CHx << 1u);
    payload |= (uint16)reg->bits.PWM_EN_CHx;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildIlimPayload(
    const tTps2hcs08IlimConfigCh *reg)
{
    uint16 payload = 0u;

    payload |= (uint16)((uint16)reg->bits.CAP_CHRG_CHx        << 12u);
    payload |= (uint16)((uint16)reg->bits.I2T_EN_CHx          << 11u);
    payload |= (uint16)((uint16)reg->bits.INRUSH_DURATION_CHx << 8u);
    payload |= (uint16)((uint16)reg->bits.INRUSH_LIMIT_CHx    << 4u);
    payload |= (uint16)reg->bits.ILIMIT_SET_CHx;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildChConfigPayload(
    const tTps2hcs08ChConfig *reg)
{
    uint16 payload = 0u;

    payload |= (uint16)((uint16)reg->bits.VSNS_DIS_CHx     << 15u);
    payload |= (uint16)((uint16)reg->bits.VDS_SNS_DIS_CHx  << 14u);
    payload |= (uint16)((uint16)reg->bits.ISNS_DIS_CHx     << 13u);
    payload |= (uint16)((uint16)reg->bits.ISNS_SCALE_CHx   << 10u);
    payload |= (uint16)((uint16)reg->bits.OL_ON_EN_CHx      << 9u);
    payload |= (uint16)((uint16)reg->bits.OL_SVBB_BLANK_CHx << 7u);
    payload |= (uint16)((uint16)reg->bits.OL_PU_STR_CHx     << 5u);
    payload |= (uint16)((uint16)reg->bits.OL_SVBB_EN_CHx    << 3u);
    payload |= (uint16)((uint16)reg->bits.LATCH_CHx        << 2u);
    payload |= (uint16)reg->bits.SLRT_CHx;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildI2tPayload(
    const tTps2hcs08I2tConfigCh *reg)
{
    uint16 payload = 0u;

    payload |= (uint16)((uint16)reg->bits.TCLDN_CHx       << 14u);
    payload |= (uint16)((uint16)reg->bits.SWCL_DLY_TMR_CHx << 9u);
    payload |= (uint16)((uint16)reg->bits.ISWCL_CHx       << 7u);
    payload |= (uint16)((uint16)reg->bits.I2T_TRIP_CHx     << 3u);
    payload |= (uint16)reg->bits.NOM_CUR_CHx;

    return payload;
}

#endif /* TPS2HCS08_REG_H */
