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
extern boolean ExVioDb_Tps2hcs08_Port_SpiTransfer(uint8 devIdx,
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

/* SDO header ( GLOBAL_FAULT_TYPE[15:8] ) of the last SPI transaction         */
D_STATIC uint8                   exVioDbTps2hcs08SdoHeader[TPS2HCS08_DEV_MAX];

/* register write skip mask by DB parsing result                              */
D_STATIC uint16                  exVioDbTps2hcs08SkipMask[TPS2HCS08_DEV_MAX][TPS2HCS08_CH_MAX];

D_STATIC uint16                  exVioDbTps2hcs08WaitTick;
D_STATIC uint16                  exVioDbTps2hcs08WdTick;
D_STATIC uint16                  exVioDbTps2hcs08LpmTick;
D_STATIC boolean                 exVioDbTps2hcs08SleepReq;
D_STATIC boolean                 exVioDbTps2hcs08WakeUpReq;
D_STATIC boolean                 exVioDbTps2hcs08ReCfgReq;

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
D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(uint8 devIdx, uint8 addr, uint16 payload);
D_STATIC Std_ReturnType ExVioDb_ReadRegister_Tps2hcs08(uint8 devIdx, uint8 addr, uint16 *readValue);

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
 *  ExVioDb_WriteRegister_Tps2hcs08
 *      24bit write frame : [23]=1 [22:16]=ADDR [15:0]=DATA
 *----------------------------------------------------------------------------*/
D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(uint8 devIdx, uint8 addr, uint16 payload)
{
    uint8           txBuf[TPS2HCS08_SPI_FRAME_LEN];
    uint8           rxBuf[TPS2HCS08_SPI_FRAME_LEN];
    Std_ReturnType  retVal = E_NOT_OK;

    if (devIdx < TPS2HCS08_DEV_MAX)
    {
        txBuf[0] = (uint8)(TPS2HCS08_SPI_CMD_WRITE | (addr & TPS2HCS08_SPI_ADDR_MASK));
        txBuf[1] = (uint8)((payload >> 8u) & 0x00FFu);
        txBuf[2] = (uint8)(payload & 0x00FFu);

        if (ExVioDb_Tps2hcs08_Port_SpiTransfer(devIdx, txBuf, rxBuf,
                                               TPS2HCS08_SPI_FRAME_LEN) == TRUE)
        {
            /* SDO[23:16] is always GLOBAL_FAULT_TYPE[15:8] */
            exVioDbTps2hcs08SdoHeader[devIdx] = rxBuf[0];
            retVal = E_OK;
        }
        else
        {
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] SPI WRITE FAIL. dev=%d addr=0x%02X\r\n", devIdx, addr);
        }
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ReadRegister_Tps2hcs08
 *      The 16bit "Data Out" of SDO is always the data of the PREVIOUS SPI
 *      frame (Figure 8-8), therefore a read needs 2 transactions.
 *----------------------------------------------------------------------------*/
D_STATIC Std_ReturnType ExVioDb_ReadRegister_Tps2hcs08(uint8 devIdx, uint8 addr, uint16 *readValue)
{
    uint8           txBuf[TPS2HCS08_SPI_FRAME_LEN];
    uint8           rxBuf[TPS2HCS08_SPI_FRAME_LEN];
    Std_ReturnType  retVal = E_NOT_OK;

    if ((devIdx < TPS2HCS08_DEV_MAX) && (readValue != NULL_PTR))
    {
        txBuf[0] = (uint8)(TPS2HCS08_SPI_CMD_READ | (addr & TPS2HCS08_SPI_ADDR_MASK));
        txBuf[1] = 0x00u;
        txBuf[2] = 0x00u;

        /* 1st frame : send the read command                                  */
        if (ExVioDb_Tps2hcs08_Port_SpiTransfer(devIdx, txBuf, rxBuf,
                                               TPS2HCS08_SPI_FRAME_LEN) == TRUE)
        {
            /* 2nd frame : dummy read, SDO carries the data of the 1st frame  */
            if (ExVioDb_Tps2hcs08_Port_SpiTransfer(devIdx, txBuf, rxBuf,
                                                   TPS2HCS08_SPI_FRAME_LEN) == TRUE)
            {
                exVioDbTps2hcs08SdoHeader[devIdx] = rxBuf[0];
                *readValue = (uint16)(((uint16)rxBuf[1] << 8u) | (uint16)rxBuf[2]);
                retVal = E_OK;
            }
        }

        if (retVal != E_OK)
        {
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] SPI READ FAIL. dev=%d addr=0x%02X\r\n", devIdx, addr);
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
            pCtx->ilimCfgCh[chIdx].bits.I2T_EN_CHx          = 1u;
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

        exVioDbTps2hcs08SdoHeader[devIdx] = 0u;
    }

    exVioDbTps2hcs08WaitTick   = 0u;
    exVioDbTps2hcs08WdTick     = 0u;
    exVioDbTps2hcs08LpmTick    = 0u;
    exVioDbTps2hcs08SleepReq   = FALSE;
    exVioDbTps2hcs08WakeUpReq  = FALSE;
    exVioDbTps2hcs08ReCfgReq   = FALSE;
    exVioDbTps2hcs08RunState   = TPS2HCS08_RUN_ACTIVE;
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

        for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
        {
            if (exVioDbTps2hcs08Ctx[devIdx].devPresent != TRUE)
            {
                continue;
            }

            if (ExVioDb_ReadRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_ID, &devId) == E_OK)
            {
                if ((devId != TPS2HCS08_DEV_ID_VER_A) && (devId != TPS2HCS08_DEV_ID_VER_B))
                {
                    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                        "[TPS2HCS08] INVALID DEV_ID. dev=%d id=0x%04X\r\n", devIdx, devId);
                    retVal = TPS2HCS08_BUSY;
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
        pCtx->swState.word = 0x0000u;
        if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_SW_STATE,
                                            pCtx->swState.word) != E_OK)
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

            pCtx->chConfig[chIdx].bits.OL_SVBB_EN_CHx = TPS2HCS08_OL_SVBB_PULLDOWN;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                    pCtx->chConfig[chIdx].word);
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

            pCtx->chConfig[chIdx].bits.OL_SVBB_EN_CHx = TPS2HCS08_OL_SVBB_PULLUP;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                    pCtx->chConfig[chIdx].word);
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
            pCtx->chConfig[chIdx].bits.OL_SVBB_EN_CHx =
                (pCtx->chCfg[chIdx].oldUse == TRUE) ? TPS2HCS08_OL_SVBB_PULLUP
                                                    : TPS2HCS08_OL_SVBB_DISABLE;

            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx,
                    TPS2HCS08_CH_REG(TPS2HCS08_REG_CH1_CONFIG, chIdx),
                    pCtx->chConfig[chIdx].word);
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
                    pCtx->swState.bits.CH1_ON = TPS2HCS08_CH_ON;
                }
                else
                {
                    pCtx->swState.bits.CH2_ON = TPS2HCS08_CH_ON;
                }
            }
        }

        (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_SW_STATE,
                                              pCtx->swState.word);
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
            ExVioDb_InitRegValue_Tps2hcs08();
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DB_PARSING;
            break;

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
            if (ExVioDb_WaitReadyDone_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] INIT & ABIST DONE -> CONFIG STATE...\r\n");
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CLEAR_POR;
            }
            break;

        case TPS2HCS08_SETUP_SCN_CLEAR_POR:                 /* process #4     */
            if (ExVioDb_ClearPorFault_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_WRITE;
            }
            break;

        case TPS2HCS08_SETUP_SCN_CONFIG_WRITE:              /* process #5     */
            if (ExVioDb_WriteConfig_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_VERIFY;
            }
            break;

        case TPS2HCS08_SETUP_SCN_CONFIG_VERIFY:             /* process #5     */
            if (ExVioDb_VerifyConfig_Tps2hcs08() == TPS2HCS08_COMPLETE)
            {
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] REGISTER CONFIGURATION DONE...\r\n");
                exVioDbTps2hcs08WaitTick      = 0u;
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_PULLDOWN;
            }
            else
            {
                exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_WRITE;
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
            exVioDbTps2hcs08WdTick        = 0u;
            exVioDbTps2hcs08RunState      = TPS2HCS08_RUN_ACTIVE;
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_COMPLETE;
            break;

        case TPS2HCS08_SETUP_SCN_COMPLETE:
            /* nothing */
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

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        if (enable == TRUE)
        {
            /* AUTO_LPM_EXIT_CHx = 0 ( entry condition )                      */
            pCtx->lpm.bits.AUTO_LPM_EXIT_CH1 = 0u;
            pCtx->lpm.bits.AUTO_LPM_EXIT_CH2 = 0u;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                                  pCtx->lpm.word);

            /* all ADC diagnostics except ISNS must be disabled               */
            pCtx->adcConfig.bits.ADC_VSNS_DIS = 1u;
            pCtx->adcConfig.bits.ADC_VDS_DIS  = 1u;
            pCtx->adcConfig.bits.ADC_TSNS_DIS = 1u;
            pCtx->adcConfig.bits.ADC_VBB_DIS  = 1u;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                                  pCtx->adcConfig.word);

            /* watchdog disable + AUTO_LPM_ENTRY = 1                          */
            pCtx->devConfig.bits.WD_EN          = 0u;
            pCtx->devConfig.bits.AUTO_LPM_ENTRY = 1u;
        }
        else
        {
            pCtx->devConfig.bits.AUTO_LPM_ENTRY = 0u;
            pCtx->devConfig.bits.WD_EN          = 1u;
        }

        (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_CONFIG,
                                              pCtx->devConfig.word);

        if (enable != TRUE)
        {
            /* restore the ADC diagnostic setting of the signal DB            */
            uint8 chIdx;

            pCtx->adcConfig.bits.ADC_VSNS_DIS = 0u;
            (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                                  pCtx->adcConfig.word);

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

        if (pCtx->devPresent != TRUE)
        {
            continue;
        }

        if (exit == TRUE)
        {
            pCtx->lpm.bits.AUTO_LPM_EXIT_CH1 =
                (pCtx->chCfg[TPS2HCS08_CH1].used == TRUE) ? 1u : 0u;
            pCtx->lpm.bits.AUTO_LPM_EXIT_CH2 =
                (pCtx->chCfg[TPS2HCS08_CH2].used == TRUE) ? 1u : 0u;
        }
        else
        {
            pCtx->lpm.bits.AUTO_LPM_EXIT_CH1 = 0u;
            pCtx->lpm.bits.AUTO_LPM_EXIT_CH2 = 0u;
        }

        (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                              pCtx->lpm.word);
    }
}

/*******************************************************************************
 *  SECTION 6 : FAULT LOG EVALUATION
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_EvalGlobalFaultLog_Tps2hcs08
 *      Prints the field name once per fault occurrence ( one shot latch ).
 *----------------------------------------------------------------------------*/
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
            if (chIdx == TPS2HCS08_CH1)
            {
                pCtx->swState.bits.CH1_ON = (onOff == TRUE) ? TPS2HCS08_CH_ON
                                                            : TPS2HCS08_CH_OFF;
            }
            else
            {
                pCtx->swState.bits.CH2_ON = (onOff == TRUE) ? TPS2HCS08_CH_ON
                                                            : TPS2HCS08_CH_OFF;
            }

            retVal = ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_SW_STATE,
                                                     pCtx->swState.word);
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

/* End of file ExVioDb_Tps2hcs08.c                                            */
