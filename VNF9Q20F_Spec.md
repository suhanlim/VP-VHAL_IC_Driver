# VNF9Q20F Driver Specification

## 1. Operation Process

| Step | Mode | Description | Note |
|------|------|-------------|------|
| 1 | Sleep mode | Auto-enter after standard controller power-on | |
| 2 | Fail-safe mode | Enter by setting STDBY_NOT to HIGH | |
| 3 | Fail-safe mode | Register Write based on Vehicle IO Signal DB | See "Register Initial Setup" and "Vehicle IO Signal DB Register Configuration" |
| 4 | Fail-safe mode | Diagnose all channels and output report | See [Initial Diagnostics](#initial-diagnostics-step-4) |
| 5 | Normal mode | Enter via SPI | 1. UNLOCK=1<br>2. EN=1 AND GOSTBY=0 |
| 6 | Normal mode | Watchdog operation | MCU cycle (5ms) |
| 7 | Normal mode | PWM clock synchronization | PWM SYNC=1 |
| 8 | Normal mode | Activate assigned channels by AppSW | |
| 9 | Normal mode | Prepare for sleep mode | After vehicle ACC OFF and specific conditions met |
| 10 | Normal mode | Deactivate assigned channels by AppSW | |
| 11 | Pre-standby mode | Enter via SPI | 1. UNLOCK=1 AND EN=1<br>2. EN=0 AND GOSTBY=1 |
| 12 | Standby mode | Enter by setting STDBY_NOT to LOW | |
| 13 | Standby mode | Enter normal mode | On specific signal input (e.g., door handle switch) |
| 14 | Fail-safe mode | Enter by setting STDBY_NOT to HIGH | |
| 15 | - | Repeat from Step 4 | |

### Initial Diagnostics (Step 4)

1. **ADC Diagnostics**
   - Read `ADCLSRx`: Normal if value is around 81 (range: 71~91)
   - Read `ADCMSRx`: Normal if value is around 325 (range: 293~357)
   - Read `ADCHSRx`: Normal if value is around 890 (range: 819~961)

2. **I2T Diagnostics**
   - Check if `ITSTx` is `111b`

3. **Open/Short Diagnostics per Channel**
   - See [Open/Short Diagnostic Process](#1-openshort-diagnostic-process)

---

## 2. Register Specifications

### 2.1 Write Registers

| Address | Bit | Field | Condition | Initial Value | Description | Note |
|---------|-----|-------|-----------|---------------|-------------|------|
| 00h~03h | 13-4 | DUTYCRx | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 00h~03h | 2 | OLOFFCR | Open/Short diagnostics | 0b → 1b | See Open/Short Diagnostic Process | |
| | | | All other states | 0b | | |
| 00h~03h | 1 | WDTB | | - | Not used | Only 13h WDTB is used |
| 08h~0Bh | 15-14 | SLOPECRx | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 08h~0Bh | 12-8 | CHPHAx | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 08h~0Bh | 5-4 | PWMFCYx | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 08h~0Bh | 3 | CCR | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 10h | 15-12 | CHLOFFTCRx | | 1111b | Set to 240ms | If CHLOFFSRx=1, delay 1s then rewrite CHLOFFTCRx to release latch-off |
| 10h | 11-8 | CHLOFFTCRx | | 1111b | | |
| 10h | 7-4 | CHLOFFTCRx | | 1111b | | |
| 11h | 7-4 | CHLOFFTCRx | | 1111b | | |
| 13h | 15-12 | EXIT_CAPCRx | | - | See CCM Mode Process | |
| 13h | 11-8 | SOCRx | | 0b | Controlled by AppSW | |
| 13h | 5-2 | CAPCRx | | - | See CCM Mode Process | |
| 13h | 1 | WDTB | | - | Toggle every MCU cycle (5ms) | |
| 14h | 15 | GOSTBY | | - | See Operation Process | |
| 14h | 11 | EN | | - | See Operation Process | |
| 14h | 1 | PWM SYNC | | - | See Operation Process | |
| 15h~18h | 10 | PARAL | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 15h~18h | 6-4 | INOM2x-INOM0x | | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 15h~18h | 3-1 | TNOM2x-TNOM0x | | Signal DB | See Vehicle IO Signal DB Register Configuration | |

### 2.2 Read Registers - GSB (Global Status Byte)

| Register | Bit | Field | Condition | Description | Note |
|----------|-----|-------|-----------|-------------|------|
| GSB | 7 | GSBN | WDTB toggle | If 0: Read related registers and output conditions to console log (exclude initial 0 in Fail-safe mode) | - |
| GSB | 6 | RSTB | WDTB toggle | If 1: Output field name to console log (exclude initial 1 in Fail-safe mode). Resume from Operation Process Step 3 | On condition met |
| GSB | 5 | SPIE | WDTB toggle | If 1: Output field name to console log. Attempt SPI communication 10 times at 100ms intervals. If all 10 fail, perform SW reset. Resume from Operation Process Step 3 | Output each progress once |
| GSB | 4 | TSD/PL | WDTB toggle | If 1: Output field name to console log | Output once when condition met, no output until GSBN becomes 1 |
| GSB | 3 | ITLOFF | WDTB toggle | If 1: Output field name to console log | Output once when condition met, no output until GSBN becomes 1 |
| GSB | 2 | LOFF | WDTB toggle | If 1: Output field name to console log | Output once when condition met, no output until GSBN becomes 1 |
| GSB | 1 | TCASE | WDTB toggle | If 1: Output field name to console log | Output once when condition met, no output until GSBN becomes 1 |
| GSB | 0 | FS | - | - | - |

### 2.3 RAM Registers

| Address | Bit | Field | Condition | Description | Console Log Output |
|---------|-----|-------|-----------|-------------|-------------------|
| 20h~23h | 12 | CHFBSRx | WDTB toggle | If 1: Output channel and field name to console log | Output once when condition met (re-output after cleared to 0) |
| 20h~23h | 11 | VDSHSRx | Initial diagnostics | See Open/Short Diagnostic Process | Output once after diagnostics complete |
| | | | Channel ON (non-CCM) + WDTB toggle | If 1: Output channel and field name to console log | Output once when condition met (re-output after cleared to 0) |
| | | | Channel ON (CCM mode) + WDTB toggle | See CCM Mode Process | See CCM Mode Process |
| | | | Channel OFF | Not used | |
| 20h~23h | 10 | ITOFFSRx | WDTB toggle | If 1: Output channel and field name to console log | Output once when condition met (re-output after cleared to 0) |
| 20h~23h | 8 | CHLOFFSRx | WDTB toggle | If 1: Output channel and field name to console log | Output once when condition met (re-output after cleared to 0) |
| 20h~23h | 7 | RST | WDTB toggle | Read and clear on initial setup. If 1 afterwards: Output field name to console log | Output once when condition met (re-output after cleared to 0) |
| 20h~23h | 6 | SPIE | WDTB toggle | If 1: Output field name to console log | Output once when condition met (re-output after cleared to 0) |
| 20h~23h | 5 | PWMCLOCKLOW | WDTB toggle | Not used | |
| 20h~23h | 4 | VCCUV | WDTB toggle | If 1: Output field name to console log | Output once when condition met (re-output after cleared to 0) |
| 20h~23h | 3-1 | TWx | WDTB toggle | If 1: Output field name to console log | Output once when condition met (re-output after cleared to 0) |
| 28h~2Bh | 13-4 | ADCxSRx | AppSW request | Update to memory accessible by AppSW. Formula: Register value (decimal) / 79 = Current (A) | Output on AppSW request |
| 31h | 13-4 | ADC9SRx | AppSW request | Update to memory accessible by AppSW. Formula: 401.8 - (1.009 × Register value (decimal)) = Case temperature (°C) | Output on AppSW request |
| 32h | 13-4 | ADCLSRx | Initial diagnostics | See Operation Process. Output to console log if out of normal range | Output once after diagnostics complete |
| 33h | 13-4 | ADCMSRx | Initial diagnostics | See Operation Process. Output to console log if out of normal range | Output once after diagnostics complete |
| 34h | 13-4 | ADCHSRx | Initial diagnostics | See Operation Process. Output to console log if out of normal range | Output once after diagnostics complete |
| 35h | 12-10, 9-7, 6-4, 3-1 | ITCNTx | AppSW request | Update to memory accessible by AppSW | Output on AppSW request |
| 36h | 12-10, 9-7, 6-4, 3-1 | ITSTx | Initial diagnostics | See Operation Process. Output to console log if not `111b` | Output once after diagnostics complete |

---

## 3. Vehicle IO Signal DB Register Configuration

> **Note**: If a setting not defined in tables below is encountered, exclude that register setting and output error log with Signal ID and parameter name.

### 3.1 CAT_1 (Use ID 9)

| Signal DB Parameter | ID (DEC) | ID (BIN) | Note |
|---------------------|----------|----------|------|
| CAT1_E_FUSE_2 | 9 | 1001 | e-Fuse (VNF9Q20F) |

### 3.2 CAT_2 (Use ID 0)

| Signal DB Parameter | ID (DEC) | ID (BIN) | Note |
|---------------------|----------|----------|------|
| CAT2_ACTIVE_HIGH | 0 | 00 | Active High |

### 3.3 SC (Standard Controller)

Signal assigned to standard controller (e.g., Driver ZONE = SC1, Passenger ZONE = SC2)

### 3.4 IC, PIN

IC and PIN assigned sequentially based on HW configuration.

| Signal DB Parameter | | | | | IC Register |
|---------------------|-----|------|-----|------|-------------|
| **IC** | ID (DEC) | ID (BIN) | **PIN** | ID (DEC) | **CHPHAx** |
| IC_1 | 0 | 0000 | IC_PIN_0 | 0 | 00001b |
| | | | IC_PIN_1 | 1 | 00010b |
| | | | IC_PIN_2 | 2 | 00011b |
| | | | IC_PIN_3 | 3 | 00100b |
| IC_2 | 1 | 0001 | IC_PIN_0 | 0 | 00101b |
| | | | IC_PIN_1 | 1 | 00110b |
| | | | IC_PIN_2 | 2 | 00111b |
| | | | IC_PIN_3 | 3 | 01000b |
| IC_3 | 2 | 0010 | IC_PIN_0 | 0 | 01001b |
| | | | ... | | ... |

### 3.5 USED

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register (PARAL 15h) | Note |
|---------------------|----------|----------|-------------------------|------|
| USED_1 | 1 | 0001 | 0b | Default value |
| USED_2 | 2 | 0010 | 1b | Only CH0 can use USED_2 (CH0 and CH1 combined). Other channels cannot use USED_2 (output error log if attempted) |

### 3.6 MOC

| Signal DB Parameter | ID (DEC) | ID (BIN) | INOMxx | TNOMxx | Note |
|---------------------|----------|----------|--------|--------|------|
| MOC_1A | 1 | 0001 | 001b | 000b | |
| MOC_3A | 2 | 0010 | 101b | 000b | |
| MOC_5A | 3 | 0011 | 000b | 111b | |
| MOC_10A | 4 | 0100 | 000b | 111b | Only configurable when CH0 is set to USED_2 |

### 3.7 OCP

| Signal DB Parameter | ID (DEC) | ID (BIN) | CCR | Description | Note |
|---------------------|----------|----------|-----|-------------|------|
| OCP_100mV | 1 | 0001 | 1b | 5.6A~9.2A | LED mode - many functional restrictions including I2T |
| OCP_200mV | 2 | 0010 | - | Not configurable | |
| OCP_300mV | 3 | 0011 | - | Not configurable | |
| OCP_400mV | 4 | 0100 | - | Not configurable | |
| OCP_500mV | 5 | 0101 | - | Not configurable | |
| OCP_600mV | 6 | 0110 | - | Not configurable | |
| OCP_700mV | 7 | 0111 | - | Not configurable | |
| OCP_800mV | 8 | 1000 | 0b | 34.5A | Bulb mode - default setting |

### 3.8 RT

Not used parameter.

### 3.9 PWM

| Signal DB Parameter | ID (DEC) | ID (BIN) | DUTYCRx | Note |
|---------------------|----------|----------|---------|------|
| PWM_O | 0 | 00 | Set according to PWM_Duty value | Convert 8-bit parameter to 10-bit |
| PWM_X | 1 | 01 | 1111111111b | |
| PWM_C | 2 | 10 | 1111111111b | |

### 3.10 OLD

Not used parameter. (All channels auto-diagnosed for Open/Short during initial diagnostics)

### 3.11 PWM_F

Applies only to PWM_O signals. (Ignore parameter value for non-applicable signals)

| Signal DB Parameter | ID (DEC) | ID (BIN) | PWMFCY1 | PWMFCY0 | Note |
|---------------------|----------|----------|---------|---------|------|
| PWM_40Hz | 0 | 0000 | - | - | Not configurable |
| PWM_80Hz | 1 | 0001 | - | - | Not configurable (requires 300KHz input to PWM_CLK pin) |
| PWM_100Hz | 2 | 0010 | 1 | 0 | 98Hz |
| PWM_120Hz | 3 | 0011 | - | - | Not configurable |
| PWM_200Hz | 4 | 0100 | 0 | 1 | 195Hz |
| PWM_400Hz | 5 | 0101 | 0 | 0 | 391Hz |
| PWM_800Hz | 6 | 0110 | 1 | 1 | 781Hz |
| PWM_1000Hz | 7 | 0111 | - | - | Not configurable (requires 500KHz input to PWM_CLK pin) |

### 3.12 CT

Applies only to PWM_C signals. (CCM mode process not required for non-applicable signals)

| Signal DB Parameter | ID (DEC) | ID (BIN) | Note |
|---------------------|----------|----------|------|
| CT_5ms | 0 | 0000 | CCM Mode Process required |
| CT_10ms | 1 | 0001 | |
| CT_15ms | 2 | 0010 | |
| CT_20ms | 3 | 0011 | |
| CT_25ms | 4 | 0100 | |
| CT_30ms | 5 | 0101 | |
| CT_40ms | 6 | 0110 | |
| CT_50ms | 7 | 0111 | |

### 3.13 SR

| Signal DB Parameter | ID (DEC) | ID (BIN) | SLOPECRx | Note |
|---------------------|----------|----------|----------|------|
| SR_1mA | 0 | 0000 | 00b | 0.55V/us |
| SR_2mA | 1 | 0001 | 01b | 0.62V/us |
| SR_4mA | 2 | 0010 | 10b | 0.74V/us |
| SR_8mA | 3 | 0011 | 11b | 1.00V/us |

### 3.14 VOL_DET

Not used parameter.

### 3.15 DEF_Value

All channels must be set to DEF_IDLE specification. (No DEF_ACTIVE specification)

### 3.16 WAKE

Not used parameter.

### 3.17 PRE_Value

Not used parameter.

### 3.18 WC

Not used parameter.

### 3.19 Threshold_V

Not used parameter.

### 3.20 PWM_Duty

Applies only to PWM_O signals. (Ignore parameter value for non-applicable signals)

| Signal DB Parameter | ID (DEC) | ID (BIN) | DUTYCRx | Note |
|---------------------|----------|----------|---------|------|
| 0 | 0 | 00000000 | 0000000000b | Convert 8-bit parameter to 10-bit linearly |
| 255 | 255 | 11111111 | 1111111111b | Formula: `DUTYCRx = round((PWM_Duty * 1023) / 255)` |

---

## 4. Other Processes

### 4.1 Open/Short Diagnostic Process

Initial diagnostics performed with output OFF state.

#### a. Diagnosis by VDSHSRx State

| VDSHSRx State | Step 1 (OLOFFCR=0) | Step 2 (OLOFFCR=1) | Diagnosis Result | Note |
|---------------|--------------------|--------------------|------------------|------|
| | 1 | 1 | Normal | |
| | 1 | 0 | Open | Output channel status to console log |
| | 0 | x | Battery Short | Output channel status to console log |

#### b. GND Short

GND Short is determined when overcurrent flows in output ON state.
(Excluded from initial diagnostics, no separate console log output required)

### 4.2 CCM Mode Process

#### a. Operation Sequence

| Step | Process | Note |
|------|---------|------|
| 1 | If Signal DB PWM parameter is `PWM_C`, that signal must operate in CCM mode when output ON | |
| 2 | When channel ON by AppSW, set `CAPCRx` and `SOCRx` to 1 simultaneously | Synchronize CCM mode start with output ON timing |
| 3 | Every MCU cycle (5ms):<br>- If `VDSHSRx=1`: Compare with Signal DB CT parameter time, repeat until reached<br>- If `VDSHSRx=0`: Set `EXIT_CAPCRx=1` to end CCM mode and output to console log how many ms until manual termination | On manual termination, output CCM mode duration (use MCU cycle time as CCM mode duration unit) |
| 4 | When CCM mode duration reaches Signal DB CT parameter value, set `EXIT_CAPCRx=1` and output to console log that CCM mode was maintained equal to CT parameter value | On termination, output CCM mode duration and `VDSHSRx` value together (use MCU cycle time as CCM mode duration unit) |
