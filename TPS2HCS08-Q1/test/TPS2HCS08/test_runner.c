/*******************************************************************************
 *  File            : test_runner.c
 *  Description     : Unity test runner for TPS2HCS08 unit tests
 ******************************************************************************/
#include "unity.h"

/* Test suite declarations */
extern void test_M01_ResetValue_LPM(void);
extern void test_M01_ResetValue_PWM_CH1(void);
extern void test_M01_ResetValue_PWM_CH2(void);
extern void test_M01_ResetValue_FAULT_MASK(void);
extern void test_M01_ResetValue_SW_STATE(void);
extern void test_M01_ResetValue_DEV_CONFIG(void);
extern void test_M04_I2T_InitiallyDisabled(void);
extern void test_M04_I2T_OtherFieldsInitialized(void);
extern void test_M05_InitialState_Is_RUN_INIT(void);
extern void test_M05_SetupState_Is_SET_DEF(void);
extern void test_Init_AllDevices_Initialized(void);
extern void test_Init_ContextFieldsZeroed(void);

extern void test_Retry_InitiallyZero(void);
extern void test_Retry_LimitsDefined(void);
extern void test_Retry_ConfigVerify_Success_ResetsCounter(void);
extern void test_Retry_ConfigVerify_Failure_IncrementsCounter(void);
extern void test_Retry_ConfigVerify_ExceedLimit_TransitionsToError(void);
extern void test_Retry_ErrorState_Defined(void);
extern void test_Retry_ErrorState_NoAutoRecovery(void);
extern void test_Retry_IndependentPerDevice(void);
extern void test_Retry_AllDevicesFailed_TransitionsToError(void);

extern void test_SetPort_Mode0_TurnOn_CH1(void);
extern void test_SetPort_Mode0_TurnOff_CH2(void);
extern void test_GetPort_Mode0_ReturnsOutputState(void);
extern void test_SetPort_Mode1_SetDuty_50Percent(void);
extern void test_SetPort_Mode1_SetDuty_MaxValue(void);
extern void test_SetPort_Mode1_InvalidDuty_Rejected(void);
extern void test_GetPort_Mode1_ReturnsDutyCycle(void);
extern void test_SetPort_Mode2_EventClear_NoOp(void);
extern void test_GetPort_Mode2_EventStatus_AlwaysZero(void);
extern void test_SetPort_Mode3_FaultClear_ReadsRegisters(void);
extern void test_GetPort_Mode3_ReturnsFaultStatus(void);
extern void test_SetPort_Mode4_SetFrequency_100Hz(void);
extern void test_SetPort_Mode4_SetFrequency_Max_1770Hz(void);
extern void test_SetPort_Mode4_InvalidFrequency_Rejected(void);
extern void test_GetPort_Mode4_ReturnsFrequency(void);
extern void test_GetPort_Mode5_ReturnsCurrent(void);
extern void test_SetPort_InvalidDevIdx_Rejected(void);
extern void test_SetPort_InvalidChIdx_Rejected(void);
extern void test_SetPort_DeviceNotPresent_Rejected(void);
extern void test_GetPort_NullPointer_Rejected(void);
extern void test_SetPort_InvalidMode_Rejected(void);
extern void test_GetPort_InvalidMode_Rejected(void);

extern void test_M08_SwState_CH1_ON_BitPosition(void);
extern void test_M08_SwState_CH2_ON_BitPosition(void);
extern void test_M08_SwState_BothChannels_Independent(void);
extern void test_M08_SwState_ReverseBitOrder(void);
extern void test_M08_Pwm_PWM_DTY_BitPosition(void);
extern void test_M08_Pwm_PWM_FREQ_BitPosition(void);
extern void test_M08_Pwm_EN_PWM_BitPosition(void);
extern void test_M08_Lpm_AUTO_LPM_EXIT_CH1_BitPosition(void);
extern void test_M08_Lpm_AUTO_LPM_EXIT_CH2_BitPosition(void);
extern void test_M08_GlobalFault_VBB_UVLO_BitPosition(void);
extern void test_M08_GlobalFault_VBB_UV_WRN_BitPosition(void);
extern void test_M08_GlobalFault_VDD_UVLO_BitPosition(void);
extern void test_M08_GlobalFault_WD_ERR_BitPosition(void);
extern void test_M08_GlobalFault_SPI_ERR_BitPosition(void);
extern void test_M08_SwState_Size_Is_2Bytes(void);
extern void test_M08_PwmCh_Size_Is_2Bytes(void);
extern void test_M08_Lpm_Size_Is_2Bytes(void);
extern void test_M08_GlobalFaultType_Size_Is_2Bytes(void);
extern void test_M08_Endianness_LittleEndian_ByteOrder(void);
extern void test_M08_SwState_ReservedBits_Preserved(void);
extern void test_M08_Pwm_ReservedBits_Preserved(void);

/*==============================================================================
 *  MAIN TEST RUNNER
 *============================================================================*/
int main(void)
{
    UNITY_BEGIN();

    /* Initialization Tests */
    RUN_TEST(test_M01_ResetValue_LPM);
    RUN_TEST(test_M01_ResetValue_PWM_CH1);
    RUN_TEST(test_M01_ResetValue_PWM_CH2);
    RUN_TEST(test_M01_ResetValue_FAULT_MASK);
    RUN_TEST(test_M01_ResetValue_SW_STATE);
    RUN_TEST(test_M01_ResetValue_DEV_CONFIG);
    RUN_TEST(test_M04_I2T_InitiallyDisabled);
    RUN_TEST(test_M04_I2T_OtherFieldsInitialized);
    RUN_TEST(test_M05_InitialState_Is_RUN_INIT);
    RUN_TEST(test_M05_SetupState_Is_SET_DEF);
    RUN_TEST(test_Init_AllDevices_Initialized);
    RUN_TEST(test_Init_ContextFieldsZeroed);

    /* Retry Counter Tests */
    RUN_TEST(test_Retry_InitiallyZero);
    RUN_TEST(test_Retry_LimitsDefined);
    RUN_TEST(test_Retry_ConfigVerify_Success_ResetsCounter);
    RUN_TEST(test_Retry_ConfigVerify_Failure_IncrementsCounter);
    RUN_TEST(test_Retry_ConfigVerify_ExceedLimit_TransitionsToError);
    RUN_TEST(test_Retry_ErrorState_Defined);
    RUN_TEST(test_Retry_ErrorState_NoAutoRecovery);
    RUN_TEST(test_Retry_IndependentPerDevice);
    RUN_TEST(test_Retry_AllDevicesFailed_TransitionsToError);

    /* SetPort/GetPort Tests */
    RUN_TEST(test_SetPort_Mode0_TurnOn_CH1);
    RUN_TEST(test_SetPort_Mode0_TurnOff_CH2);
    RUN_TEST(test_GetPort_Mode0_ReturnsOutputState);
    RUN_TEST(test_SetPort_Mode1_SetDuty_50Percent);
    RUN_TEST(test_SetPort_Mode1_SetDuty_MaxValue);
    RUN_TEST(test_SetPort_Mode1_InvalidDuty_Rejected);
    RUN_TEST(test_GetPort_Mode1_ReturnsDutyCycle);
    RUN_TEST(test_SetPort_Mode2_EventClear_NoOp);
    RUN_TEST(test_GetPort_Mode2_EventStatus_AlwaysZero);
    RUN_TEST(test_SetPort_Mode3_FaultClear_ReadsRegisters);
    RUN_TEST(test_GetPort_Mode3_ReturnsFaultStatus);
    RUN_TEST(test_SetPort_Mode4_SetFrequency_100Hz);
    RUN_TEST(test_SetPort_Mode4_SetFrequency_Max_1770Hz);
    RUN_TEST(test_SetPort_Mode4_InvalidFrequency_Rejected);
    RUN_TEST(test_GetPort_Mode4_ReturnsFrequency);
    RUN_TEST(test_GetPort_Mode5_ReturnsCurrent);
    RUN_TEST(test_SetPort_InvalidDevIdx_Rejected);
    RUN_TEST(test_SetPort_InvalidChIdx_Rejected);
    RUN_TEST(test_SetPort_DeviceNotPresent_Rejected);
    RUN_TEST(test_GetPort_NullPointer_Rejected);
    RUN_TEST(test_SetPort_InvalidMode_Rejected);
    RUN_TEST(test_GetPort_InvalidMode_Rejected);

    /* Bitfield Layout Tests */
    RUN_TEST(test_M08_SwState_CH1_ON_BitPosition);
    RUN_TEST(test_M08_SwState_CH2_ON_BitPosition);
    RUN_TEST(test_M08_SwState_BothChannels_Independent);
    RUN_TEST(test_M08_SwState_ReverseBitOrder);
    RUN_TEST(test_M08_Pwm_PWM_DTY_BitPosition);
    RUN_TEST(test_M08_Pwm_PWM_FREQ_BitPosition);
    RUN_TEST(test_M08_Pwm_EN_PWM_BitPosition);
    RUN_TEST(test_M08_Lpm_AUTO_LPM_EXIT_CH1_BitPosition);
    RUN_TEST(test_M08_Lpm_AUTO_LPM_EXIT_CH2_BitPosition);
    RUN_TEST(test_M08_GlobalFault_VBB_UVLO_BitPosition);
    RUN_TEST(test_M08_GlobalFault_VBB_UV_WRN_BitPosition);
    RUN_TEST(test_M08_GlobalFault_VDD_UVLO_BitPosition);
    RUN_TEST(test_M08_GlobalFault_WD_ERR_BitPosition);
    RUN_TEST(test_M08_GlobalFault_SPI_ERR_BitPosition);
    RUN_TEST(test_M08_SwState_Size_Is_2Bytes);
    RUN_TEST(test_M08_PwmCh_Size_Is_2Bytes);
    RUN_TEST(test_M08_Lpm_Size_Is_2Bytes);
    RUN_TEST(test_M08_GlobalFaultType_Size_Is_2Bytes);
    RUN_TEST(test_M08_Endianness_LittleEndian_ByteOrder);
    RUN_TEST(test_M08_SwState_ReservedBits_Preserved);
    RUN_TEST(test_M08_Pwm_ReservedBits_Preserved);

    return UNITY_END();
}
