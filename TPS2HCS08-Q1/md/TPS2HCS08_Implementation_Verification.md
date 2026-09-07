# TPS2HCS08-Q1 Implementation Verification Guide

## Overview

This document provides detailed verification instructions for ExVioDb_Tps2hcs08.c/h implementation against the TPS2HCS08-Q1 datasheet specifications.

---

## A. Automatic State Transitions (Chip-Managed)

### States: 1, 2, 3, 7, 11, 13, 15, 16

These states are managed automatically by the IC. Driver responsibilities:

| Step | State | Driver Action | Verification Required |
|------|-------|---------------|----------------------|
| 1 | OFF | None (vehicle battery connection) | N/A |
| 2 | SLEEP | Update enum only | Cannot verify (SPI off) |
| 3 | INIT & ABIST | None (transient ~65µs) | Can merge with step 4 |
| 7 | - | None (reserved/skipped) | N/A |
| 11 | ACTIVE → Sleep prep | Update enum only | Verify via step 12 |
| 13 | AUTO_LPM entry standby | Update enum + **MUST verify** | **Verify in step 14** |
| 15 | AUTO_LPM → Wakeup | Update enum only | Verify via step 17 |
| 16 | AUTO_LPM exit | Update enum only | Verify via step 17 |

### Critical Rule

**DO NOT** just update enum blindly. Always verify state synchronization:

```c
// BAD - Blind enum update
state = STATE_AUTO_LPM;

// GOOD - Verify then update
if (verify_lpm_entry_status() == SUCCESS) {
    state = STATE_AUTO_LPM;
} else {
    // Retry or error handling
}
```

### Post-Wakeup Synchronization

**Required**: After any wakeup, check POR bit to detect chip reset.

**PDF Reference**: p.71-72, GLOBAL_FAULT_TYPE register, POR bit

**Code Check**:
- [ ] Does code read GLOBAL_FAULT_TYPE after wakeup?
- [ ] Does POR=1 trigger re-initialization sequence (step 5 onwards)?
- [ ] Is there a state enum synchronization mechanism?

---

## B. Pin/Timing Control Stage

### Step 4: CONFIG State Entry (CSN Timing Control)

#### Purpose
Wake device from SLEEP via CSN pin timing control and enter CONFIG state.

#### PDF Reference

| Item | Location | Content |
|------|----------|---------|
| Wake-up methods (2 types) | p.31-32, Section 8.3.5 SLEEP | "The device can be woken from the SLEEP state through the CSN pin going low" |
| Valid/invalid waveform examples | p.31-32, Figure 8-13 | Good frame vs SPI_ERR cases with tREADY timing |
| tREADY specification | p.22, Figure 8-4 | State diagram shows "tREADY= ~65 us" in INIT & ABIST box |
| | p.13, Table 6-6 | SPI Timing Requirements |
| Post-wakeup register state | p.31, last paragraph | "Upon wakeup from SLEEP state..." - registers reset, FLT pin LOW, POR/UVLO flags set |

#### Wake-up Method Options

**Method 1**: CSN pulse < tREADY (short pulse wake-up)
- CSN goes LOW for < 65µs
- Chip wakes but first transaction fails
- Need second transaction for actual communication

**Method 2**: CSN held LOW ≥ tREADY until first SPI transaction completes (recommended)
- CSN goes LOW and stays LOW
- Wait ≥ 65µs
- Perform first SPI transaction while CSN still LOW
- No SPI_ERR

#### Code Verification Checklist

**File**: ExVioDb_Tps2hcs08.c/h

- [ ] **CSN pin control implementation**
  - [ ] Is CSN pin defined and controlled?
  - [ ] Which wake-up method is implemented? (Method 1 or 2?)
  - [ ] If Method 2: Is there a ≥65µs delay between CSN LOW and first SPI transaction?

- [ ] **Timing compliance**
  ```c
  // Expected pattern for Method 2
  SET_CSN_LOW();
  DELAY_US(65);  // or longer
  spi_first_transaction();
  ```

- [ ] **Post-wakeup handling**
  - [ ] Does code read GLOBAL_FAULT_TYPE immediately after wakeup?
  - [ ] Is POR bit checked and cleared?
  - [ ] Are UVLO/UV_WRN bits checked?
  - [ ] Is FLT pin state monitored?

**Search Keywords in Code**:
```
CSN, tREADY, wake, SLEEP, 65, delay
```

---

## C. SPI Frame Transmission Stages

### Common Prerequisites (All C Stages)

#### SPI Frame Structure

**PDF Reference**: p.27, Section 8.3.3.2, Figures 8-8 to 8-11

| Frame Type | Structure |
|------------|-----------|
| Write Frame | ADDR[7:0] + RW + DATA[15:0] + PAR + CRC[7:0] |
| Read Frame | ADDR[7:0] + RW + dummy[15:0] + PAR + CRC[7:0] |
| SDO Header | GLOBAL_FAULT_TYPE[15:8] in every response |

#### Code Verification - Frame Building

- [ ] **Frame structure matches datasheet**
  - [ ] Address byte format correct?
  - [ ] RW bit position correct? (Write=0, Read=1)
  - [ ] Data byte order correct? (MSB first)
  - [ ] Parity calculation implemented? (Even parity)
  - [ ] CRC calculation implemented? (if enabled)

- [ ] **SDO header parsing**
  - [ ] Does code extract GLOBAL_FAULT_TYPE[15:8] from every SDO response?
  - [ ] Are critical fault bits checked in header (WD_ERR, POR, CHx_FLT)?

**PDF Reference**:
- p.27-28, SDO header description
- p.13, Table 6-6 & Table 6-7, SPI timing specs (for MCAL configuration)
- p.65, Table 8-13, Complete register map

**Search Keywords in Code**:
```
SPI, frame, MOSI, MISO, SDO, parity, CRC
```

---

### Step 5: Initial Register Configuration (Vehicle IO Signal DB Based)

#### Purpose
Write all configuration registers based on Vehicle IO Signal Database parameters.

#### Registers to Configure

| Register | Offset | PDF Reference | Configuration Items |
|----------|--------|---------------|-------------------|
| DEV_CONFIG | 9h | p.76-77 | WD_EN, PARALLEL_12, FLT_LTCH_DIS, AUTO_LPM_ENTRY, etc. |
| ADC_CONFIG | Ah | p.78-79 | ADC diagnostics enable |
| CH1_CONFIG | 10h | p.86-87 | Channel 1 settings: OL_SVBB_EN, SLRT, etc. |
| CH2_CONFIG | 19h | p.99-100 | Channel 2 settings |
| ILIM_CONFIG_CH1 | Fh | p.84-85 | Overcurrent protection: ILIMIT_SET, INRUSH_LIMIT, CAP_CHRG, I2T_EN |
| ILIM_CONFIG_CH2 | 18h | p.97-98 | Same for CH2 |
| I2T_CONFIG_CH1 | 15h | p.92-93 | I²T wire protection: NOM_CUR, I2T_TRIP, ISWCL |
| I2T_CONFIG_CH2 | 1Eh | p.105-106 | Same for CH2 |
| PWM_CH1 | Eh | p.83 | PWM settings: PWM_FREQ, PWM_DTY, PWM_EN |
| PWM_CH2 | 17h | p.96 | Same for CH2 |
| FAULT_MASK | 5h | p.74-75 | Fault reporting mask |
| CRC_CONFIG | 1h | p.68 | CRC enable/disable (optional) |

#### Critical Rule

**PDF Reference**: p.32-33, Section 8.3.6

> "Register settings must be completed in CONFIG state. If device enters SLEEP, re-configuration is required."

#### Code Verification Checklist

- [ ] **Signal DB parameter mapping**
  - [ ] Are all parameters from Section 3 of TPS2HCS08-Q1_Spec.md mapped to registers?
  - [ ] USED (PARALLEL_12)
  - [ ] MOC (NOM_CUR, I2T_TRIP)
  - [ ] OCP (ILIMIT_SET)
  - [ ] PWM (CAP_CHRG, PWM_EN)
  - [ ] OLD (OL_SVBB_EN)
  - [ ] PWM_F (PWM_FREQ)
  - [ ] CT (INRUSH_DURATION)
  - [ ] SR (SLRT)
  - [ ] VOL_DET (VSNS_DIS)
  - [ ] DEF_Value (CHx_ON initial)
  - [ ] PWM_Duty (INRUSH_LIMIT for PWM_C/X, PWM_DTY for PWM_O)

- [ ] **Register write sequence**
  - [ ] Are registers written in correct order?
  - [ ] Is each write verified (read-back check)?
  - [ ] Error handling if write fails?

- [ ] **Missing parameter handling**
  - [ ] Does code output error log for undefined Signal DB parameters?
  - [ ] Are undefined parameters excluded from register write?

**Search Keywords in Code**:
```
DEV_CONFIG, ADC_CONFIG, CH1_CONFIG, ILIM_CONFIG, I2T_CONFIG, PWM_CH, FAULT_MASK
signal_db, config, init, setup
```

**Expected Code Pattern**:
```c
// Example: MOC parameter mapping
switch (signal_db.MOC) {
    case MOC_1A:
        reg_i2t.NOM_CUR = 0x0;
        reg_i2t.I2T_TRIP = 0x0;
        break;
    case MOC_3A:
        reg_i2t.NOM_CUR = 0x0;
        reg_i2t.I2T_TRIP = 0x2;
        break;
    // ... (refer to Section 3.7 of spec)
}
```

---

### Step 6: Open/Short Diagnostics

#### Purpose
Diagnose all channels for OPEN and VBB SHORT faults in OFF state.

#### Diagnostic Methods

**PDF Reference**: p.59-61, Section 8.3.12.1

| Fault Type | Detection Method | Register Setting | Result Bit |
|------------|------------------|------------------|------------|
| Open Load | Off-state pull-up | OL_SVBB_EN_CHx = 10b | OL_OFF_CHx = 1 |
| Short-to-VBB | Off-state pull-down | OL_SVBB_EN_CHx = 01b | SHRT_VBB_CHx = 1 |

#### Diagnostic Procedure

**PDF Reference**: p.60-61, "Distinguishing Between..." + Figure 8-37

**Step 1**: Set OL_SVBB_EN_CHx = 10b (pull-up mode)
- Wait for blanking time (OL_SVBB_BLANK_CHx setting)
- Read FLT_STAT_CHx
- If OL_OFF_CHx = 0: **Normal** (proceed to next channel)
- If OL_OFF_CHx = 1: Proceed to Step 2

**Step 2**: Set OL_SVBB_EN_CHx = 01b (pull-down mode)
- Wait for blanking time
- Read FLT_STAT_CHx 3 times consecutively
- If SHRT_VBB_CHx = 0: **Open Load** fault
- If SHRT_VBB_CHx = 1: **Short-to-VBB** fault

#### Register Details

**Configuration Register**: CH1_CONFIG (10h) / CH2_CONFIG (19h)
- OL_SVBB_EN_CHx [4:3]: Detection mode
- OL_SVBB_BLANK_CHx [8:7]: Blanking time
- OL_PU_STR_CHx: Pull-up strength (optional)

**PDF Reference**: p.87 (CH1_CONFIG) / p.100 (CH2_CONFIG)

**Status Register**: FLT_STAT_CH1 (Dh) / FLT_STAT_CH2 (16h)
- OL_OFF_CHx [2]: Open load flag
- SHRT_VBB_CHx [3]: Short-to-battery flag

**PDF Reference**: p.81-82 (FLT_STAT_CH1) / p.94-95 (FLT_STAT_CH2)

#### Code Verification Checklist

- [ ] **Diagnostic sequence implementation**
  ```c
  // Expected pattern
  for (each channel) {
      // Step 1: Check for normal/abnormal
      set_OL_SVBB_EN(channel, 0b10);  // Pull-up
      wait_blanking_time();
      status = read_FLT_STAT(channel);

      if (status.OL_OFF == 0) {
          result[channel] = NORMAL;
          continue;
      }

      // Step 2: Distinguish open vs short
      set_OL_SVBB_EN(channel, 0b01);  // Pull-down
      wait_blanking_time();

      // Read 3 times consecutively
      for (i = 0; i < 3; i++) {
          status = read_FLT_STAT(channel);
          shrt_vbb_samples[i] = status.SHRT_VBB;
      }

      if (all_zero(shrt_vbb_samples)) {
          result[channel] = OPEN_LOAD;
      } else {
          result[channel] = SHORT_TO_VBB;
      }
  }
  ```

- [ ] **Timing compliance**
  - [ ] Is blanking time correctly calculated from OL_SVBB_BLANK_CHx setting?
  - [ ] Are delays inserted between mode changes?

- [ ] **Result reporting**
  - [ ] Are diagnostic results logged to console?
  - [ ] Format: Channel number + fault type
  - [ ] Is result stored for application SW access?

- [ ] **GND short handling**
  - [ ] Is GND short detection excluded from initial diagnostics?
  - [ ] Is GND short detected during ON state (overcurrent)?

**PDF Reference**: p.60, "b. GND Short" note

**Search Keywords in Code**:
```
OL_SVBB_EN, OL_OFF, SHRT_VBB, open, short, diagnostic, FLT_STAT
blanking, pull-up, pull-down
```

---

### Step 8 & 9: Channel Activation (CHx_ON = 1)

#### Purpose
- Step 8: Activate channels based on B+ signal input
- Step 9: Activate channels based on Application SW input

#### Register

**SW_STATE (7h)**: ON/OFF control for VOUT1 and VOUT2

**PDF Reference**: p.75

| Bit | Field | Description |
|-----|-------|-------------|
| 0 | CH1_ON | 0: Channel 1 OFF, 1: Channel 1 ON |
| 1 | CH2_ON | 0: Channel 2 OFF, 1: Channel 2 ON |

#### Output Control by State

**PDF Reference**: p.24, Table 8-1

| State | Output Control Method |
|-------|----------------------|
| CONFIG | VOUTx = OFF (forced) |
| ACTIVE | VOUTx controlled by CHx_ON |
| AUTO_LPM | VOUTx maintained at LPM entry state |
| MANUAL_LPM | VOUTx maintained at LPM entry state |
| LIMP_HOME | VOUTx controlled by CHx_LH_IN (hardware input) |
| VBB_WRN | VOUTx controlled by CHx_ON (maintained) |
| VBB_UVLO | VOUTx = OFF (forced) |

#### Code Verification Checklist

- [ ] **Channel control interface**
  ```c
  // Expected functions
  set_channel_on(channel_id, enable);
  get_channel_status(channel_id);
  ```

- [ ] **Input source handling**
  - [ ] Step 8: Is B+ signal input (hardware switch) mapped to CHx_ON?
  - [ ] Step 9: Is Application SW command mapped to CHx_ON?
  - [ ] Are both input sources properly arbitrated?

- [ ] **State validation**
  - [ ] Is channel activation only allowed in ACTIVE state?
  - [ ] Is error returned if attempted in CONFIG/SLEEP state?

- [ ] **Write and verify**
  - [ ] Is SW_STATE register written correctly?
  - [ ] Is write operation verified by read-back?
  - [ ] Is SW_STATE_STAT_CHx checked for actual switch state?

**PDF Reference**: p.81, FLT_STAT_CH1, bit [9] SW_STATE_STAT_CH1

**Search Keywords in Code**:
```
SW_STATE, CH1_ON, CH2_ON, channel, activate, enable, disable
B+, app_sw, application
```

---

### Step 10: Periodic Fault READ (SPI Watchdog)

#### Purpose
- Monitor faults periodically
- Satisfy SPI watchdog requirement
- Read GLOBAL_FAULT_TYPE, FLT_STAT_CH1, FLT_STAT_CH2

#### Watchdog Specification

**PDF Reference**: p.28-29, Table 8-4, Section 8.3.4.2

| WD_TO Setting | Timeout Period | Required Read Interval |
|---------------|----------------|----------------------|
| 00b | 10 ms | < 10 ms |
| 01b | 20 ms | < 20 ms |
| 10b | 50 ms | < 50 ms |
| 11b | 100 ms | < 100 ms |

**Register**: DEV_CONFIG (9h), bits [2:1] WD_TO, bit [3] WD_EN

**PDF Reference**: p.76

#### Fault Registers

**GLOBAL_FAULT_TYPE (4h)**

**PDF Reference**: p.71-73

| Bit | Field | Description | Handling |
|-----|-------|-------------|----------|
| 13 | CH2_FLT | Channel 2 fault | If 1: Read FLT_STAT_CH2 for details |
| 12 | CH1_FLT | Channel 1 fault | If 1: Read FLT_STAT_CH1 for details |
| 11 | LPM_STATUS | LPM entry status (real-time) | Used in step 14 |
| 10 | CAHN_OCP_I2T_TSD | Global OCP/I2T/TSD | Output to console |
| 9 | OL_SHRT_VBB_OFF_FLT | Off-state fault | Auto-cleared by FLT_STAT read |
| 8 | GLOBAL_ERR_WRN | Global error/warning | Output to console |
| 7 | LIMPHOME_STAT | Limp-home status | Check state transition |
| 6 | POR | Power-on reset | Trigger re-init if 1 |
| 5 | LPM_STATUS_1 | LPM wakeup flag | Read-clear type |
| 4 | SPI_ERR | SPI communication error | Output to console |
| 3 | WD_ERR | Watchdog timeout | **Critical**: Output + check WD config |
| 2 | VDD_UVLO | VDD undervoltage | Output to console |
| 1 | VBB_UV_WRN | VBB warning | Check state (VBB_WRN) |
| 0 | VBB_UVLO | VBB undervoltage | Check state (VBB_UVLO) |

**FLT_STAT_CH1 (Dh) / FLT_STAT_CH2 (16h)**

**PDF Reference**: p.81-82 / p.94-95

| Bit | Field | Description |
|-----|-------|-------------|
| 12 | I2T_MOD_CH1 | I²T modulation active |
| 11 | LATCH_STAT_CH1 | Latch-off status |
| 10 | FLT_CH1 | Channel fault summary |
| 9 | SW_STATE_STAT_CH1 | Actual switch state |
| 8 | VOUT_ERR_CH1 | Output voltage error |
| 7 | I2T_FLT_CH1 | I²T fault |
| 6 | LPM_WAKE_CH1 | LPM wake source |
| 5 | THERMAL_SD_CH1 | Thermal shutdown |
| 4 | ILIMIT_CH1 | Current limit active |
| 3 | SHRT_VBB_CH1 | Short-to-battery |
| 2 | OL_OFF_CH1 | Open load (off-state) |
| 0 | THERMAL_WRN_CH1 | Thermal warning |

#### Fault Clearing Rules

**PDF Reference**: p.28, Table 8-3

| Fault Bit | Clear Method |
|-----------|--------------|
| Most faults in FLT_STAT_CHx | Read FLT_STAT_CHx register |
| POR, UVLO, UV_WRN | Read GLOBAL_FAULT_TYPE register |
| LPM_STATUS_1 | Read GLOBAL_FAULT_TYPE register |
| Some latched faults | Specific clear sequence (check Table 8-3) |

**Critical**: Store previous fault state and compare with current to detect new faults.

```c
// Expected pattern
static uint16_t prev_global_fault = 0;
static uint16_t prev_ch1_fault = 0;
static uint16_t prev_ch2_fault = 0;

void periodic_fault_read(void) {
    uint16_t global_fault = read_GLOBAL_FAULT_TYPE();

    // Detect new faults (rising edge)
    uint16_t new_faults = (global_fault ^ prev_global_fault) & global_fault;

    if (new_faults & BIT_CH1_FLT) {
        uint16_t ch1_fault = read_FLT_STAT_CH1();
        handle_ch1_faults(ch1_fault);
    }

    if (new_faults & BIT_CH2_FLT) {
        uint16_t ch2_fault = read_FLT_STAT_CH2();
        handle_ch2_faults(ch2_fault);
    }

    // ... check other fault bits

    prev_global_fault = global_fault;
}
```

#### Code Verification Checklist

- [ ] **Periodic timing**
  - [ ] Is fault read executed at correct interval based on WD_TO setting?
  - [ ] Is interval < timeout period? (e.g., if WD_TO=01b (20ms), read every ≤20ms)
  - [ ] Is this tied to MCU cycle (5ms mentioned in operation process)?

- [ ] **Fault detection logic**
  - [ ] Is previous fault state stored?
  - [ ] Are new faults detected by comparing previous vs current?
  - [ ] Is rising-edge detection implemented?

- [ ] **Fault handling per bit**
  - [ ] CH1_FLT / CH2_FLT: Read detailed status register?
  - [ ] POR: Trigger re-initialization (step 5 onwards)?
  - [ ] WD_ERR: Log + check watchdog configuration?
  - [ ] VBB_UV_WRN / VBB_UVLO: Check state machine transition?
  - [ ] LIMPHOME_STAT: Check state machine transition?

- [ ] **Console logging**
  - [ ] Are fault events logged to console with field name?
  - [ ] Is logging limited to "once when condition met" to avoid spam?
  - [ ] Is re-logging allowed after fault clears then reappears?

- [ ] **Fault clearing**
  - [ ] Are faults cleared by reading appropriate registers?
  - [ ] Is GLOBAL_FAULT_TYPE read after initial setup to clear POR/UVLO?
  - [ ] Are FLT_STAT_CHx registers read to clear channel faults?

**PDF Reference**: p.29-30, Section 8.3.4, Complete fault reporting rules

**Search Keywords in Code**:
```
GLOBAL_FAULT_TYPE, FLT_STAT, watchdog, WD_ERR, WD_TO, periodic, poll
fault, POR, UVLO, CH1_FLT, CH2_FLT
```

---

### Step 12: AUTO_LPM Entry Configuration

#### Purpose
Configure device to enter AUTO_LPM mode automatically when conditions are met.

#### Register

**DEV_CONFIG (9h)**, bit [5] AUTO_LPM_ENTRY

**PDF Reference**: p.76

- 0: AUTO_LPM entry disabled
- 1: AUTO_LPM entry enabled

#### Entry Conditions

**PDF Reference**: p.39-40, Section 8.3.9.2 AUTO_LPM State

Must satisfy ALL of the following:
1. `AUTO_LPM_ENTRY = 1` (this step)
2. Watchdog disabled OR watchdog timeout satisfied
3. ISNS ADC diagnostics active, other ADC diagnostics inactive
4. `AUTO_LPM_EXIT_CHx = 0` for all channels
5. Load current below threshold: `IOUT < ILPM_ENTRY_AUTO`
6. Either:
   - All channels OFF (`CHx_ON = 0`), OR
   - Channels ON but current below threshold

#### Code Verification Checklist

- [ ] **Pre-condition validation**
  ```c
  // Expected validation before setting AUTO_LPM_ENTRY
  bool can_enter_auto_lpm(void) {
      if (watchdog_enabled && !watchdog_timeout_met) return false;
      if (non_isns_adc_active) return false;
      if (any_AUTO_LPM_EXIT_set) return false;
      if (load_current >= ILPM_ENTRY_AUTO) return false;
      return true;
  }
  ```

- [ ] **Register write**
  - [ ] Is DEV_CONFIG read-modify-write used (preserve other bits)?
  - [ ] Is write operation verified?

- [ ] **Sequencing**
  - [ ] Is step 12 executed only after step 11 (prepare for sleep)?
  - [ ] Is vehicle ACC OFF condition checked?

- [ ] **Step 18 (de-activation) also implemented**
  - [ ] Is AUTO_LPM_ENTRY cleared back to 0 when exiting LPM mode?

**Search Keywords in Code**:
```
AUTO_LPM_ENTRY, DEV_CONFIG, auto_lpm, sleep_mode, low_power
ILPM_ENTRY_AUTO, lpm_entry
```

---

### Step 14: AUTO_LPM Entry Confirmation

#### Purpose
Verify that device actually entered AUTO_LPM mode by reading LPM_STATUS.

#### Status Bits

**GLOBAL_FAULT_TYPE (4h)**

**PDF Reference**: p.71

| Bit | Field | Type | Description |
|-----|-------|------|-------------|
| 11 | LPM_STATUS | Real-time | Current LPM state (0: not in LPM, 1: in LPM) |
| 5 | LPM_STATUS_1 | Read-clear | LPM wakeup event flag |

**Difference**:
- `LPM_STATUS`: Real-time status, updates immediately
- `LPM_STATUS_1`: Sticky flag, cleared by reading GLOBAL_FAULT_TYPE

#### Verification Method

**PDF Reference**: p.40, Section 8.3.9.2

> "When conditions are met, SDO frame's LPM_STATUS will update."

**Method 1**: Poll GLOBAL_FAULT_TYPE register
```c
// Read full register
uint16_t fault_status = read_GLOBAL_FAULT_TYPE();
bool in_lpm = (fault_status & BIT_LPM_STATUS) != 0;
```

**Method 2**: Check SDO header (every transaction)

**PDF Reference**: p.27-28, p.34

Every SPI transaction returns GLOBAL_FAULT_TYPE[15:8] in SDO header.
Bit 11 (LPM_STATUS) can be read even during LPM state via daisy-chain.

#### Timeout Specification

**From Operation Process Table**:
> "If not changed to 1 within 5 seconds, output field name to console log (repeat every 5 seconds)"

#### Code Verification Checklist

- [ ] **Polling implementation**
  ```c
  // Expected pattern
  set_AUTO_LPM_ENTRY(1);  // Step 12

  uint32_t start_time = get_tick();
  uint32_t timeout = 5000;  // 5 seconds
  bool entered = false;

  while (get_tick() - start_time < timeout) {
      uint16_t status = read_GLOBAL_FAULT_TYPE();
      if (status & LPM_STATUS) {
          entered = true;
          break;
      }
      delay_ms(poll_interval);
  }

  if (!entered) {
      log_error("LPM_STATUS not set within 5 seconds");
      // Repeat logging every 5 seconds if still not entered
  } else {
      state = STATE_AUTO_LPM;  // Update enum
  }
  ```

- [ ] **SDO header extraction**
  - [ ] Does code parse SDO header from every SPI response?
  - [ ] Is LPM_STATUS bit extracted from header?
  - [ ] Can LPM_STATUS be checked without explicit register read?

- [ ] **Timeout and logging**
  - [ ] Is 5-second timeout implemented?
  - [ ] Is "LPM_STATUS" logged if timeout occurs?
  - [ ] Is logging repeated every 5 seconds while not entered?

- [ ] **State update**
  - [ ] Is enum state updated to AUTO_LPM only after LPM_STATUS=1 confirmed?
  - [ ] Is this the verification for step 13?

**Search Keywords in Code**:
```
LPM_STATUS, lpm_status, confirm, verify, timeout, 5000, poll
SDO, header, GLOBAL_FAULT_TYPE
```

---

### Step 17: AUTO_LPM Forced Exit + Channel Activation

#### Purpose
Force device to exit AUTO_LPM and activate channels via AUTO_LPM_EXIT_CHx bits.

#### Register

**LPM (3h)**: Low Power Mode settings register

**PDF Reference**: p.70

| Bit | Field | Description |
|-----|-------|-------------|
| 2 | AUTO_LPM_EXIT_CH2 | 0: Normal, 1: Exit AUTO_LPM and activate CH2 |
| 1 | AUTO_LPM_EXIT_CH1 | 0: Normal, 1: Exit AUTO_LPM and activate CH1 |

**Critical Rule**:
> "Write 1 to exit AUTO_LPM and activate channel. Must write 0 again to allow re-entry (step 19)."

#### Exit Behavior

**PDF Reference**: p.39-41, Section 8.3.9.2, "System Wakeup from AUTO_LPM"

When `AUTO_LPM_EXIT_CHx = 1`:
1. Device exits AUTO_LPM state
2. Enters ACTIVE state
3. Channel is automatically activated (CHx_ON becomes effective)

#### Write Access Restriction

**PDF Reference**: p.34, Section 8.3.9 + p.22 state diagram

> "In AUTO_LPM state, only LPM register is writable. Other registers are read-only."

**Implication**: Step 17 must ONLY write to LPM register, not SW_STATE or other registers.

#### Code Verification Checklist

- [ ] **LPM register write**
  ```c
  // Expected pattern
  void exit_auto_lpm_and_activate(uint8_t channel) {
      uint8_t lpm_reg = 0;

      if (channel == 1) {
          lpm_reg |= BIT_AUTO_LPM_EXIT_CH1;
      } else if (channel == 2) {
          lpm_reg |= BIT_AUTO_LPM_EXIT_CH2;
      }

      write_LPM_register(lpm_reg);

      // Verify exit (state should change to ACTIVE)
      // Channel should be ON automatically
  }
  ```

- [ ] **Write restriction compliance**
  - [ ] Does code write ONLY to LPM register (offset 3h) in AUTO_LPM state?
  - [ ] Is there protection against writing other registers in LPM?
  - [ ] Error handling if write to other register attempted?

- [ ] **Exit verification**
  - [ ] Is state transition to ACTIVE verified?
  - [ ] Is channel activation verified (read SW_STATE_STAT_CHx)?

- [ ] **Relationship with step 16**
  - [ ] Step 16 is automatic (B+ or AppSW input triggers exit)
  - [ ] Step 17 is forced exit via SPI
  - [ ] Are both paths handled correctly?

- [ ] **Step 19 preparation**
  - [ ] After exit and channel operation, is step 19 (clear to 0) executed?

**Search Keywords in Code**:
```
AUTO_LPM_EXIT, LPM register, exit, wake, force_exit
offset 3h, 0x03
```

---

### Step 18: AUTO_LPM Entry De-activation

#### Purpose
Clear AUTO_LPM_ENTRY bit to prevent automatic re-entry into LPM mode.

#### Register

**DEV_CONFIG (9h)**, bit [5] AUTO_LPM_ENTRY

Set to 0 to disable AUTO_LPM entry.

#### Code Verification Checklist

- [ ] **Register write**
  - [ ] Is DEV_CONFIG read-modify-write used?
  - [ ] Is AUTO_LPM_ENTRY bit specifically cleared to 0?
  - [ ] Are other bits in DEV_CONFIG preserved?

- [ ] **Sequencing**
  - [ ] Is step 18 executed after step 17 (exit AUTO_LPM)?
  - [ ] Is this part of returning to normal ACTIVE operation?

- [ ] **Symmetry with step 12**
  - [ ] Step 12: Set AUTO_LPM_ENTRY = 1 (enable)
  - [ ] Step 18: Set AUTO_LPM_ENTRY = 0 (disable)
  - [ ] Are both directions implemented?

**Search Keywords in Code**:
```
AUTO_LPM_ENTRY, DEV_CONFIG, disable, clear, deactivate
```

---

### Step 19: AUTO_LPM_EXIT_CHx Clear

#### Purpose
Clear AUTO_LPM_EXIT_CHx bits back to 0 to allow future AUTO_LPM re-entry.

#### Register

**LPM (3h)**, bits [2:1] AUTO_LPM_EXIT_CH2/CH1

Set to 0 to complete exit sequence.

#### Critical Rule

**PDF Reference**: p.70, LPM register description

> "AUTO_LPM_EXIT_CHx must be written back to 0 after exit to allow device to re-enter AUTO_LPM in the future."

Without clearing these bits, AUTO_LPM entry condition (p.40) cannot be satisfied:
> "AUTO_LPM_EXIT_CHx = 0 for all channels"

#### Code Verification Checklist

- [ ] **Register write**
  ```c
  // Expected pattern
  void clear_auto_lpm_exit_flags(void) {
      write_LPM_register(0x00);  // Clear all AUTO_LPM_EXIT bits
  }
  ```

- [ ] **Sequencing**
  - [ ] Is step 19 executed after step 17 (exit) and step 18 (disable entry)?
  - [ ] Is there proper delay between step 17 and 19?

- [ ] **Completion**
  - [ ] After step 19, does operation loop back to step 10 (periodic fault read)?
  - [ ] Is device ready for next LPM cycle?

**Search Keywords in Code**:
```
AUTO_LPM_EXIT, LPM register, clear, reset, 0x00
```

---

## Verification Summary Table

| Step | Type | Registers Involved | PDF Key Pages | Verification Priority |
|------|------|-------------------|---------------|---------------------|
| 4 | B | CSN pin timing | 31-32, 13, 22 | **CRITICAL** - Wake-up foundation |
| 5 | C | All config registers | 68-106 | **CRITICAL** - IC configuration |
| 6 | C | CH_CONFIG, FLT_STAT | 59-61, 81-82, 94-95 | **HIGH** - Diagnostics |
| 8, 9 | C | SW_STATE | 24, 75 | **HIGH** - Output control |
| 10 | C | GLOBAL_FAULT_TYPE, FLT_STAT | 28-30, 71-73, 81-82, 94-95 | **CRITICAL** - Watchdog + fault monitoring |
| 12 | C | DEV_CONFIG | 39-40, 76 | **MEDIUM** - LPM entry enable |
| 14 | C | GLOBAL_FAULT_TYPE | 40, 71, 27-28 | **MEDIUM** - LPM entry verification |
| 17 | C | LPM | 39-41, 70 | **MEDIUM** - LPM exit |
| 18 | C | DEV_CONFIG | 76 | **MEDIUM** - LPM entry disable |
| 19 | C | LPM | 70 | **MEDIUM** - LPM exit completion |

---

## Verification Execution Plan

### Phase 1: Code Structure Review
1. Identify all SPI communication functions
2. Map operation process steps to code functions
3. Check state machine enum implementation
4. Verify register offset definitions match datasheet

### Phase 2: B Stage Verification (Step 4)
1. Verify CSN pin control implementation
2. Check tREADY timing compliance
3. Validate post-wakeup POR handling
4. Test wake-up sequence

### Phase 3: C Stage Verification (Steps 5, 6, 8, 9, 10)
1. Verify Signal DB parameter mapping (Step 5)
2. Verify diagnostic sequence (Step 6)
3. Verify channel control (Steps 8, 9)
4. Verify periodic fault read + watchdog (Step 10)

### Phase 4: LPM Verification (Steps 12, 14, 17, 18, 19)
1. Verify AUTO_LPM entry sequence (12 → 13 → 14)
2. Verify AUTO_LPM exit sequence (17 → 18 → 19)
3. Verify write access restrictions in LPM state
4. Test complete LPM cycle

### Phase 5: Integration Test
1. Execute complete operation process (1-19)
2. Verify state transitions
3. Verify fault handling
4. Verify console logging

---

## Code Search Strategy

### Step-by-Step Approach

1. **Find SPI layer**
   ```
   Search: "SPI", "MOSI", "MISO", "frame", "transmit", "receive"
   ```

2. **Find register definitions**
   ```
   Search: "DEV_CONFIG", "0x09", "GLOBAL_FAULT_TYPE", "0x04"
   ```

3. **Find operation process steps**
   ```
   Search: "CONFIG", "ACTIVE", "AUTO_LPM", "wake", "sleep"
   ```

4. **Find fault handling**
   ```
   Search: "fault", "FLT", "WD_ERR", "POR", "UVLO"
   ```

5. **Find diagnostics**
   ```
   Search: "OL_OFF", "SHRT_VBB", "open", "short", "diagnostic"
   ```

---

## Expected Code Patterns

### Pattern 1: Register Write with Verification
```c
bool write_register_verified(uint8_t addr, uint16_t data) {
    write_register(addr, data);
    uint16_t readback = read_register(addr);
    return (readback == data);
}
```

### Pattern 2: Bit Manipulation (Read-Modify-Write)
```c
void set_bit_in_register(uint8_t addr, uint8_t bit) {
    uint16_t reg = read_register(addr);
    reg |= (1 << bit);
    write_register(addr, reg);
}
```

### Pattern 3: Fault Detection (Edge Detection)
```c
static uint16_t prev_fault = 0;

void check_faults(void) {
    uint16_t curr_fault = read_GLOBAL_FAULT_TYPE();
    uint16_t new_faults = (curr_fault ^ prev_fault) & curr_fault;

    if (new_faults) {
        handle_new_faults(new_faults);
    }

    prev_fault = curr_fault;
}
```

### Pattern 4: State Machine
```c
typedef enum {
    STATE_OFF,
    STATE_SLEEP,
    STATE_INIT,
    STATE_CONFIG,
    STATE_ACTIVE,
    STATE_AUTO_LPM,
    // ...
} IC_State_t;

static IC_State_t current_state = STATE_OFF;

void state_machine(void) {
    switch (current_state) {
        case STATE_SLEEP:
            // Handle SLEEP state
            break;
        case STATE_CONFIG:
            // Handle CONFIG state
            break;
        // ...
    }
}
```

---

## Deliverables

After verification, create:

1. **Verification Report**
   - Checklist completion status (✅/❌) for each item
   - Issues found with severity (Critical/High/Medium/Low)
   - Code line numbers for each verified item

2. **Gap Analysis**
   - Missing implementations
   - Incorrect implementations
   - Deviations from datasheet

3. **Recommendations**
   - Required fixes (Critical/High priority)
   - Suggested improvements (Medium/Low priority)
   - Test cases to add

---

## Notes

- All page numbers refer to tps2hcs08-q1.pdf datasheet
- Register offsets are in hexadecimal
- Bit positions are 0-indexed (LSB = bit 0)
- "Console log" means MCU UART/debug output, not IC internal logging
- Timing values (65µs, 5s, etc.) must be strictly enforced
- State machine transitions must match Figure 8-4 (p.22) exactly

---
