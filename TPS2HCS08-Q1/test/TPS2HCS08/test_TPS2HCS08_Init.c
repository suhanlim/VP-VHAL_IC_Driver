/*******************************************************************************
 *  File            : test_TPS2HCS08_Init.c
 *  Description     : Unit tests for TPS2HCS08 initialization and reset values
 *                    Tests M-01 (reset values), M-04 (I2T safety), M-05 (RUN_INIT)
 ******************************************************************************/
#include "unity.h"
#include "ExVioDb_Tps2hcs08.h"
#include "mock_SPI.h"
#include <string.h>

/* Access to internal context for testing */
extern tTps2hcs08Ctx exVioDbTps2hcs08Ctx[TPS2HCS08_DEV_MAX];
extern tTps2hcs08RunState exVioDbTps2hcs08RunState;
extern tTps2hcs08SetupScnState exVioDbTps2hcs08SetupScnState;

/*==============================================================================
 *  TEST SETUP/TEARDOWN
 *============================================================================*/
void setUp(void)
{
    MockSpi_Reset();
    ExVioDb_InitRegValue_Tps2hcs08();
}

void tearDown(void)
{
    /* Clean up after each test */
}

/*==============================================================================
 *  M-01: RESET VALUES MATCH DATASHEET
 *============================================================================*/
void test_M01_ResetValue_LPM(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-01: LPM register reset = 0xFF80 (datasheet p.70) */
    TEST_ASSERT_EQUAL_HEX16(0xFF80, pCtx->lpm.word);

    /* Verify individual bits */
    TEST_ASSERT_EQUAL(1, pCtx->lpm.bits.AUTO_LPM_EXIT_CH1);
    TEST_ASSERT_EQUAL(1, pCtx->lpm.bits.AUTO_LPM_EXIT_CH2);
}

void test_M01_ResetValue_PWM_CH1(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-01: PWM_CH1 reset = 0xF000 (datasheet p.83) */
    TEST_ASSERT_EQUAL_HEX16(0xF000, pCtx->pwmCh[TPS2HCS08_CH1].word);

    /* Verify PWM_DTY_CHx = 0 (bits [8:1]) */
    TEST_ASSERT_EQUAL(0, pCtx->pwmCh[TPS2HCS08_CH1].bits.PWM_DTY_CHx);
}

void test_M01_ResetValue_PWM_CH2(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-01: PWM_CH2 reset = 0xF000 (datasheet p.83) */
    TEST_ASSERT_EQUAL_HEX16(0xF000, pCtx->pwmCh[TPS2HCS08_CH2].word);
}

void test_M01_ResetValue_FAULT_MASK(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-01: FAULT_MASK initial = 0xFF80 (project default) */
    TEST_ASSERT_EQUAL_HEX16(0xFF80, pCtx->faultMask.word);
}

void test_M01_ResetValue_SW_STATE(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-01: SW_STATE reset = 0xFFFC (datasheet p.75) */
    TEST_ASSERT_EQUAL_HEX16(0xFFFC, pCtx->swState.word);

    /* Verify both channels OFF */
    TEST_ASSERT_EQUAL(0, pCtx->swState.bits.CH1_ON);
    TEST_ASSERT_EQUAL(0, pCtx->swState.bits.CH2_ON);
}

void test_M01_ResetValue_DEV_CONFIG(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-01: DEV_CONFIG reset = 0xF800 (datasheet p.76) */
    TEST_ASSERT_EQUAL_HEX16(0xF800, pCtx->devConfig.word);

    /* Verify WD_EN = 1 (watchdog enabled) */
    TEST_ASSERT_EQUAL(1, pCtx->devConfig.bits.WD_EN);

    /* Verify WD_TO = 400ms (project default) */
    TEST_ASSERT_EQUAL(TPS2HCS08_WD_TO_400MS, pCtx->devConfig.bits.WD_TO);
}

/*==============================================================================
 *  M-04: I2T SAFETY INITIALIZATION
 *============================================================================*/
void test_M04_I2T_InitiallyDisabled(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* M-04: I2T_EN initially disabled for safety */
    TEST_ASSERT_EQUAL(0, pCtx->ilimCfgCh[TPS2HCS08_CH1].bits.I2T_EN_CHx);
    TEST_ASSERT_EQUAL(0, pCtx->ilimCfgCh[TPS2HCS08_CH2].bits.I2T_EN_CHx);
}

void test_M04_I2T_OtherFieldsInitialized(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* Verify other ILIM_CONFIG fields */
    TEST_ASSERT_EQUAL(0x8, pCtx->ilimCfgCh[TPS2HCS08_CH1].bits.ILIMIT_SET_CHx);  /* 40A */
    TEST_ASSERT_EQUAL(0x8, pCtx->ilimCfgCh[TPS2HCS08_CH1].bits.INRUSH_LIMIT_CHx); /* 40A */
}

/*==============================================================================
 *  M-05: RUN_INIT STATE
 *============================================================================*/
void test_M05_InitialState_Is_RUN_INIT(void)
{
    /* M-05: Initial state is INIT, not ACTIVE */
    TEST_ASSERT_EQUAL(TPS2HCS08_RUN_INIT, exVioDbTps2hcs08RunState);
}

void test_M05_SetupState_Is_SET_DEF(void)
{
    /* Setup scan starts at SET_DEF */
    TEST_ASSERT_EQUAL(TPS2HCS08_SETUP_SCN_SET_DEF, exVioDbTps2hcs08SetupScnState);
}

/*==============================================================================
 *  MULTI-DEVICE INITIALIZATION
 *============================================================================*/
void test_Init_AllDevices_Initialized(void)
{
    /* All 4 devices should be initialized */
    for (uint8 devIdx = 0; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

        TEST_ASSERT_EQUAL_HEX16(0xFF80, pCtx->lpm.word);
        TEST_ASSERT_EQUAL_HEX16(0xF000, pCtx->pwmCh[0].word);
        TEST_ASSERT_EQUAL_HEX16(0xF000, pCtx->pwmCh[1].word);
        TEST_ASSERT_EQUAL(FALSE, pCtx->devPresent);
    }
}

void test_Init_ContextFieldsZeroed(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    /* Verify fault/status fields are cleared */
    TEST_ASSERT_EQUAL(0, pCtx->globalFault.word);
    TEST_ASSERT_EQUAL(0, pCtx->logLatchGlobal);
    TEST_ASSERT_EQUAL(FALSE, pCtx->porCleared);
    TEST_ASSERT_EQUAL(FALSE, pCtx->lpmStatus1Cleared);
}
