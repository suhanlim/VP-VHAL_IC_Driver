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
 * Typed software-shadow register structures
 *
 * IMPORTANT:
 * - These are NOT C bit-field structures.
 * - Each member is simply the logical IC register field value.
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

    payload |= (uint16)((uint16)reg->autoLpmExitCh2 << 2u);
    payload |= (uint16)((uint16)reg->autoLpmExitCh1 << 1u);

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildFaultMaskPayload(
    const tTps2hcs08FaultMask *reg)
{
    uint16 payload = 0xFF80u;

    payload |= (uint16)((uint16)reg->maskShrtVbb << 5u);
    payload |= (uint16)((uint16)reg->maskOlOff   << 4u);
    payload |= (uint16)((uint16)reg->maskSpiErr  << 2u);
    payload |= (uint16)((uint16)reg->maskWdErr   << 1u);
    payload |= (uint16)reg->maskVbbUvlo;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildSwStatePayload(
    const tTps2hcs08SwState *reg)
{
    uint16 payload = 0xFFFCu;

    payload |= (uint16)((uint16)reg->ch2On << 1u);
    payload |= (uint16)reg->ch1On;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildDevConfigPayload(
    const tTps2hcs08DevConfig *reg)
{
    uint16 payload = 0xF800u;

    payload |= (uint16)((uint16)reg->ch2LhIn      << 9u);
    payload |= (uint16)((uint16)reg->ch1LhIn      << 7u);
    payload |= (uint16)((uint16)reg->pwmShiftDis  << 6u);
    payload |= (uint16)((uint16)reg->autoLpmEntry << 5u);
    payload |= (uint16)((uint16)reg->parallel12   << 4u);
    payload |= (uint16)((uint16)reg->wdEn         << 3u);
    payload |= (uint16)((uint16)reg->wdTo         << 1u);
    payload |= (uint16)reg->fltLtchDis;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildAdcConfigPayload(
    const tTps2hcs08AdcConfig *reg)
{
    uint16 payload = 0xFF00u;

    payload |= (uint16)((uint16)reg->adcIsnsSampleConfig << 6u);
    payload |= (uint16)((uint16)reg->adcVdsDis           << 5u);
    payload |= (uint16)((uint16)reg->adcVsnsDis          << 4u);
    payload |= (uint16)((uint16)reg->adcTsnsDis          << 3u);
    payload |= (uint16)((uint16)reg->adcIsnsDis          << 2u);
    payload |= (uint16)((uint16)reg->adcVbbDis           << 1u);
    payload |= (uint16)reg->adcDis;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildPwmPayload(
    const tTps2hcs08PwmCh *reg)
{
    uint16 payload = 0xF000u;

    payload |= (uint16)((uint16)reg->pwmFreq << 9u);
    payload |= (uint16)((uint16)reg->pwmDuty << 1u);
    payload |= (uint16)reg->pwmEn;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildIlimPayload(
    const tTps2hcs08IlimConfigCh *reg)
{
    uint16 payload = 0u;

    payload |= (uint16)((uint16)reg->capChrg        << 12u);
    payload |= (uint16)((uint16)reg->i2tEn          << 11u);
    payload |= (uint16)((uint16)reg->inrushDuration << 8u);
    payload |= (uint16)((uint16)reg->inrushLimit    << 4u);
    payload |= (uint16)reg->ilimitSet;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildChConfigPayload(
    const tTps2hcs08ChConfig *reg)
{
    uint16 payload = 0u;

    payload |= (uint16)((uint16)reg->vsnsDis     << 15u);
    payload |= (uint16)((uint16)reg->vdsSnsDis  << 14u);
    payload |= (uint16)((uint16)reg->isnsDis     << 13u);
    payload |= (uint16)((uint16)reg->isnsScale   << 10u);
    payload |= (uint16)((uint16)reg->olOnEn      << 9u);
    payload |= (uint16)((uint16)reg->olSvbbBlank << 7u);
    payload |= (uint16)((uint16)reg->olPuStr     << 5u);
    payload |= (uint16)((uint16)reg->olSvbbEn    << 3u);
    payload |= (uint16)((uint16)reg->latch        << 2u);
    payload |= (uint16)reg->slrt;

    return payload;
}

D_STATIC inline uint16 Tps2hcs08_BuildI2tPayload(
    const tTps2hcs08I2tConfigCh *reg)
{
    uint16 payload = 0u;

    payload |= (uint16)((uint16)reg->tcldn       << 14u);
    payload |= (uint16)((uint16)reg->swclDlyTmr << 9u);
    payload |= (uint16)((uint16)reg->iswcl       << 7u);
    payload |= (uint16)((uint16)reg->i2tTrip     << 3u);
    payload |= (uint16)reg->nomCur;

    return payload;
}

#endif /* TPS2HCS08_REG_H */
