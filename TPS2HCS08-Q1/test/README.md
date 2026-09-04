# TPS2HCS08-Q1 Unit Tests

Comprehensive unit test suite for TPS2HCS08-Q1 IC driver using Unity test framework.

---

## Test Coverage

### Test Suites

| Suite | File | Tests | Coverage |
|-------|------|-------|----------|
| **Initialization** | `test_TPS2HCS08_Init.c` | 12 tests | M-01, M-04, M-05 |
| **Retry Counters** | `test_TPS2HCS08_Retry.c` | 9 tests | Phase 2 Issue #2 & #7 |
| **SetPort/GetPort** | `test_TPS2HCS08_SetGetPort.c` | 22 tests | Phase 3 compatibility |
| **Bitfield Layout** | `test_TPS2HCS08_Bitfield.c` | 21 tests | M-08 compiler verification |

**Total**: 64 unit tests

---

## Prerequisites

### 1. Install Unity Test Framework

```bash
cd modules/IC_Driver/TPS2HCS08-Q1/test
git clone https://github.com/ThrowTheSwitch/Unity.git
```

### 2. Install GCC Compiler

**Windows (MinGW)**:
```bash
# Install via MSYS2 or MinGW-w64
pacman -S mingw-w64-x86_64-gcc
```

**Linux/Mac**:
```bash
# Usually pre-installed, verify with:
gcc --version
```

---

## Build and Run

### Quick Start

```bash
cd modules/IC_Driver/TPS2HCS08-Q1/test
make test
```

This will:
1. Compile all test files
2. Link with Unity framework
3. Run all 64 tests
4. Display results

### Build Only

```bash
make all
```

### Run Tests

```bash
make test
```

### Clean Build

```bash
make clean
```

---

## Test Details

### Initialization Tests (12 tests)

**M-01: Reset Values**
- `test_M01_ResetValue_LPM` - Verify LPM = 0xFF80
- `test_M01_ResetValue_PWM_CH1` - Verify PWM_CH1 = 0xF000
- `test_M01_ResetValue_PWM_CH2` - Verify PWM_CH2 = 0xF000
- `test_M01_ResetValue_FAULT_MASK` - Verify FAULT_MASK = 0xFF80
- `test_M01_ResetValue_SW_STATE` - Verify SW_STATE = 0xFFFC
- `test_M01_ResetValue_DEV_CONFIG` - Verify DEV_CONFIG = 0xF800

**M-04: I2T Safety**
- `test_M04_I2T_InitiallyDisabled` - I2T_EN = 0 for safety
- `test_M04_I2T_OtherFieldsInitialized` - ILIMIT_SET = 40A

**M-05: Run State**
- `test_M05_InitialState_Is_RUN_INIT` - Verify RUN_INIT state
- `test_M05_SetupState_Is_SET_DEF` - Verify setup state

**Multi-Device**
- `test_Init_AllDevices_Initialized` - All 4 devices initialized
- `test_Init_ContextFieldsZeroed` - Fault/status fields cleared

### Retry Counter Tests (9 tests)

**Initialization**
- `test_Retry_InitiallyZero` - All counters start at 0
- `test_Retry_LimitsDefined` - Verify max retry limits

**CONFIG_VERIFY Logic**
- `test_Retry_ConfigVerify_Success_ResetsCounter` - Reset on success
- `test_Retry_ConfigVerify_Failure_IncrementsCounter` - Increment on failure
- `test_Retry_ConfigVerify_ExceedLimit_TransitionsToError` - ERROR on limit

**ERROR State**
- `test_Retry_ErrorState_Defined` - ERROR state exists
- `test_Retry_ErrorState_NoAutoRecovery` - No auto-recovery

**Multi-Device**
- `test_Retry_IndependentPerDevice` - Independent counters
- `test_Retry_AllDevicesFailed_TransitionsToError` - All failed → ERROR

### SetPort/GetPort Tests (22 tests)

**Mode 0: Output State**
- `test_SetPort_Mode0_TurnOn_CH1` - Turn ON channel 1
- `test_SetPort_Mode0_TurnOff_CH2` - Turn OFF channel 2
- `test_GetPort_Mode0_ReturnsOutputState` - Read state

**Mode 1: PWM Duty**
- `test_SetPort_Mode1_SetDuty_50Percent` - Set 50% duty
- `test_SetPort_Mode1_SetDuty_MaxValue` - Set 100% duty
- `test_SetPort_Mode1_InvalidDuty_Rejected` - Reject > 255
- `test_GetPort_Mode1_ReturnsDutyCycle` - Read duty

**Mode 2: Event**
- `test_SetPort_Mode2_EventClear_NoOp` - No-op for TPS2HCS08
- `test_GetPort_Mode2_EventStatus_AlwaysZero` - Always 0

**Mode 3: Fault**
- `test_SetPort_Mode3_FaultClear_ReadsRegisters` - Read to clear
- `test_GetPort_Mode3_ReturnsFaultStatus` - Return fault word

**Mode 4: PWM Frequency**
- `test_SetPort_Mode4_SetFrequency_100Hz` - Set 100Hz
- `test_SetPort_Mode4_SetFrequency_Max_1770Hz` - Set 1770Hz
- `test_SetPort_Mode4_InvalidFrequency_Rejected` - Reject > 7
- `test_GetPort_Mode4_ReturnsFrequency` - Read frequency

**Mode 5: Current**
- `test_GetPort_Mode5_ReturnsCurrent` - Return ISNS value

**Parameter Validation**
- `test_SetPort_InvalidDevIdx_Rejected` - Reject invalid device
- `test_SetPort_InvalidChIdx_Rejected` - Reject invalid channel
- `test_SetPort_DeviceNotPresent_Rejected` - Reject absent device
- `test_GetPort_NullPointer_Rejected` - Reject NULL pointer
- `test_SetPort_InvalidMode_Rejected` - Reject invalid mode (SetPort)
- `test_GetPort_InvalidMode_Rejected` - Reject invalid mode (GetPort)

### Bitfield Layout Tests (21 tests)

**SW_STATE Register**
- `test_M08_SwState_CH1_ON_BitPosition` - CH1_ON at bit 0
- `test_M08_SwState_CH2_ON_BitPosition` - CH2_ON at bit 1
- `test_M08_SwState_BothChannels_Independent` - Independent bits
- `test_M08_SwState_ReverseBitOrder` - Word → bitfield mapping

**PWM Register**
- `test_M08_Pwm_PWM_DTY_BitPosition` - Duty at bits [8:1]
- `test_M08_Pwm_PWM_FREQ_BitPosition` - Frequency at bits [11:9]
- `test_M08_Pwm_EN_PWM_BitPosition` - Enable at bit 0

**LPM Register**
- `test_M08_Lpm_AUTO_LPM_EXIT_CH1_BitPosition` - CH1 at bit 7
- `test_M08_Lpm_AUTO_LPM_EXIT_CH2_BitPosition` - CH2 at bit 8

**GLOBAL_FAULT_TYPE Register**
- `test_M08_GlobalFault_VBB_UVLO_BitPosition` - Bit 0
- `test_M08_GlobalFault_VBB_UV_WRN_BitPosition` - Bit 1
- `test_M08_GlobalFault_VDD_UVLO_BitPosition` - Bit 2
- `test_M08_GlobalFault_WD_ERR_BitPosition` - Bit 3
- `test_M08_GlobalFault_SPI_ERR_BitPosition` - Bit 4

**Struct Sizes**
- `test_M08_SwState_Size_Is_2Bytes` - 16-bit register
- `test_M08_PwmCh_Size_Is_2Bytes` - 16-bit register
- `test_M08_Lpm_Size_Is_2Bytes` - 16-bit register
- `test_M08_GlobalFaultType_Size_Is_2Bytes` - 16-bit register

**Endianness & Reserved Bits**
- `test_M08_Endianness_LittleEndian_ByteOrder` - Byte order check
- `test_M08_SwState_ReservedBits_Preserved` - Reserved bits intact
- `test_M08_Pwm_ReservedBits_Preserved` - Reserved bits intact

---

## Mock Functions

### SPI Mock (`mock_SPI.c`)

**Control Functions**:
```c
void MockSpi_Reset(void);
void MockSpi_SetNextTransferResult(boolean success, uint8 sdoHeader, uint16 data);
```

**Mocked Function**:
```c
Std_ReturnType ExVioDb_Tps2hcs08_Port_SpiTransfer(
    uint8 devIdx,
    const uint8 *txBuf,
    uint8 *rxBuf,
    uint8 len);
```

**Features**:
- Records call count
- Stores last transmitted data
- Returns configurable results

### Logger Mock (`mock_Logger.c`)

**Mocked Functions**:
```c
void TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(uint32 tag, const char *fmt, ...);
void TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(uint32 tag, const char *fmt, ...);
void TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(uint32 tag, const char *fmt, ...);
```

**Features**:
- Suppresses log output during tests
- Prevents console spam

---

## Expected Output

```
Running TPS2HCS08 Unit Tests...
=========================================

test/TPS2HCS08/test_TPS2HCS08_Init.c:23:test_M01_ResetValue_LPM:PASS
test/TPS2HCS08/test_TPS2HCS08_Init.c:32:test_M01_ResetValue_PWM_CH1:PASS
test/TPS2HCS08/test_TPS2HCS08_Init.c:41:test_M01_ResetValue_PWM_CH2:PASS
...
[64 tests]
...

-----------------------
64 Tests 0 Failures 0 Ignored
OK
```

---

## Troubleshooting

### Unity Not Found

**Error**: `unity.h: No such file or directory`

**Solution**:
```bash
cd test
git clone https://github.com/ThrowTheSwitch/Unity.git
```

### GCC Not in PATH

**Error**: `make: gcc: command not found`

**Solution**:
```bash
# Windows: Add MinGW bin to PATH
export PATH="/c/mingw64/bin:$PATH"

# Or install via MSYS2
pacman -S mingw-w64-x86_64-gcc
```

### Build Errors

**Error**: Compilation errors in test files

**Solution**:
1. Ensure `D_STATIC=` is defined in CFLAGS (exposes internal symbols)
2. Check include paths in Makefile
3. Verify `Std_Types.h` is available

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: TPS2HCS08 Unit Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install Unity
        run: |
          cd modules/IC_Driver/TPS2HCS08-Q1/test
          git clone https://github.com/ThrowTheSwitch/Unity.git
      - name: Run Tests
        run: |
          cd modules/IC_Driver/TPS2HCS08-Q1/test
          make test
```

---

## Next Steps

### Hardware-in-Loop Testing

After unit tests pass, proceed to HIL testing:

1. **Setup Hardware**
   - TPS2HCS08-Q1 evaluation board (4 devices)
   - Oscilloscope for timing verification
   - Electronic load for I2T testing

2. **Test Scenarios**
   - Power-on sequence (SLEEP → INIT → ACTIVE)
   - SPI communication (4-device daisy chain)
   - I2T protection triggering
   - AUTO_LPM entry/exit
   - Fault detection (UVLO, WD_ERR, etc.)

3. **Integration Tests**
   - Vehicle IO DB integration
   - Application layer API usage
   - Multi-driver interaction

---

## References

- [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity)
- [TPS2HCS08-Q1 Datasheet](https://www.ti.com/product/TPS2HCS08-Q1)
- `COMPREHENSIVE_REFACTORING_PLAN.md` - Implementation plan
- `TPS2HCS08_IMPLEMENTATION_SUMMARY.md` - Phase 1-3 summary

---

**Test Suite Version**: 1.0
**Last Updated**: 2026-09-04
**Maintainer**: Development Team
