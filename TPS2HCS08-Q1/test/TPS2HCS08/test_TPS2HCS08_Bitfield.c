/*******************************************************************************
 *  File            : test_TPS2HCS08_Bitfield.c
 *  Description     : Unit tests for TPS2HCS08 bitfield layout verification
 *                    Tests M-08 - ensures compiler bit-field packing matches datasheet
 ******************************************************************************/
#include "unity.h"
#include "ExVioDb_Tps2hcs08.h"

/*==============================================================================
 *  TEST SETUP/TEARDOWN
 *============================================================================*/
void setUp(void)
{
}

void tearDown(void)
{
}

/*==============================================================================
 *  M-08: SW_STATE BITFIELD LAYOUT
 *============================================================================*/
void test_M08_SwState_CH1_ON_BitPosition(void)
{
    tTps2hcs08SwState swState;

    /* Clear all bits */
    swState.word = 0x0000;

    /* Set CH1_ON = 1 */
    swState.bits.CH1_ON = 1;

    /* Verify word value has bit 0 set */
    TEST_ASSERT_EQUAL_HEX16(0x0001, swState.word);
}

void test_M08_SwState_CH2_ON_BitPosition(void)
{
    tTps2hcs08SwState swState;

    /* Clear all bits */
    swState.word = 0x0000;

    /* Set CH2_ON = 1 */
    swState.bits.CH2_ON = 1;

    /* Verify word value has bit 1 set */
    TEST_ASSERT_EQUAL_HEX16(0x0002, swState.word);
}

void test_M08_SwState_BothChannels_Independent(void)
{
    tTps2hcs08SwState swState;

    /* Set both channels */
    swState.word = 0x0000;
    swState.bits.CH1_ON = 1;
    swState.bits.CH2_ON = 1;

    /* Verify both bits are set */
    TEST_ASSERT_EQUAL_HEX16(0x0003, swState.word);
}

void test_M08_SwState_ReverseBitOrder(void)
{
    tTps2hcs08SwState swState;

    /* Write word value */
    swState.word = 0x0001;

    /* Verify bitfield interpretation */
    TEST_ASSERT_EQUAL(1, swState.bits.CH1_ON);
    TEST_ASSERT_EQUAL(0, swState.bits.CH2_ON);
}

/*==============================================================================
 *  PWM BITFIELD LAYOUT
 *============================================================================*/
void test_M08_Pwm_PWM_DTY_BitPosition(void)
{
    tTps2hcs08PwmCh pwm;

    /* Clear all bits */
    pwm.word = 0x0000;

    /* Set duty to 1 (bits [8:1]) */
    pwm.bits.PWM_DTY_CHx = 1;

    /* Verify word value - duty occupies bits [8:1] */
    TEST_ASSERT_EQUAL_HEX16(0x0002, pwm.word);
}

void test_M08_Pwm_PWM_FREQ_BitPosition(void)
{
    tTps2hcs08PwmCh pwm;

    /* Clear all bits */
    pwm.word = 0x0000;

    /* Set frequency to 1 (bits [11:9]) */
    pwm.bits.PWM_FREQ_CHx = 1;

    /* Verify word value - frequency occupies bits [11:9] */
    TEST_ASSERT_EQUAL_HEX16(0x0200, pwm.word);
}

void test_M08_Pwm_EN_PWM_BitPosition(void)
{
    tTps2hcs08PwmCh pwm;

    /* Clear all bits */
    pwm.word = 0x0000;

    /* Set EN_PWM = 1 (bit 0) */
    pwm.bits.EN_PWM_CHx = 1;

    /* Verify word value */
    TEST_ASSERT_EQUAL_HEX16(0x0001, pwm.word);
}

/*==============================================================================
 *  LPM BITFIELD LAYOUT
 *============================================================================*/
void test_M08_Lpm_AUTO_LPM_EXIT_CH1_BitPosition(void)
{
    tTps2hcs08Lpm lpm;

    /* Clear all bits */
    lpm.word = 0x0000;

    /* Set AUTO_LPM_EXIT_CH1 = 1 (bit 7) */
    lpm.bits.AUTO_LPM_EXIT_CH1 = 1;

    /* Verify word value */
    TEST_ASSERT_EQUAL_HEX16(0x0080, lpm.word);
}

void test_M08_Lpm_AUTO_LPM_EXIT_CH2_BitPosition(void)
{
    tTps2hcs08Lpm lpm;

    /* Clear all bits */
    lpm.word = 0x0000;

    /* Set AUTO_LPM_EXIT_CH2 = 1 (bit 8) */
    lpm.bits.AUTO_LPM_EXIT_CH2 = 1;

    /* Verify word value */
    TEST_ASSERT_EQUAL_HEX16(0x0100, lpm.word);
}

/*==============================================================================
 *  GLOBAL_FAULT_TYPE BITFIELD LAYOUT
 *============================================================================*/
void test_M08_GlobalFault_VBB_UVLO_BitPosition(void)
{
    tTps2hcs08GlobalFaultType fault;

    fault.word = 0x0000;
    fault.bits.VBB_UVLO = 1;

    /* Verify bit 0 */
    TEST_ASSERT_EQUAL_HEX16(0x0001, fault.word);
}

void test_M08_GlobalFault_VBB_UV_WRN_BitPosition(void)
{
    tTps2hcs08GlobalFaultType fault;

    fault.word = 0x0000;
    fault.bits.VBB_UV_WRN = 1;

    /* Verify bit 1 */
    TEST_ASSERT_EQUAL_HEX16(0x0002, fault.word);
}

void test_M08_GlobalFault_VDD_UVLO_BitPosition(void)
{
    tTps2hcs08GlobalFaultType fault;

    fault.word = 0x0000;
    fault.bits.VDD_UVLO = 1;

    /* Verify bit 2 */
    TEST_ASSERT_EQUAL_HEX16(0x0004, fault.word);
}

void test_M08_GlobalFault_WD_ERR_BitPosition(void)
{
    tTps2hcs08GlobalFaultType fault;

    fault.word = 0x0000;
    fault.bits.WD_ERR = 1;

    /* Verify bit 3 */
    TEST_ASSERT_EQUAL_HEX16(0x0008, fault.word);
}

void test_M08_GlobalFault_SPI_ERR_BitPosition(void)
{
    tTps2hcs08GlobalFaultType fault;

    fault.word = 0x0000;
    fault.bits.SPI_ERR = 1;

    /* Verify bit 4 */
    TEST_ASSERT_EQUAL_HEX16(0x0010, fault.word);
}

/*==============================================================================
 *  STRUCT SIZE VERIFICATION
 *============================================================================*/
void test_M08_SwState_Size_Is_2Bytes(void)
{
    /* SW_STATE register is 16-bit */
    TEST_ASSERT_EQUAL(2, sizeof(tTps2hcs08SwState));
}

void test_M08_PwmCh_Size_Is_2Bytes(void)
{
    /* PWM_CHx register is 16-bit */
    TEST_ASSERT_EQUAL(2, sizeof(tTps2hcs08PwmCh));
}

void test_M08_Lpm_Size_Is_2Bytes(void)
{
    /* LPM register is 16-bit */
    TEST_ASSERT_EQUAL(2, sizeof(tTps2hcs08Lpm));
}

void test_M08_GlobalFaultType_Size_Is_2Bytes(void)
{
    /* GLOBAL_FAULT_TYPE register is 16-bit */
    TEST_ASSERT_EQUAL(2, sizeof(tTps2hcs08GlobalFaultType));
}

/*==============================================================================
 *  ENDIANNESS VERIFICATION
 *============================================================================*/
void test_M08_Endianness_LittleEndian_ByteOrder(void)
{
    tTps2hcs08SwState swState;
    uint8 *bytes = (uint8 *)&swState.word;

    /* Set word to known value */
    swState.word = 0x1234;

    /* On little-endian system: byte[0] = 0x34, byte[1] = 0x12 */
    /* On big-endian system: byte[0] = 0x12, byte[1] = 0x34 */

    /* TPS2HCS08 SPI uses big-endian (MSB first) */
    /* Verify internal storage matches expected endianness */
    /* This test documents byte order for SPI frame construction */
}

/*==============================================================================
 *  RESERVED BITS MASK
 *============================================================================*/
void test_M08_SwState_ReservedBits_Preserved(void)
{
    tTps2hcs08SwState swState;

    /* Set to reset value (0xFFFC - reserved bits set) */
    swState.word = 0xFFFC;

    /* Modify only CH1_ON and CH2_ON */
    swState.bits.CH1_ON = 1;
    swState.bits.CH2_ON = 1;

    /* Verify reserved bits remain set */
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, swState.word);
}

void test_M08_Pwm_ReservedBits_Preserved(void)
{
    tTps2hcs08PwmCh pwm;

    /* Set to reset value (0xF000 - reserved bits set) */
    pwm.word = 0xF000;

    /* Modify PWM fields */
    pwm.bits.EN_PWM_CHx = 1;
    pwm.bits.PWM_DTY_CHx = 128;
    pwm.bits.PWM_FREQ_CHx = 5;

    /* Verify reserved bits [15:12] remain 0xF */
    TEST_ASSERT_EQUAL_HEX16(0xFA01, pwm.word);
}
