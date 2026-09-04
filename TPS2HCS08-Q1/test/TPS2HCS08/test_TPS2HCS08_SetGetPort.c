/*******************************************************************************
 *  File            : test_TPS2HCS08_SetGetPort.c
 *  Description     : Unit tests for TPS2HCS08 SetPort/GetPort interface
 *                    Tests Phase 3 compatibility layer
 ******************************************************************************/
#include "unity.h"
#include "ExVioDb_Tps2hcs08.h"
#include "mock_SPI.h"

/* Access to internal context */
extern tTps2hcs08Ctx exVioDbTps2hcs08Ctx[TPS2HCS08_DEV_MAX];

/*==============================================================================
 *  TEST SETUP/TEARDOWN
 *============================================================================*/
void setUp(void)
{
    MockSpi_Reset();
    ExVioDb_InitRegValue_Tps2hcs08();

    /* Mark device 0 as present for testing */
    exVioDbTps2hcs08Ctx[0].devPresent = TRUE;
}

void tearDown(void)
{
}

/*==============================================================================
 *  MODE 0: OUTPUT STATE
 *============================================================================*/
void test_SetPort_Mode0_TurnOn_CH1(void)
{
    Std_ReturnType result;

    /* Turn ON CH1 (value = 1) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 1, 0);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(1, exVioDbTps2hcs08Ctx[0].swState.bits.CH1_ON);
    TEST_ASSERT_EQUAL(1, mockSpiControl.callCount);
}

void test_SetPort_Mode0_TurnOff_CH2(void)
{
    Std_ReturnType result;

    /* Turn OFF CH2 (value = 0) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH2, 0, 0);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(0, exVioDbTps2hcs08Ctx[0].swState.bits.CH2_ON);
}

void test_GetPort_Mode0_ReturnsOutputState(void)
{
    Std_ReturnType result;
    uint16 value;

    /* Set CH1 ON */
    exVioDbTps2hcs08Ctx[0].swState.bits.CH1_ON = 1;

    /* Read back state */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 0);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(1, value);
}

/*==============================================================================
 *  MODE 1: PWM DUTY CYCLE
 *============================================================================*/
void test_SetPort_Mode1_SetDuty_50Percent(void)
{
    Std_ReturnType result;

    /* Set duty to 128 (50% of 255) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 128, 1);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(128, exVioDbTps2hcs08Ctx[0].pwmCh[TPS2HCS08_CH1].bits.PWM_DTY_CHx);
}

void test_SetPort_Mode1_SetDuty_MaxValue(void)
{
    Std_ReturnType result;

    /* Set duty to 255 (100%) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH2, 255, 1);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(255, exVioDbTps2hcs08Ctx[0].pwmCh[TPS2HCS08_CH2].bits.PWM_DTY_CHx);
}

void test_SetPort_Mode1_InvalidDuty_Rejected(void)
{
    Std_ReturnType result;

    /* Try to set duty > 255 */
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 300, 1);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_GetPort_Mode1_ReturnsDutyCycle(void)
{
    Std_ReturnType result;
    uint16 value;

    /* Set duty to 200 */
    exVioDbTps2hcs08Ctx[0].pwmCh[TPS2HCS08_CH1].bits.PWM_DTY_CHx = 200;

    /* Read back duty */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 1);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(200, value);
}

/*==============================================================================
 *  MODE 2: EVENT STATUS/CLEAR
 *============================================================================*/
void test_SetPort_Mode2_EventClear_NoOp(void)
{
    Std_ReturnType result;

    /* TPS2HCS08 doesn't have separate event registers */
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 0, 2);

    /* Should succeed (no-op) */
    TEST_ASSERT_EQUAL(E_OK, result);
}

void test_GetPort_Mode2_EventStatus_AlwaysZero(void)
{
    Std_ReturnType result;
    uint16 value = 0xFFFF;

    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 2);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(0, value);
}

/*==============================================================================
 *  MODE 3: FAULT STATUS/CLEAR
 *============================================================================*/
void test_SetPort_Mode3_FaultClear_ReadsRegisters(void)
{
    Std_ReturnType result;

    /* Mock two SPI reads (GLOBAL_FAULT_TYPE + FLT_STAT_CH1) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);

    /* Clear faults on CH1 */
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 0, 3);

    /* Should call SPI read twice (2 frames per read = 4 total) */
    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT(mockSpiControl.callCount >= 2);
}

void test_GetPort_Mode3_ReturnsFaultStatus(void)
{
    Std_ReturnType result;
    uint16 value;

    /* Set fault status */
    exVioDbTps2hcs08Ctx[0].fltStatCh[TPS2HCS08_CH1].word = 0x1234;

    /* Read fault status */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 3);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL_HEX16(0x1234, value);
}

/*==============================================================================
 *  MODE 4: PWM FREQUENCY
 *============================================================================*/
void test_SetPort_Mode4_SetFrequency_100Hz(void)
{
    Std_ReturnType result;

    /* Set frequency to code 2 (100Hz) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 2, 4);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(2, exVioDbTps2hcs08Ctx[0].pwmCh[TPS2HCS08_CH1].bits.PWM_FREQ_CHx);
}

void test_SetPort_Mode4_SetFrequency_Max_1770Hz(void)
{
    Std_ReturnType result;

    /* Set frequency to code 7 (1770Hz) */
    MockSpi_SetNextTransferResult(TRUE, 0x00, 0x0000);
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH2, 7, 4);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(7, exVioDbTps2hcs08Ctx[0].pwmCh[TPS2HCS08_CH2].bits.PWM_FREQ_CHx);
}

void test_SetPort_Mode4_InvalidFrequency_Rejected(void)
{
    Std_ReturnType result;

    /* Try to set frequency > 7 */
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 8, 4);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_GetPort_Mode4_ReturnsFrequency(void)
{
    Std_ReturnType result;
    uint16 value;

    /* Set frequency to code 5 (1000Hz) */
    exVioDbTps2hcs08Ctx[0].pwmCh[TPS2HCS08_CH1].bits.PWM_FREQ_CHx = 5;

    /* Read back frequency */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 4);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(5, value);
}

/*==============================================================================
 *  MODE 5: CURRENT MEASUREMENT
 *============================================================================*/
void test_GetPort_Mode5_ReturnsCurrent(void)
{
    Std_ReturnType result;
    uint16 value;

    /* Set current measurement value */
    exVioDbTps2hcs08Ctx[0].adcIsns[TPS2HCS08_CH1] = 1234;

    /* Read current */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 5);

    TEST_ASSERT_EQUAL(E_OK, result);
    TEST_ASSERT_EQUAL(1234, value);
}

/*==============================================================================
 *  PARAMETER VALIDATION
 *============================================================================*/
void test_SetPort_InvalidDevIdx_Rejected(void)
{
    Std_ReturnType result;

    /* Invalid device index */
    result = ExVioDb_SetPortTps2hcs08(TPS2HCS08_DEV_MAX, 0, 0, 0);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_SetPort_InvalidChIdx_Rejected(void)
{
    Std_ReturnType result;

    /* Invalid channel index */
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH_MAX, 0, 0);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_SetPort_DeviceNotPresent_Rejected(void)
{
    Std_ReturnType result;

    /* Mark device as not present */
    exVioDbTps2hcs08Ctx[0].devPresent = FALSE;

    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 1, 0);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_GetPort_NullPointer_Rejected(void)
{
    Std_ReturnType result;

    /* Pass NULL pointer */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, NULL_PTR, 0);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_SetPort_InvalidMode_Rejected(void)
{
    Std_ReturnType result;

    /* Invalid mode (> 5) */
    result = ExVioDb_SetPortTps2hcs08(0, TPS2HCS08_CH1, 0, 99);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}

void test_GetPort_InvalidMode_Rejected(void)
{
    Std_ReturnType result;
    uint16 value;

    /* Invalid mode (> 5) */
    result = ExVioDb_GetPortTps2hcs08(0, TPS2HCS08_CH1, &value, 99);

    TEST_ASSERT_EQUAL(E_NOT_OK, result);
}
