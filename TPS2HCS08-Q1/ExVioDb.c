/*******************************************************************************
 *  File            : ExVioDb.c
 *  Component       : SWC_EXVIODB / EX_VIO_DB
 *  Description     : Component level implementation of the external vehicle IO
 *                    database SWC. It owns the component state sequence and
 *                    drives the setup scan / run scan of every external IO
 *                    device driver from the 10ms RTE runnable.
 *
 *                    [ state sequence ]
 *                      DISABLE -> EXVIO_SETUP -> RUN
 *                                      |
 *                                      +-------> ERROR (setup timeout)
 *
 *  Called from     : RTE ( RE_Swc_ExVioDb_Init , RE_Swc_ExVioDb_Task_10ms )
 *  Calls           : ExVioDb_SetupScnXxxReg() / ExVioDb_RunScnXxxReg()
 ******************************************************************************/

/*==============================================================================
 *  INCLUDES
 *============================================================================*/
#include "ExVioDb.h"

/*==============================================================================
 *  LOCAL DEFINE
 *============================================================================*/
#define EXVIODB_MS_TO_TICK(ms)          ((uint16)((ms) / EXVIODB_TASK_PERIOD_MS))

/* setup scan supervision : the setup of every device must be finished within
 * this time, otherwise the component goes to EXVIODB_STATE_ERROR.            */
#define EXVIODB_TICK_SETUP_TIMEOUT      EXVIODB_MS_TO_TICK(10000u)

/*==============================================================================
 *  LOCAL VARIABLE
 *============================================================================*/
D_STATIC uint16 exVioDbSetupTick;

/*==============================================================================
 *  COMPONENT BASE LAYER
 *      Only compiled for the stand alone delivery. Set
 *      EXVIODB_PROVIDE_BASE_LAYER to STD_OFF when the file is merged into the
 *      existing SWC_EXVIODB project, the symbols below are provided there.
 *============================================================================*/
#if (EXVIODB_PROVIDE_BASE_LAYER == STD_ON)

tExVioDbStateSeq        exVioDbStateSeq       = EXVIODB_STATE_DISABLE;
uint16                  evdbNotiInputChanged  = 0u;

/* vehicle IO signal DB instance ( generated from the DB specification )      */
const tExVioDbRec       exVioDbRec[EXVIODB_SIG_MAX] = { { 0u } };
uint16                  exVioDbMemCnt         = 0u;

/* --- sub device driver stub ---------------------------------------------- */
#if (EXVIODB_USE_TIC12400 == STD_ON)
uint8 exVioDbTicSetupScnState  = TIC_SETUP_SCN_COMPLETE;
void ExVioDb_SetupScnTicMSDIReg(void)   { /* provided by ExVioDb_Tic12400.c  */ }
void ExVioDb_ReadTic12400FaultReg(void) { /* provided by ExVioDb_Tic12400.c  */ }
#endif

#if (EXVIODB_USE_DRV8912 == STD_ON)
uint8 exVioDbDrvSetupScnState  = DRV_SETUP_SCN_COMPLETE;
void ExVioDb_SetupScnDrv8912Reg(void)   { /* provided by ExVioDb_Drv8912.c   */ }
#endif

#if (EXVIODB_USE_MPQ6620 == STD_ON)
uint8 exVioDbMpqSetupScnState  = MPQ_SETUP_SCN_COMPLETE;
void ExVioDb_SetupScnMpq6620Reg(void)   { /* provided by ExVioDb_Mpq6620.c   */ }
#endif

#if (EXVIODB_USE_VNFD1248 == STD_ON)
uint8 exVioDbVnfdSetupScnState = VNFD_SETUP_SCN_COMPLETE;
void ExVioDb_SetupScnVnfd1248Reg(void)  { /* provided by ExVioDb_Vnfd1248.c  */ }
#endif

void ExVioDb_TaskSigDbUpdate(void)      { /* provided by the base layer      */ }
void ExVioDb_TaskInputReg(void)         { /* provided by the base layer      */ }
void ExVioDb_TaskOutputLpReg(void)      { /* provided by the base layer      */ }
void ExVioDb_TaskOutputHpReg(void)      { /* provided by the base layer      */ }
void ExVioDb_TaskOutputVhpReg(void)     { /* provided by the base layer      */ }

/* --- RTE write stub ------------------------------------------------------- */
Std_ReturnType Rte_Write_P_SR_EXVIODB_NOTI_StatusEvent(uint8 status)
{
    (void)status;
    return E_OK;
}

Std_ReturnType Rte_Write_P_SR_EXVIODB_NOTI_InputChanged(sint16 changed)
{
    (void)changed;
    return E_OK;
}

#endif /* EXVIODB_PROVIDE_BASE_LAYER */

/*==============================================================================
 *  LOCAL FUNCTION PROTOTYPE
 *============================================================================*/
D_STATIC void           ExVioDb_SetupScnSupervision(void);
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
D_STATIC Std_ReturnType ExVioDb_ResolveTps2hcs08(uint16 sigIdx,
                                                 uint8 *devIdx,
                                                 uint8 *chIdx);
#endif

/*******************************************************************************
 *  SECTION 1 : RTE RUNNABLE ENTITY
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  RE_Swc_ExVioDb_Init
 *      Component initialisation runnable. Sets the shadow register of every
 *      external IO device to the project default value and starts the setup
 *      scan sequence.
 *----------------------------------------------------------------------------*/
FUNC(void, SWC_EXVIODB_CODE) RE_Swc_ExVioDb_Init(void)
{
    exVioDbSetupTick     = 0u;
    evdbNotiInputChanged = 0u;

#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    ExVioDb_InitRegValue_Tps2hcs08();
#endif

    exVioDbStateSeq = EXVIODB_STATE_EXVIO_SETUP;

    TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
        "[EXVIODB] INIT DONE -> EXVIO SETUP STATE...\r\n");
}

/*------------------------------------------------------------------------------
 *  RE_Swc_ExVioDb_Task_10ms
 *      10ms cyclic runnable of SWC_EXVIODB.
 *
 *      EXVIODB_STATE_EXVIO_SETUP : the setup scan of every device is called
 *          once per task. When all of them report COMPLETE the component
 *          notifies the RTE and enters EXVIODB_STATE_RUN.
 *      EXVIODB_STATE_RUN         : cyclic signal DB update, input scan, output
 *          register update and device run scan.
 *----------------------------------------------------------------------------*/
FUNC(void, SWC_EXVIODB_CODE) RE_Swc_ExVioDb_Task_10ms(void)
{
    switch (exVioDbStateSeq)
    {
        case EXVIODB_STATE_LOAD_DB:
            ExVioDb_LoadDb();
            break;

        case EXVIODB_STATE_DISABLE:
            break;

        case EXVIODB_STATE_EXVIO_SETUP:
#if (EXVIODB_USE_TIC12400 == STD_ON)
            ExVioDb_SetupScnTicMSDIReg();
#endif
#if (EXVIODB_USE_DRV8912 == STD_ON)
            ExVioDb_SetupScnDrv8912Reg();
#endif
#if (EXVIODB_USE_MPQ6620 == STD_ON)
            ExVioDb_SetupScnMpq6620Reg();
#endif
#if (EXVIODB_USE_VNFD1248 == STD_ON)
            ExVioDb_SetupScnVnfd1248Reg();
#endif
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
            ExVioDb_SetupScnTps2hcs08Reg();
#endif

            if (ExVioDb_IsSetupComplete() == TRUE)
            {
                (void)Rte_Write_P_SR_EXVIODB_NOTI_StatusEvent(
                                                    EXVIODB_NOTI_STATUS_READY);
                (void)Rte_Write_P_SR_EXVIODB_NOTI_InputChanged(
                                                (sint16)evdbNotiInputChanged);

                TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
                    "[EXVIODB] ALL DEVICE SETUP COMPLETE -> RUN STATE...\r\n");

                exVioDbStateSeq = EXVIODB_STATE_RUN;
            }
            else
            {
                ExVioDb_SetupScnSupervision();
            }
            break;

        case EXVIODB_STATE_RUN:
            ExVioDb_TaskSigDbUpdate();
            ExVioDb_TaskInputReg();
            ExVioDb_TaskOutputLpReg();
            ExVioDb_TaskOutputHpReg();
            ExVioDb_TaskOutputVhpReg();
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
            ExVioDb_RunScnTps2hcs08Reg();
#endif
#if (EXVIODB_USE_TIC12400 == STD_ON)
            ExVioDb_ReadTic12400FaultReg();
#endif
            break;

        case EXVIODB_STATE_ERROR:
            break;

        default:
            break;
    }
}

/*******************************************************************************
 *  SECTION 2 : STATE SUPERVISION
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_SetupScnSupervision
 *      Setup scan timeout supervision. A device that never reaches its
 *      COMPLETE state ( SPI failure, device not mounted ) must not block the
 *      component in EXVIODB_STATE_EXVIO_SETUP for ever.
 *----------------------------------------------------------------------------*/
D_STATIC void ExVioDb_SetupScnSupervision(void)
{
    if (exVioDbSetupTick < EXVIODB_TICK_SETUP_TIMEOUT)
    {
        exVioDbSetupTick++;
    }
    else
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[EXVIODB] SETUP SCAN TIMEOUT. tic=%d drv=%d mpq=%d vnfd=%d tps=%d\r\n",
#if (EXVIODB_USE_TIC12400 == STD_ON)
            (int)exVioDbTicSetupScnState,
#else
            (int)0,
#endif
#if (EXVIODB_USE_DRV8912 == STD_ON)
            (int)exVioDbDrvSetupScnState,
#else
            (int)0,
#endif
#if (EXVIODB_USE_MPQ6620 == STD_ON)
            (int)exVioDbMpqSetupScnState,
#else
            (int)0,
#endif
#if (EXVIODB_USE_VNFD1248 == STD_ON)
            (int)exVioDbVnfdSetupScnState,
#else
            (int)0,
#endif
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
            (int)ExVioDb_GetSetupScnState_Tps2hcs08());
#else
            (int)0);
#endif

        exVioDbStateSeq = EXVIODB_STATE_ERROR;
    }
}

D_STATIC void ExVioDb_LoadDb(void) 
{
    (void)ExVioDb_InitRegValue_LoadDb();
}

/*------------------------------------------------------------------------------
 *  ExVioDb_IsSetupComplete
 *      TRUE when the setup scan of every used device is finished.
 *----------------------------------------------------------------------------*/
boolean ExVioDb_IsSetupComplete(void)
{
    boolean complete = TRUE;

#if (EXVIODB_USE_TIC12400 == STD_ON)
    if (exVioDbTicSetupScnState != TIC_SETUP_SCN_COMPLETE)
    {
        complete = FALSE;
    }
#endif
#if (EXVIODB_USE_DRV8912 == STD_ON)
    if (exVioDbDrvSetupScnState != DRV_SETUP_SCN_COMPLETE)
    {
        complete = FALSE;
    }
#endif
#if (EXVIODB_USE_MPQ6620 == STD_ON)
    if (exVioDbMpqSetupScnState != MPQ_SETUP_SCN_COMPLETE)
    {
        complete = FALSE;
    }
#endif
#if (EXVIODB_USE_VNFD1248 == STD_ON)
    if (exVioDbVnfdSetupScnState != VNFD_SETUP_SCN_COMPLETE)
    {
        complete = FALSE;
    }
#endif
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    if (ExVioDb_GetSetupScnState_Tps2hcs08() != (uint8)TPS2HCS08_SETUP_SCN_COMPLETE)
    {
        complete = FALSE;
    }
#endif

    return complete;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_GetStateSeq
 *----------------------------------------------------------------------------*/
tExVioDbStateSeq ExVioDb_GetStateSeq(void)
{
    return exVioDbStateSeq;
}

/*******************************************************************************
 *  SECTION 3 : SIGNAL DB RECORD RESOLUTION
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_FindSigIdx
 *      Searches the vehicle IO signal DB for the record of my standard
 *      controller that is assigned to the given IC category / device index /
 *      pin. Returns EXVIODB_SIG_IDX_INVALID when the record does not exist.
 *----------------------------------------------------------------------------*/
uint16 ExVioDb_FindSigIdx(uint8 cat1, uint8 ic, uint8 pin)
{
    uint16 sigIndex;
    uint16 retIdx = EXVIODB_SIG_IDX_INVALID;

    for (sigIndex = 0u; sigIndex < exVioDbMemCnt; sigIndex++)
    {
        if (((uint8)exVioDbRec[sigIndex].SC    == (uint8)EXVIODB_MY_SC_ID) &&
            ((uint8)exVioDbRec[sigIndex].CAT_1 == cat1)                    &&
            ((uint8)exVioDbRec[sigIndex].IC    == ic)                      &&
            ((uint8)exVioDbRec[sigIndex].PIN   == pin)                     &&
            ((uint8)exVioDbRec[sigIndex].USED  != 0u))
        {
            retIdx = sigIndex;
            break;
        }
    }

    return retIdx;
}

#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
/*------------------------------------------------------------------------------
 *  ExVioDb_ResolveTps2hcs08
 *      Converts a signal DB record index into the TPS2HCS08-Q1 device index
 *      ( IC ) and channel index ( PIN ).
 *----------------------------------------------------------------------------*/
D_STATIC Std_ReturnType ExVioDb_ResolveTps2hcs08(uint16 sigIdx,
                                                 uint8 *devIdx,
                                                 uint8 *chIdx)
{
    Std_ReturnType retVal = E_NOT_OK;

    *devIdx = (uint8)exVioDbRec[sigIdx].IC;

    switch ((uint8)exVioDbRec[sigIdx].PIN)
    {
        case DB_PIN_IC_PIN_1:
            *chIdx = TPS2HCS08_CH1;
            retVal = E_OK;
            break;

        case DB_PIN_IC_PIN_2:
            *chIdx = TPS2HCS08_CH2;
            retVal = E_OK;
            break;

        default:
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[EXVIODB] INVALID PIN. sigId=%d pin=%d\r\n",
                (int)sigIdx, (int)exVioDbRec[sigIdx].PIN);
            break;
    }

    if ((retVal == E_OK) && (*devIdx >= TPS2HCS08_DEV_MAX))
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[EXVIODB] INVALID IC. sigId=%d ic=%d\r\n",
            (int)sigIdx, (int)(*devIdx));
        retVal = E_NOT_OK;
    }

    return retVal;
}
#endif /* EXVIODB_USE_TPS2HCS08 */

/*******************************************************************************
 *  SECTION 4 : COMPONENT LEVEL SERVICE API
 ******************************************************************************/
/*------------------------------------------------------------------------------
 *  ExVioDb_SetOutput
 *      Device independent output ON / OFF request. The target device driver is
 *      selected from the CAT_1 parameter of the signal DB record.
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_SetOutput(uint16 sigIdx, boolean onOff)
{
    Std_ReturnType retVal = E_NOT_OK;
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    uint8          devIdx;
    uint8          chIdx;
#endif

    if (sigIdx >= exVioDbMemCnt)
    {
        return E_NOT_OK;
    }

    if (exVioDbStateSeq != EXVIODB_STATE_RUN)
    {
        return E_NOT_OK;
    }

    switch ((uint8)exVioDbRec[sigIdx].CAT_1)
    {
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
        case DB_CAT1_E_FUSE_TPS2HCS08:
            if (ExVioDb_ResolveTps2hcs08(sigIdx, &devIdx, &chIdx) == E_OK)
            {
                retVal = ExVioDb_SetChannelOutput_Tps2hcs08(devIdx, chIdx, onOff);
            }
            break;
#endif

        default:
            /* the remaining IC categories are served by the output register
             * tasks of the base layer ( ExVioDb_TaskOutputXxxReg )           */
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[EXVIODB] SET OUTPUT NOT SUPPORTED. sigId=%d cat1=%d\r\n",
                (int)sigIdx, (int)exVioDbRec[sigIdx].CAT_1);
            break;
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_GetAdcValue
 *      Device independent ADC monitoring value request
 *      ( output current / junction temperature / VDS ).
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_GetAdcValue(uint16 sigIdx, uint16 *isns, uint16 *tsns,
                                   uint16 *vds)
{
    Std_ReturnType retVal = E_NOT_OK;
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    uint8          devIdx;
    uint8          chIdx;
#endif

    if ((sigIdx >= exVioDbMemCnt) || (isns == NULL_PTR) ||
        (tsns == NULL_PTR) || (vds == NULL_PTR))
    {
        return E_NOT_OK;
    }

    switch ((uint8)exVioDbRec[sigIdx].CAT_1)
    {
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
        case DB_CAT1_E_FUSE_TPS2HCS08:
            if (ExVioDb_ResolveTps2hcs08(sigIdx, &devIdx, &chIdx) == E_OK)
            {
                retVal = ExVioDb_GetAdcValue_Tps2hcs08(devIdx, chIdx,
                                                       isns, tsns, vds);
            }
            break;
#endif

        default:
            break;
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_GetDiagResult
 *      Open load / short to VBB diagnostic result of the setup scan
 *      ( see tTps2hcs08DiagResult ).
 *----------------------------------------------------------------------------*/
Std_ReturnType ExVioDb_GetDiagResult(uint16 sigIdx, uint8 *diagResult)
{
    Std_ReturnType retVal = E_NOT_OK;
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    uint8          devIdx;
    uint8          chIdx;
#endif

    if ((sigIdx >= exVioDbMemCnt) || (diagResult == NULL_PTR))
    {
        return E_NOT_OK;
    }

    switch ((uint8)exVioDbRec[sigIdx].CAT_1)
    {
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
        case DB_CAT1_E_FUSE_TPS2HCS08:
            if (ExVioDb_ResolveTps2hcs08(sigIdx, &devIdx, &chIdx) == E_OK)
            {
                retVal = ExVioDb_GetDiagResult_Tps2hcs08(devIdx, chIdx,
                                                         diagResult);
            }
            break;
#endif

        default:
            break;
    }

    return retVal;
}

/*------------------------------------------------------------------------------
 *  ExVioDb_ReqSleep / ExVioDb_ReqWakeUp
 *      Sleep / wake-up request of the vehicle power mode manager. The request
 *      is forwarded to every device driver that supports a low power mode.
 *----------------------------------------------------------------------------*/
void ExVioDb_ReqSleep(boolean req)
{
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    ExVioDb_ReqSleep_Tps2hcs08(req);
#else
    (void)req;
#endif
}

void ExVioDb_ReqWakeUp(void)
{
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
    ExVioDb_ReqWakeUp_Tps2hcs08();
#endif
}

/* End of file ExVioDb.c                                                      */
