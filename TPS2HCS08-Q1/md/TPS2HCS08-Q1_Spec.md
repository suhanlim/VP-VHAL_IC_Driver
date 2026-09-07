# TPS2HCS08-Q1 Driver Specification

## 1. Operation Process

| Step | IC State | Mode and Process Description | Note |
|------|----------|------------------------------|------|
| 1 | OFF | Standard controller power-on, load switch transparent operation input | Vehicle battery connection |
| 2 | SLEEP | IC POR release and entry into SLEEP mode | |
| 3 | INIT & ABIST | CSN = 0 entry | NVM Read communication possible |
| 4 | CONFIG | CSN = 0 entry after 65us or more | |
| 5 | CONFIG | Vehicle IO signal DB based register Write | Verify register settings and vehicle IO signal DB based register settings match |
| 6 | CONFIG | All channels OPEN / VBB SHORT diagnosis and report output | See Open/Short diagnostic process |
| 8 | ACTIVE | B+ signal input activation of assigned channels | CHx_ON = 1h |
| 9 | ACTIVE | Application SW input activation of assigned channels | CHx_ON = 1h |
| 10 | ACTIVE | SPI Watchdog periodic READ communication<br>(GLOBAL_FAULT_TYPE, FLT_STAT_CH1, FLT_STAT_CH2) | FAULT exit output when 0, output periodic FAULT exit condition if not cleared |
| 11 | ACTIVE | Standard controller sleep mode, all inputs disabled | After vehicle ACC OFF and specific conditions met |
| 12 | ACTIVE | AUTO_LPM mode entry settings | AUTO_LPM_ENTRY = 1h |
| 13 | AUTO_LPM | Enter entry standby with channel deactivation | IOUTx < ILPM_ENTRY_AUTO |
| 14 | AUTO_LPM | AUTO_LPM mode entry confirmation after standard controller sleep mode entry | LPM_STATUS = 1h |
| 15 | AUTO_LPM | Standard controller normal mode entry | Check when door handle switches are not duplicated |
| 16 | AUTO_LPM | Exit AUTO_LPM mode upon B+ signal input or application SW input activation, or exit AUTO_LPM mode | |
| 17 | ACTIVE | B+ signal input or application SW input activation exit ACTIVE mode | AUTO_LPM_EXIT_CHx = 1h |
| 18 | ACTIVE | AUTO_LPM mode entry deactivation | AUTO_LPM_ENTRY = 0h |
| 19 | ACTIVE | AUTO_LPM_EXIT_CHx settings cleared | AUTO_LPM_EXIT_CHx = 0h |
| 19 | ACTIVE | Return to step 10 | |

---

## 2. Register Specifications

### 2.1 Register Map

| Offset | Acronym | Register Name | Section |
|--------|---------|---------------|---------|
| 0h | DEV_ID | Read the device ID from NVM | Go |
| 1h | CRC_CONFIG | CRC configuration register | Go |
| 2h | SLEEP | Set to go to SLEEP state from ACTIVE or CONFIG state | Go |
| 3h | LPM | Low power mode (LPM) settings register | Go |
| 4h | GLOBAL_FAULT_TYPE | Channel Fault Status and Global Fault Type | Go |
| 5h | FAULT_MASK | Mask the reporting of faults on the fault pin | Go |
| 7h | SW_STATE | ON/OFF control for VOUT1 and VOUT2 | Go |
| 9h | DEV_CONFIG | Global device configuration register | Go |
| Ah | ADC_CONFIG | ADC configuration register | Go |
| Bh | ADC_RESULT_VBB | ADC conversion result - VBB | Go |
| Dh | FLT_STAT_CH1 | Channel 1 fault status | Go |
| Eh | PWM_CH1 | PWM configuration register for channel 1 | Go |
| Fh | ILIM_CONFIG_CH1 | Protection configuration register for channel 1 | Go |
| 10h | CH1_CONFIG | Configuration register for channel 1 | Go |
| 11h | ADC_RESULT_CH1_I | ADC conversion result - load current sense for channel 1 | Go |
| 12h | ADC_RESULT_CH1_T | ADC conversion result - TJ.FET temperature sense for channel 1 | Go |
| 13h | ADC_RESULT_CH1_V | ADC conversion result - VOUT sense for channel 1 | Go |
| 14h | ADC_RESULT_CH1_VDS | ADC conversion result - VDS sense for channel 1 | Go |
| 15h | I2T_CONFIG_CH1 | I2T configuration register for channel 1 | Go |
| 16h | FLT_STAT_CH2 | Fault status for channel 2 | Go |
| 17h | PWM_CH2 | PWM configuration register for channel 2 | Go |
| 18h | ILIM_CONFIG_CH2 | Protection configuration register for channel 2 | Go |
| 19h | CH2_CONFIG | Configuration register for channel 2 | Go |
| 1Ah | ADC_RESULT_CH2_I | ADC conversion result - load current sense for channel 2 | Go |
| 1Bh | ADC_RESULT_CH2_T | ADC conversion result - TJ.FET temperature sense for channel 2 | Go |
| 1Ch | ADC_RESULT_CH2_V | ADC conversion result - VOUT sense for channel 2 | Go |
| 1Dh | ADC_RESULT_CH2_VDS | ADC conversion result - VDS sense for channel 2 | Go |
| 1Eh | I2T_CONFIG_CH2 | I2T configuration register for channel 2 | Go |

### 2.2 Write Registers

| Offset | Bit | Field | Condition | Initial Setting | Description | Note |
|--------|-----|-------|-----------|----------------|-------------|------|
| 3h | 2 | AUTO_LPM_EXIT_CH2 | - | - | Set according to operation process | |
| 3h | 1 | AUTO_LPM_EXIT_CH1 | - | - | Set according to operation process | |
| 5h | 5 | MASK_SHRT_VBB | - | 1h | Unused except for initial diagnostics | |
| 5h | 4 | MASK_OL_OFF | - | 1h | Unused except for initial diagnostics | |
| 7h | 1 | CH2_ON | - | 0h | Controlled by signal DB and application SW | |
| 7h | 0 | CH1_ON | - | 0h | Controlled by signal DB and application SW | |
| 9h | 10-9 | CH2_LH_IN | - | 1h | - | Signal DB parameter assignment planned for future functional safety requirements |
| 9h | 8-7 | CH1_LH_IN | - | 1h | - | Signal DB parameter assignment planned for future functional safety requirements |
| 9h | 5 | AUTO_LPM_ENTRY | - | 0h | Set according to operation process | |
| 9h | 4 | PARALLEL_12 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 9h | 3 | WD_EN | - | 1h | - | |
| 9h | 2-1 | WD_TO | - | 1h | - | |
| Ah | 4 | ADC_VSNS_DIS | - | 0h | - | |
| Eh | 11-9 | PWM_FREQ_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| Eh | 8-1 | PWM_DTY_CH1 | Signal DB parameter is PWM_C | Signal DB | See Vehicle IO Signal DB Register Configuration | Setting method varies by condition |
| | | | Signal DB parameter is PWM_X | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| | | | Signal DB parameter is PWM_O | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| Eh | 0 | PWM_EN_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| Fh | 13-12 | CAP_CHRG_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| Fh | 11 | I2T_EN_CH1 | - | 1h | - | |
| Fh | 10-8 | INRUSH_DURATION_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| Fh | 7-4 | INRUSH_LIMIT_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| Fh | 3-0 | ILIMIT_SET_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 10h | 15 | VSNS_DIS_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 10h | 8-7 | OL_SVBB_BLANK_CH1 | - | 3h | - | |
| 10h | 4-3 | OL_SVBB_EN_CH1 | - | 2h | - | |
| 10h | 1-0 | SLRT_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 15h | 8-7 | ISWCL_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 15h | 6-3 | I2T_TRIP_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 15h | 2-0 | NOM_CUR_CH1 | - | Signal DB | See Vehicle IO Signal DB Register Configuration | |
| 16h onwards | - | - | - | - | Same as CH1 | |

### 2.3 Read Registers

| Offset | Bit | Field | Description | Console Log Output |
|--------|-----|-------|-------------|-------------------|
| 4h | 13 | CH2_FLT | If FLT pin LOW or SPI WD and value is 1:<br>- Output field name to console log<br>- Read FLT_STAT_CH2 and output detailed FAULT | Output once when condition met |
| 4h | 12 | CH1_FLT | If FLT pin LOW or SPI WD and value is 1:<br>- Output field name to console log<br>- Read FLT_STAT_CH1 and output detailed FAULT | Output once when condition met |
| 4h | 11 | LPM_STATUS | - See operation process step 14<br>- During SPI WD, check AUTO_LPM mode entry status<br>- If not changed to 1 within 5 seconds, output field name to console log (repeat every 5 seconds) | Output every 5 seconds according to operation spec |
| 4h | 10 | CAHN_OCP_I2T_TSD | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| 4h | 9 | OL_SHRT_VBB_OFF_FLT | - Unused (review if functional safety spec required in future)<br>- Auto-cleared when FLT_STAT_CHx is read during initial diagnostics | Read FLT_STAT_CHx if not auto-cleared |
| 4h | 8 | GLOBAL_ERR_WRN | - Read and CLEAR during initial setup (no console log output required)<br>- If FLT pin LOW or SPI WD and value is 1 afterwards, output field name to console log | Output once when condition met (excluding initial setup) |
| 4h | 7 | LIMPHOME_STAT | Unused | |
| 4h | 6 | POR | - Read and CLEAR during initial setup<br>- If FLT pin LOW or SPI WD and value is 1 afterwards, output field name to console log and restart from operation process step 5 | Output once when condition met (excluding initial setup) |
| 4h | 5 | LPM_STATUS_1 | - Read and CLEAR this register when waking up from SLEEP mode<br>- If this register is 1 while not entering SLEEP mode, output field name to console log | Output once when condition met (excluding first WAKEUP) |
| 4h | 4 | SPI_ERR | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| 4h | 3 | WD_ERR | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| 4h | 2 | VDD_UVLO | - Read and CLEAR during initial setup<br>- If FLT pin LOW or SPI WD and value is 1 afterwards, output field name to console log | Output once when condition met (excluding initial setup) |
| 4h | 1 | VBB_UV_WRN | - Read and CLEAR during initial setup<br>- If FLT pin LOW or SPI WD and value is 1 afterwards, output field name to console log | Output once when condition met (excluding initial setup) |
| 4h | 0 | VBB_UVLO | - Read and CLEAR during initial setup<br>- If FLT pin LOW or SPI WD and value is 1 afterwards, output field name to console log | Output once when condition met (excluding initial setup) |
| Dh | 12 | I2T_MOD_CH1 | - If 1 during SPI WD, output field name and value to console log<br>- If 0 during subsequent SPI WD, output field name and value to console log | Output once when condition met<br>(ignore values that change within minimum output period) |
| Dh | 11 | LATCH_STAT_CH1 | Unused (LATCH_STAT_CH1 is always 0 because LATCH_CH1 is set to 0) | |
| Dh | 10 | FLT_CH1 | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| Dh | 9 | SW_STATE_STAT_CH1 | If FLT pin LOW or SPI WD and value differs from CH1_ON setting, output field name to console log | Output once when condition met |
| Dh | 8 | VOUT_ERR_CH1 | When CH1_ON=1, if FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| Dh | 7 | I2T_FLT_CH1 | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| Dh | 6 | LPM_WAKE_CH1 | Unused (cannot be 1 because MANUAL LPM is not used) | |
| Dh | 5 | THERMAL_SD_CH1 | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| Dh | 4 | ILIMIT_CH1 | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| Dh | 3 | SHRT_VBB_CH1 | See Open/Short diagnostic process | Output once after diagnostics complete |
| Dh | 2 | OL_OFF_CH1 | See Open/Short diagnostic process | Output once after diagnostics complete |
| Dh | 0 | THERMAL_WRN_CH1 | If FLT pin LOW or SPI WD and value is 1, output field name to console log | Output once when condition met |
| 11h | 11-0 | - | On AppSW request, read and update to memory area accessible by AppSW | |
| 12h | 10-0 | - | On AppSW request, read and update to memory area accessible by AppSW | |
| 13h | 10-0 | - | See VOL_DET in Vehicle IO Signal DB Register Configuration | |
| 14h | 10-0 | - | On AppSW request, read and update to memory area accessible by AppSW | |
| 16h onwards | - | - | Same as CH1 | |

---

## 3. Vehicle IO Signal DB Register Configuration

> **Note**: If a setting not defined in tables below is encountered, exclude that register WRITE and output error log with the excluded signal ID and parameter.

### 3.1 CAT_1

TPS2HCS08-Q1 assigned signals use ID 8.

| Signal DB Parameter | ID (DEC) | ID (BIN) | Note |
|---------------------|----------|----------|------|
| CAT1_E_FUSE_18 | 18 | 10010 | e-Fuse (TPS2HCS08) |

### 3.2 CAT_2

TPS2HCS08-Q1 assigned signals use ID 0.

| Signal DB Parameter | ID (DEC) | ID (BIN) | Note |
|---------------------|----------|----------|------|
| CAT2_ACTIVE_HIGH | 0 | 00 | Active High |

### 3.3 SC (Standard Controller)

Signal assigned to standard controller (e.g., Driver ZONE = SC1, Passenger ZONE = SC2)

### 3.4 IC

IC ID is set sequentially based on SPI MISO input order in daisy chain configuration.

### 3.5 PIN

| Signal DB Parameter | IC Channel | PIN | ID (DEC) | ID (BIN) |
|---------------------|-----------|-----|----------|----------|
| IC_PIN_1 | 1 | Channel 1 | 1 | 00001 | Signal assigned to output channel 1 |
| IC_PIN_2 | 2 | Channel 2 | 2 | 00010 | Signal assigned to output channel 2 |

### 3.6 USED

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Note |
|---------------------|----------|----------|-------------|------|
| USED_1 | 1 | 0001 | PARALLEL_12 = 0h | |
| USED_2 | 2 | 0010 | PARALLEL_12 = 1h | - Parallel mode only CH1 register used<br>- CH2 cannot use USED_2 (output error log if attempted) |

### 3.7 MOC

| Signal DB Parameter | ID (DEC) | ID (BIN) | NOM_CUR_CHx | I2T_TRIP_CHx | SWCL_DLY_TMR_CHx | ISWCL_CHx | Note |
|---------------------|----------|----------|-------------|--------------|------------------|-----------|------|
| MOC_1A | 1 | 0001 | 0h | 0h | 3h | 0h | |
| MOC_3A | 2 | 0010 | 0h | 2h | 3h | 0h | |
| MOC_5A | 3 | 0011 | 3h | 5h | 3h | 0h | |
| MOC_10A | 4 | 0100 | 6h | Ch | 3h | 0h | |
| MOC_15A | 5 | 0101 | 5h | Ah | 3h | 0h | Parallel mode only available<br>(Set only when USED parameter is 1 and MOC_10A or greater. Otherwise output error log) |
| MOC_20A | 6 | 0110 | 6h | Fh | 3h | 0h | Parallel mode only available<br>(Set only when USED parameter is 1 and MOC_10A or greater. Otherwise output error log) |
| MOC_30A | - | - | MOC_30A setting not possible<br>(Set only when USED parameter is 1 and MOC_10A or greater. Otherwise output error log. Set when USED parameter is 2 and MOC_20A or greater) | |

### 3.8 OCP

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description | Note |
|---------------------|----------|----------|-------------|-------------|------|
| | | | **ILIMIT_SET_CHx** | | |
| OCP_100mV | 1 | 0001 | 0h | 10A | |
| OCP_200mV | 2 | 0010 | 1h | 12.5A | |
| OCP_300mV | 3 | 0011 | 2h | 15A | |
| OCP_400mV | 4 | 0100 | 3h | 17.5A | |
| OCP_500mV | 5 | 0101 | 4h | 20A | |
| OCP_600mV | 6 | 0110 | 5h | 22.5A | |
| OCP_700mV | 7 | 0111 | 6h | 25A | |
| OCP_800mV | 8 | 1000 | 7h | 32.5A | Default setting for single channel use |
| OCP_9 | 9 | 1001 | 8h | 40A | Default setting for parallel mode use |
| OCP_10 | 10 | 1010 | 9h | 47.5A | |
| OCP_11 | 11 | 1011 | Ah | 55A | |

### 3.9 RT

Unused parameter.

### 3.10 PWM

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | | Note |
|---------------------|----------|----------|-------------|-------------|------|
| | | | **CAP_CHRG_CHx** | **PWM_EN_CHx** | |
| PWM_O | 0 | 00 | 00b | 1h | |
| PWM_X | 1 | 01 | 00b | 0h | |
| PWM_C | 2 | 10 | 10b | 0h | |

### 3.11 OLD

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description | Note |
|---------------------|----------|----------|-------------|-------------|------|
| | | | **OL_SVBB_EN_CHx** | | |
| OLD_OFF | 0 | 0000 | 0h | Disable | |
| OLD_PWR | 2 | 0010 | 2h | OPEN/Battery Short detection | If OL_OFF_CHx is 1 at entry during at least 1 second, read OL_SVBB_EN_CHx setting to 1h after entry standby and read SHRT_VBB_CHx to read report to command |

### 3.12 PWM_F

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description |
|---------------------|----------|----------|-------------|-------------|
| | | | **PWM_FREQ_CH1** | |
| PWM_40Hz | 0 | 0000 | 0h | 0.8Hz |
| PWM_80Hz | 1 | 0001 | 1h | 3.4Hz |
| PWM_100Hz | 2 | 0010 | 2h | 13.8Hz |
| PWM_120Hz | 3 | 0011 | 3h | 111Hz |
| PWM_200Hz | 4 | 0100 | 4h | 221Hz |
| PWM_400Hz | 5 | 0101 | 5h | 425Hz |
| PWM_800Hz | 6 | 0110 | 6h | 885Hz |
| PWM_1000Hz | 7 | 0111 | 7h | 1770Hz |

### 3.13 CT

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description | Note |
|---------------------|----------|----------|-------------|-------------|------|
| | | | **INRUSH_DURATION_CHx** | | |
| CT_5ms | 0 | 0000 | 0h | 0ms | CAP_CHRG_CHx must be 00 |
| CT_10ms | 1 | 0001 | 1h | 2ms | Charging Time set as inrush time for accurate regulation.<br>However, for PWM_X, fixed at 4ms or higher. |
| CT_15ms | 2 | 0010 | 2h | 4ms | |
| CT_20ms | 3 | 0011 | 3h | 6ms | |
| CT_25ms | 4 | 0100 | 4h | 10ms | |
| CT_30ms | 5 | 0101 | 5h | 20ms | |
| CT_40ms | 6 | 0110 | 6h | 50ms | |
| CT_50ms | 7 | 0111 | 7h | 100ms | |

### 3.14 SR

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description |
|---------------------|----------|----------|-------------|-------------|
| | | | **SLRT_CHx** | |
| SR_1mA | 0 | 0000 | 0h | 0.25V/us |
| SR_2mA | 1 | 0001 | 1h | 0.34V/us |
| SR_4mA | 2 | 0010 | 2h | 0.45V/us |
| SR_8mA | 3 | 0011 | 3h | 0.55V/us |

### 3.15 VOL_DET

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description | Note |
|---------------------|----------|----------|-------------|-------------|------|
| | | | **VSNS_DIS_CHx** | | |
| VOL_DET_OFF | 0 | 00 | 1h | | |
| VOL_DET_ON | 1 | 01 | 0h | Default 5ms periodic ADC_RESULT_CHx_V (10bit) read and check if open | Every 5ms if battery voltage difference exceeds memory entry default |

### 3.16 DEF_Value

| Signal DB Parameter | ID (DEC) | ID (BIN) | IC Register | Description |
|---------------------|----------|----------|-------------|-------------|
| | | | **CHx_ON** | |
| DEF_IDLE | 0 | 00 | 0h | IC keep idle, disable until 0h setting until no external entry |
| DEF_ACTIVE | 1 | 01 | 1h | IC keep on until no exception, setting 1h (LPM state 1h exempt) |

### 3.17 WAKE

Unused parameter.

### 3.18 PRE_Value

Unused parameter.

### 3.19 WC

Unused parameter.

### 3.20 Threshold_V

Unused parameter.

### 3.21 PWM_Duty

#### a. When Signal DB Parameter is PWM_C

| Signal DB Parameter | | | IC Register | |
|---------------------|------|------|-------------|-------------|
| **PWM_Duty** | **ID (DEC)** | **ID (BIN)** | **INRUSH_LIMIT_CH1** | **Description (CAP_CHRG_CHx=10 basis)** |
| 0 | 0 | 00000000 | 0h | 1.6A |
| 1 | 1 | 00000001 | 1h | 2A |
| 2 | 2 | 00000010 | 2h | 2.4A |
| 3 | 3 | 00000011 | 3h | 2.8A |
| 4 | 4 | 00000100 | 4h | 3.3A |
| 5 | 5 | 00000101 | 5h | 3.6A |
| 6 | 6 | 00000110 | 6h | 4.2A |
| 7 | 7 | 00000111 | 7h | 5.5A |
| 8 | 8 | 00001000 | 8h | 6.8A |
| 9 | 9 | 00001001 | 9h | 8.1A |
| 10 | 10 | 00001010 | Ah | 9.5A |
| 11 | 11 | 00001011 | Bh | 11A |
| 12 | 12 | 00001100 | Ch | 12A |

#### b. When Signal DB Parameter is PWM_X

| Signal DB Parameter | | | IC Register | |
|---------------------|------|------|-------------|-------------|
| **PWM_Duty** | **ID (DEC)** | **ID (BIN)** | **INRUSH_LIMIT_CH1** | **Description (CAP_CHRG_CHx=00 basis)** |
| 0 | 0 | 00000000 | 0h | 10A |
| 1 | 1 | 00000001 | 1h | 12.5A |
| 2 | 2 | 00000010 | 2h | 15A |
| 3 | 3 | 00000011 | 3h | 17.5A |
| 4 | 4 | 00000100 | 4h | 20A |
| 5 | 5 | 00000101 | 5h | 22.5A |
| 6 | 6 | 00000110 | 6h | 25A |
| 7 | 7 | 00000111 | 7h | 32.5A |
| 8 | 8 | 00001000 | 8h | 40A |
| 9 | 9 | 00001001 | 9h | 47.5A |
| 10 | 10 | 00001010 | Ah | 55A |

#### c. When Signal DB Parameter is PWM_O

| Signal DB Parameter | | | IC Register | Description |
|---------------------|------|------|-------------|-------------|
| **PWM_Duty** | **ID (DEC)** | **ID (BIN)** | **PWM_DTY_CH1** | |
| 0 | 0 | 00000000 | 00000000 | ID value and register value have 1:1 correspondence |
| 255 | 255 | 11111111 | 11111111 | |

---

## 4. Other Processes

### 4.1 Open/Short Diagnostic Process (Performed with output OFF during initial diagnostics)

#### a. Diagnosis by OL_OFF_CHx and SHRT_VBB_CHx Status

| Step | Step 1 | Step 2 | Diagnosis Result | Note |
|------|--------|--------|------------------|------|
| | (OL_SVBB_EN_CHx=2h) | (OL_SVBB_EN_CHx=1h) | | |
| a. | OL_OFF_CHx=0 | - | Normal | |
| | OL_OFF_CHx=1 | SHRT_VBB_CHx=0 | Open | Output channel status to console log |
| | | SHRT_VBB_CHx=1 | Battery Short | Output channel status to console log |

#### b. GND Short

GND Short is determined when overcurrent flows in output ON state.
(Excluded from initial diagnostics, no separate console log output required)

---

## 5. State Machine Diagram

### 5.1 State Definitions

#### OFF
Power-on reset state.
- **Entry**: `ANY STATE → OFF` via **POR (Power-On Reset)**
- **Exit**: `OFF → SLEEP` when **POR is reset/released**
- VOUTx: OFF
- Registers: Cleared
- SPI: OFF

#### SLEEP
Lowest power standby state.
- **Entry**:
  - From OFF when POR is released
  - From AUTO_LPM when `VDD < VDD_UVLOF`
  - From CONFIG/ACTIVE via SPI SLEEP command
- **Exit**: `SLEEP → INIT & ABIST` when `CSN = 0 && VDD > VDD_UVLOR`
- VOUTx: OFF
- Registers: Cleared
- SPI: OFF
- IQ: Very low (IQ_SLEEP)
- Diagnostics: OFF
- Protection: OFF

#### INIT & ABIST
Initialization and built-in self-test phase.
- **Entry**: From SLEEP when SPI access conditions are met
- **Exit**: `INIT & ABIST → CONFIG` after digital initialization
- Digital ON
- NVM read
- Duration: tREADY ≈ 65 μs

#### CONFIG
Output inactive state where IC can be configured via SPI.
- **Entry**:
  - From INIT & ABIST
  - From ACTIVE via `SPI VOUTx OFF`
  - From MANUAL_LPM via SPI Manual LPM Exit CMD
  - From LIMP_HOME when conditions clear
- **Exit**:
  - `CONFIG → ACTIVE` via **SPI CMD**
  - `CONFIG → MANUAL_LPM` via **SPI Manual LPM Entry Command**
  - `CONFIG → LIMP_HOME` via **LHI high || SPI Watch Dog Fault**
  - `CONFIG → SLEEP` via **SPI SLEEP command**
- VOUTx: OFF
- Registers: Retained
- SPI: Full Read/Write
- IQ: Full
- Diagnostics: Enabled
- Protection: OFF

#### ACTIVE
Normal operation mode with output control enabled.
- **Entry**:
  - From CONFIG via SPI CMD
  - From AUTO_LPM via exit conditions
  - From VBB_WRN when `VBB ≥ VBB_UV_WRN_R && VDD > VDD_UVLOR`
  - From VBB_UVLO when `VBB_UVLOR < VBB < VBB_UV_WRN_F && VDD > VDD_UVLOR`
- **Exit**:
  - `ACTIVE → CONFIG` via **SPI VOUTx OFF**
  - `ACTIVE → AUTO_LPM` via **AUTO_LPM_ENTRY = 1 && (CHx_ON = 0 || IOUT < ILPM_ENTRY_AUTO) && AUTO_LPM_EXIT_CHx = 0**
  - `ACTIVE → VBB_WRN` via **VBB < VBB_UVLOF && IPOR && VDD > VDD_UVLOR**
  - `ACTIVE → VBB_UVLO` via **VBB < VBB_UVLOF && IPOR && VDD > VDD_UVLOR**
  - `ACTIVE → LIMP_HOME` via **LHI high || SPI Watch Dog Fault**
- VOUTx: Set by CHx_ON
- Registers: Retained
- SPI: Full Read/Write
- IQ: Full
- Diagnostics: Enabled
- Protection: Enabled

#### AUTO_LPM
Automatic low power mode entered from ACTIVE state.
- **Entry**: From ACTIVE when automatic LPM conditions are met
- **Exit**:
  - `AUTO_LPM → ACTIVE` via **AUTO_LPM_EXIT_CHx = 1 || Load Current Increase || LHI = 1**
  - `AUTO_LPM → SLEEP` via **VDD < VDD_UVLOF**
- VOUTx: Set by CHx_ON (maintained during LPM entry)
- Registers: Retained
- SPI: Write only to LPM register (All registers can be read)
- IQ: Low (IQ_LPM_AUTO)
- Diagnostics: OFF
- Protection: Enabled

#### MANUAL_LPM
Manual low power mode entered via SPI command.
- **Entry**: From CONFIG via SPI Manual LPM Entry Command
- **Exit**:
  - `MANUAL_LPM → CONFIG` via **SPI Manual LPM Exit CMD || Load Current Increase || LHI = 1**
  - `MANUAL_LPM → SLEEP` via **VDD < VDD_UVLOF**
- VOUTx: Set by CHx_ON (maintained during LPM entry)
- Registers: Retained
- SPI: Write only to LPM register (All registers can be read)
- IQ: Low (IQ_LPM_MAN)
- Diagnostics: OFF
- Protection: Enabled

#### LIMP_HOME
Fail-safe mode for fault conditions, allowing limited HW input control.
- **Entry**:
  - From CONFIG via **LHI low && SPI LH CMD || (LIMPHOME_STAT = 1)**
  - From ACTIVE via **LHI high || SPI Watch Dog Fault**
- **Exit**: `LIMP_HOME → CONFIG` when fault conditions clear
- VOUTx: Set by CHx_LH_IN
- Registers: Retained
- SPI: All registers can be read / Write only to GLOBAL_FAULT_TYPE register if LHI = 0 (VDD > VDD_UVLOR required for SPI)
- IQ: Full
- Diagnostics: Enabled
- Protection: Enabled

#### VBB_WRN
Battery voltage warning state (output maintained).
- **Entry**: From ACTIVE when `VBB < VBB_UVLOF && IPOR && VDD > VDD_UVLOR`
- **Exit**: `VBB_WRN → ACTIVE` when `VBB ≥ VBB_UV_WRN_R && VDD > VDD_UVLOR`
- VOUTx: Set by CHx_ON (maintained)
- Registers: Retained
- SPI: Full Read/Write
- IQ: Full
- Diagnostics: OFF
- Protection: Enabled

#### VBB_UVLO
Battery under-voltage lockout state (output disabled).
- **Entry**: From ACTIVE when `VBB < VBB_UVLOF && IPOR && VDD > VDD_UVLOR`
- **Exit**: `VBB_UVLO → ACTIVE` when `VBB_UVLOR < VBB < VBB_UV_WRN_F && VDD > VDD_UVLOR`
- VOUTx: OFF
- Registers: Retained
- SPI: Full Read/Write
- IQ: Full
- Diagnostics: OFF
- Protection: OFF

### 5.2 State Transition Summary

#### Normal Operation Flow
```
OFF → SLEEP → INIT & ABIST → CONFIG ⇄ ACTIVE
```

#### Low Power Modes
```
ACTIVE ⇄ AUTO_LPM
CONFIG ⇄ MANUAL_LPM
LPM → SLEEP (when VDD undervoltage)
```

#### Power Supply Abnormal States
```
ACTIVE ⇄ VBB_WRN
ACTIVE ⇄ VBB_UVLO
```

#### Fail-Safe Mode
```
CONFIG/ACTIVE ⇄ LIMP_HOME
```

#### Global Reset
```
ANY STATE → OFF (via POR)
```

---

## 6. Notes

### 6.1 Register Write Exclusion

If Signal DB parameters are not defined in Section 3, the corresponding register WRITE operation must be excluded and an error log must be output with the excluded signal ID and parameter name.

### 6.2 Channel Configuration

- CH1 and CH2 have identical configuration structure
- Registers from 16h onwards follow the same pattern as CH1

### 6.3 Diagnostic Process

- Open/Short diagnostics are performed during initial setup (Step 6 of operation process)
- See Section 4.1 for detailed diagnostic behavior

### 6.4 Parallel Mode

- Only available when USED_2 is selected
- Both channels operate in parallel with combined current capability
- CH1 register controls both channels in parallel mode

---
