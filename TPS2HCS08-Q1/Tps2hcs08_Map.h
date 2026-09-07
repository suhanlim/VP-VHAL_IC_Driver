#ifndef TPS2HCS08_MAP_H
#define TPS2HCS08_MAP_H

#include "Tps2hcs08_Reg.h"

/* =========================================================================
 * Signal DB IDs
 * ========================================================================= */

/* CAT_1 */
#define CAT1_E_FUSE_181000                  (8u)

/* CAT_2 */
#define CAT2_ACTIVE_HIGH                    (0u)

/* SC */
#define SC_1                                (1u)
#define SC_2                                (2u)

/* IC - mock topology IDs */
#define IC_0                                (0u)
#define IC_1                                (1u)
#define IC_2                                (2u)
#define IC_3                                (3u)

/* PIN */
#define IC_PIN_1                            (1u)
#define IC_PIN_2                            (2u)

/* USED */
#define USED_1                              (1u)
#define USED_2                              (2u)

/* MOC */
#define MOC_1A                              (1u)
#define MOC_3A                              (2u)
#define MOC_5A                              (3u)
#define MOC_10A                             (4u)
#define MOC_15A                             (5u)
#define MOC_20A                             (6u)
#define MOC_30A                             (7u)


/* OCP */
#define OCP_100mV                          (1u)
#define OCP_200mV                          (2u)
#define OCP_300mV                          (3u)
#define OCP_400mV                          (4u)
#define OCP_500mV                          (5u)
#define OCP_600mV                          (6u)
#define OCP_700mV                          (7u)
#define OCP_800mV                          (8u)   /* single-channel default */
#define OCP_9                              (9u)   /* parallel-mode default */
#define OCP_10                             (10u)
#define OCP_11                             (11u)

/* PWM */
#define PWM_O                               (0u)
#define PWM_X                               (1u)
#define PWM_C                               (2u)

/* OLD */
#define OLD_OFF                             (0u)
#define OLD_PWR                             (2u)

/* PWM_F */
#define PWM_40HZ                            (0u)
#define PWM_80HZ                            (1u)
#define PWM_100HZ                           (2u)
#define PWM_120HZ                           (3u)
#define PWM_200HZ                           (4u)
#define PWM_400HZ                           (5u)
#define PWM_800HZ                           (6u)
#define PWM_1000HZ                          (7u)

/* CT */
#define CT_5MS                              (0u)
#define CT_10MS                             (1u)
#define CT_15MS                             (2u)
#define CT_20MS                             (3u)
#define CT_25MS                             (4u)
#define CT_30MS                             (5u)
#define CT_40MS                             (6u)
#define CT_50MS                             (7u)

/* SR */
#define SR_1MA                              (0u)
#define SR_2MA                              (1u)
#define SR_4MA                              (2u)
#define SR_8MA                              (3u)

/* VOL_DET */
#define VOL_DET_OFF                         (0u)
#define VOL_DET_ON                          (1u)

/* DEF_VALUE */
#define DEF_IDLE                            (0u)
#define DEF_ACTIVE                          (1u)

/* PWM_Duty: supplied PWM_X table */
#define PWM_DUTY_0                          (0u)
#define PWM_DUTY_1                          (1u)
#define PWM_DUTY_2                          (2u)
#define PWM_DUTY_3                          (3u)
#define PWM_DUTY_4                          (4u)
#define PWM_DUTY_5                          (5u)
#define PWM_DUTY_6                          (6u)
#define PWM_DUTY_7                          (7u)
#define PWM_DUTY_8                          (8u)
#define PWM_DUTY_9                          (9u)
#define PWM_DUTY_10                         (10u)

/* =========================================================================
 * Public API
 *
 * Each mapping applies the supplied value to all TPS2HCS08_DEV_MAX devices.
 * Devices must have matching mapped register payloads for chain-wide writes.
 * Channel-specific mappings receive ch explicitly.
 * There is NO GetChannel() / implicit channel selection.
 * ========================================================================= */

void Tps2hcs08_Init(void);
void Tps2hcs08_SetSignalId(uint16 signalId);

/* DB metadata: no IC register write */
Std_ReturnType Tps2hcs08_MapCat1(uint8 id);
Std_ReturnType Tps2hcs08_MapCat2(uint8 id);
Std_ReturnType Tps2hcs08_MapSc(uint8 id);
Std_ReturnType Tps2hcs08_MapIc(uint8 id);
Std_ReturnType Tps2hcs08_MapPin(uint8 id);

/* DB -> IC register */
Std_ReturnType Tps2hcs08_MapUsed(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapMoc(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapOcp(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapRt(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapPwm(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapOld(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapPwmFreq(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapCt(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapSr(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapVolDet(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapDefValue(uint8 ch, uint8 id);
Std_ReturnType Tps2hcs08_MapPwmDuty(uint8 ch, uint8 id);


/* =========================================================================
 * Open / Short-to-VBB initial diagnostic
 *
 * One synchronous API:
 *
 *   Step1:
 *     OL_SVBB_EN_CHx = 2h
 *     read FLT_STAT_CHx.OL_OFF_CHx
 *
 *       OL_OFF = 0 -> NORMAL
 *       OL_OFF = 1 -> Step2
 *
 *   Step2:
 *     OL_SVBB_EN_CHx = 1h
 *     read FLT_STAT_CHx.SHRT_VBB_CHx
 *
 *       SHRT_VBB = 0 -> OPEN
 *       SHRT_VBB = 1 -> SHORT_VBB
 *
 * GND short is intentionally excluded from this initial diagnostic.
 * ========================================================================= */

typedef enum
{
    TPS2HCS08_DIAG_NORMAL = 0,
    TPS2HCS08_DIAG_OPEN,
    TPS2HCS08_OPEN_SHORT_DIAG_SHORT_VBB
} tTps2hcs08OpenShortResult;

Std_ReturnType Tps2hcs08_OpenShortDiag(
    uint8 ch,
    tTps2hcs08OpenShortResult *result);

/* Mock/static-verification helper for register-read contents.
 * Replace ExVioDb_ReadRegister_Tps2hcs08() with real SPI read later.
 */
Std_ReturnType Tps2hcs08_SetMockReadRegister(uint8 addr, uint16 payload);

/* Fixed register settings from project Write table */
Std_ReturnType Tps2hcs08_WriteFixedInitialConfig(void);

/* Chain transaction counters/results, represented by device zero. */
uint32 Tps2hcs08_GetWriteCount(void);
Std_ReturnType Tps2hcs08_GetLastWrite(uint8 *seqid,
                                      uint8 *addr,
                                      uint16 *payload);

#endif /* TPS2HCS08_MAP_H */
