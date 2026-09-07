#include <stdio.h>
#include <string.h>

#include "Tps2hcs08_Map.h"

#define TPS2HCS08_MOCK_SEQID                (0u)


/* Mapping APIs apply one DB value to the whole chain.
 * Register I/O already handles the chain, so it stays outside device loops.
 */

/* =========================================================================
 * Logging
 * ========================================================================= */

D_STATIC void ExVioDb_LogMappingError(uint16 signalId,
                                      const char *parameter,
                                      uint8 ch,
                                      uint8 id,
                                      const char *reason)
{
    /*
     * Target replacement:
     *
     * TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(
     *     TAG_EEVP_EXVIODB,
     *     "[EXVIODB][TPS2HCS08] SIG_ID=%u CH=%u PARAM=%s ID=%u %s\r\n",
     *     signalId, ch, parameter, id, reason);
     */
    (void)printf(
        "[EXVIODB][TPS2HCS08][ERROR] "
        "SIG_ID=%u CH=%u PARAM=%s ID=%u : %s. Register WRITE skipped.\n",
        (unsigned int)signalId,
        (unsigned int)ch,
        parameter,
        (unsigned int)id,
        reason);
}

D_STATIC void ExVioDb_LogMappingFallback(uint16 signalId,
                                         const char *parameter,
                                         uint8 ch,
                                         uint8 id,
                                         const char *fallback)
{
    (void)printf(
        "[EXVIODB][TPS2HCS08][ERROR] "
        "SIG_ID=%u CH=%u PARAM=%s ID=%u : fallback=%s applied.\n",
        (unsigned int)signalId,
        (unsigned int)ch,
        parameter,
        (unsigned int)id,
        fallback);
}

/*
 * 매핑/초기 설정/진단 함수에서 사용하는 파일 내부 전용 쓰기 보조 함수.
 * 외부 호출자가 직접 사용하는 진입 API가 아니다(D_STATIC = static).
 *
 * 하위 ExVioDb_WriteRegister_Tps2hcs08()는 전달된 payload 하나를
 * 체인 전체에 동일하게 전송하므로, 장치별 payload 차이를 표현할 수 없다.
 * 이 함수는 장치별 컨텍스트 설정이 다를 가능성을 가정하여 추가한 방어 코드다.
 * 각 컨텍스트의 해당 레지스터에서 payload를 다시 계산하고, 전달받은 값과
 * 하나라도 다르면 하위 쓰기 함수를 호출하지 않고 E_NOT_OK를 반환한다.
 * 이는 전송 전 설정 일치 검사이며, SPI 프레임 복사/전송을 수행하는 로직은 아니다.
 *
 * 모든 장치에 공통 payload를 적용하는 것이 의도라면 이 검사는 필수가 아니며,
 * 컨텍스트 값의 차이 때문에 의도한 공통 설정 전송을 막을 수 있다.
 * 검사 실패 시 호출부에서 이미 변경한 컨텍스트 값을 되돌리지는 않는다.
 *
 * 검사 통과 시 하위 쓰기 함수를 한 번 호출하고, 성공하면 각 컨텍스트에
 * 테스트용 쓰기 횟수와 마지막 전송 정보를 기록한다.
 */
D_STATIC Std_ReturnType Tps2hcs08_WriteMappedRegister(
    uint8 seqid, uint8 addr, uint16 payload)
{
    uint8 devIdx;
    uint16 devicePayload;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        const tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        switch (addr)
        {
            case TPS2HCS08_REG_LPM:
                devicePayload = Tps2hcs08_BuildLpmPayload(&pCtx->lpm);
                break;
            case TPS2HCS08_REG_FAULT_MASK:
                devicePayload = Tps2hcs08_BuildFaultMaskPayload(&pCtx->faultMask);
                break;
            case TPS2HCS08_REG_SW_STATE:
                devicePayload = Tps2hcs08_BuildSwStatePayload(&pCtx->swState);
                break;
            case TPS2HCS08_REG_DEV_CONFIG:
                devicePayload = Tps2hcs08_BuildDevConfigPayload(&pCtx->devConfig);
                break;
            case TPS2HCS08_REG_ADC_CONFIG:
                devicePayload = Tps2hcs08_BuildAdcConfigPayload(&pCtx->adcConfig);
                break;
            case TPS2HCS08_REG_PWM_CH1:
                devicePayload = Tps2hcs08_BuildPwmPayload(&pCtx->pwmCh[TPS2HCS08_CH1]);
                break;
            case TPS2HCS08_REG_PWM_CH2:
                devicePayload = Tps2hcs08_BuildPwmPayload(&pCtx->pwmCh[TPS2HCS08_CH2]);
                break;
            case TPS2HCS08_REG_ILIM_CONFIG_CH1:
                devicePayload = Tps2hcs08_BuildIlimPayload(&pCtx->ilimCfgCh[TPS2HCS08_CH1]);
                break;
            case TPS2HCS08_REG_ILIM_CONFIG_CH2:
                devicePayload = Tps2hcs08_BuildIlimPayload(&pCtx->ilimCfgCh[TPS2HCS08_CH2]);
                break;
            case TPS2HCS08_REG_CH1_CONFIG:
                devicePayload = Tps2hcs08_BuildChConfigPayload(&pCtx->chConfig[TPS2HCS08_CH1]);
                break;
            case TPS2HCS08_REG_CH2_CONFIG:
                devicePayload = Tps2hcs08_BuildChConfigPayload(&pCtx->chConfig[TPS2HCS08_CH2]);
                break;
            case TPS2HCS08_REG_I2T_CONFIG_CH1:
                devicePayload = Tps2hcs08_BuildI2tPayload(&pCtx->i2tCfgCh[TPS2HCS08_CH1]);
                break;
            case TPS2HCS08_REG_I2T_CONFIG_CH2:
                devicePayload = Tps2hcs08_BuildI2tPayload(&pCtx->i2tCfgCh[TPS2HCS08_CH2]);
                break;
            default:
                return E_NOT_OK;
        }

        if (devicePayload != payload)
        {
            ExVioDb_LogMappingError(pCtx->signalId, "CHAIN_PAYLOAD",
                0u, devIdx, "device payloads differ; broadcast is not possible");
            return E_NOT_OK;
        }
    }

    if (ExVioDb_WriteRegister_Tps2hcs08(seqid, addr, payload) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].writeCount++;
        exVioDbTps2hcs08Ctx[devIdx].lastWriteValid = TRUE;
        exVioDbTps2hcs08Ctx[devIdx].lastSeqid = seqid;
        exVioDbTps2hcs08Ctx[devIdx].lastAddr = addr;
        exVioDbTps2hcs08Ctx[devIdx].lastPayload = payload;
    }

    return E_OK;
}

/* =========================================================================
 * Common validation / address helpers
 * ========================================================================= */

D_STATIC boolean Tps2hcs08_IsValidChannel(uint8 ch)
{
    return ((ch == TPS2HCS08_CH1) ||
            (ch == TPS2HCS08_CH2)) ? TRUE : FALSE;
}

D_STATIC Std_ReturnType Tps2hcs08_CheckChannel(uint8 ch,
                                               const char *parameter,
                                               uint8 id)
{
    if (Tps2hcs08_IsValidChannel(ch) == FALSE)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId,
            parameter,
            ch,
            id,
            "invalid channel");

        return E_NOT_OK;
    }

    return E_OK;
}

D_STATIC Std_ReturnType Tps2hcs08_CheckParallelCh1Only(uint8 ch,
                                                       const char *parameter,
                                                       uint8 id)
{
    uint8 devIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if ((exVioDbTps2hcs08Ctx[devIdx].used == USED_2) &&
            (ch != TPS2HCS08_CH1))
        {
            ExVioDb_LogMappingError(
                exVioDbTps2hcs08Ctx[devIdx].signalId,
                parameter,
                ch,
                id,
                "parallel mode uses CH1 register only for this function");

            return E_NOT_OK;
        }
    }

    return E_OK;
}

D_STATIC uint8 Tps2hcs08_GetPwmAddr(uint8 ch)
{
    return (ch == TPS2HCS08_CH2)
           ? TPS2HCS08_REG_PWM_CH2
           : TPS2HCS08_REG_PWM_CH1;
}

D_STATIC uint8 Tps2hcs08_GetIlimAddr(uint8 ch)
{
    return (ch == TPS2HCS08_CH2)
           ? TPS2HCS08_REG_ILIM_CONFIG_CH2
           : TPS2HCS08_REG_ILIM_CONFIG_CH1;
}

D_STATIC uint8 Tps2hcs08_GetChConfigAddr(uint8 ch)
{
    return (ch == TPS2HCS08_CH2)
           ? TPS2HCS08_REG_CH2_CONFIG
           : TPS2HCS08_REG_CH1_CONFIG;
}

D_STATIC uint8 Tps2hcs08_GetI2tAddr(uint8 ch)
{
    return (ch == TPS2HCS08_CH2)
           ? TPS2HCS08_REG_I2T_CONFIG_CH2
           : TPS2HCS08_REG_I2T_CONFIG_CH1;
}

/* =========================================================================
 * Init
 * ========================================================================= */

void Tps2hcs08_Init(void)
{
    uint8 devIdx;

    uint8 ch;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        (void)memset(&exVioDbTps2hcs08Ctx[devIdx], 0, sizeof(exVioDbTps2hcs08Ctx[devIdx]));

        exVioDbTps2hcs08Ctx[devIdx].used = USED_1;

        /* LPM reset/project defaults */
        exVioDbTps2hcs08Ctx[devIdx].lpm.bits.AUTO_LPM_EXIT_CH2 = 0u;
        exVioDbTps2hcs08Ctx[devIdx].lpm.bits.AUTO_LPM_EXIT_CH1 = 0u;

        /* FAULT_MASK reset, then project fixed configuration changes 5/4 to 1 */
        exVioDbTps2hcs08Ctx[devIdx].faultMask.bits.MASK_SHRT_VBB = 1u;
        exVioDbTps2hcs08Ctx[devIdx].faultMask.bits.MASK_OL_OFF = 1u;
        exVioDbTps2hcs08Ctx[devIdx].faultMask.bits.MASK_SPI_ERR = 0u;
        exVioDbTps2hcs08Ctx[devIdx].faultMask.bits.MASK_WD_ERR = 0u;
        exVioDbTps2hcs08Ctx[devIdx].faultMask.bits.MASK_VBB_UVLO = 0u;

        /* SW_STATE reset */
        exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH2_ON = 0u;
        exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH1_ON = 0u;

        /* DEV_CONFIG project fixed settings */
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.CH2_LH_IN = 1u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.CH1_LH_IN = 1u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.PWM_SHIFT_DIS = 0u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.AUTO_LPM_ENTRY = 0u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.PARALLEL_12 = 0u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.WD_EN = 1u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.WD_TO = 1u;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.FLT_LTCH_DIS = 0u;

        /*
         * ADC_CONFIG reset = FF3Ah.
         * Project Write requirement changes ADC_VSNS_DIS to 0.
         */
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_ISNS_SAMPLE_CONFIG = 0u;
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_VDS_DIS = 1u;
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_VSNS_DIS = 0u;
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_TSNS_DIS = 1u;
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_ISNS_DIS = 0u;
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_VBB_DIS = 1u;
        exVioDbTps2hcs08Ctx[devIdx].adcConfig.bits.ADC_DIS = 0u;

        for (ch = 0u; ch < TPS2HCS08_CH_MAX; ch++)
        {
            exVioDbTps2hcs08Ctx[devIdx].pwmMode[ch] = PWM_O;

            /* PWM_CHx reset = F000h */
            exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_FREQ_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_DTY_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_EN_CHx = 0u;

            /*
             * ILIM_CONFIG_CHx reset = 0088h.
             * Project fixed setting changes I2T_EN = 1.
             */
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.CAP_CHRG_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.I2T_EN_CHx = 1u;
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.INRUSH_DURATION_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.INRUSH_LIMIT_CHx = 8u;
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 8u;

            /*
             * CHx_CONFIG reset = C002h.
             * Project fixed OL_SVBB_BLANK = 3.
             */
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.VSNS_DIS_CHx = 1u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.VDS_SNS_DIS_CHx = 1u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.ISNS_DIS_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.ISNS_SCALE_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_ON_EN_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_SVBB_BLANK_CHx = 3u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_PU_STR_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_SVBB_EN_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.LATCH_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.SLRT_CHx = 2u;

            /* I2T_CONFIG_CHx reset = 0000h */
            exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.TCLDN_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.SWCL_DLY_TMR_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.ISWCL_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0u;
            exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx = 0u;
        }
    }


}

void Tps2hcs08_SetSignalId(uint16 signalId)
{
    uint8 devIdx;

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].signalId = signalId;
    }
}

/* =========================================================================
 * 1. CAT_1
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapCat1(uint8 id)
{
    uint8 devIdx;

    if (id != CAT1_E_FUSE_181000)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "CAT_1", 0u, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].cat1 = id;
    }

    return E_OK;
}

/* =========================================================================
 * 2. CAT_2
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapCat2(uint8 id)
{
    uint8 devIdx;

    if (id != CAT2_ACTIVE_HIGH)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "CAT_2", 0u, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].cat2 = id;
    }

    return E_OK;
}

/* =========================================================================
 * 3. SC
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapSc(uint8 id)
{
    uint8 devIdx;

    if ((id != SC_1) &&
        (id != SC_2))
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "SC", 0u, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].sc = id;
    }

    return E_OK;
}

/* =========================================================================
 * 4. IC
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapIc(uint8 id)
{
    uint8 devIdx;

    if (id > IC_3)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "IC", 0u, id,
            "undefined mock daisy-chain ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].ic = id;
    }

    return E_OK;
}

/* =========================================================================
 * 5. PIN
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapPin(uint8 id)
{
    uint8 devIdx;

    if ((id != IC_PIN_1) &&
        (id != IC_PIN_2))
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "PIN", 0u, id,
            "undefined Signal DB PIN ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].pin = id;
    }

    return E_OK;
}

/* =========================================================================
 * 6. USED
 *
 * IC register set:
 *   - SW_STATE.CH1_ON = 0
 *   - SW_STATE.CH2_ON = 0
 *   - DEV_CONFIG.PARALLEL_12 = 0/1
 *
 * Both channels must be OFF before PARALLEL_12 changes.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapUsed(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "USED", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if ((id != USED_1) &&
        (id != USED_2))
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "USED", ch, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    if ((id == USED_2) &&
        (ch == TPS2HCS08_CH2))
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "USED", ch, id,
            "CH2 cannot be configured as USED_2");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH1_ON = 0u;
        exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH2_ON = 0u;
    }

    payload = Tps2hcs08_BuildSwStatePayload(&exVioDbTps2hcs08Ctx[0].swState);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            TPS2HCS08_REG_SW_STATE,
            payload) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].used = id;
        exVioDbTps2hcs08Ctx[devIdx].devConfig.bits.PARALLEL_12 = (id == USED_2) ? 1u : 0u;
    }

    payload = Tps2hcs08_BuildDevConfigPayload(&exVioDbTps2hcs08Ctx[0].devConfig);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        TPS2HCS08_REG_DEV_CONFIG,
        payload);
}

/* =========================================================================
 * 7. MOC
 *
 * IC register set:
 *   I2T_CONFIG_CHx.NOM_CUR_CHx
 *   I2T_CONFIG_CHx.I2T_TRIP_CHx
 *   I2T_CONFIG_CHx.SWCL_DLY_TMR_CHx = 3h
 *   I2T_CONFIG_CHx.ISWCL_CHx        = 0h
 *
 * IMPORTANT:
 * SWCL_DLY_TMR and ISWCL are explicitly assigned for EVERY valid MOC.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapMoc(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "MOC", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "MOC", id) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        switch (id)
        {
            case MOC_1A:
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x0u;
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0x0u;
                break;

            case MOC_3A:
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x0u;
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0x2u;
                break;

            case MOC_5A:
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x3u;
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0x5u;
                break;

            case MOC_10A:
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x6u;
                exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xCu;
                break;

            case MOC_15A:
                if (exVioDbTps2hcs08Ctx[devIdx].used == USED_1)
                {
                    ExVioDb_LogMappingFallback(
                        exVioDbTps2hcs08Ctx[devIdx].signalId, "MOC", ch, id, "MOC_10A");

                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x6u;
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xCu;
                }
                else
                {
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x5u;
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xAu;
                }
                break;

            case MOC_20A:
                if (exVioDbTps2hcs08Ctx[devIdx].used == USED_1)
                {
                    ExVioDb_LogMappingFallback(
                        exVioDbTps2hcs08Ctx[devIdx].signalId, "MOC", ch, id, "MOC_10A");

                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x6u;
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xCu;
                }
                else
                {
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x6u;
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xFu;
                }
                break;

            case MOC_30A:
                if (exVioDbTps2hcs08Ctx[devIdx].used == USED_2)
                {
                    ExVioDb_LogMappingFallback(
                        exVioDbTps2hcs08Ctx[devIdx].signalId, "MOC", ch, id, "MOC_20A");

                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x6u;
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xFu;
                }
                else
                {
                    ExVioDb_LogMappingFallback(
                        exVioDbTps2hcs08Ctx[devIdx].signalId, "MOC", ch, id, "MOC_10A");

                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.NOM_CUR_CHx  = 0x6u;
                    exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.I2T_TRIP_CHx = 0xCu;
                }
                break;

            default:
                ExVioDb_LogMappingError(
                    exVioDbTps2hcs08Ctx[devIdx].signalId, "MOC", ch, id,
                    "undefined Signal DB ID");
                return E_NOT_OK;
        }

        /*
         * These two fields are part of the MOC mapping table and must not be
         * omitted even though they have the same value for every MOC row.
         */
        exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.SWCL_DLY_TMR_CHx = 0x3u;
        exVioDbTps2hcs08Ctx[devIdx].i2tCfgCh[ch].bits.ISWCL_CHx      = 0x0u;
    }

    addr = Tps2hcs08_GetI2tAddr(ch);
    payload = Tps2hcs08_BuildI2tPayload(&exVioDbTps2hcs08Ctx[0].i2tCfgCh[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 8. OCP
 *
 * Signal DB -> ILIM_CONFIG_CHx.ILIMIT_SET_CHx
 *
 * DB mapping:
 *   OCP_100mV  (ID  1) -> 0h = 10A
 *   OCP_200mV  (ID  2) -> 1h = 12.5A
 *   OCP_300mV  (ID  3) -> 2h = 15A
 *   OCP_400mV  (ID  4) -> 3h = 17.5A
 *   OCP_500mV  (ID  5) -> 4h = 20A
 *   OCP_600mV  (ID  6) -> 5h = 22.5A
 *   OCP_700mV  (ID  7) -> 6h = 25A
 *   OCP_800mV  (ID  8) -> 7h = 32.5A  [single-channel default]
 *   OCP_9      (ID  9) -> 8h = 40A    [parallel-mode default]
 *   OCP_10     (ID 10) -> 9h = 47.5A
 *   OCP_11     (ID 11) -> Ah = 55A
 *
 * Parallel-mode data-sheet restriction:
 *   ILIMIT_SET_CH1 maximum supported value = 40A (8h).
 * Therefore ID 10/11 are rejected in USED_2.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapOcp(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "OCP", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "OCP", id) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        switch (id)
        {
            case OCP_100mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x0u;
                break;

            case OCP_200mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x1u;
                break;

            case OCP_300mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x2u;
                break;

            case OCP_400mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x3u;
                break;

            case OCP_500mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x4u;
                break;

            case OCP_600mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x5u;
                break;

            case OCP_700mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x6u;
                break;

            case OCP_800mV:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x7u;
                break;

            case OCP_9:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x8u;
                break;

            case OCP_10:
                if (exVioDbTps2hcs08Ctx[devIdx].used == USED_2)
                {
                    ExVioDb_LogMappingError(
                        exVioDbTps2hcs08Ctx[devIdx].signalId,
                        "OCP",
                        ch,
                        id,
                        "47.5A is not supported in parallel mode; maximum is 40A");
                    return E_NOT_OK;
                }

                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0x9u;
                break;

            case OCP_11:
                if (exVioDbTps2hcs08Ctx[devIdx].used == USED_2)
                {
                    ExVioDb_LogMappingError(
                        exVioDbTps2hcs08Ctx[devIdx].signalId,
                        "OCP",
                        ch,
                        id,
                        "55A is not supported in parallel mode; maximum is 40A");
                    return E_NOT_OK;
                }

                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.ILIMIT_SET_CHx = 0xAu;
                break;

            default:
                ExVioDb_LogMappingError(
                    exVioDbTps2hcs08Ctx[devIdx].signalId,
                    "OCP",
                    ch,
                    id,
                    "undefined Signal DB OCP ID");
                return E_NOT_OK;
        }
    }

    addr = Tps2hcs08_GetIlimAddr(ch);
    payload = Tps2hcs08_BuildIlimPayload(&exVioDbTps2hcs08Ctx[0].ilimCfgCh[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 9. RT
 *
 * TPS2HCS08 has TCLDN_CHx for I2T cool-down/retry, but the supplied
 * project material does not prove RT == TCLDN.
 *
 * Therefore RT is intentionally NOT inferred.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapRt(uint8 ch, uint8 id)
{
    if (Tps2hcs08_CheckChannel(ch, "RT", id) != E_OK)
    {
        return E_NOT_OK;
    }

    ExVioDb_LogMappingError(
        exVioDbTps2hcs08Ctx[0].signalId, "RT", ch, id,
        "RT Signal DB mapping table not supplied; no IC field inferred");

    return E_NOT_OK;
}

/* =========================================================================
 * 10. PWM
 *
 * IC register set:
 *   ILIM_CONFIG_CHx.CAP_CHRG_CHx
 *   PWM_CHx.PWM_EN_CHx
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapPwm(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 pwmAddr;
    uint8 ilimAddr;
    uint16 pwmPayload;
    uint16 ilimPayload;

    if (Tps2hcs08_CheckChannel(ch, "PWM", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "PWM", id) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        switch (id)
        {
            case PWM_O:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.CAP_CHRG_CHx = 0x0u;
                exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_EN_CHx       = 0x1u;
                break;

            case PWM_X:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.CAP_CHRG_CHx = 0x0u;
                exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_EN_CHx       = 0x0u;
                break;

            case PWM_C:
                exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.CAP_CHRG_CHx = 0x2u;
                exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_EN_CHx       = 0x0u;
                break;

            default:
                ExVioDb_LogMappingError(
                    exVioDbTps2hcs08Ctx[devIdx].signalId, "PWM", ch, id,
                    "undefined Signal DB ID");
                return E_NOT_OK;
        }

        exVioDbTps2hcs08Ctx[devIdx].pwmMode[ch] = id;
    }

    pwmAddr = Tps2hcs08_GetPwmAddr(ch);
    ilimAddr = Tps2hcs08_GetIlimAddr(ch);

    pwmPayload = Tps2hcs08_BuildPwmPayload(&exVioDbTps2hcs08Ctx[0].pwmCh[ch]);
    ilimPayload = Tps2hcs08_BuildIlimPayload(&exVioDbTps2hcs08Ctx[0].ilimCfgCh[ch]);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            pwmAddr,
            pwmPayload) != E_OK)
    {
        return E_NOT_OK;
    }

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        ilimAddr,
        ilimPayload);
}

/* =========================================================================
 * 11. OLD
 *
 * IC register set:
 *   CHx_CONFIG.OL_SVBB_EN_CHx
 *
 * Parallel mode:
 *   off-state open/short-to-VBB detection uses CH1_CONFIG only.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapOld(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "OLD", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "OLD", id) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        switch (id)
        {
            case OLD_OFF:
                exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_SVBB_EN_CHx = 0x0u;
                break;

            case OLD_PWR:
                exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_SVBB_EN_CHx = 0x2u;
                break;

            default:
                ExVioDb_LogMappingError(
                    exVioDbTps2hcs08Ctx[devIdx].signalId, "OLD", ch, id,
                    "undefined Signal DB ID");
                return E_NOT_OK;
        }
    }

    addr = Tps2hcs08_GetChConfigAddr(ch);
    payload = Tps2hcs08_BuildChConfigPayload(&exVioDbTps2hcs08Ctx[0].chConfig[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 12. PWM_F
 *
 * IC register set:
 *   PWM_CHx.PWM_FREQ_CHx
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapPwmFreq(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "PWM_F", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "PWM_F", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (id > PWM_1000HZ)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "PWM_F", ch, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].pwmCh[ch].bits.PWM_FREQ_CHx = id;
    }

    addr = Tps2hcs08_GetPwmAddr(ch);
    payload = Tps2hcs08_BuildPwmPayload(&exVioDbTps2hcs08Ctx[0].pwmCh[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 13. CT
 *
 * IC register set:
 *   ILIM_CONFIG_CHx.INRUSH_DURATION_CHx
 *
 * Project requirement:
 *   PWM_X -> fixed INRUSH_DURATION = 4h.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapCt(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "CT", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "CT", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (id > CT_50MS)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "CT", ch, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if (exVioDbTps2hcs08Ctx[devIdx].pwmMode[ch] == PWM_X)
        {
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.INRUSH_DURATION_CHx = 0x4u;
        }
        else
        {
            exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.INRUSH_DURATION_CHx = id;
        }
    }

    addr = Tps2hcs08_GetIlimAddr(ch);
    payload = Tps2hcs08_BuildIlimPayload(&exVioDbTps2hcs08Ctx[0].ilimCfgCh[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 14. SR
 *
 * IC register set:
 *   CHx_CONFIG.SLRT_CHx
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapSr(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "SR", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (id > SR_8MA)
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "SR", ch, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.SLRT_CHx = id;
    }

    addr = Tps2hcs08_GetChConfigAddr(ch);
    payload = Tps2hcs08_BuildChConfigPayload(&exVioDbTps2hcs08Ctx[0].chConfig[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 15. VOL_DET
 *
 * IC register set:
 *   CHx_CONFIG.VSNS_DIS_CHx
 *
 * NOTE:
 * ADC_CONFIG.ADC_VSNS_DIS must also be 0 globally.
 * That global bit is set to 0 by Tps2hcs08_WriteFixedInitialConfig().
 *
 * Parallel mode still supports per-channel ADC diagnostics, so CH1/CH2
 * are both allowed here.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapVolDet(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "VOL_DET", id) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        switch (id)
        {
            case VOL_DET_OFF:
                exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.VSNS_DIS_CHx = 0x1u;
                break;

            case VOL_DET_ON:
                exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.VSNS_DIS_CHx = 0x0u;
                break;

            default:
                ExVioDb_LogMappingError(
                    exVioDbTps2hcs08Ctx[devIdx].signalId, "VOL_DET", ch, id,
                    "undefined Signal DB ID");
                return E_NOT_OK;
        }
    }

    addr = Tps2hcs08_GetChConfigAddr(ch);
    payload = Tps2hcs08_BuildChConfigPayload(&exVioDbTps2hcs08Ctx[0].chConfig[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}

/* =========================================================================
 * 16. DEF_VALUE
 *
 * IC register set:
 *   SW_STATE.CHx_ON
 *
 * Parallel mode:
 *   CH1_ON controls the combined output.
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapDefValue(uint8 ch, uint8 id)
{
    uint8 devIdx;

    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "DEF_Value", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "DEF_Value", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if ((id != DEF_IDLE) &&
        (id != DEF_ACTIVE))
    {
        ExVioDb_LogMappingError(
            exVioDbTps2hcs08Ctx[0].signalId, "DEF_Value", ch, id,
            "undefined Signal DB ID");
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if (ch == TPS2HCS08_CH1)
        {
            exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH1_ON = (id == DEF_ACTIVE) ? 1u : 0u;
        }
        else
        {
            exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH2_ON = (id == DEF_ACTIVE) ? 1u : 0u;
        }
    }

    payload = Tps2hcs08_BuildSwStatePayload(&exVioDbTps2hcs08Ctx[0].swState);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        TPS2HCS08_REG_SW_STATE,
        payload);
}

/* =========================================================================
 * 17. PWM_Duty
 *
 * Supplied project mapping:
 *   PWM_X:
 *     PWM_Duty ID 0..10 -> INRUSH_LIMIT_CHx 0h..Ah
 *
 * Missing project tables:
 *   PWM_C -> not inferred
 *   PWM_O -> not inferred
 * ========================================================================= */

Std_ReturnType Tps2hcs08_MapPwmDuty(uint8 ch, uint8 id)
{
    uint8 devIdx;
    uint8 addr;
    uint16 payload;

    if (Tps2hcs08_CheckChannel(ch, "PWM_Duty", id) != E_OK)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckParallelCh1Only(ch, "PWM_Duty", id) != E_OK)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if ((exVioDbTps2hcs08Ctx[devIdx].pwmMode[ch] != PWM_X) ||
            (id > PWM_DUTY_10))
        {
            ExVioDb_LogMappingError(
                exVioDbTps2hcs08Ctx[devIdx].signalId, "PWM_Duty", ch, id,
                "unsupported PWM mode or undefined PWM_X Signal DB ID");
            return E_NOT_OK;
        }
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].ilimCfgCh[ch].bits.INRUSH_LIMIT_CHx = id;
    }

    addr = Tps2hcs08_GetIlimAddr(ch);
    payload = Tps2hcs08_BuildIlimPayload(&exVioDbTps2hcs08Ctx[0].ilimCfgCh[ch]);

    return Tps2hcs08_WriteMappedRegister(
        TPS2HCS08_MOCK_SEQID,
        addr,
        payload);
}


/* =========================================================================
 * Open / Short-to-VBB initial diagnostic
 *
 * Synchronous one-call implementation requested by project design.
 * ========================================================================= */

D_STATIC uint8 Tps2hcs08_GetFltStatAddr(uint8 ch)
{
    return (ch == TPS2HCS08_CH2)
           ? TPS2HCS08_REG_FLT_STAT_CH2
           : TPS2HCS08_REG_FLT_STAT_CH1;
}

D_STATIC void Tps2hcs08_ParseFltStat(
    uint16 payload,
    tTps2hcs08FltStatCh *reg)
{
    if (reg == (tTps2hcs08FltStatCh *)0)
    {
        return;
    }

    reg->bits.THERMAL_WRN_CHx  = (uint8)((payload >> 0u)  & 0x01u);
    reg->bits.OL_OFF_CHx       = (uint8)((payload >> 2u)  & 0x01u);
    reg->bits.SHRT_VBB_CHx     = (uint8)((payload >> 3u)  & 0x01u);
    reg->bits.ILIMIT_CHx      = (uint8)((payload >> 4u)  & 0x01u);
    reg->bits.THERMAL_SD_CHx   = (uint8)((payload >> 5u)  & 0x01u);
    reg->bits.LPM_WAKE_CHx     = (uint8)((payload >> 6u)  & 0x01u);
    reg->bits.I2T_FLT_CHx      = (uint8)((payload >> 7u)  & 0x01u);
    reg->bits.VOUT_ERR_CHx     = (uint8)((payload >> 8u)  & 0x01u);
    reg->bits.SW_STATE_STAT_CHx = (uint8)((payload >> 9u)  & 0x01u);
    reg->bits.FLT_CHx         = (uint8)((payload >> 10u) & 0x01u);
    reg->bits.LATCH_STAT_CHx   = (uint8)((payload >> 11u) & 0x01u);
    reg->bits.I2T_MOD_CHx      = (uint8)((payload >> 12u) & 0x01u);
}

Std_ReturnType Tps2hcs08_OpenShortDiag(
    uint8 ch,
    tTps2hcs08OpenShortResult *result)
{
    uint8 devIdx;

    uint8 addr;
    uint16 payload;
    uint16 readData;
    tTps2hcs08FltStatCh fltStat;

    if (result == (tTps2hcs08OpenShortResult *)0)
    {
        return E_NOT_OK;
    }

    if (Tps2hcs08_CheckChannel(
            ch,
            "OPEN_SHORT_DIAG",
            0u) != E_OK)
    {
        return E_NOT_OK;
    }

    /*
     * Parallel mode uses CH1 off-state Open/Short-to-VBB diagnostic.
     */
    if (Tps2hcs08_CheckParallelCh1Only(
            ch,
            "OPEN_SHORT_DIAG",
            0u) != E_OK)
    {
        return E_NOT_OK;
    }

    /*
     * Initial diagnosis is only allowed while output is OFF.
     */
    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if (((ch == TPS2HCS08_CH1) &&
             (exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH1_ON != 0u)) ||
            ((ch == TPS2HCS08_CH2) &&
             (exVioDbTps2hcs08Ctx[devIdx].swState.bits.CH2_ON != 0u)))
        {
            ExVioDb_LogMappingError(
                exVioDbTps2hcs08Ctx[devIdx].signalId,
                "OPEN_SHORT_DIAG",
                ch,
                0u,
                "initial Open/Short diagnostic requires output OFF");

            return E_NOT_OK;
        }
    }

    /* ===============================================================
     * Step1
     * OL_SVBB_EN_CHx = 2h
     * =============================================================== */

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_SVBB_EN_CHx = 0x2u;
    }

    addr = Tps2hcs08_GetChConfigAddr(ch);

    payload =
        Tps2hcs08_BuildChConfigPayload(
            &exVioDbTps2hcs08Ctx[0].chConfig[ch]);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            addr,
            payload) != E_OK)
    {
        return E_NOT_OK;
    }

    /*
     * If the real IC requires diagnostic settling / blanking time,
     * insert the project delay/wait mechanism here.
     */

    addr = Tps2hcs08_GetFltStatAddr(ch);

    if (ExVioDb_ReadRegister_Tps2hcs08(
            TPS2HCS08_MOCK_SEQID,
            addr,
            &readData) != E_OK)
    {
        return E_NOT_OK;
    }

    Tps2hcs08_ParseFltStat(
        readData,
        &fltStat);

    /*
     * OL_OFF_CHx = 0
     * -> NORMAL
     */
    if (fltStat.bits.OL_OFF_CHx == 0u)
    {
        *result = TPS2HCS08_DIAG_NORMAL;
        return E_OK;
    }

    /* ===============================================================
     * Step2
     * OL_OFF_CHx = 1
     *
     * OL_SVBB_EN_CHx = 1h
     * =============================================================== */

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].chConfig[ch].bits.OL_SVBB_EN_CHx = 0x1u;
    }

    addr = Tps2hcs08_GetChConfigAddr(ch);

    payload =
        Tps2hcs08_BuildChConfigPayload(
            &exVioDbTps2hcs08Ctx[0].chConfig[ch]);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            addr,
            payload) != E_OK)
    {
        return E_NOT_OK;
    }

    /*
     * If the real IC requires diagnostic settling / blanking time,
     * insert the project delay/wait mechanism here.
     */

    addr = Tps2hcs08_GetFltStatAddr(ch);

    if (ExVioDb_ReadRegister_Tps2hcs08(
            TPS2HCS08_MOCK_SEQID,
            addr,
            &readData) != E_OK)
    {
        return E_NOT_OK;
    }

    Tps2hcs08_ParseFltStat(
        readData,
        &fltStat);

    /*
     * SHRT_VBB_CHx = 0 -> OPEN
     * SHRT_VBB_CHx = 1 -> Battery Short
     */
    if (fltStat.bits.SHRT_VBB_CHx == 0u)
    {
        *result = TPS2HCS08_DIAG_OPEN;
    }
    else
    {
        *result = TPS2HCS08_OPEN_SHORT_DIAG_SHORT_VBB;
    }

    return E_OK;
}

Std_ReturnType Tps2hcs08_SetMockReadRegister(uint8 addr, uint16 payload)
{
    uint8 devIdx;

    if (addr >= 0x20u)
    {
        return E_NOT_OK;
    }

    for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        exVioDbTps2hcs08Ctx[devIdx].mockReadReg[addr] = payload;
    }

    return E_OK;
}

/* =========================================================================
 * Fixed initial settings from project Write table
 * ========================================================================= */

Std_ReturnType Tps2hcs08_WriteFixedInitialConfig(void)
{
    uint8 ch;
    uint8 addr;
    uint16 payload;

    /*
     * 0x05:
     * MASK_SHRT_VBB = 1
     * MASK_OL_OFF   = 1
     */
    payload = Tps2hcs08_BuildFaultMaskPayload(&exVioDbTps2hcs08Ctx[0].faultMask);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            TPS2HCS08_REG_FAULT_MASK,
            payload) != E_OK)
    {
        return E_NOT_OK;
    }

    /*
     * 0x09:
     * CH2_LH_IN     = 1
     * CH1_LH_IN     = 1
     * AUTO_LPM_ENTRY= 0
     * WD_EN         = 1
     * WD_TO         = 1
     *
     * PARALLEL_12 is later set by MapUsed().
     */
    payload = Tps2hcs08_BuildDevConfigPayload(&exVioDbTps2hcs08Ctx[0].devConfig);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            TPS2HCS08_REG_DEV_CONFIG,
            payload) != E_OK)
    {
        return E_NOT_OK;
    }

    /*
     * 0x0A:
     * ADC_VSNS_DIS = 0
     */
    payload = Tps2hcs08_BuildAdcConfigPayload(&exVioDbTps2hcs08Ctx[0].adcConfig);

    if (Tps2hcs08_WriteMappedRegister(
            TPS2HCS08_MOCK_SEQID,
            TPS2HCS08_REG_ADC_CONFIG,
            payload) != E_OK)
    {
        return E_NOT_OK;
    }

    for (ch = TPS2HCS08_CH1; ch < TPS2HCS08_CH_MAX; ch++)
    {
        /*
         * ILIM_CONFIG_CHx:
         * I2T_EN_CHx = 1
         */
        addr = Tps2hcs08_GetIlimAddr(ch);
        payload = Tps2hcs08_BuildIlimPayload(&exVioDbTps2hcs08Ctx[0].ilimCfgCh[ch]);

        if (Tps2hcs08_WriteMappedRegister(
                TPS2HCS08_MOCK_SEQID,
                addr,
                payload) != E_OK)
        {
            return E_NOT_OK;
        }

        /*
         * CHx_CONFIG:
         * OL_SVBB_BLANK_CHx = 3
         */
        addr = Tps2hcs08_GetChConfigAddr(ch);
        payload = Tps2hcs08_BuildChConfigPayload(&exVioDbTps2hcs08Ctx[0].chConfig[ch]);

        if (Tps2hcs08_WriteMappedRegister(
                TPS2HCS08_MOCK_SEQID,
                addr,
                payload) != E_OK)
        {
            return E_NOT_OK;
        }
    }

    return E_OK;
}

/* =========================================================================
 * Static-verification helpers
 * ========================================================================= */

uint32 Tps2hcs08_GetWriteCount(void)
{
    return exVioDbTps2hcs08Ctx[0].writeCount;
}

Std_ReturnType Tps2hcs08_GetLastWrite(uint8 *seqid,
                                      uint8 *addr,
                                      uint16 *payload)
{
    if ((seqid == (uint8 *)0) ||
        (addr == (uint8 *)0) ||
        (payload == (uint16 *)0) ||
        (exVioDbTps2hcs08Ctx[0].lastWriteValid == FALSE))
    {
        return E_NOT_OK;
    }

    *seqid = exVioDbTps2hcs08Ctx[0].lastSeqid;
    *addr = exVioDbTps2hcs08Ctx[0].lastAddr;
    *payload = exVioDbTps2hcs08Ctx[0].lastPayload;

    return E_OK;
}
