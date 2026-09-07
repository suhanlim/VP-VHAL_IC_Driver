/*******************************************************************************
 *  File            : ExVioDb_Tps2hcs08.c
 *  Component       : SWC_EXVIODB / EX_VIO_DB
 *  Target Device   : Texas Instruments TPS2HCS08-Q1 (SLVSHR0 - MAY 2025)
 *  Description     : TPS2HCS08-Q1 setup / run scan state machine.
 *                    The structure follows ExVioDb_SetupScnVnfd1248Reg().
 *
 *                    [ setup scan : EXVIODB_STATE_EXVIO_SETUP ]
 *                      SET_DEF -> DB_PARSING -> WAKEUP -> WAIT_READY
 *                      -> CLEAR_POR -> CONFIG_WRITE -> CONFIG_VERIFY
 *                      -> DIAG_PULLDOWN -> DIAG_PULLUP -> DIAG_JUDGE_OL
 *                      -> DIAG_JUDGE_STB -> DIAG_REPORT -> ACTIVE_ENTRY
 *                      -> COMPLETE
 *
 *                    [ run scan : EXVIODB_STATE_RUN ]
 *                      ACTIVE -> LPM_PREPARE -> LPM_ENTRY -> LPM_WAIT_STATUS
 *                      -> LPM_ACTIVE -> LPM_EXIT -> LPM_RESTORE -> ACTIVE
 *
 *  Called from     : RE_Swc_ExVioDb_Task_10ms()
 ******************************************************************************/

/*==============================================================================
 *  INCLUDES
 *============================================================================*/
#include "ExVioDb.h"                /* exVioDbRec[] / exVioDbMemCnt           */
#include "ExVioDb_Tps2hcs08.h"

/*==============================================================================
 *  LOCAL DEFINE
 *============================================================================*/
#ifndef D_STATIC
#define D_STATIC                        static
#endif

#define TPS2HCS08_TASK_PERIOD_MS        (10u)   /* Task_10ms                  */

#define TPS2HCS08_MS_TO_TICK(ms)        ((uint16)((ms) / TPS2HCS08_TASK_PERIOD_MS))

/* tREADY = 65us -> 1 task tick is enough (10ms)                              */
#define TPS2HCS08_TICK_READY            TPS2HCS08_MS_TO_TICK(10u)
/* discharge wait : 5 x tau ( tau = RSHRT_VBB 6.8k x COUT 100nF = 0.68ms )    */
#define TPS2HCS08_TICK_DISCHARGE        TPS2HCS08_MS_TO_TICK(10u)
/* blanking wait : OL_SVBB_BLANK = 4.0ms                                      */
#define TPS2HCS08_TICK_BLANK            TPS2HCS08_MS_TO_TICK(10u)
/* SPI watchdog periodic read : WD_TO = 400ms -> read every 100ms             */
#define TPS2HCS08_TICK_WD_READ          TPS2HCS08_MS_TO_TICK(100u)
/* AUTO_LPM entry monitoring timeout : 5s                                     */
#define TPS2HCS08_TICK_LPM_TIMEOUT      TPS2HCS08_MS_TO_TICK(5000u)
/* Phase 2: Issue #5 & #6 - Setup scan state timeout : 1s per blocking state  */
#define TPS2HCS08_TICK_STATE_TIMEOUT    TPS2HCS08_MS_TO_TICK(1000u)

/* register write skip mask ( DB parameter mapping failure )                  */
#define TPS2HCS08_SKIP_PWM              (0x0001u)   /* Eh  PWM_CHx            */
#define TPS2HCS08_SKIP_ILIM             (0x0002u)   /* Fh  ILIM_CONFIG_CHx    */
#define TPS2HCS08_SKIP_CH_CFG           (0x0004u)   /* 10h CHx_CONFIG         */
#define TPS2HCS08_SKIP_I2T              (0x0008u)   /* 15h I2T_CONFIG_CHx     */
#define TPS2HCS08_SKIP_DEV_CFG          (0x0010u)   /* 9h  DEV_CONFIG         */

/* my standard controller ID (SC parameter of the vehicle IO signal DB)       */
#ifndef EXVIODB_MY_SC_ID
#define EXVIODB_MY_SC_ID                (1u)
#endif

/*==============================================================================
 *  PORTING LAYER ( implemented by the MCAL / board layer )
 *      - CSN control is separated from the transfer because the wake up
 *        sequence needs a CSN low pulse without any SCLK.
 *============================================================================*/
extern Std_ReturnType ExVioDb_Tps2hcs08_Port_SpiTransfer(uint8 devIdx,
                                                  const uint8 *txData,
                                                  uint8 *rxData,
                                                  uint8 len);
extern void    ExVioDb_Tps2hcs08_Port_SetCsn(uint8 devIdx, boolean high);
extern boolean ExVioDb_Tps2hcs08_Port_GetFltPin(uint8 devIdx);  /* TRUE = LOW */
extern void    ExVioDb_Tps2hcs08_Port_NotifyLpmReady(boolean ready);

/*==============================================================================
 *  LOCAL VARIABLE
 *============================================================================*/
D_STATIC tTps2hcs08SetupScnState exVioDbTps2hcs08SetupScnState;
D_STATIC tTps2hcs08RunState      exVioDbTps2hcs08RunState;

D_STATIC tTps2hcs08Ctx           exVioDbTps2hcs08Ctx[TPS2HCS08_DEV_MAX];
D_STATIC tTps2hcs08SpiRuntime    exVioDbTps2hcs08SpiRuntime;

/* M-06: SDO header moved to context structure (tTps2hcs08Ctx.sdoHeader).
 * Previously was global array exVioDbTps2hcs08SdoHeader[].
 * Now properly encapsulated with other per-device data.
 */

/* Phase 2: Issue #2 & #7 - Retry counters for robustness.
 * Tracks retry attempts for each setup scan phase per device.
 * Prevents infinite loops on persistent SPI errors by enforcing max retry limits.
 */
D_STATIC tTps2hcs08RetryCounters exVioDbTps2hcs08Retry[TPS2HCS08_DEV_MAX];

/* M-09: Valid register address whitelist (28 registers).
 * Datasheet p.65 Table 8-13: addresses 0x06, 0x08, 0x0C, 0x1F~0x7F are RESERVED.
 * Attempting to write/read reserved addresses results in silent ignore by chip.
 * Reject invalid addresses early to catch configuration errors.
 */
D_STATIC const uint8 tps2hcs08ValidAddresses[] =
{
    TPS2HCS08_REG_DEV_ID,              /* 0x00 */
    TPS2HCS08_REG_CRC_CONFIG,          /* 0x01 */
    TPS2HCS08_REG_SLEEP,               /* 0x02 */
    TPS2HCS08_REG_LPM,                 /* 0x03 */
    TPS2HCS08_REG_GLOBAL_FAULT_TYPE,   /* 0x04 */
    TPS2HCS08_REG_FAULT_MASK,          /* 0x05 */
    /* 0x06 - RESERVED */
    TPS2HCS08_REG_SW_STATE,            /* 0x07 */
    /* 0x08 - RESERVED */
    TPS2HCS08_REG_DEV_CONFIG,          /* 0x09 */
    TPS2HCS08_REG_ADC_CONFIG,          /* 0x0A */
    TPS2HCS08_REG_ADC_RESULT_VBB,      /* 0x0B */
    /* 0x0C - RESERVED */
    TPS2HCS08_REG_FLT_STAT_CH1,        /* 0x0D */
    TPS2HCS08_REG_PWM_CH1,             /* 0x0E */
    TPS2HCS08_REG_ILIM_CONFIG_CH1,     /* 0x0F */
    TPS2HCS08_REG_CH1_CONFIG,          /* 0x10 */
    TPS2HCS08_REG_ADC_RESULT_CH1_I,    /* 0x11 */
    TPS2HCS08_REG_ADC_RESULT_CH1_T,    /* 0x12 */
    TPS2HCS08_REG_ADC_RESULT_CH1_V,    /* 0x13 */
    TPS2HCS08_REG_ADC_RESULT_CH1_VDS,  /* 0x14 */
    TPS2HCS08_REG_I2T_CONFIG_CH1,      /* 0x15 */
    TPS2HCS08_REG_FLT_STAT_CH2,        /* 0x16 */
    TPS2HCS08_REG_PWM_CH2,             /* 0x17 */
    TPS2HCS08_REG_ILIM_CONFIG_CH2,     /* 0x18 */
    TPS2HCS08_REG_CH2_CONFIG,          /* 0x19 */
    TPS2HCS08_REG_ADC_RESULT_CH2_I,    /* 0x1A */
    TPS2HCS08_REG_ADC_RESULT_CH2_T,    /* 0x1B */
    TPS2HCS08_REG_ADC_RESULT_CH2_V,    /* 0x1C */
    TPS2HCS08_REG_ADC_RESULT_CH2_VDS,  /* 0x1D */
    TPS2HCS08_REG_I2T_CONFIG_CH2       /* 0x1E */
    /* 0x1F~0x7F - RESERVED */
};

#define TPS2HCS08_VALID_ADDR_COUNT  (sizeof(tps2hcs08ValidAddresses) / sizeof(uint8))

/* register write skip mask by DB parsing result                              */
D_STATIC uint16                  exVioDbTps2hcs08SkipMask[TPS2HCS08_DEV_MAX][TPS2HCS08_CH_MAX];

D_STATIC uint16                  exVioDbTps2hcs08WaitTick;
D_STATIC uint16                  exVioDbTps2hcs08WdTick;
D_STATIC uint16                  exVioDbTps2hcs08LpmTick;

/* Phase 2: Issue #5 & #6 - Setup scan state timeout counter.
 * Prevents infinite loops in blocking states (WAIT_READY, CLEAR_POR, CONFIG_*, etc.)
 * Timeout triggers transition to ERROR state.
 */
D_STATIC uint16                  exVioDbTps2hcs08StateTimeout;

D_STATIC boolean                 exVioDbTps2hcs08SleepReq;
D_STATIC boolean                 exVioDbTps2hcs08WakeUpReq;
D_STATIC boolean                 exVioDbTps2hcs08ReCfgReq;

/* Phase 2: Issue #9 - Execution time monitoring statistics.
 * Tracks RunScan performance to detect timing issues.
 */
D_STATIC tTps2hcs08ExecStats     exVioDbTps2hcs08ExecStats;

/*==============================================================================
 *  VEHICLE IO SIGNAL DB -> IC REGISTER MAPPING TABLE
 *
 *  CAUTION : the tables below are built from the register enumeration of the
 *            TPS2HCS08-Q1 data sheet. When a DB parameter ID is not present in
 *            the table the related register write is skipped and the signal ID
 *            and the parameter name are printed as an error log.
 *============================================================================*/

/* OCP  -> ILIMIT_SET_CHx [3:0] (immediate shutdown overcurrent threshold)    */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapOcp[] =
{
    { 0u, 0x0u },   /* 10.0A  */
    { 1u, 0x1u },   /* 12.5A  */
    { 2u, 0x2u },   /* 15.0A  */
    { 3u, 0x3u },   /* 17.5A  */
    { 4u, 0x4u },   /* 20.0A  */
    { 5u, 0x5u },   /* 22.5A  */
    { 6u, 0x6u },   /* 25.0A  */
    { 7u, 0x7u },   /* 32.5A  */
    { 8u, 0x8u },   /* 40.0A  */
    { 9u, 0x9u },   /* 47.5A  */
    { 10u, 0xAu }   /* 55.0A  */
};

/* OCP  -> NOM_CUR_CHx [2:0] (I2T nominal current, RSNS = 700ohm)             */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapNomCur[] =
{
    { 0u, 0x0u },   /*  4.0A  */
    { 1u, 0x1u },   /*  5.0A  */
    { 2u, 0x2u },   /*  5.7A  */
    { 3u, 0x3u },   /*  6.5A  */
    { 4u, 0x4u },   /*  7.5A  */
    { 5u, 0x5u },   /*  9.0A  */
    { 6u, 0x6u },   /* 12.0A  */
    { 7u, 0x7u }    /* 15.0A  */
};

/* OCP  -> I2T_TRIP_CHx [6:3] (I2T trip energy, RSNS = 700ohm)                */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapI2tTrip[] =
{
    {  0u, 0x0u },  /*   8.8 A2s */
    {  1u, 0x1u },  /*  13.1 A2s */
    {  2u, 0x2u },  /*  26.3 A2s */
    {  3u, 0x3u },  /*  39.4 A2s */
    {  4u, 0x4u },  /*  52.5 A2s */
    {  5u, 0x5u },  /*  65.6 A2s */
    {  6u, 0x6u },  /*  78.8 A2s */
    {  7u, 0x7u },  /*  91.9 A2s */
    {  8u, 0x8u },  /* 109.4 A2s */
    {  9u, 0x9u },  /* 126.9 A2s */
    { 10u, 0xAu },  /* 144.4 A2s */
    { 11u, 0xBu },  /* 166.3 A2s */
    { 12u, 0xCu },  /* 192.5 A2s */
    { 13u, 0xDu },  /* 218.8 A2s */
    { 14u, 0xEu },  /* 262.5 A2s */
    { 15u, 0xFu }   /* 350.0 A2s */
};

/* OCP  -> ISWCL_CHx [8:7] (delayed turn off current, RSNS = 700ohm)          */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapIswcl[] =
{
    { 0u, 0x0u },   /* 19.55A */
    { 1u, 0x1u },   /* 17.60A */
    { 2u, 0x2u },   /* 16.05A */
    { 3u, 0x3u }    /* 13.30A */
};

/* PWM_F -> PWM_FREQ_CHx [11:9]                                               */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapPwmFreq[] =
{
    { 0u, 0x0u },   /*    0.8 Hz */
    { 1u, 0x1u },   /*    3.4 Hz */
    { 2u, 0x2u },   /*   13.8 Hz */
    { 3u, 0x3u },   /*  111   Hz */
    { 4u, 0x4u },   /*  221   Hz */
    { 5u, 0x5u },   /*  425   Hz */
    { 6u, 0x6u },   /*  885   Hz */
    { 7u, 0x7u }    /* 1770   Hz */
};

/* CT   -> INRUSH_DURATION_CHx [10:8] (capacitive charging time)              */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapInrushDur[] =
{
    { 0u, 0x0u },   /*   0 ms */
    { 1u, 0x1u },   /*   2 ms */
    { 2u, 0x2u },   /*   4 ms */
    { 3u, 0x3u },   /*   6 ms */
    { 4u, 0x4u },   /*  10 ms */
    { 5u, 0x5u },   /*  20 ms */
    { 6u, 0x6u },   /*  50 ms */
    { 7u, 0x7u }    /* 100 ms */
};

/* SR   -> SLRT_CHx [1:0] (output slew rate)                                  */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapSlrt[] =
{
    { 0u, 0x0u },   /* 0.25 V/us */
    { 1u, 0x1u },   /* 0.34 V/us */
    { 2u, 0x2u },   /* 0.45 V/us */
    { 3u, 0x3u }    /* 0.55 V/us */
};

/* PWM_Duty -> INRUSH_LIMIT_CHx [7:4] when PWM parameter is PWM_C             */
/*             ( CAP_CHRG_CHx = 10 : current limit regulation, 1.6A ~ 12A )   */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapInrushLimitC[] =
{
    {  0u, 0x0u },  /*  1.6A */
    {  1u, 0x1u },  /*  2.0A */
    {  2u, 0x2u },  /*  2.4A */
    {  3u, 0x3u },  /*  2.8A */
    {  4u, 0x4u },  /*  3.3A */
    {  5u, 0x5u },  /*  3.6A */
    {  6u, 0x6u },  /*  4.2A */
    {  7u, 0x7u },  /*  5.5A */
    {  8u, 0x8u },  /*  6.8A */
    {  9u, 0x9u },  /*  8.1A */
    { 10u, 0xAu },  /*  9.5A */
    { 11u, 0xBu },  /* 11.0A */
    { 12u, 0xCu }   /* 12.0A */
};

/* PWM_Duty -> INRUSH_LIMIT_CHx [7:4] when PWM parameter is PWM_X             */
/*             ( CAP_CHRG_CHx = 00 : immediate shutdown, 10A ~ 55A )          */
D_STATIC const tTps2hcs08MapEntry exVioDbTps2hcs08MapInrushLimitX[] =
{
    {  0u, 0x0u },  /* 10.0A */
    {  1u, 0x1u },  /* 12.5A */
    {  2u, 0x2u },  /* 15.0A */
    {  3u, 0x3u },  /* 17.5A */
    {  4u, 0x4u },  /* 20.0A */
    {  5u, 0x5u },  /* 22.5A */
    {  6u, 0x6u },  /* 25.0A */
    {  7u, 0x7u },  /* 32.5A */
    {  8u, 0x8u },  /* 40.0A */
    {  9u, 0x9u },  /* 47.5A */
    { 10u, 0xAu }   /* 55.0A */
};

/*==============================================================================
 *  FAULT BIT -> LOG STRING TABLE
 *============================================================================*/
typedef struct
{
    uint8        bitPos;
    const char  *name;
} tTps2hcs08FaultLogEntry;

D_STATIC const tTps2hcs08FaultLogEntry exVioDbTps2hcs08GlobalLogTbl[] =
{
    { TPS2HCS08_GF_BIT_CH2_FLT,          "CH2_FLT"              },
    { TPS2HCS08_GF_BIT_CH1_FLT,          "CH1_FLT"              },
    { TPS2HCS08_GF_BIT_CHAN_OCP_I2T_TSD, "CHAN_OCP_I2T_TSD"     },
    { TPS2HCS08_GF_BIT_GLOBAL_ERR_WRN,   "GLOBAL_ERR_WRN"       },
    { TPS2HCS08_GF_BIT_POR,              "POR"                  },
    { TPS2HCS08_GF_BIT_LPM_STATUS_1,     "LPM_STATUS_1"         },
    { TPS2HCS08_GF_BIT_SPI_ERR,          "SPI_ERR"              },
    { TPS2HCS08_GF_BIT_WD_ERR,           "WD_ERR"               },
    { TPS2HCS08_GF_BIT_VDD_UVLO,         "VDD_UVLO"             },
    { TPS2HCS08_GF_BIT_VBB_UV_WRN,       "VBB_UV_WRN"           },
    { TPS2HCS08_GF_BIT_VBB_UVLO,         "VBB_UVLO"             }
};

D_STATIC const tTps2hcs08FaultLogEntry exVioDbTps2hcs08ChLogTbl[] =
{
    { TPS2HCS08_FS_BIT_FLT_CH,       "FLT_CHx"          },
    { TPS2HCS08_FS_BIT_VOUT_ERR,     "VOUT_ERR_CHx"     },
    { TPS2HCS08_FS_BIT_I2T_FLT,      "I2T_FLT_CHx"      },
    { TPS2HCS08_FS_BIT_THERMAL_SD,   "THERMAL_SD_CHx"   },
    { TPS2HCS08_FS_BIT_ILIMIT,       "ILIMIT_CHx"       },
    { TPS2HCS08_FS_BIT_THERMAL_WRN,  "THERMAL_WRN_CHx"  }
};

/*==============================================================================
 *  LOCAL FUNCTION PROTOTYPE
 *============================================================================*/
/* --- SPI access ---------------------------------------------------------- */
D_STATIC boolean        IsValidRegisterAddress_Tps2hcs08(uint8 addr);
D_STATIC void           ExVioDb_ValidateSdoHeader_Tps2hcs08(uint8 devIdx, uint8 sdoHeader);
D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(uint8 devIdx, uint8 addr, uint16 payload);
D_STATIC Std_ReturnType ExVioDb_ReadRegister_Tps2hcs08(uint8 devIdx, uint8 addr, uint16 *readValue);
D_STATIC uint16        *ExVioDb_GetWritableShadowPtr_Tps2hcs08(uint8 devIdx, uint8 addr);

/* --- DB parsing ---------------------------------------------------------- */
D_STATIC void           ExVioDb_ParsingOutputTps2hcs08Reg(uint16 sigIndex);
D_STATIC Std_ReturnType ExVioDb_MapDbParam_Tps2hcs08(const tTps2hcs08MapEntry *tbl,
                                                     uint8 tblSize, uint8 dbId,
                                                     uint8 *regValue,
                                                     uint16 sigIndex,
                                                     const char *paramName);

/* --- state action -------------------------------------------------------- */
D_STATIC void  ExVioDb_WakeUp_Tps2hcs08(void);
D_STATIC uint8 ExVioDb_WaitReadyDone_Tps2hcs08(void);
D_STATIC uint8 ExVioDb_ClearPorFault_Tps2hcs08(void);
D_STATIC uint8 ExVioDb_WriteConfig_Tps2hcs08(void);
D_STATIC uint8 ExVioDb_VerifyConfig_Tps2hcs08(void);
D_STATIC void  ExVioDb_DiagSetPullDown_Tps2hcs08(void);
D_STATIC void  ExVioDb_DiagSetPullUp_Tps2hcs08(void);
D_STATIC uint8 ExVioDb_DiagJudgeOpenLoad_Tps2hcs08(void);
D_STATIC uint8 ExVioDb_DiagJudgeShortVbb_Tps2hcs08(void);
D_STATIC void  ExVioDb_DiagReport_Tps2hcs08(void);
D_STATIC void  ExVioDb_ActiveEntry_Tps2hcs08(void);

/* --- run scan action ----------------------------------------------------- */
D_STATIC boolean ExVioDb_IsFltPinLow_Tps2hcs08(void);
D_STATIC void  ExVioDb_WdRead_Tps2hcs08(void);
D_STATIC void  ExVioDb_ReadAdcResult_Tps2hcs08(uint8 devIdx, uint8 chIdx);
D_STATIC void  ExVioDb_SetAutoLpmEntry_Tps2hcs08(boolean enable);
D_STATIC uint8 ExVioDb_CheckLpmStatus_Tps2hcs08(void);
D_STATIC void  ExVioDb_SetAutoLpmExit_Tps2hcs08(boolean exit);

/* --- fault log ----------------------------------------------------------- */
D_STATIC void  ExVioDb_EvalGlobalFaultLog_Tps2hcs08(uint8 devIdx);
D_STATIC void  ExVioDb_EvalChFaultLog_Tps2hcs08(uint8 devIdx, uint8 chIdx);

/*******************************************************************************
 *  SECTION 1 : SPI ACCESS
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_GetWritableShadowPtr_Tps2hcs08
 *      Maps a writable register address to the runtime shadow word.
 *      Read-only/reserved registers return NULL_PTR and are rejected before SPI.
 *      SLEEP(2h) is a one-shot command register; keep it out of this shadow map
 *      and add a dedicated EnterSleep function if SLEEP command support is added.
 *----------------------------------------------------------------------------*/
D_STATIC uint16 *ExVioDb_GetWritableShadowPtr_Tps2hcs08(uint8 devIdx, uint8 addr)
{
    uint16        *pShadow = NULL_PTR;
    tTps2hcs08Ctx *pCtx;

    if (devIdx < TPS2HCS08_DEV_MAX)
    {
        pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        switch (addr)
        {
            case TPS2HCS08_REG_LPM:
                pShadow = &pCtx->lpm.word;
                break;

            case TPS2HCS08_REG_FAULT_MASK:
                pShadow = &pCtx->faultMask.word;
                break;

            case TPS2HCS08_REG_SW_STATE:
                pShadow = &pCtx->swState.word;
                break;

            case TPS2HCS08_REG_DEV_CONFIG:
                pShadow = &pCtx->devConfig.word;
                break;

            case TPS2HCS08_REG_ADC_CONFIG:
                pShadow = &pCtx->adcConfig.word;
                break;

            case TPS2HCS08_REG_PWM_CH1:
                pShadow = &pCtx->pwmCh[TPS2HCS08_CH1].word;
                break;

            case TPS2HCS08_REG_PWM_CH2:
                pShadow = &pCtx->pwmCh[TPS2HCS08_CH2].word;
                break;

            case TPS2HCS08_REG_ILIM_CONFIG_CH1:
                pShadow = &pCtx->ilimCfgCh[TPS2HCS08_CH1].word;
                break;

            case TPS2HCS08_REG_ILIM_CONFIG_CH2:
                pShadow = &pCtx->ilimCfgCh[TPS2HCS08_CH2].word;
                break;

            case TPS2HCS08_REG_CH1_CONFIG:
                pShadow = &pCtx->chConfig[TPS2HCS08_CH1].word;
                break;

            case TPS2HCS08_REG_CH2_CONFIG:
                pShadow = &pCtx->chConfig[TPS2HCS08_CH2].word;
                break;

            case TPS2HCS08_REG_I2T_CONFIG_CH1:
                pShadow = &pCtx->i2tCfgCh[TPS2HCS08_CH1].word;
                break;

            case TPS2HCS08_REG_I2T_CONFIG_CH2:
                pShadow = &pCtx->i2tCfgCh[TPS2HCS08_CH2].word;
                break;

            default:
                /* read-only, reserved, or one-shot command register */
                break;
        }
    }

    return pShadow;
}

/*------------------------------------------------------------------------------
 *  IsValidRegisterAddress_Tps2hcs08
 *      M-09: Validates register address against whitelist.
 *      Rejects reserved addresses (0x06, 0x08, 0x0C, 0x1F~0x7F).
 *----------------------------------------------------------------------------*/
D_STATIC boolean IsValidRegisterAddress_Tps2hcs08(uint8 addr)
{
    uint8 i;

    for (i = 0u; i < TPS2HCS08_VALID_ADDR_COUNT; i++)
    {
        if (tps2hcs08ValidAddresses[i] == addr)
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ValidateSdoHeader_Tps2hcs08
 *      [DEACTIVATED] SDO header interpretation deferred until integration validation.
 *
 *      Rationale:
 *      - Hardware verification required: SLEEP wake-up test will capture first
 *        transaction header to confirm bit mapping (POR bit position determines
 *        whether rxBuf[0] = GFT[15:8] or GFT[7:0]).
 *      - Fault detection already covered by periodic GLOBAL_FAULT_TYPE READ (#10).
 *      - Premature header-based logic risks false positives/negatives before mapping
 *        is verified, complicating diagnostics.
 *
 *      Datasheet-specified mapping (p.26~27, p.71~73) to implement after validation:
 *        rxBuf[0] = GLOBAL_FAULT_TYPE[15:8], latched at CS falling edge
 *        bit0 = GLOBAL_ERR_WRN       (GFT[8])
 *        bit1 = OL_SHRT_VBB_OFF_FLT  (GFT[9])
 *        bit2 = CHAN_OCP_I2T_TSD     (GFT[10])
 *        bit3 = LPM_STATUS           (GFT[11])  <- Process #14 polling target
 *        bit4 = CH1_FLT              (GFT[12])
 *        bit5 = CH2_FLT              (GFT[13])
 *        bit7:6 = RESERVED           (GFT[15:14])
 *
 *      TODO [M-19]: After hardware validation, restore this function with correct
 *      [15:8] mapping and connect to Process #14 (LPM_STATUS polling).
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_ValidateSdoHeader_Tps2hcs08(uint8 devIdx, uint8 sdoHeader)
{
    /* DEACTIVATED - see function header comment */
    (void)devIdx;
    (void)sdoHeader;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_WriteRegister_Tps2hcs08
 *      24bit write frame : [23]=1 [22:16]=ADDR [15:0]=DATA
 *----------------------------------------------------------------------------*/
D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(uint8 seqid, uint8 addr, uint16 payload)
{
    uint8           txBuf[TPS2HCS08_CHAIN_BUF_LEN_MAX] = exVioDbTps2hcs08SpiRuntime.txData;
    uint8           rxBuf[TPS2HCS08_CHAIN_BUF_LEN_MAX] = exVioDbTps2hcs08SpiRuntime.rxData;
    uint16         *pShadow;
    Std_ReturnType  retVal = E_NOT_OK;

    /* M-09: Validate register address first */
    if (IsValidRegisterAddress_Tps2hcs08(addr) == FALSE)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] INVALID REGISTER ADDRESS (RESERVED): dev=%d addr=0x%02X\r\n",
            seqid, addr);
        return E_NOT_OK;
    }

	// 한개의 세트에 데이터를 채우는 로직은 적절하지만 SPI 통신을 4번 하는게 아닌 데이터 4개를 이어붙여서 한번에 보내야 함
	pShadow = ExVioDb_GetWritableShadowPtr_Tps2hcs08(seqid, addr);
	if (pShadow != NULL_PTR)
	{
        for (int i = 0; i < TPS2HCS08_DEV_MAX; i++) 
        {
            txBuf[i] = (uint8)(TPS2HCS08_SPI_CMD_WRITE | (addr & TPS2HCS08_SPI_ADDR_MASK));
		    txBuf[i+1] = (uint8)((payload >> 8u) & 0x00FFu);
		    txBuf[i+2] = (uint8)(payload & 0x00FFu);
        }

		if (ExVioDb_Tps2hcs08_Port_SpiTransfer(seqid, txBuf, rxBuf, TPS2HCS08_SPI_FRAME_LEN) == E_OK)
		{
			*pShadow = payload;
			retVal = E_OK;
		}
		else
		{
			TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
				"[TPS2HCS08] SPI WRITE FAIL. dev=%d addr=0x%02X\r\n", seqid, addr);
		}
	}
	else
	{
		TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
			"[TPS2HCS08] INVALID WRITE REGISTER. dev=%d addr=0x%02X\r\n", seqid, addr);
	}

	return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ReadRegister_Tps2hcs08
 *      The 16bit "Data Out" of SDO is always the data of the PREVIOUS SPI
 *      frame (Figure 8-8), therefore a read needs 2 transactions.
 *----------------------------------------------------------------------------*/
// TODO: 로직 수정 필요 SPI 데이터 프레임 생성 로직 수정 필요 + SPI 요청 2번 이유 확인 필요
D_STATIC Std_ReturnType ExVioDb_ReadRegister_Tps2hcs08(uint8 seqid, uint8 addr, uint16 *readValue)
{
    uint8           txBuf[TPS2HCS08_CHAIN_BUF_LEN_MAX] = exVioDbTps2hcs08SpiRuntime.txData;
    uint8           rxBuf[TPS2HCS08_CHAIN_BUF_LEN_MAX] = exVioDbTps2hcs08SpiRuntime.rxData;
    Std_ReturnType  retVal = E_NOT_OK;

    /* M-09: Validate register address first */
    if (IsValidRegisterAddress_Tps2hcs08(addr) == FALSE)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] INVALID REGISTER ADDRESS (RESERVED): dev=%d addr=0x%02X\r\n",
            seqid, addr);
        return E_NOT_OK;
    }

    if (readValue != NULL_PTR)
    {
		for (int i = 0; i < TPS2HCS08_DEV_MAX; i++)
		{
			txBuf[0] = (uint8)(TPS2HCS08_SPI_CMD_READ | (addr & TPS2HCS08_SPI_ADDR_MASK));
			/* M-12: Read frame data bytes must be 0x00 (datasheet p.27).
			 * Write frame uses actual data, read frame ignores these bytes.
			 */
			txBuf[1] = 0x00u;
			txBuf[2] = 0x00u;
		}

        /* 4개의 데이지 체인 구조 모두 동일한 동작결과를 기대함으로 1개의 프레임 결과만 반환                                 */
        if (ExVioDb_Tps2hcs08_Port_SpiTransfer(seqid, txBuf, rxBuf, TPS2HCS08_SPI_FRAME_LEN) == E_OK)
        {
           *readValue = (uint16)(((uint16)rxBuf[1] << 8u) | (uint16)rxBuf[2]);
            retVal = E_OK;
        }

        if (retVal != E_OK)
        {
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] SPI READ FAIL. dev=%d addr=0x%02X\r\n", seqid, addr);
        }
    }

    return retVal;
}

/*******************************************************************************
 *  SECTION 2 : REGISTER DEFAULT VALUE / DB PARSING
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_InitRegValue_Tps2hcs08
 *      Sets the shadow register of every device to the project default value.
 *      ( "초기 설정" column of the register specification )
 *----------------------------------------------------------------------------*/
void ExVioDb_InitRegValue_Tps2hcs08(void)
{
    uint8 devIdx;
    uint8 chIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        /* --- 5h FAULT_MASK : initial diagnostic only -> mask after setup --- */
        /* EDIT::Init 이슈 pdf 74p 기준 pCtx->faultMask.word = 0xFF80u 수정      */
        pCtx->faultMask.word                      = 0xFF80u;
        pCtx->faultMask.bits.MASK_SHRT_VBB        = 1u;
        pCtx->faultMask.bits.MASK_OL_OFF          = 1u;
        pCtx->faultMask.bits.MASK_SPI_ERR         = 0u;
        pCtx->faultMask.bits.MASK_WD_ERR          = 0u;
        pCtx->faultMask.bits.MASK_VBB_UVLO        = 0u;

        /* --- 7h SW_STATE : all output OFF --------------------------------- */
        /* EDIT::Init 이슈 pdf 75p 기준 pCtx->swState.word = 0xFFFCu 수정      */
        pCtx->swState.word                        = 0xFFFCu;

        /* --- 9h DEV_CONFIG ------------------------------------------------ */
        /* EDIT::Init 이슈 pdf 76p 기준 pCtx->devConfig.word = 0xF800u 수정      */
        pCtx->devConfig.word                      = 0xF800u;
        pCtx->devConfig.bits.CH2_LH_IN            = TPS2HCS08_LH_IN_KEEP_CHx_ON;
        pCtx->devConfig.bits.CH1_LH_IN            = TPS2HCS08_LH_IN_KEEP_CHx_ON;
        pCtx->devConfig.bits.PWM_SHIFT_DIS        = 0u;
        pCtx->devConfig.bits.AUTO_LPM_ENTRY       = 0u;   /* process #12       */
        pCtx->devConfig.bits.PARALLEL_12          = 0u;   /* from signal DB    */
        pCtx->devConfig.bits.WD_EN                = 1u;
        pCtx->devConfig.bits.WD_TO                = TPS2HCS08_WD_TO_400MS;
        pCtx->devConfig.bits.FLT_LTCH_DIS         = 0u;   /* latched fault     */

        /* --- Ah ADC_CONFIG ( reset = FF3Ah ) ------------------------------ */
        pCtx->adcConfig.word                      = 0xFF3Au;
        pCtx->adcConfig.bits.ADC_VSNS_DIS         = 0u;   /* VSNS enable       */
        pCtx->adcConfig.bits.ADC_ISNS_DIS         = 0u;   /* ISNS enable(I2T)  */
        pCtx->adcConfig.bits.ADC_DIS              = 0u;

        /* M-03: VBB measurement configuration.
         * Reset value has ADC_VBB_DIS=1 (disabled).
         * Enable only if project requires VBB monitoring.
         * If enabled, must read ADC_RESULT_VBB (Bh) in periodic diagnostics.
         */
#if defined(TPS2HCS08_USE_VBB_MEASUREMENT)
        pCtx->adcConfig.bits.ADC_VBB_DIS          = 0u;   /* Enable VBB measurement */
#else
        /* VBB measurement disabled (reset default = 1) - no action needed */
        /* If VBB measurement needed in future, define TPS2HCS08_USE_VBB_MEASUREMENT */
#endif

        /* --- 3h LPM ------------------------------------------------------- */
        /* EDIT::Init 이슈 pdf 70p 기준 pCtx->lpm.word = 0xFF80u 수정          */
        pCtx->lpm.word                            = 0xFF80u;

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            /* --- Eh PWM_CHx ---------------------------------------------- */
            /* EDIT::Init 이슈 pdf 83p 기준 pCtx->pwmCh[chIdx].word = 0xF000u 수정 */
            pCtx->pwmCh[chIdx].word               = 0xF000u;

            /* --- Fh ILIM_CONFIG_CHx ( reset = 0088h ) -------------------- */
            pCtx->ilimCfgCh[chIdx].word           = 0x0000u;
            pCtx->ilimCfgCh[chIdx].bits.CAP_CHRG_CHx        = TPS2HCS08_CAP_CHRG_NONE;
            /* M-04: I2T_EN initially disabled for safety.
             * I2T_TRIP=0h + I2T_EN=1 → minimum 8.8A²s active (datasheet p.93)
             * Could cause unintended trip during init before DB parsing.
             * DB parsing will enable I2T_EN if required.
             */
            pCtx->ilimCfgCh[chIdx].bits.I2T_EN_CHx          = 0u;  /* Disable until DB parsed */
            pCtx->ilimCfgCh[chIdx].bits.INRUSH_DURATION_CHx = 0u;
            pCtx->ilimCfgCh[chIdx].bits.INRUSH_LIMIT_CHx    = 0x8u;  /* 40A    */
            pCtx->ilimCfgCh[chIdx].bits.ILIMIT_SET_CHx      = 0x8u;  /* 40A    */

            /* --- 10h CHx_CONFIG ------------------------------------------ */
            pCtx->chConfig[chIdx].word            = 0x0000u;
            pCtx->chConfig[chIdx].bits.VSNS_DIS_CHx      = 1u;  /* from DB     */
            pCtx->chConfig[chIdx].bits.VDS_SNS_DIS_CHx   = 1u;
            pCtx->chConfig[chIdx].bits.ISNS_DIS_CHx      = 0u;
            pCtx->chConfig[chIdx].bits.ISNS_SCALE_CHx    = 0u;
            pCtx->chConfig[chIdx].bits.OL_ON_EN_CHx      = 0u;
            pCtx->chConfig[chIdx].bits.OL_SVBB_BLANK_CHx = TPS2HCS08_OL_BLANK_4P0MS;
            pCtx->chConfig[chIdx].bits.OL_PU_STR_CHx     = TPS2HCS08_OL_PU_STR_26U5A;
            pCtx->chConfig[chIdx].bits.OL_SVBB_EN_CHx    = TPS2HCS08_OL_SVBB_PULLUP;
            pCtx->chConfig[chIdx].bits.LATCH_CHx         = 0u;  /* auto retry  */
            pCtx->chConfig[chIdx].bits.SLRT_CHx          = 0x2u;/* 0.45V/us    */

            /* --- 15h I2T_CONFIG_CHx -------------------------------------- */
            pCtx->i2tCfgCh[chIdx].word            = 0x0000u;
            pCtx->i2tCfgCh[chIdx].bits.TCLDN_CHx        = TPS2HCS08_TCLDN_2P0S;
            pCtx->i2tCfgCh[chIdx].bits.SWCL_DLY_TMR_CHx = 0u;
            pCtx->i2tCfgCh[chIdx].bits.ISWCL_CHx        = 0u;
            pCtx->i2tCfgCh[chIdx].bits.I2T_TRIP_CHx     = 0u;
            pCtx->i2tCfgCh[chIdx].bits.NOM_CUR_CHx      = 0u;

            /* --- DB parsing result / diagnostic result -------------------- */
            pCtx->chCfg[chIdx].used               = FALSE;
            pCtx->chCfg[chIdx].sigIdx             = 0u;
            pCtx->chCfg[chIdx].bPlusAlways        = FALSE;
            pCtx->chCfg[chIdx].defValueOn         = FALSE;
            pCtx->chCfg[chIdx].parallel           = FALSE;
            pCtx->chCfg[chIdx].volDetUse          = FALSE;
            pCtx->chCfg[chIdx].oldUse             = FALSE;
            pCtx->chCfg[chIdx].pwmType            = DB_PWM_TYPE_NONE;

            pCtx->diagResult[chIdx]               = TPS2HCS08_DIAG_NOT_EXECUTED;
            pCtx->logLatchCh[chIdx]               = 0u;

            exVioDbTps2hcs08SkipMask[devIdx][chIdx] = 0u;
        }

        pCtx->globalFault.word     = 0u;
        pCtx->logLatchGlobal       = 0u;
        pCtx->porCleared           = FALSE;
        pCtx->lpmStatus1Cleared    = FALSE;
        pCtx->devPresent           = FALSE;

        exVioDbTps2hcs08Ctx[devIdx].sdoHeader = 0u;
    }

    exVioDbTps2hcs08WaitTick   = 0u;
    exVioDbTps2hcs08WdTick     = 0u;
    exVioDbTps2hcs08LpmTick    = 0u;
    exVioDbTps2hcs08StateTimeout = 0u;  /* Phase 2: Issue #5 & #6 */
    exVioDbTps2hcs08SleepReq   = FALSE;
    exVioDbTps2hcs08WakeUpReq  = FALSE;
    exVioDbTps2hcs08ReCfgReq   = FALSE;

    /* Phase 2: Issue #2 & #7 - Initialize retry counters to zero.
     * Counters will be incremented on failures and reset on success.
     */
    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Retry[devIdx].configWrite  = 0u;
        exVioDbTps2hcs08Retry[devIdx].configVerify = 0u;
        exVioDbTps2hcs08Retry[devIdx].devIdRead    = 0u;
        exVioDbTps2hcs08Retry[devIdx].diagRead     = 0u;
    }

    /* Phase 2: Issue #9 - Initialize execution time statistics */
    exVioDbTps2hcs08ExecStats.lastExecTime_us = 0u;
    exVioDbTps2hcs08ExecStats.maxExecTime_us  = 0u;
    exVioDbTps2hcs08ExecStats.avgExecTime_us  = 0u;
    exVioDbTps2hcs08ExecStats.execCount       = 0u;

    /* M-05: Initial state is INIT, not ACTIVE.
     * Chip is in SLEEP state after power-on/reset.
     * Transition to ACTIVE only after setup scan complete and device ready.
     */
    exVioDbTps2hcs08RunState   = TPS2HCS08_RUN_INIT;
    exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_SET_DEF;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_MapDbParam_Tps2hcs08
 *      DB parameter ID -> IC register field value.
 *      When the ID is not defined in the table an error log is printed with
 *      the signal ID and the parameter name, and E_NOT_OK is returned so that
 *      the caller can skip the related register write.
 *----------------------------------------------------------------------------*/
D_STATIC Std_ReturnType ExVioDb_MapDbParam_Tps2hcs08(const tTps2hcs08MapEntry *tbl,
                                                     uint8 tblSize, uint8 dbId,
                                                     uint8 *regValue,
                                                     uint16 sigIndex,
                                                     const char *paramName)
{
    uint8           idx;
    Std_ReturnType  retVal = E_NOT_OK;

    for (idx = 0u; idx < tblSize; idx++)
    {
        if (tbl[idx].dbId == dbId)
        {
            *regValue = tbl[idx].regValue;
            retVal    = E_OK;
            break;
        }
    }

    if (retVal != E_OK)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] DB PARAM NOT DEFINED. sigId=%d param=%s value=%d -> REG WRITE SKIP\r\n",
            (int)sigIndex, paramName, (int)dbId);
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ParsingOutputTps2hcs08Reg
 *      Converts one vehicle IO signal DB record into the shadow register of
 *      the assigned device / channel.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_ParsingOutputTps2hcs08Reg(uint16 sigIndex)
{
    tTps2hcs08Ctx   *pCtx;
    tTps2hcs08ChCfg *pCfg;
    uint8            devIdx;
    uint8            chIdx;
    uint8            regVal;
    uint16           skipMask = 0u;

    /* --- CAT_2 check : TPS2HCS08-Q1 signal must be Active High ------------ */
    if ((uint8)exVioDbRec[sigIndex].CAT_2 != (uint8)DB_CAT2_ACTIVE_HIGH)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] DB PARAM NOT DEFINED. sigId=%d param=CAT_2 value=%d\r\n",
            (int)sigIndex, (int)exVioDbRec[sigIndex].CAT_2);
        return;
    }

    /* --- SC check : only the signal of my standard controller ------------- */
    if ((uint8)exVioDbRec[sigIndex].SC != (uint8)EXVIODB_MY_SC_ID)
    {
        return;
    }

    /* --- IC : daisy chain device index ------------------------------------ */
    devIdx = (uint8)exVioDbRec[sigIndex].IC;
    if (devIdx >= TPS2HCS08_DEV_MAX)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] DB PARAM NOT DEFINED. sigId=%d param=IC value=%d\r\n",
            (int)sigIndex, (int)devIdx);
        return;
    }

    /* --- PIN : output channel --------------------------------------------- */
    switch ((uint8)exVioDbRec[sigIndex].PIN)
    {
        case DB_PIN_IC_PIN_1:   chIdx = TPS2HCS08_CH1;  break;
        case DB_PIN_IC_PIN_2:   chIdx = TPS2HCS08_CH2;  break;
        default:
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] DB PARAM NOT DEFINED. sigId=%d param=PIN value=%d\r\n",
                (int)sigIndex, (int)exVioDbRec[sigIndex].PIN);
            return;
    }

    pCtx = &exVioDbTps2hcs08Ctx[devIdx];
    pCfg = &pCtx->chCfg[chIdx];

    /* --- USED : channel assignment ---------------------------------------- */
    if ((uint8)exVioDbRec[sigIndex].USED == 0u)
    {
        pCfg->used = FALSE;
        return;
    }

    pCtx->devPresent = TRUE;
    pCfg->used       = TRUE;
    pCfg->sigIdx     = sigIndex;

    /* --- MOC : parallel operation of CH1 / CH2 ---------------------------- */
    pCfg->parallel = ((uint8)exVioDbRec[sigIndex].MOC != 0u) ? TRUE : FALSE;
    if (pCfg->parallel == TRUE)
    {
        pCtx->devConfig.bits.PARALLEL_12 = 1u;
    }

    /* --- DEF_Value : initial output level / B+ always on ------------------ */
    pCfg->defValueOn  = ((uint8)exVioDbRec[sigIndex].DEF_Value != 0u) ? TRUE : FALSE;
    pCfg->bPlusAlways = pCfg->defValueOn;

    /* --- VOL_DET : VOUT voltage sensing ----------------------------------- */
    pCfg->volDetUse = ((uint8)exVioDbRec[sigIndex].VOL_DET != 0u) ? TRUE : FALSE;
    pCtx->chConfig[chIdx].bits.VSNS_DIS_CHx = (pCfg->volDetUse == TRUE) ? 0u : 1u;

    /* --- OLD : off state open load detection ------------------------------ */
    pCfg->oldUse = ((uint8)exVioDbRec[sigIndex].OLD != 0u) ? TRUE : FALSE;

    /* --- OCP -> ILIMIT_SET_CHx -------------------------------------------- */
    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapOcp,
            (uint8)(sizeof(exVioDbTps2hcs08MapOcp) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].OCP, &regVal, sigIndex, "OCP") == E_OK)
    {
        pCfg->ilimitSet = regVal;
        pCtx->ilimCfgCh[chIdx].bits.ILIMIT_SET_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_ILIM;
    }

    /* --- OCP -> I2T setting ( NOM_CUR / I2T_TRIP / ISWCL ) ---------------- */
    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapNomCur,
            (uint8)(sizeof(exVioDbTps2hcs08MapNomCur) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].OCP, &regVal, sigIndex, "OCP(NOM_CUR)") == E_OK)
    {
        pCfg->nomCur = regVal;
        pCtx->i2tCfgCh[chIdx].bits.NOM_CUR_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_I2T;
    }

    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapI2tTrip,
            (uint8)(sizeof(exVioDbTps2hcs08MapI2tTrip) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].OCP, &regVal, sigIndex, "OCP(I2T_TRIP)") == E_OK)
    {
        pCfg->i2tTrip = regVal;
        pCtx->i2tCfgCh[chIdx].bits.I2T_TRIP_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_I2T;
    }

    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapIswcl,
            (uint8)(sizeof(exVioDbTps2hcs08MapIswcl) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].WC, &regVal, sigIndex, "OCP(ISWCL)") == E_OK)
    {
        pCfg->iswcl = regVal;
        pCtx->i2tCfgCh[chIdx].bits.ISWCL_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_I2T;
    }

    /* M-04: Enable I2T protection after DB parameters are configured.
     * I2T_EN was initially disabled (=0) for safety during init.
     * Now that NOM_CUR, I2T_TRIP, ISWCL are set from DB, enable I2T.
     * If I2T params were not successfully parsed, I2T_EN remains 0.
     */
    if ((skipMask & TPS2HCS08_SKIP_I2T) == 0u)
    {
        pCtx->ilimCfgCh[chIdx].bits.I2T_EN_CHx = 1u;  /* Enable I2T protection */
    }

    /* --- CT -> CAP_CHRG / INRUSH_DURATION --------------------------------- */
    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapInrushDur,
            (uint8)(sizeof(exVioDbTps2hcs08MapInrushDur) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].CT, &regVal, sigIndex, "CT") == E_OK)
    {
        pCfg->inrushDuration = regVal;
        pCtx->ilimCfgCh[chIdx].bits.INRUSH_DURATION_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_ILIM;
    }

    /* --- SR -> SLRT_CHx --------------------------------------------------- */
    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapSlrt,
            (uint8)(sizeof(exVioDbTps2hcs08MapSlrt) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].SR, &regVal, sigIndex, "SR") == E_OK)
    {
        pCfg->slewRate = regVal;
        pCtx->chConfig[chIdx].bits.SLRT_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_CH_CFG;
    }

    /* --- PWM_F -> PWM_FREQ_CHx -------------------------------------------- */
    if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapPwmFreq,
            (uint8)(sizeof(exVioDbTps2hcs08MapPwmFreq) / sizeof(tTps2hcs08MapEntry)),
            (uint8)exVioDbRec[sigIndex].PWM_F, &regVal, sigIndex, "PWM_F") == E_OK)
    {
        pCfg->pwmFreq = regVal;
        pCtx->pwmCh[chIdx].bits.PWM_FREQ_CHx = regVal;
    }
    else
    {
        skipMask |= TPS2HCS08_SKIP_PWM;
    }

    /* --- PWM / PWM_Duty --------------------------------------------------- */
    pCfg->pwmType = (tDbPwmType)exVioDbRec[sigIndex].PWM;

    switch (pCfg->pwmType)
    {
        case DB_PWM_TYPE_C:
            /* capacitive charging : Duty -> INRUSH_LIMIT (CAP_CHRG = 10)     */
            pCtx->ilimCfgCh[chIdx].bits.CAP_CHRG_CHx = TPS2HCS08_CAP_CHRG_CUR_REG;
            pCtx->pwmCh[chIdx].bits.PWM_EN_CHx       = 0u;
            if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapInrushLimitC,
                    (uint8)(sizeof(exVioDbTps2hcs08MapInrushLimitC) / sizeof(tTps2hcs08MapEntry)),
                    (uint8)exVioDbRec[sigIndex].PWM_Duty, &regVal, sigIndex,
                    "PWM_Duty(PWM_C)") == E_OK)
            {
                pCfg->inrushLimit = regVal;
                pCtx->ilimCfgCh[chIdx].bits.INRUSH_LIMIT_CHx = regVal;
            }
            else
            {
                skipMask |= TPS2HCS08_SKIP_ILIM;
            }
            break;

        case DB_PWM_TYPE_X:
            /* no capacitive charging : Duty -> INRUSH_LIMIT (CAP_CHRG = 00)  */
            pCtx->ilimCfgCh[chIdx].bits.CAP_CHRG_CHx = TPS2HCS08_CAP_CHRG_NONE;
            pCtx->pwmCh[chIdx].bits.PWM_EN_CHx       = 0u;
            if (ExVioDb_MapDbParam_Tps2hcs08(exVioDbTps2hcs08MapInrushLimitX,
                    (uint8)(sizeof(exVioDbTps2hcs08MapInrushLimitX) / sizeof(tTps2hcs08MapEntry)),
                    (uint8)exVioDbRec[sigIndex].PWM_Duty, &regVal, sigIndex,
                    "PWM_Duty(PWM_X)") == E_OK)
            {
                pCfg->inrushLimit = regVal;
                pCtx->ilimCfgCh[chIdx].bits.INRUSH_LIMIT_CHx = regVal;
            }
            else
            {
                skipMask |= TPS2HCS08_SKIP_ILIM;
            }
            break;

        case DB_PWM_TYPE_O:
            /* PWM output : Duty -> PWM_DTY_CHx ( 1 : 1 mapping )             */
            /* PWM can only be enabled when CAP_CHRG_CHx = 00                 */
            pCtx->ilimCfgCh[chIdx].bits.CAP_CHRG_CHx = TPS2HCS08_CAP_CHRG_NONE;
            pCfg->pwmDuty                            = (uint8)exVioDbRec[sigIndex].PWM_Duty;
            pCtx->pwmCh[chIdx].bits.PWM_DTY_CHx      = pCfg->pwmDuty;
            pCtx->pwmCh[chIdx].bits.PWM_EN_CHx       = 1u;
            break;

        case DB_PWM_TYPE_NONE:
            pCtx->ilimCfgCh[chIdx].bits.CAP_CHRG_CHx = TPS2HCS08_CAP_CHRG_NONE;
            pCtx->pwmCh[chIdx].bits.PWM_EN_CHx       = 0u;
            break;

        default:
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] DB PARAM NOT DEFINED. sigId=%d param=PWM value=%d -> REG WRITE SKIP\r\n",
                (int)sigIndex, (int)exVioDbRec[sigIndex].PWM);
            skipMask |= (TPS2HCS08_SKIP_PWM | TPS2HCS08_SKIP_ILIM);
            break;
    }

    exVioDbTps2hcs08SkipMask[devIdx][chIdx] = skipMask;
}

/*******************************************************************************
 *  SECTION 3 : SETUP SCAN STATE ACTION
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_WakeUp_Tps2hcs08                        [ process #2, #3 ]
 *      SLEEP -> INIT & ABIST : CSN low transition.
 *      A dummy SPI frame is used so that the device wakes up without SPI_ERR.
 *----------------------------------------------------------------------------*/
// TODO: 데이지 체인 방식으로 변경 필요
// TODO: ~SetCsn 함수 구현 필요
D_STATIC void ExVioDb_WakeUp_Tps2hcs08(void)
{
    uint8 devIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if (exVioDbTps2hcs08Ctx[devIdx].devPresent == TRUE)
        {
            /* CSN low pulse ( t < tREADY ) : wake up trigger                 */
            ExVioDb_Tps2hcs08_Port_SetCsn(devIdx, FALSE);
            ExVioDb_Tps2hcs08_Port_SetCsn(devIdx, TRUE);
        }
    }

    TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
        "[TPS2HCS08] WAKE-UP (CSN LOW) REQUESTED...\r\n");
}

/*------------------------------------------------------------------------------
 *  ExVioDb_WaitReadyDone_Tps2hcs08                 [ process #4 ]
 *      Waits tREADY (65us) and confirms the CONFIG state by reading DEV_ID.
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_WaitReadyDone_Tps2hcs08(void)
{
    uint8  devIdx;
    uint16 devId;
    uint8  retVal = TPS2HCS08_BUSY;

    if (exVioDbTps2hcs08WaitTick < TPS2HCS08_TICK_READY)
    {
        exVioDbTps2hcs08WaitTick++;
    }
    else
    {
        retVal = TPS2HCS08_COMPLETE;

        // TODO: 데이지 체인 방식으로 변경 필요 4번의 반복 x 하나의 데이터 프레임을 x 4 배로 준비하는 과정
        for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
        {
            if (exVioDbTps2hcs08Ctx[devIdx].devPresent != TRUE)
            {
                continue;
            }

            if (ExVioDb_ReadRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_ID, &devId) == E_OK)
            {
                /* M-16: Strict version verification - only accept target version */
                if (devId == TPS2HCS08_TARGET_VERSION)
                {
                    /* Correct version - device is ready */
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] dev=%d DEV_ID VERIFIED: 0x%04X (Ver A)\r\n",
                        devIdx, devId);
                }
                else if ((devId == TPS2HCS08_DEV_ID_VER_A) || (devId == TPS2HCS08_DEV_ID_VER_B))
                {
                    /* Valid TPS2HCS08 chip, but wrong version for this project */
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] dev=%d VERSION MISMATCH: read=0x%04X (Ver %c), expected=0x%04X (Ver %c)\r\n",
                        devIdx, devId,
                        (devId == TPS2HCS08_DEV_ID_VER_A) ? 'A' : 'B',
                        TPS2HCS08_TARGET_VERSION,
                        (TPS2HCS08_TARGET_VERSION == TPS2HCS08_DEV_ID_VER_A) ? 'A' : 'B');
                    retVal = TPS2HCS08_BUSY;
                    /* Mark device as not present to skip this device */
                    exVioDbTps2hcs08Ctx[devIdx].devPresent = FALSE;
                }
                else
                {
                    /* Completely invalid DEV_ID - not a TPS2HCS08 chip */
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] dev=%d INVALID DEV_ID: read=0x%04X (expected 0xFFF0/0xFFF1)\r\n",
                        devIdx, devId);
                    retVal = TPS2HCS08_BUSY;
                    /* Mark device as not present to skip this device */
                    exVioDbTps2hcs08Ctx[devIdx].devPresent = FALSE;
                }
            }
            else
            {
                retVal = TPS2HCS08_BUSY;
            }
        }

        exVioDbTps2hcs08WaitTick = 0u;
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ClearPorFault_Tps2hcs08                 [ process #4 ]
 *      GLOBAL_FAULT_TYPE read to clear POR / VDD_UVLO / VBB_UVLO /
 *      VBB_UV_WRN / GLOBAL_ERR_WRN / LPM_STATUS_1 ( no console log ).
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_ClearPorFault_Tps2hcs08(void)
{
    uint8  devIdx;
    uint8  chIdx;
    uint16 regValue;
    uint8  retVal = TPS2HCS08_COMPLETE;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        /* GLOBAL_FAULT_TYPE : read clear ( initial setting -> no log )       */
        if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                TPS2HCS08_REG_GLOBAL_FAULT_TYPE, &regValue) == E_OK)
        {
            pCtx->globalFault.word    = regValue;
            pCtx->porCleared          = TRUE;
            pCtx->lpmStatus1Cleared   = TRUE;
            pCtx->logLatchGlobal      = 0u;
        }
        else
        {
            retVal = TPS2HCS08_BUSY;
        }

        /* FLT_STAT_CHx : read clear of OL_SHRT_VBB_OFF_FLT                  */
        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            (void)ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_FLT_STAT_CH1, chIdx), &regValue);
            pCtx->logLatchCh[chIdx] = 0u;
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_WriteConfig_Tps2hcs08                   [ process #5 ]
 *      Writes every configuration register in the CONFIG state (output OFF).
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_WriteConfig_Tps2hcs08(void)
{
    uint8 devIdx;
    uint8 chIdx;
    uint8 retVal = TPS2HCS08_COMPLETE;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        /* 7h SW_STATE : keep all outputs OFF during configuration           */
        if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_SW_STATE,
                                            0x0000u) != E_OK)
        {
            retVal = TPS2HCS08_BUSY;
        }

        /* 3h LPM : AUTO_LPM_EXIT_CHx = 0, MANUAL_LPM not used               */
        if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                            pCtx->lpm.word) != E_OK)
        {
            retVal = TPS2HCS08_BUSY;
        }

        /* 5h FAULT_MASK                                                     */
        if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_FAULT_MASK,
                                            pCtx->faultMask.word) != E_OK)
        {
            retVal = TPS2HCS08_BUSY;
        }

        /* 9h DEV_CONFIG ( PARALLEL_12 write is valid only when outputs OFF )*/
        if ((exVioDbTps2hcs08SkipMask[devIdx][TPS2HCS08_CH1] & TPS2HCS08_SKIP_DEV_CFG) == 0u)
        {
            if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_CONFIG,
                                                pCtx->devConfig.word) != E_OK)
            {
                retVal = TPS2HCS08_BUSY;
            }
        }

        /* Ah ADC_CONFIG                                                     */
        if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                            pCtx->adcConfig.word) != E_OK)
        {
            retVal = TPS2HCS08_BUSY;
        }

        /* per channel register                                              */
        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            uint16 skip = exVioDbTps2hcs08SkipMask[devIdx][chIdx];

            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            /* Eh PWM_CHx                                                    */
            if ((skip & TPS2HCS08_SKIP_PWM) == 0u)
            {
                if (ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_PWM_CH1, chIdx),
                        pCtx->pwmCh[chIdx].word) != E_OK)
                {
                    retVal = TPS2HCS08_BUSY;
                }
            }

            /* Fh ILIM_CONFIG_CHx                                            */
            if ((skip & TPS2HCS08_SKIP_ILIM) == 0u)
            {
                if (ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_ILIM_CONFIG_CH1, chIdx),
                        pCtx->ilimCfgCh[chIdx].word) != E_OK)
                {
                    retVal = TPS2HCS08_BUSY;
                }
            }

            /* 10h CHx_CONFIG                                                */
            if ((skip & TPS2HCS08_SKIP_CH_CFG) == 0u)
            {
                if (ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                        pCtx->chConfig[chIdx].word) != E_OK)
                {
                    retVal = TPS2HCS08_BUSY;
                }
            }

            /* 15h I2T_CONFIG_CHx                                            */
            if ((skip & TPS2HCS08_SKIP_I2T) == 0u)
            {
                if (ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_I2T_CONFIG_CH1, chIdx),
                        pCtx->i2tCfgCh[chIdx].word) != E_OK)
                {
                    retVal = TPS2HCS08_BUSY;
                }
            }
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_VerifyConfig_Tps2hcs08                  [ process #5 ]
 *      Reads back the configuration register and compares it with the shadow.
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_VerifyConfig_Tps2hcs08(void)
{
    uint8  devIdx;
    uint8  chIdx;
    uint16 regValue;
    uint8  retVal = TPS2HCS08_COMPLETE;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        if (ExVioDb_ReadRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_CONFIG,
                                           &regValue) == E_OK)
        {
            if ((uint16)(regValue & 0x07FFu) != (uint16)(pCtx->devConfig.word & 0x07FFu))
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] DEV_CONFIG VERIFY FAIL. dev=%d w=0x%04X r=0x%04X\r\n",
                    devIdx, pCtx->devConfig.word, regValue);
                retVal = TPS2HCS08_BUSY;
            }
        }
        else
        {
            retVal = TPS2HCS08_BUSY;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_ILIM_CONFIG_CH1, chIdx),
                    &regValue) == E_OK)
            {
                if ((uint16)(regValue & 0x3FFFu) !=
                    (uint16)(pCtx->ilimCfgCh[chIdx].word & 0x3FFFu))
                {
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] ILIM_CONFIG_CH%d VERIFY FAIL. dev=%d w=0x%04X r=0x%04X\r\n",
                        (chIdx + 1u), devIdx, pCtx->ilimCfgCh[chIdx].word, regValue);
                    retVal = TPS2HCS08_BUSY;
                }
            }
            else
            {
                retVal = TPS2HCS08_BUSY;
            }
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_DiagSetPullDown_Tps2hcs08               [ process #6 ]
 *      OL_SVBB_EN_CHx = 01 : RSHRT_VBB pull down to discharge COUT.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_DiagSetPullDown_Tps2hcs08(void)
{
    uint8 devIdx;
    uint8 chIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            {
                tTps2hcs08ChConfig chConfig = pCtx->chConfig[chIdx];

                chConfig.bits.OL_SVBB_EN_CHx = TPS2HCS08_OL_SVBB_PULLDOWN;
                (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                        chConfig.word);
            }
        }
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_DiagSetPullUp_Tps2hcs08                 [ process #6 ]
 *      OL_SVBB_EN_CHx = 10 : OL_PU pull up ( open load / short to VBB ).
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_DiagSetPullUp_Tps2hcs08(void)
{
    uint8 devIdx;
    uint8 chIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            {
                tTps2hcs08ChConfig chConfig = pCtx->chConfig[chIdx];

                chConfig.bits.OL_SVBB_EN_CHx = TPS2HCS08_OL_SVBB_PULLUP;
                (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                        chConfig.word);
            }
        }
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_DiagJudgeOpenLoad_Tps2hcs08             [ process #6 ]
 *      OL_OFF_CHx = 0 : no fault
 *      OL_OFF_CHx = 1 : open load or short to VBB -> next judgement needed
 *      ( 3 successive reads are used as described in the data sheet )
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_DiagJudgeOpenLoad_Tps2hcs08(void)
{
    uint8               devIdx;
    uint8               chIdx;
    uint16              regValue;
    tTps2hcs08FltStatCh fltStat;
    uint8               retVal = TPS2HCS08_COMPLETE;   /* no more judgement   */

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            /* 1st read : select the register / 2nd, 3rd read : judgement    */
            (void)ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_FLT_STAT_CH1, chIdx), &regValue);
            (void)ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_FLT_STAT_CH1, chIdx), &regValue);

            fltStat.word = regValue;

            if (fltStat.bits.OL_OFF_CHx == 0u)
            {
                pCtx->diagResult[chIdx] = TPS2HCS08_DIAG_NONE;
            }
            else
            {
                /* open load or short to VBB : needs the pull down judgement  */
                pCtx->diagResult[chIdx] = TPS2HCS08_DIAG_OPEN_LOAD;
                retVal = TPS2HCS08_BUSY;
            }
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_DiagJudgeShortVbb_Tps2hcs08             [ process #6 ]
 *      SHRT_VBB_CHx = 1 : short to VBB
 *      SHRT_VBB_CHx = 0 : open load
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_DiagJudgeShortVbb_Tps2hcs08(void)
{
    uint8               devIdx;
    uint8               chIdx;
    uint16              regValue;
    tTps2hcs08FltStatCh fltStat;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            if (pCtx->diagResult[chIdx] != TPS2HCS08_DIAG_OPEN_LOAD)
            {
                continue;   /* already judged as no fault                     */
            }

            (void)ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_FLT_STAT_CH1, chIdx), &regValue);
            (void)ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_FLT_STAT_CH1, chIdx), &regValue);

            fltStat.word = regValue;

            if (fltStat.bits.SHRT_VBB_CHx == 1u)
            {
                pCtx->diagResult[chIdx] = TPS2HCS08_DIAG_SHORT_VBB;
            }
            else
            {
                pCtx->diagResult[chIdx] = TPS2HCS08_DIAG_OPEN_LOAD;
            }
        }
    }

    return TPS2HCS08_COMPLETE;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_DiagReport_Tps2hcs08                    [ process #6 ]
 *      Prints the open / short diagnostic result of every used channel once
 *      and restores the OL_SVBB_EN setting.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_DiagReport_Tps2hcs08(void)
{
    uint8 devIdx;
    uint8 chIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            switch (pCtx->diagResult[chIdx])
            {
                case TPS2HCS08_DIAG_SHORT_VBB:
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] dev=%d CH%d SHRT_VBB_CH%d = 1 (SHORT TO VBB)\r\n",
                        devIdx, (chIdx + 1u), (chIdx + 1u));
                    break;

                case TPS2HCS08_DIAG_OPEN_LOAD:
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] dev=%d CH%d OL_OFF_CH%d = 1 (OPEN LOAD)\r\n",
                        devIdx, (chIdx + 1u), (chIdx + 1u));
                    break;

                default:
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] dev=%d CH%d OPEN/SHORT DIAG NO FAULT\r\n",
                        devIdx, (chIdx + 1u));
                    break;
            }

            /* restore : keep the pull up when OLD is used, otherwise disable */
            {
                tTps2hcs08ChConfig chConfig = pCtx->chConfig[chIdx];

                chConfig.bits.OL_SVBB_EN_CHx =
                    (pCtx->chCfg[chIdx].oldUse == TRUE) ? TPS2HCS08_OL_SVBB_PULLUP
                                                        : TPS2HCS08_OL_SVBB_DISABLE;

                (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                        chConfig.word);
            }
        }

        /* the fault bit of the initial diagnostic is cleared by the read     */
        pCtx->logLatchGlobal = 0u;
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ActiveEntry_Tps2hcs08                   [ process #8 ]
 *      Turns on the channel that is assigned to the B+ always on power.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_ActiveEntry_Tps2hcs08(void)
{
    uint8 devIdx;
    uint8 chIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];
        tTps2hcs08SwState swState = pCtx->swState;

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if ((pCtx->chCfg[chIdx].used == TRUE) &&
                (pCtx->chCfg[chIdx].bPlusAlways == TRUE))
            {
                if (chIdx == TPS2HCS08_CH1)
                {
                    swState.bits.CH1_ON = TPS2HCS08_CH_ON;
                }
                else
                {
                    swState.bits.CH2_ON = TPS2HCS08_CH_ON;
                }
            }
        }

        (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_SW_STATE,
                                              swState.word);
    }
}

/*******************************************************************************
 *  SECTION 4 : SETUP SCAN STATE MACHINE
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_SetupScnTps2hcs08Reg
 *      Called cyclically from RE_Swc_ExVioDb_Task_10ms() while the component
 *      state is EXVIODB_STATE_EXVIO_SETUP.
 *----------------------------------------------------------------------------*/
void ExVioDb_SetupScnTps2hcs08Reg(void)
{
    uint16 sigIndex;

    switch (exVioDbTps2hcs08SetupScnState)
    {
        case TPS2HCS08_SETUP_SCN_SET_DEF:
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DB_PARSING;
            break;

        // TODO: 시퀀스 변경 필요 Wakeup 이후 진행해야 함
        /* M-15: DB_PARSING vs VHAL_FR DBLoad - NOT duplicate, different roles:
         * - VHAL_FR DBLoad: Application-level, loads entire Vehicle IO DB into memory
         * - SETUP_SCN_DB_PARSING: IC driver-level, filters TPS2HCS08 signals only
         *   and translates DB parameters into IC-specific shadow register values
         * This stage must run during IC initialization to configure shadow registers
         * from DB before writing to actual hardware.
         */
        case TPS2HCS08_SETUP_SCN_DB_PARSING:
            for (sigIndex = 0u; sigIndex < exVioDbMemCnt; sigIndex++)
            {
                if (exVioDbRec[sigIndex].CAT_1 == (uint8)DB_CAT1_E_FUSE_TPS2HCS08)
                {
                    ExVioDb_ParsingOutputTps2hcs08Reg(sigIndex);
                }
            }
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_WAKEUP;
            break;

        case TPS2HCS08_SETUP_SCN_WAKEUP:                    /* process #2, #3 */
            ExVioDb_WakeUp_Tps2hcs08();
            exVioDbTps2hcs08WaitTick      = 0u;
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_WAIT_READY;
            break;

        case TPS2HCS08_SETUP_SCN_WAIT_READY:                /* process #4     */
            /* Phase 2: Issue #5 & #6 - Timeout check */
            if (exVioDbTps2hcs08StateTimeout >= TPS2HCS08_TICK_STATE_TIMEOUT)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] WAIT_READY timeout\r\n");
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
                exVioDbTps2hcs08StateTimeout = 0u;
            }
            else if (ExVioDb_WaitReadyDone_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] INIT & ABIST DONE -> CONFIG STATE...\r\n");
                exVioDbTps2hcs08StateTimeout = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CLEAR_POR;
            }
            else
            {
                exVioDbTps2hcs08StateTimeout++;
            }
            break;

        case TPS2HCS08_SETUP_SCN_CLEAR_POR:                 /* process #4     */
            /* Phase 2: Issue #5 & #6 - Timeout check */
            if (exVioDbTps2hcs08StateTimeout >= TPS2HCS08_TICK_STATE_TIMEOUT)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] CLEAR_POR timeout\r\n");
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
                exVioDbTps2hcs08StateTimeout = 0u;
            }
            else if (ExVioDb_ClearPorFault_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                exVioDbTps2hcs08StateTimeout = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_WRITE;
            }
            else
            {
                exVioDbTps2hcs08StateTimeout++;
            }
            break;

        case TPS2HCS08_SETUP_SCN_CONFIG_WRITE:              /* process #5     */
            /* Phase 2: Issue #5 & #6 - Timeout check */
            if (exVioDbTps2hcs08StateTimeout >= TPS2HCS08_TICK_STATE_TIMEOUT)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] CONFIG_WRITE timeout\r\n");
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
                exVioDbTps2hcs08StateTimeout = 0u;
            }
            else if (ExVioDb_WriteConfig_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                exVioDbTps2hcs08StateTimeout = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_VERIFY;
            }
            else
            {
                exVioDbTps2hcs08StateTimeout++;
            }
            break;

        case TPS2HCS08_SETUP_SCN_CONFIG_VERIFY:             /* process #5     */
            if (ExVioDb_VerifyConfig_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                /* Phase 2: Success - reset retry counters */
                for (int devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
                {
                    exVioDbTps2hcs08Retry[devIdx].configVerify = 0u;
                }

                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] REGISTER CONFIGURATION DONE...\r\n");
                exVioDbTps2hcs08WaitTick      = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_PULLDOWN;
            }
            else
            {
                /* Phase 2: Failure - check retry limit */
                boolean allFailed = TRUE;

                for (int devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
                {
                    if (exVioDbTps2hcs08Ctx[devIdx].devPresent == TRUE)
                    {
                        exVioDbTps2hcs08Retry[devIdx].configVerify++;

                        if (exVioDbTps2hcs08Retry[devIdx].configVerify < TPS2HCS08_MAX_RETRY_CONFIG_VERIFY)
                        {
                            allFailed = FALSE;
                        }
                    }
                }

                if (allFailed == TRUE)
                {
                    /* All devices exceeded retry limit */
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] CONFIG_VERIFY failed after %d retries\r\n",
                        TPS2HCS08_MAX_RETRY_CONFIG_VERIFY);
                    exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
                }
                else
                {
                    /* Retry configuration */
                    exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_WRITE;
                }
            }
            break;

        case TPS2HCS08_SETUP_SCN_DIAG_PULLDOWN:             /* process #6     */
            if (exVioDbTps2hcs08WaitTick == 0u)
            {
                ExVioDb_DiagSetPullDown_Tps2hcs08();
            }

            if (exVioDbTps2hcs08WaitTick < TPS2HCS08_TICK_DISCHARGE)
            {
                exVioDbTps2hcs08WaitTick++;
            }
            else
            {
                exVioDbTps2hcs08WaitTick      = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_PULLUP;
            }
            break;

        case TPS2HCS08_SETUP_SCN_DIAG_PULLUP:               /* process #6     */
            if (exVioDbTps2hcs08WaitTick == 0u)
            {
                ExVioDb_DiagSetPullUp_Tps2hcs08();
            }

            if (exVioDbTps2hcs08WaitTick < TPS2HCS08_TICK_BLANK)
            {
                exVioDbTps2hcs08WaitTick++;
            }
            else
            {
                exVioDbTps2hcs08WaitTick      = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_JUDGE_OL;
            }
            break;

        case TPS2HCS08_SETUP_SCN_DIAG_JUDGE_OL:             /* process #6     */
            if (ExVioDb_DiagJudgeOpenLoad_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                /* every channel is fault free                                */
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_REPORT;
            }
            else
            {
                /* open load or short to VBB : pull down and judge again      */
                ExVioDb_DiagSetPullDown_Tps2hcs08();
                exVioDbTps2hcs08WaitTick      = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_JUDGE_STB;
            }
            break;

        case TPS2HCS08_SETUP_SCN_DIAG_JUDGE_STB:            /* process #6     */
            if (exVioDbTps2hcs08WaitTick < TPS2HCS08_TICK_BLANK)
            {
                exVioDbTps2hcs08WaitTick++;
            }
            else
            {
                (void)ExVioDb_DiagJudgeShortVbb_Tps2hcs08();
                exVioDbTps2hcs08WaitTick      = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_REPORT;
            }
            break;

        case TPS2HCS08_SETUP_SCN_DIAG_REPORT:               /* process #6     */
            ExVioDb_DiagReport_Tps2hcs08();
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ACTIVE_ENTRY;
            break;

        case TPS2HCS08_SETUP_SCN_ACTIVE_ENTRY:              /* process #8     */
            ExVioDb_ActiveEntry_Tps2hcs08();
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] ACTIVE STATE ENTRY DONE...\r\n");
            /* M-05: Don't set RunState here. Let RUN_INIT handler do the transition.
             * This ensures proper state tracking from INIT -> ACTIVE.
             */
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_COMPLETE;
            break;

        case TPS2HCS08_SETUP_SCN_COMPLETE:
            /* nothing */
            break;

        case TPS2HCS08_SETUP_SCN_ERROR:
            /* Phase 2: Issue #5 & #6 - Fatal error state.
             * Setup scan failed after exceeding max retries or timeout.
             * Log error details and remain in ERROR state.
             * Recovery requires system reset or power cycle.
             */
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] SETUP SCAN ERROR - halted\r\n");
            /* Remain in ERROR state - no auto-recovery */
            break;

        default:
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_SET_DEF;
            break;
    }
}

/*******************************************************************************
 *  SECTION 5 : RUN SCAN STATE ACTION
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_IsFltPinLow_Tps2hcs08
 *      Returns TRUE when the FLT / WAKE_SIG pin of any device is LOW.
 *----------------------------------------------------------------------------*/
D_STATIC boolean ExVioDb_IsFltPinLow_Tps2hcs08(void)
{
    uint8   devIdx;
    boolean retVal = FALSE;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if (exVioDbTps2hcs08Ctx[devIdx].devPresent == TRUE)
        {
            if (ExVioDb_Tps2hcs08_Port_GetFltPin(devIdx) == TRUE)
            {
                retVal = TRUE;
                break;
            }
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_WdRead_Tps2hcs08                        [ process #10 ]
 *      SPI watchdog periodic read.
 *      GLOBAL_FAULT_TYPE / FLT_STAT_CH1 / FLT_STAT_CH2 are read and the
 *      fault information is printed one shot per fault occurrence.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_WdRead_Tps2hcs08(void)
{
    uint8  devIdx;
    uint8  chIdx;
    uint16 regValue;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        /* --- 4h GLOBAL_FAULT_TYPE ----------------------------------------- */
        if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                TPS2HCS08_REG_GLOBAL_FAULT_TYPE, &regValue) == E_OK)
        {
            pCtx->globalFault.word = regValue;
            ExVioDb_EvalGlobalFaultLog_Tps2hcs08(devIdx);
        }

        /* --- Dh / 16h FLT_STAT_CHx ---------------------------------------- */
        for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
        {
            if (pCtx->chCfg[chIdx].used != TRUE)
            {
                continue;
            }

            if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_FLT_STAT_CH1, chIdx),
                    &regValue) == E_OK)
            {
                pCtx->fltStatCh[chIdx].word = regValue;
                ExVioDb_EvalChFaultLog_Tps2hcs08(devIdx, chIdx);
            }

            /* --- 13h VOUT sense ( VOL_DET ) ------------------------------- */
            if (pCtx->chCfg[chIdx].volDetUse == TRUE)
            {
                if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                        TPS2HCS08_CH_REG(TPS2HCS08_REG_ADC_RESULT_CH1_V, chIdx),
                        &regValue) == E_OK)
                {
                    pCtx->adcVsns[chIdx] = (uint16)(regValue & 0x03FFu);
                }
            }
        }

        /* --- POR detected : re-configure from process #5 ------------------ */
        if (pCtx->globalFault.bits.POR == 1u)
        {
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] dev=%d POR DETECTED -> RE-CONFIGURATION\r\n", devIdx);
            exVioDbTps2hcs08ReCfgReq = TRUE;
        }
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ReadAdcResult_Tps2hcs08
 *      Reads ISNS / TSNS / VDS of one channel ( application SW request ).
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_ReadAdcResult_Tps2hcs08(uint8 devIdx, uint8 chIdx)
{
    uint16 regValue;

    if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
            TPS2HCS08_CH_REG(TPS2HCS08_REG_ADC_RESULT_CH1_I, chIdx), &regValue) == E_OK)
    {
        exVioDbTps2hcs08Ctx[devIdx].adcIsns[chIdx] = (uint16)(regValue & 0x0FFFu);
    }

    if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
            TPS2HCS08_CH_REG(TPS2HCS08_REG_ADC_RESULT_CH1_T, chIdx), &regValue) == E_OK)
    {
        exVioDbTps2hcs08Ctx[devIdx].adcTsns[chIdx] = (uint16)(regValue & 0x07FFu);
    }

    if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
            TPS2HCS08_CH_REG(TPS2HCS08_REG_ADC_RESULT_CH1_VDS, chIdx), &regValue) == E_OK)
    {
        exVioDbTps2hcs08Ctx[devIdx].adcVds[chIdx] = (uint16)(regValue & 0x07FFu);
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_SetAutoLpmEntry_Tps2hcs08               [ process #12, #18 ]
 *      DEV_CONFIG.AUTO_LPM_ENTRY = 1 / 0
 *      AUTO_LPM entry conditions : watchdog disabled, ADC diagnostic except
 *      ISNS disabled, AUTO_LPM_EXIT_CHx = 0, no fault.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_SetAutoLpmEntry_Tps2hcs08(boolean enable)
{
    uint8 devIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];
        tTps2hcs08Lpm       lpm = pCtx->lpm;
        tTps2hcs08AdcConfig adcConfig = pCtx->adcConfig;
        tTps2hcs08DevConfig devConfig = pCtx->devConfig;

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        if (enable == TRUE)
        {
            /* AUTO_LPM_EXIT_CHx = 0 ( entry condition )                      */
            lpm.bits.AUTO_LPM_EXIT_CH1 = 0u;
            lpm.bits.AUTO_LPM_EXIT_CH2 = 0u;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                                  lpm.word);

            /* all ADC diagnostics except ISNS must be disabled               */
            adcConfig.bits.ADC_VSNS_DIS = 1u;
            adcConfig.bits.ADC_VDS_DIS  = 1u;
            adcConfig.bits.ADC_TSNS_DIS = 1u;
            adcConfig.bits.ADC_VBB_DIS  = 1u;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                                  adcConfig.word);

            /* watchdog disable + AUTO_LPM_ENTRY = 1                          */
            devConfig.bits.WD_EN          = 0u;
            devConfig.bits.AUTO_LPM_ENTRY = 1u;
        }
        else
        {
            devConfig.bits.AUTO_LPM_ENTRY = 0u;
            devConfig.bits.WD_EN          = 1u;
        }

        (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_CONFIG,
                                              devConfig.word);

        if (enable != TRUE)
        {
            /* restore the ADC diagnostic setting of the signal DB            */
            uint8 chIdx;

            adcConfig = pCtx->adcConfig;
            adcConfig.bits.ADC_VSNS_DIS = 0u;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                                  adcConfig.word);

            for (chIdx = 0u; chIdx < TPS2HCS08_CH_MAX; chIdx++)
            {
                if (pCtx->chCfg[chIdx].used == TRUE)
                {
                    (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                            TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                            pCtx->chConfig[chIdx].word);
                }
            }
        }
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_CheckLpmStatus_Tps2hcs08                [ process #13, #14 ]
 *      GLOBAL_FAULT_TYPE.LPM_STATUS = 1 confirms the AUTO_LPM entry.
 *----------------------------------------------------------------------------*/
D_STATIC uint8 ExVioDb_CheckLpmStatus_Tps2hcs08(void)
{
    uint8  devIdx;
    uint16 regValue;
    uint8  retVal = TPS2HCS08_COMPLETE;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        if (ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                TPS2HCS08_REG_GLOBAL_FAULT_TYPE, &regValue) == E_OK)
        {
            pCtx->globalFault.word = regValue;

            if (pCtx->globalFault.bits.LPM_STATUS != 1u)
            {
                retVal = TPS2HCS08_BUSY;
            }
        }
        else
        {
            retVal = TPS2HCS08_BUSY;
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_SetAutoLpmExit_Tps2hcs08                [ process #17, #19 ]
 *      LPM.AUTO_LPM_EXIT_CHx = 1 : forces ACTIVE state and enables the channel
 *      LPM.AUTO_LPM_EXIT_CHx = 0 : restores the setting
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_SetAutoLpmExit_Tps2hcs08(boolean exit)
{
    uint8 devIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];
        tTps2hcs08Lpm  lpm = pCtx->lpm;

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        if (exit == TRUE)
        {
            lpm.bits.AUTO_LPM_EXIT_CH1 =
                (pCtx->chCfg[TPS2HCS08_CH1].used == TRUE) ? 1u : 0u;
            lpm.bits.AUTO_LPM_EXIT_CH2 =
                (pCtx->chCfg[TPS2HCS08_CH2].used == TRUE) ? 1u : 0u;
        }
        else
        {
            lpm.bits.AUTO_LPM_EXIT_CH1 = 0u;
            lpm.bits.AUTO_LPM_EXIT_CH2 = 0u;
        }

        (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                              lpm.word);
    }
}

/*******************************************************************************
 *  SECTION 6 : FAULT LOG EVALUATION
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_EvalGlobalFaultLog_Tps2hcs08
 *      Prints the field name once per fault occurrence ( one shot latch ).
 *----------------------------------------------------------------------------*/
/* M-18: Fault READ and edge detection
 * Implements edge-triggered fault logging to avoid log spam:
 * - logLatchGlobal/logLatchCh: Stores previous fault state
 * - Rising edge: New fault detected → log ERROR
 * - Falling edge: Fault cleared → silently update latch
 * - No change: No log output
 * Also detects POR (Power-On Reset) to trigger re-configuration.
 */
D_STATIC void ExVioDb_EvalGlobalFaultLog_Tps2hcs08(uint8 devIdx)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];
    uint8          idx;
    uint8          tblSize;
    uint16         bitMask;

    tblSize = (uint8)(sizeof(exVioDbTps2hcs08GlobalLogTbl) / sizeof(tTps2hcs08FaultLogEntry));

    for (idx = 0u; idx < tblSize; idx++)
    {
        bitMask = (uint16)(1u << exVioDbTps2hcs08GlobalLogTbl[idx].bitPos);

        if ((pCtx->globalFault.word & bitMask) != 0u)
        {
            if ((pCtx->logLatchGlobal & bitMask) == 0u)
            {
                pCtx->logLatchGlobal |= bitMask;

                TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] dev=%d GLOBAL_FAULT_TYPE : %s\r\n",
                    devIdx, exVioDbTps2hcs08GlobalLogTbl[idx].name);
            }
        }
        else
        {
            pCtx->logLatchGlobal &= (uint16)(~bitMask);
        }
    }

    /* LPM_STATUS_1 : must be 0 when the device did not enter the LPM state   */
    if ((pCtx->globalFault.bits.LPM_STATUS_1 == 1u) &&
        (exVioDbTps2hcs08RunState == TPS2HCS08_RUN_ACTIVE))
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d LPM_STATUS_1 SET WITHOUT LPM ENTRY\r\n", devIdx);
    }
}

/*------------------------------------------------------------------------------
 *  ExVioDb_EvalChFaultLog_Tps2hcs08
 *      Prints the FLT_STAT_CHx field name once per fault occurrence and
 *      handles the I2T_MOD / SW_STATE_STAT / VOUT_ERR special rules.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_EvalChFaultLog_Tps2hcs08(uint8 devIdx, uint8 chIdx)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];
    uint8          idx;
    uint8          tblSize;
    uint16         bitMask;
    uint16         status  = pCtx->fltStatCh[chIdx].word;
    uint8          chOnCfg;

    tblSize = (uint8)(sizeof(exVioDbTps2hcs08ChLogTbl) / sizeof(tTps2hcs08FaultLogEntry));

    for (idx = 0u; idx < tblSize; idx++)
    {
        bitMask = (uint16)(1u << exVioDbTps2hcs08ChLogTbl[idx].bitPos);

        /* VOUT_ERR is evaluated only while the channel is commanded ON       */
        if (exVioDbTps2hcs08ChLogTbl[idx].bitPos == TPS2HCS08_FS_BIT_VOUT_ERR)
        {
            chOnCfg = (chIdx == TPS2HCS08_CH1) ? (uint8)pCtx->swState.bits.CH1_ON
                                               : (uint8)pCtx->swState.bits.CH2_ON;
            if (chOnCfg != TPS2HCS08_CH_ON)
            {
                pCtx->logLatchCh[chIdx] &= (uint16)(~bitMask);
                continue;
            }
        }

        if ((status & bitMask) != 0u)
        {
            if ((pCtx->logLatchCh[chIdx] & bitMask) == 0u)
            {
                pCtx->logLatchCh[chIdx] |= bitMask;

                TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] dev=%d CH%d FLT_STAT : %s\r\n",
                    devIdx, (chIdx + 1u), exVioDbTps2hcs08ChLogTbl[idx].name);
            }
        }
        else
        {
            pCtx->logLatchCh[chIdx] &= (uint16)(~bitMask);
        }
    }

    /* --- I2T_MOD_CHx : print the value on both 0->1 and 1->0 transition --- */
    bitMask = (uint16)(1u << TPS2HCS08_FS_BIT_I2T_MOD);

    if ((status & bitMask) != (pCtx->logLatchCh[chIdx] & bitMask))
    {
        pCtx->logLatchCh[chIdx] = (uint16)((pCtx->logLatchCh[chIdx] & (uint16)(~bitMask))
                                            | (status & bitMask));

        TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d CH%d I2T_MOD_CH%d = %d\r\n",
            devIdx, (chIdx + 1u), (chIdx + 1u),
            (int)((status & bitMask) != 0u));
    }

    /* --- SW_STATE_STAT_CHx : compare with the CHx_ON setting -------------- */
    chOnCfg = (chIdx == TPS2HCS08_CH1) ? (uint8)pCtx->swState.bits.CH1_ON
                                       : (uint8)pCtx->swState.bits.CH2_ON;
    bitMask = (uint16)(1u << TPS2HCS08_FS_BIT_SW_STATE_STAT);

    if ((uint8)pCtx->fltStatCh[chIdx].bits.SW_STATE_STAT_CHx != chOnCfg)
    {
        if ((pCtx->logLatchCh[chIdx] & bitMask) == 0u)
        {
            pCtx->logLatchCh[chIdx] |= bitMask;

            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] dev=%d CH%d SW_STATE_STAT_CH%d MISMATCH (cfg=%d sts=%d)\r\n",
                devIdx, (chIdx + 1u), (chIdx + 1u), (int)chOnCfg,
                (int)pCtx->fltStatCh[chIdx].bits.SW_STATE_STAT_CHx);
        }
    }
    else
    {
        pCtx->logLatchCh[chIdx] &= (uint16)(~bitMask);
    }
}

/*******************************************************************************
 *  SECTION 7 : RUN SCAN STATE MACHINE
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_RunScnTps2hcs08Reg
 *      Called cyclically from RE_Swc_ExVioDb_Task_10ms() while the component
 *      state is EXVIODB_STATE_RUN.
 *----------------------------------------------------------------------------*/
void ExVioDb_RunScnTps2hcs08Reg(void)
{
#ifdef TPS2HCS08_ENABLE_EXEC_TIME_MONITORING
    /* Phase 2: Issue #9 - Start execution time measurement.
     * Requires GetMicroseconds() function from BSW/HAL layer.
     * Enable by defining TPS2HCS08_ENABLE_EXEC_TIME_MONITORING in project config.
     */
    extern uint32 GetMicroseconds(void);
    uint32 startTime = GetMicroseconds();
#endif

    /* re-configuration request by POR detection ( process #5 restart )       */
    if (exVioDbTps2hcs08ReCfgReq == TRUE)
    {
        if (exVioDbTps2hcs08SetupScnState == TPS2HCS08_SETUP_SCN_COMPLETE)
        {
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CLEAR_POR;
        }

        ExVioDb_SetupScnTps2hcs08Reg();

        if (exVioDbTps2hcs08SetupScnState == TPS2HCS08_SETUP_SCN_COMPLETE)
        {
            exVioDbTps2hcs08ReCfgReq = FALSE;
        }
        return;
    }

    switch (exVioDbTps2hcs08RunState)
    {
        case TPS2HCS08_RUN_INIT:
            /* M-05: Initial state after power-on/reset.
             * Chip is in SLEEP state. Wait for setup scan to complete.
             * Transition to ACTIVE only when device is ready.
             */
            if (exVioDbTps2hcs08SetupScnState == TPS2HCS08_SETUP_SCN_COMPLETE)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] INIT -> ACTIVE (Setup complete)\r\n");
                exVioDbTps2hcs08WdTick   = 0u;
                exVioDbTps2hcs08RunState = TPS2HCS08_RUN_ACTIVE;
            }
            /* Otherwise, stay in INIT state until setup scan completes */
            break;

        case TPS2HCS08_RUN_ACTIVE:                          /* process #9,#10 */
            /* SPI watchdog periodic read ( immediate read when FLT is LOW )  */
            if ((exVioDbTps2hcs08WdTick >= TPS2HCS08_TICK_WD_READ) ||
                (ExVioDb_IsFltPinLow_Tps2hcs08() == TRUE))
            {
                exVioDbTps2hcs08WdTick = 0u;
                ExVioDb_WdRead_Tps2hcs08();
            }
            else
            {
                exVioDbTps2hcs08WdTick++;
            }

            if (exVioDbTps2hcs08SleepReq == TRUE)
            {
                exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_PREPARE;
            }
            break;

        /* M-17: AUTO_LPM entry/exit sequence (datasheet p.39-40, processes #11~#19)
         * Ensures proper low-power mode handling:
         * - #11 LPM_PREPARE: Notify application, prepare for sleep
         * - #12 LPM_ENTRY: Set AUTO_LPM_ENTRY=1, disable WD, ADC diagnostics
         * - #13-14 LPM_WAIT_STATUS: Wait for LPM_STATUS=1 (max 5s timeout)
         * - #15-16 LPM_ACTIVE: Stay in LPM until wake request
         * - #17 LPM_EXIT: Set AUTO_LPM_EXIT_CHx=1 in LPM register ONLY
         * - #18-19 LPM_RESTORE: Clear AUTO_LPM_ENTRY/EXIT, restore settings
         * CRITICAL: During AUTO_LPM, only LPM(3h) register can be written!
         * Writing other registers is silently ignored by chip (datasheet p.34).
         */
        case TPS2HCS08_RUN_LPM_PREPARE:                     /* process #11    */
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] SLEEP MODE PREPARATION...\r\n");
            ExVioDb_Tps2hcs08_Port_NotifyLpmReady(FALSE);
            exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_ENTRY;
            break;

        case TPS2HCS08_RUN_LPM_ENTRY:                       /* process #12    */
            ExVioDb_SetAutoLpmEntry_Tps2hcs08(TRUE);
            exVioDbTps2hcs08LpmTick  = 0u;
            exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_WAIT_STATUS;
            break;

        case TPS2HCS08_RUN_LPM_WAIT_STATUS:                 /* process #13,#14*/
            if (ExVioDb_CheckLpmStatus_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] AUTO_LPM ENTRY DONE (LPM_STATUS = 1)...\r\n");
                ExVioDb_Tps2hcs08_Port_NotifyLpmReady(TRUE);
                exVioDbTps2hcs08LpmTick  = 0u;
                exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_ACTIVE;
            }
            else
            {
                if (exVioDbTps2hcs08LpmTick < TPS2HCS08_TICK_LPM_TIMEOUT)
                {
                    exVioDbTps2hcs08LpmTick++;
                }
                else
                {
                    /* not entered within 5s -> repeat the log every 5s       */
                    exVioDbTps2hcs08LpmTick = 0u;
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] GLOBAL_FAULT_TYPE : LPM_STATUS (AUTO_LPM ENTRY TIMEOUT)\r\n");
                }

                if (exVioDbTps2hcs08SleepReq != TRUE)
                {
                    exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_RESTORE;
                }
            }
            break;

        case TPS2HCS08_RUN_LPM_ACTIVE:                      /* process #15,#16*/
            /* stays in AUTO_LPM until a channel activation is requested      */
            if ((exVioDbTps2hcs08WakeUpReq == TRUE) ||
                (exVioDbTps2hcs08SleepReq != TRUE))
            {
                exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_EXIT;
            }
            break;

        case TPS2HCS08_RUN_LPM_EXIT:                        /* process #17    */
            ExVioDb_SetAutoLpmExit_Tps2hcs08(TRUE);
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] AUTO_LPM EXIT (AUTO_LPM_EXIT_CHx = 1)...\r\n");
            exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_RESTORE;
            break;

        case TPS2HCS08_RUN_LPM_RESTORE:                     /* process #18,#19*/
            ExVioDb_SetAutoLpmEntry_Tps2hcs08(FALSE);   /* AUTO_LPM_ENTRY = 0 */
            ExVioDb_SetAutoLpmExit_Tps2hcs08(FALSE);    /* AUTO_LPM_EXIT  = 0 */

            /* LPM_STATUS_1 is read cleared after the wake up                 */
            {
                uint8  devIdx;
                uint16 regValue;

                for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
                {
                    if (exVioDbTps2hcs08Ctx[devIdx].devPresent == TRUE)
                    {
                        (void)ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                                TPS2HCS08_REG_GLOBAL_FAULT_TYPE, &regValue);
                        exVioDbTps2hcs08Ctx[devIdx].lpmStatus1Cleared = TRUE;
                    }
                }
            }

            exVioDbTps2hcs08WakeUpReq = FALSE;
            exVioDbTps2hcs08SleepReq  = FALSE;
            exVioDbTps2hcs08WdTick    = 0u;
            exVioDbTps2hcs08RunState  = TPS2HCS08_RUN_ACTIVE;
            break;

        default:
            exVioDbTps2hcs08RunState = TPS2HCS08_RUN_ACTIVE;
            break;
    }

#ifdef TPS2HCS08_ENABLE_EXEC_TIME_MONITORING
    /* Phase 2: Issue #9 - Calculate and update execution time statistics */
    {
        uint32 execTime = GetMicroseconds() - startTime;

        exVioDbTps2hcs08ExecStats.lastExecTime_us = execTime;
        exVioDbTps2hcs08ExecStats.execCount++;

        /* Update maximum if new peak */
        if (execTime > exVioDbTps2hcs08ExecStats.maxExecTime_us)
        {
            exVioDbTps2hcs08ExecStats.maxExecTime_us = execTime;
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] New max exec time: %lu us\r\n", execTime);
        }

        /* Exponential moving average: avg_new = (avg_old * 7 + new) / 8 */
        exVioDbTps2hcs08ExecStats.avgExecTime_us =
            (exVioDbTps2hcs08ExecStats.avgExecTime_us * 7u + execTime) / 8u;
    }
#endif
}

/*******************************************************************************
 *  SECTION 8 : PUBLIC INTERFACE
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_GetSetupScnState_Tps2hcs08
 *----------------------------------------------------------------------------*/
uint8 ExVioDb_GetSetupScnState_Tps2hcs08(void)
{
    return (uint8)exVioDbTps2hcs08SetupScnState;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_SetChannelOutput_Tps2hcs08              [ process #9 ]
 *      Channel ON / OFF request from the application SW or the signal DB.
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_SetChannelOutput_Tps2hcs08(uint8 devIdx, uint8 chIdx, boolean onOff)
{
    Std_ReturnType retVal = E_NOT_OK;

    if ((devIdx < TPS2HCS08_DEV_MAX) && (chIdx < TPS2HCS08_CH_MAX))
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        if ((pCtx->devPresent == TRUE) && (pCtx->chCfg[chIdx].used == TRUE))
        {
            tTps2hcs08SwState swState = pCtx->swState;

            if (chIdx == TPS2HCS08_CH1)
            {
                swState.bits.CH1_ON = (onOff == TRUE) ? TPS2HCS08_CH_ON
                                                       : TPS2HCS08_CH_OFF;
            }
            else
            {
                swState.bits.CH2_ON = (onOff == TRUE) ? TPS2HCS08_CH_ON
                                                       : TPS2HCS08_CH_OFF;
            }

            retVal = ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_SW_STATE,
                                                     swState.word);
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ReqSleep_Tps2hcs08 / ExVioDb_ReqWakeUp_Tps2hcs08
 *----------------------------------------------------------------------------*/
void ExVioDb_ReqSleep_Tps2hcs08(boolean req)
{
    exVioDbTps2hcs08SleepReq = req;
}

void ExVioDb_ReqWakeUp_Tps2hcs08(void)
{
    exVioDbTps2hcs08WakeUpReq = TRUE;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_GetAdcValue_Tps2hcs08
 *      ADC monitoring value request from the application SW.
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_GetAdcValue_Tps2hcs08(uint8 devIdx, uint8 chIdx,
                                             uint16 *isns, uint16 *tsns, uint16 *vds)
{
    Std_ReturnType retVal = E_NOT_OK;

    if ((devIdx < TPS2HCS08_DEV_MAX) && (chIdx < TPS2HCS08_CH_MAX) &&
        (isns != NULL_PTR) && (tsns != NULL_PTR) && (vds != NULL_PTR))
    {
        if (exVioDbTps2hcs08Ctx[devIdx].chCfg[chIdx].used == TRUE)
        {
            ExVioDb_ReadAdcResult_Tps2hcs08(devIdx, chIdx);

            *isns = exVioDbTps2hcs08Ctx[devIdx].adcIsns[chIdx];
            *tsns = exVioDbTps2hcs08Ctx[devIdx].adcTsns[chIdx];
            *vds  = exVioDbTps2hcs08Ctx[devIdx].adcVds[chIdx];

            retVal = E_OK;
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_SetPortTps2hcs08
 *      Phase 3: Mode-based SetPort interface for GetPort/SetPort compatibility.
 *      Provides unified multi-function access to TPS2HCS08 channels.
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_SetPortTps2hcs08(uint8 devIdx, uint8 chIdx,
                                        uint16 value, uint8 mode)
{
    Std_ReturnType retVal = E_NOT_OK;
    tTps2hcs08Ctx *pCtx;

    /* Validate parameters */
    if ((devIdx >= TPS2HCS08_DEV_MAX) || (chIdx >= TPS2HCS08_CH_MAX))
    {
        return E_NOT_OK;
    }

    pCtx = &exVioDbTps2hcs08Ctx[devIdx];

    /* Check device presence */
    if (pCtx->devPresent == FALSE)
    {
        return E_NOT_OK;
    }

    /* Dispatch by mode */
    switch (mode)
    {
        case 0u:  /* Output state (ON/OFF) */
            if (value == 0u)
            {
                /* Turn OFF */
                if (chIdx == TPS2HCS08_CH1)
                    pCtx->swState.bits.CH1_ON = 0u;
                else
                    pCtx->swState.bits.CH2_ON = 0u;
                retVal = E_OK;
            }
            else
            {
                /* Turn ON */
                if (chIdx == TPS2HCS08_CH1)
                    pCtx->swState.bits.CH1_ON = 1u;
                else
                    pCtx->swState.bits.CH2_ON = 1u;
                retVal = E_OK;
            }

            /* Write to hardware */
            if (retVal == E_OK)
            {
                retVal = ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                            TPS2HCS08_REG_SW_STATE, pCtx->swState.word);
            }
            break;

        case 1u:  /* PWM duty cycle (0~255) */
            if (value <= 0xFFu)
            {
                pCtx->pwmCh[chIdx].bits.PWM_DTY_CHx = (uint8)value;
                retVal = ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                    (chIdx == TPS2HCS08_CH1) ? TPS2HCS08_REG_PWM_CH1 : TPS2HCS08_REG_PWM_CH2,
                    pCtx->pwmCh[chIdx].word);
            }
            break;

        case 2u:  /* Event clear - not applicable for TPS2HCS08 */
            /* TPS2HCS08 does not have separate event registers */
            retVal = E_OK;  /* No operation needed */
            break;

        case 3u:  /* Fault clear */
            /* Read GLOBAL_FAULT_TYPE to clear latched faults */
            {
                uint16 dummy;
                retVal = ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                            TPS2HCS08_REG_GLOBAL_FAULT_TYPE, &dummy);
                if (retVal == E_OK)
                {
                    /* Read FLT_STAT_CHx to clear channel-specific faults */
                    retVal = ExVioDb_ReadRegister_Tps2hcs08(devIdx,
                        (chIdx == TPS2HCS08_CH1) ? TPS2HCS08_REG_FLT_STAT_CH1 : TPS2HCS08_REG_FLT_STAT_CH2,
                        &dummy);
                }
            }
            break;

        case 4u:  /* PWM frequency (0~7) */
            if (value <= 0x7u)
            {
                pCtx->pwmCh[chIdx].bits.PWM_FREQ_CHx = (uint8)value;
                retVal = ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                    (chIdx == TPS2HCS08_CH1) ? TPS2HCS08_REG_PWM_CH1 : TPS2HCS08_REG_PWM_CH2,
                    pCtx->pwmCh[chIdx].word);
            }
            break;

        default:
            retVal = E_NOT_OK;
            break;
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_GetPortTps2hcs08
 *      Phase 3: Mode-based GetPort interface for GetPort/SetPort compatibility.
 *      Provides unified multi-function read access to TPS2HCS08 channels.
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_GetPortTps2hcs08(uint8 devIdx, uint8 chIdx,
                                        uint16 *value, uint8 mode)
{
    Std_ReturnType retVal = E_NOT_OK;
    const tTps2hcs08Ctx *pCtx;

    /* Validate parameters */
    if ((value == NULL_PTR) || (devIdx >= TPS2HCS08_DEV_MAX) || (chIdx >= TPS2HCS08_CH_MAX))
    {
        return E_NOT_OK;
    }

    pCtx = &exVioDbTps2hcs08Ctx[devIdx];

    /* Check device presence */
    if (pCtx->devPresent == FALSE)
    {
        return E_NOT_OK;
    }

    /* Dispatch by mode */
    switch (mode)
    {
        case 0u:  /* Output state (shadow value) */
            if (chIdx == TPS2HCS08_CH1)
                *value = (uint16)pCtx->swState.bits.CH1_ON;
            else
                *value = (uint16)pCtx->swState.bits.CH2_ON;
            retVal = E_OK;
            break;

        case 1u:  /* PWM duty cycle */
            *value = (uint16)pCtx->pwmCh[chIdx].bits.PWM_DTY_CHx;
            retVal = E_OK;
            break;

        case 2u:  /* Event status - not applicable for TPS2HCS08 */
            *value = 0u;
            retVal = E_OK;
            break;

        case 3u:  /* Fault status */
            /* Return combined fault status from last read */
            *value = pCtx->fltStatCh[chIdx].word;
            retVal = E_OK;
            break;

        case 4u:  /* PWM frequency */
            *value = (uint16)pCtx->pwmCh[chIdx].bits.PWM_FREQ_CHx;
            retVal = E_OK;
            break;

        case 5u:  /* Current measurement (ISNS) */
            ExVioDb_ReadAdcResult_Tps2hcs08(devIdx, chIdx);
            *value = pCtx->adcIsns[chIdx];
            retVal = E_OK;
            break;

        default:
            retVal = E_NOT_OK;
            break;
    }

    return retVal;
}

/* End of file ExVioDb_Tps2hcs08.c                                            */
