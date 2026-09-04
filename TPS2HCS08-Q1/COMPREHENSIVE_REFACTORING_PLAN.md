# TPS2HCS08-Q1 드라이버 종합 개선 작업 계획

**작성일**: 2026-09-04
**기준 문서**:
- `VERIFICATION_REPORT.md` (2026-08-31) - 9 Critical Issues, 16 Warnings
- `TPS2HCS08_검증체크리스트_및_코드수정요청.md` - 18개 수정 요청 (M-01 ~ M-18)
- `NEW_IC_GETPORT_SETPORT_COMPATIBILITY_GUIDE.md` - GetPort/SetPort 호환성 설계 가이드

**목표**: TPS2HCS08-Q1 드라이버의 데이터시트 준수성, 코드 안정성, 상위 Application 호환성 확보

**예상 기간**: 5-6주

---

## Executive Summary

### 작업 범위

| 영역 | 항목 수 | 우선순위 | 예상 기간 |
|------|---------|----------|-----------|
| 데이터시트 준수 수정 (M-01~M-18) | 18개 | Critical | 2주 |
| VERIFICATION_REPORT Critical Issues | 9개 | Critical | 1주 |
| GetPort/SetPort 호환성 구현 | 1개 시스템 | High | 2주 |
| 통합 검증 및 테스트 | 전체 | Mandatory | 1-2주 |

### 성공 기준

- [ ] 데이터시트 기반 검증 체크리스트 100% 완료
- [ ] VERIFICATION_REPORT 40/40 checks PASS
- [ ] GetPort/SetPort API 기존 Application과 100% 호환
- [ ] Unit test 90% coverage
- [ ] HiL test 전 시나리오 PASS

---

## Phase 1: 데이터시트 준수 수정 (Week 1-2)

### 우선순위 분류

#### Priority S (Safety Critical - 즉시 수정 필요)

**M-01: Reset값 불일치 수정**
- **문제**: LPM, PWM_CHx 초기값이 데이터시트 Reset값과 다름
- **영향**: 디바이스 동작 불일치, undefined behavior 가능
- **수정**:
  ```c
  // ExVioDb_InitRegValue_Tps2hcs08()

  // BEFORE
  pCtx->lpm.word = 0x0000u;           // ❌ Wrong
  pCtx->pwmCh[0].word = 0x0000u;      // ❌ Wrong
  pCtx->pwmCh[1].word = 0x0000u;      // ❌ Wrong

  // AFTER
  pCtx->lpm.word = 0xFF80u;           // ✅ Datasheet p.70
  pCtx->pwmCh[0].word = 0xF000u;      // ✅ Datasheet p.83
  pCtx->pwmCh[1].word = 0xF000u;      // ✅ Datasheet p.96
  ```
- **검증**: Reset값 전수 대조 (데이터시트 Table 8-13, p.65)
- **Effort**: 1 hour
- **근거**: 검증체크리스트 1절, 데이터시트 p.70/p.83/p.96

---

**M-02: WD_TO 설정값 검증**
- **문제**: WD_TO 매크로가 400µs(00b)와 400ms(01b) 혼동 가능
- **영향**: Watchdog timeout 설정 오류 → 시스템 리셋
- **수정**:
  ```c
  // ExVioDb_Tps2hcs08.h

  /* DEV_CONFIG.WD_TO - CRITICAL: 단위 주의! */
  #define TPS2HCS08_WD_TO_400US             (0x0u)  /* 400 microseconds (NOT milliseconds!) */
  #define TPS2HCS08_WD_TO_400MS             (0x1u)  /* 400 milliseconds (project default) */
  #define TPS2HCS08_WD_TO_800MS             (0x2u)
  #define TPS2HCS08_WD_TO_1200MS            (0x3u)

  // 프로젝트 기본값 확인
  STATIC_ASSERT(TPS2HCS08_WD_TO_DEFAULT == TPS2HCS08_WD_TO_400MS,
                "WD_TO must be 400ms (01b), not 400us (00b)");
  ```
- **검증**:
  - 현재 사용 중인 WD_TO 값이 `0x1u` (400ms)인지 확인
  - Watchdog 주기 READ 간격 < 400ms 확인
- **Effort**: 30 minutes
- **근거**: 검증체크리스트 1절, 데이터시트 Table 8-4 (p.28)

---

**M-04: I2T 초기 설정 안전성 검토**
- **문제**: `I2T_TRIP=0h` + `I2T_EN=1` 조합이 최솟값 8.8A²s로 동작
  - DB 덮어쓰기 전 채널 ON 시 과전류 트립 가능
- **영향**: 초기화 중 의도하지 않은 출력 차단
- **수정 Option 1** (권장):
  ```c
  // ExVioDb_InitRegValue_Tps2hcs08()

  // 초기에는 I2T 비활성
  pCtx->ilimCfgCh[chIdx].bits.I2T_EN_CHx = 0u;  // Disable initially

  // DB 파싱 후 활성화
  // In ExVioDb_ParsingOutputTps2hcs08Reg()
  if (/* DB has valid I2T_TRIP value */)
  {
      pCtx->ilimCfgCh[chIdx].bits.I2T_EN_CHx = 1u;
  }
  ```
- **수정 Option 2**:
  ```c
  // Setup 시퀀스에서 보장
  // CONFIG_WRITE 단계에서 I2T 설정 완료 후
  // ACTIVE_ENTRY 단계에서만 SW_STATE(CHx_ON) 허용
  ```
- **검증**:
  - 초기화 중 출력 ON 불가 확인
  - DB 파싱 후 I2T 설정이 정상 적용되는지 확인
- **Effort**: 2 hours
- **근거**: 검증체크리스트 1절, 데이터시트 p.93

---

**M-05: 초기 RunState 수정**
- **문제**: `RunState = RUN_ACTIVE` 초기화는 실제 칩 상태(SLEEP)와 불일치
- **영향**: 상태머신 불일치로 인한 오동작
- **수정**:
  ```c
  // ExVioDb_InitRegValue_Tps2hcs08()

  // BEFORE
  exVioDbTps2hcs08RunState = TPS2HCS08_RUN_ACTIVE;  // ❌ 칩은 SLEEP 상태

  // AFTER
  exVioDbTps2hcs08RunState = TPS2HCS08_RUN_INIT;    // ✅ 초기 상태

  // TPS2HCS08_RUN_INIT 상태 추가 필요
  typedef enum
  {
      TPS2HCS08_RUN_INIT = 0,          // NEW: Initial state after reset
      TPS2HCS08_RUN_ACTIVE,            // Normal operation
      TPS2HCS08_RUN_LPM_PREPARE,
      // ... existing states
  } tTps2hcs08RunState;

  // RUN_INIT 상태 핸들러
  case TPS2HCS08_RUN_INIT:
      // Wait for setup complete
      if (exVioDbTps2hcs08SetupScnState == TPS2HCS08_SETUP_SCN_COMPLETE)
      {
          // Verify DEV_ID and POR cleared
          if (ExVioDb_VerifyDeviceReady_Tps2hcs08() == TRUE)
          {
              exVioDbTps2hcs08RunState = TPS2HCS08_RUN_ACTIVE;
          }
      }
      break;
  ```
- **검증**: 웨이크업 후 DEV_ID/POR 확인 완료 시에만 ACTIVE 전이
- **Effort**: 3 hours
- **근거**: 검증체크리스트 5절, 데이터시트 p.22/p.31

---

**M-16: DEV_ID 버전 검증 추가**
- **문제**: DEV_ID 검증이 없어 B버전(FFF1h) 칩 사용 시 동작 불일치
- **영향**: A버전용 CHx_ON 제어가 B버전에서 무시될 수 있음
- **수정**:
  ```c
  // ExVioDb_WaitReadyDone_Tps2hcs08()

  // BEFORE
  if ((devId != TPS2HCS08_DEV_ID_VER_A) && (devId != TPS2HCS08_DEV_ID_VER_B))
  {
      // Invalid DEV_ID
  }

  // AFTER
  #define TPS2HCS08_TARGET_VERSION  TPS2HCS08_DEV_ID_VER_A  // Project target

  if (devId == TPS2HCS08_TARGET_VERSION)
  {
      // Correct version
      pCtx->devPresent = TRUE;
      retVal = TPS2HCS08_COMPLETE;
  }
  else if ((devId == TPS2HCS08_DEV_ID_VER_A) || (devId == TPS2HCS08_DEV_ID_VER_B))
  {
      // Wrong version (but valid TPS2HCS08 chip)
      TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
          "[TPS2HCS08] dev=%d WRONG VERSION: read=0x%04X, expected=0x%04X (Ver %c)\r\n",
          devIdx, devId, TPS2HCS08_TARGET_VERSION,
          (TPS2HCS08_TARGET_VERSION == TPS2HCS08_DEV_ID_VER_A) ? 'A' : 'B');
      exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
      retVal = TPS2HCS08_BUSY;
  }
  else
  {
      // Not a TPS2HCS08 chip at all
      TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
          "[TPS2HCS08] dev=%d UNKNOWN DEVICE: ID=0x%04X\r\n", devIdx, devId);
      exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
      retVal = TPS2HCS08_BUSY;
  }
  ```
- **검증**: A/B 버전 칩으로 각각 테스트
- **Effort**: 1 hour
- **근거**: 검증체크리스트 5절, 데이터시트 p.67/p.24

---

#### Priority A (Critical - Week 1 완료)

**M-03: ADC_VBB 측정 활성화**
- **문제**: VBB 측정이 비활성(ADC_VBB_DIS=1)인데 프로세스 #21에서 사용 시도
- **수정**:
  ```c
  // ExVioDb_InitRegValue_Tps2hcs08()

  // VBB 측정 사용 여부 확인
  #if defined(TPS2HCS08_USE_VBB_MEASUREMENT)
      pCtx->adcConfig.bits.ADC_VBB_DIS = 0u;  // Enable VBB measurement
  #else
      pCtx->adcConfig.bits.ADC_VBB_DIS = 1u;  // Disable (default)
  #endif
  ```
- **검증**: VBB 측정 필요 시 프로젝트 설정 확인
- **Effort**: 30 minutes
- **근거**: 검증체크리스트 1절, 데이터시트 p.78

---

**M-06: Ctx 구조체 필드 추가**
- **문제**: `adcVbb`, `crcConfig`, `sdoHeader` 필드 누락
- **수정**:
  ```c
  // ExVioDb_Tps2hcs08.h - tTps2hcs08Ctx

  typedef struct
  {
      /* --- shadow register (last written value) ---------------------------- */
      tTps2hcs08DevId             devId;
      tTps2hcs08CrcConfig         crcConfig;        // NEW: CRC configuration shadow
      tTps2hcs08Sleep             sleep;
      tTps2hcs08Lpm               lpm;

      /* --- last read status ------------------------------------------------ */
      tTps2hcs08GlobalFaultType   globalFault;
      uint8                       sdoHeader;        // NEW: SDO header from last SPI
      tTps2hcs08AdcResultVbb      adcResultVbb;     // NEW: VBB measurement result

      // ... existing fields
  } tTps2hcs08Ctx;
  ```
- **검증**: 모든 register shadow 및 status 필드 존재 확인
- **Effort**: 1 hour
- **근거**: 검증체크리스트 2절, 데이터시트 p.68/p.80/p.27

---

**M-07: CHx_CONFIG 필드명 통일**
- **문제**: bit14 필드명이 CH1(VDS_SNS_DIS)과 CH2(VDSSNS_DIS)로 다름
- **수정**:
  ```c
  // ExVioDb_Tps2hcs08.h

  typedef union
  {
      uint16 word;
      struct
      {
          unsigned SLRT_CHx               : 2;    /* [1:0]    */
          unsigned LATCH_CHx              : 1;    /* [2]      */
          unsigned OL_SVBB_EN_CHx         : 2;    /* [4:3]    */
          unsigned OL_PU_STR_CHx          : 2;    /* [6:5]    */
          unsigned OL_SVBB_BLANK_CHx      : 2;    /* [8:7]    */
          unsigned OL_ON_EN_CHx           : 1;    /* [9]      */
          unsigned ISNS_SCALE_CHx         : 1;    /* [10]     */
          unsigned RESERVED               : 2;    /* [12:11]  */
          unsigned ISNS_DIS_CHx           : 1;    /* [13]     */
          unsigned VDS_SNS_DIS_CHx        : 1;    /* [14] NOTE: p.86=VDS_SNS_DIS_CH1, p.99=VDSSNS_DIS_CH2, same function */
          unsigned VSNS_DIS_CHx           : 1;    /* [15]     */
      } bits;
  } tTps2hcs08ChConfig;
  ```
- **검증**: 두 채널 공통 타입 사용 시 문제 없음
- **Effort**: 15 minutes
- **근거**: 검증체크리스트 2절, 데이터시트 p.86/p.99

---

**M-09: Register 주소 화이트리스트**
- **문제**: Reserved 주소(6h, 8h, Ch, 0x1F~0x7F)로 SPI 전송 가능
- **영향**: 잘못된 주소로 전송 시 조용히 무시됨
- **수정**:
  ```c
  // ExVioDb_Tps2hcs08.c

  /* Valid register address whitelist (28 registers) */
  static const uint8 tps2hcs08ValidAddresses[] =
  {
      TPS2HCS08_REG_DEV_ID,              // 0x00
      TPS2HCS08_REG_CRC_CONFIG,          // 0x01
      TPS2HCS08_REG_SLEEP,               // 0x02
      TPS2HCS08_REG_LPM,                 // 0x03
      TPS2HCS08_REG_GLOBAL_FAULT_TYPE,   // 0x04
      TPS2HCS08_REG_FAULT_MASK,          // 0x05
      // 0x06 - RESERVED (NOT valid)
      TPS2HCS08_REG_SW_STATE,            // 0x07
      // 0x08 - RESERVED (NOT valid)
      TPS2HCS08_REG_DEV_CONFIG,          // 0x09
      TPS2HCS08_REG_ADC_CONFIG,          // 0x0A
      TPS2HCS08_REG_ADC_RESULT_VBB,      // 0x0B
      // 0x0C - RESERVED (NOT valid)
      TPS2HCS08_REG_FLT_STAT_CH1,        // 0x0D
      TPS2HCS08_REG_PWM_CH1,             // 0x0E
      TPS2HCS08_REG_ILIM_CONFIG_CH1,     // 0x0F
      TPS2HCS08_REG_CH1_CONFIG,          // 0x10
      TPS2HCS08_REG_ADC_RESULT_CH1_I,    // 0x11
      TPS2HCS08_REG_ADC_RESULT_CH1_T,    // 0x12
      TPS2HCS08_REG_ADC_RESULT_CH1_V,    // 0x13
      TPS2HCS08_REG_ADC_RESULT_CH1_VDS,  // 0x14
      TPS2HCS08_REG_I2T_CONFIG_CH1,      // 0x15
      TPS2HCS08_REG_FLT_STAT_CH2,        // 0x16
      TPS2HCS08_REG_PWM_CH2,             // 0x17
      TPS2HCS08_REG_ILIM_CONFIG_CH2,     // 0x18
      TPS2HCS08_REG_CH2_CONFIG,          // 0x19
      TPS2HCS08_REG_ADC_RESULT_CH2_I,    // 0x1A
      TPS2HCS08_REG_ADC_RESULT_CH2_T,    // 0x1B
      TPS2HCS08_REG_ADC_RESULT_CH2_V,    // 0x1C
      TPS2HCS08_REG_ADC_RESULT_CH2_VDS,  // 0x1D
      TPS2HCS08_REG_I2T_CONFIG_CH2       // 0x1E
      // 0x1F~0x7F - RESERVED (NOT valid)
  };

  #define TPS2HCS08_VALID_ADDR_COUNT  (sizeof(tps2hcs08ValidAddresses) / sizeof(uint8))

  static boolean IsValidRegisterAddress(uint8 addr)
  {
      for (uint8 i = 0u; i < TPS2HCS08_VALID_ADDR_COUNT; i++)
      {
          if (tps2hcs08ValidAddresses[i] == addr)
          {
              return TRUE;
          }
      }
      return FALSE;
  }

  // In WriteRegister/ReadRegister
  D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(
      uint8 devIdx,
      uint8 addr,
      uint16 payload)
  {
      if (IsValidRegisterAddress(addr) == FALSE)
      {
          TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
              "[TPS2HCS08] INVALID REGISTER ADDRESS: dev=%d addr=0x%02X\r\n",
              devIdx, addr);
          return E_NOT_OK;
      }

      // ... existing implementation
  }
  ```
- **검증**: Reserved 주소로 전송 시도 시 오류 반환 확인
- **Effort**: 2 hours
- **근거**: 검증체크리스트 2절, 데이터시트 p.65

---

**M-11: ROM 상수 테이블 추가**
- **문제**: Reset값, 유효비트 마스크, Verify 가능 비트 마스크가 코드에 산재
- **수정**:
  ```c
  // ExVioDb_Tps2hcs08.h

  /* Register Reset Values (from datasheet Table 8-13) */
  #define TPS2HCS08_RST_CRC_CONFIG      (0x0000u)  // p.68
  #define TPS2HCS08_RST_SLEEP           (0x0000u)  // p.69
  #define TPS2HCS08_RST_LPM             (0xFF80u)  // p.70
  #define TPS2HCS08_RST_FAULT_MASK      (0xFF80u)  // p.74
  #define TPS2HCS08_RST_SW_STATE        (0xFFFCu)  // p.75
  #define TPS2HCS08_RST_DEV_CONFIG      (0xF800u)  // p.76
  #define TPS2HCS08_RST_ADC_CONFIG      (0xFF3Au)  // p.78
  #define TPS2HCS08_RST_PWM_CHx         (0xF000u)  // p.83/96
  #define TPS2HCS08_RST_ILIM_CONFIG_CHx (0x0088u)  // p.84/97
  #define TPS2HCS08_RST_CHx_CONFIG      (0xC002u)  // p.86/99
  #define TPS2HCS08_RST_I2T_CONFIG_CHx  (0x0000u)  // p.93/106

  /* Register Valid Bit Masks (R/W and RW fields only) */
  #define TPS2HCS08_MSK_FAULT_MASK      (0x007Fu)  // bits [6:0]
  #define TPS2HCS08_MSK_SW_STATE        (0x0003u)  // bits [1:0]
  #define TPS2HCS08_MSK_DEV_CONFIG      (0x07FFu)  // bits [10:0]
  #define TPS2HCS08_MSK_ADC_CONFIG      (0x00FFu)  // bits [7:0]
  // ... (for all writable registers)

  /* Register Verify Masks (excludes RC, W1C bits) */
  #define TPS2HCS08_VFY_GLOBAL_FAULT    (0x0000u)  // All RC/W1C, cannot verify
  #define TPS2HCS08_VFY_DEV_CONFIG      (0x07FFu)  // All R/W, can verify
  #define TPS2HCS08_VFY_ILIM_CONFIG_CHx (0x3FFFu)  // bits [13:0] R/W
  // ... (for all registers)

  // Verification function
  static Std_ReturnType VerifyRegister(uint8 devIdx, uint8 addr, uint16 shadow)
  {
      uint16 readValue;
      uint16 verifyMask;

      if (ExVioDb_ReadRegister_Tps2hcs08(devIdx, addr, &readValue) != E_OK)
      {
          return E_NOT_OK;
      }

      // Get verify mask for this register
      verifyMask = GetVerifyMask(addr);

      // Compare only verifiable bits
      if ((readValue & verifyMask) != (shadow & verifyMask))
      {
          TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
              "[TPS2HCS08] VERIFY FAIL: addr=0x%02X read=0x%04X expected=0x%04X mask=0x%04X\r\n",
              addr, readValue, shadow, verifyMask);
          return E_NOT_OK;
      }

      return E_OK;
  }
  ```
- **검증**: 모든 writable register의 3가지 상수 정의 확인
- **Effort**: 4 hours
- **근거**: 검증체크리스트 2절, 데이터시트 p.65~106, Table 8-14 (p.66)

---

**M-12: Read 프레임 데이터부 명시적 0 채움**
- **문제**: Read 프레임의 데이터 16bit가 명시적으로 0으로 설정되지 않음
- **수정**:
  ```c
  // ExVioDb_ReadRegister_Tps2hcs08()

  // Build READ frame
  txBuf[0] = (uint8)(TPS2HCS08_SPI_CMD_READ | (addr & TPS2HCS08_SPI_ADDR_MASK));
  txBuf[1] = 0x00u;  // Data MSB = 0 (explicitly set)
  txBuf[2] = 0x00u;  // Data LSB = 0 (explicitly set)
  ```
- **검증**: Read 프레임 TX 버퍼 확인
- **Effort**: 10 minutes
- **근거**: 검증체크리스트 3절, 데이터시트 p.27

---

**M-13: CRC-4-ITU 명칭 통일**
- **문제**: "CRC-8" 잔재 혼재
- **수정**:
  ```c
  // 모든 CRC 관련 주석 및 변수명 수정

  /*
   * CRC Configuration:
   * - Algorithm: CRC-4-ITU
   * - Polynomial: X^4 + X + 1 (0x3)
   * - Initial value: 0xF (1111b)
   * - Input: 24-bit frame [23:0]
   * - Output: 4-bit CRC in byte[3] bits [7:4], bits [3:0] = 0
   *
   * Reference: Datasheet Section 8.3.4.1 (p.26)
   */

  // Function naming
  static uint8 CalculateCrc4Itu(const uint8 *frame, uint8 length);
  static boolean VerifyCrc4Itu(const uint8 *frame, uint8 length);

  // Remove any "CRC-8" references
  ```
- **검증**: 전체 파일에서 "CRC-8" 검색 → 없어야 함
- **Effort**: 1 hour
- **근거**: 검증체크리스트 3절, 데이터시트 p.26

---

**M-14: Write 성공 시에만 Shadow 갱신**
- **문제**: SPI 실패 시에도 shadow가 갱신되어 재시도 불가
- **수정**:
  ```c
  // ExVioDb_WriteRegister_Tps2hcs08()

  D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(
      uint8 devIdx,
      uint8 addr,
      uint16 payload)
  {
      uint8 txBuf[TPS2HCS08_SPI_FRAME_LEN];
      uint8 rxBuf[TPS2HCS08_SPI_FRAME_LEN];
      Std_ReturnType retVal = E_NOT_OK;

      // Validate parameters
      if ((devIdx >= TPS2HCS08_DEV_MAX) ||
          (IsValidRegisterAddress(addr) == FALSE))
      {
          return E_NOT_OK;
      }

      // Build frame
      txBuf[0] = (uint8)(TPS2HCS08_SPI_CMD_WRITE | (addr & TPS2HCS08_SPI_ADDR_MASK));
      txBuf[1] = (uint8)((payload >> 8u) & 0x00FFu);
      txBuf[2] = (uint8)(payload & 0x00FFu);

      // Transmit
      if (ExVioDb_Tps2hcs08_Port_SpiTransfer(devIdx, txBuf, rxBuf,
                                             TPS2HCS08_SPI_FRAME_LEN) == TRUE)
      {
          // SPI success - now update shadow
          UpdateShadowRegister(devIdx, addr, payload);  // NEW: Shadow update here

          exVioDbTps2hcs08SdoHeader[devIdx] = rxBuf[0];
          retVal = E_OK;
      }
      else
      {
          // SPI failed - do NOT update shadow
          TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
              "[TPS2HCS08] SPI WRITE FAIL. dev=%d addr=0x%02X\r\n", devIdx, addr);
          // Shadow remains unchanged for retry
      }

      return retVal;
  }

  // Helper function
  static void UpdateShadowRegister(uint8 devIdx, uint8 addr, uint16 value)
  {
      tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

      switch (addr)
      {
          case TPS2HCS08_REG_FAULT_MASK:
              pCtx->faultMask.word = value;
              break;
          case TPS2HCS08_REG_SW_STATE:
              pCtx->swState.word = value;
              break;
          // ... (all writable registers)

          case TPS2HCS08_REG_SLEEP:
              // One-shot command - do NOT update shadow
              // (SLEEP register automatically clears after wake-up)
              break;

          default:
              // Unknown register - log warning
              TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
                  "[TPS2HCS08] Shadow update: unknown addr=0x%02X\r\n", addr);
              break;
      }
  }
  ```
- **검증**:
  - SPI 실패 후 shadow 불변 확인
  - SPI 성공 후 shadow 갱신 확인
- **Effort**: 3 hours
- **근거**: 검증체크리스트, VERIFICATION_REPORT

---

#### Priority B (Important - Week 2 완료)

**M-08: 비트필드 배치 정적 테스트**
- **목적**: 컴파일러 비트 오더 검증
- **구현**:
  ```c
  // test_TPS2HCS08_Bitfield.c

  void test_SW_STATE_bitfield(void)
  {
      tTps2hcs08SwState swState;

      // Test case 1: Both channels OFF
      swState.word = 0x0000u;
      ASSERT_EQ(swState.bits.CH1_ON, 0u);
      ASSERT_EQ(swState.bits.CH2_ON, 0u);

      // Test case 2: CH1 ON
      swState.word = 0x0001u;
      ASSERT_EQ(swState.bits.CH1_ON, 1u);
      ASSERT_EQ(swState.bits.CH2_ON, 0u);

      // Test case 3: CH2 ON
      swState.word = 0x0002u;
      ASSERT_EQ(swState.bits.CH1_ON, 0u);
      ASSERT_EQ(swState.bits.CH2_ON, 1u);

      // Test case 4: Both channels ON
      swState.word = 0x0003u;
      ASSERT_EQ(swState.bits.CH1_ON, 1u);
      ASSERT_EQ(swState.bits.CH2_ON, 1u);

      // Reverse test: set bits, check word
      swState.word = 0x0000u;
      swState.bits.CH1_ON = 1u;
      swState.bits.CH2_ON = 1u;
      ASSERT_EQ(swState.word, 0x0003u);
  }

  // Similar tests for all register types
  void test_DEV_CONFIG_bitfield(void);
  void test_PWM_CHx_bitfield(void);
  void test_ILIM_CONFIG_CHx_bitfield(void);
  // ...
  ```
- **검증**: 모든 register union 타입에 대해 테스트
- **Effort**: 4 hours
- **근거**: 검증체크리스트 2절

---

**M-10: Daisy Chain Slot 매핑**
- **문제**: 논리 devIdx와 물리 Wire slot 변환 규칙 미정의
- **수정**:
  ```c
  // ExVioDb_Tps2hcs08.c

  /*------------------------------------------------------------------------------
   *  Daisy Chain Slot Mapping
   *
   *  Assumption: First transmitted slot reaches the last device in chain
   *
   *  Physical connection (example):
   *    MCU SDI → [Device 3] → [Device 2] → [Device 1] → [Device 0] → MCU SDO
   *
   *  TX buffer order: [Slot 3][Slot 2][Slot 1][Slot 0]
   *  Logical devIdx:   0       1       2       3
   *
   *  Mapping formula: wireSlot = (TPS2HCS08_DEV_MAX - 1) - devIdx
   *
   *  IMPORTANT: Verify with actual hardware circuit diagram!
   *----------------------------------------------------------------------------*/

  static uint8 LogicalToWireSlot(uint8 devIdx)
  {
      if (devIdx >= TPS2HCS08_DEV_MAX)
      {
          return 0xFFu;  // Invalid
      }

      // Reverse order mapping (most common for daisy chain)
      return (uint8)((TPS2HCS08_DEV_MAX - 1u) - devIdx);
  }

  static uint8 WireSlotToLogical(uint8 slot)
  {
      if (slot >= TPS2HCS08_DEV_MAX)
      {
          return 0xFFu;  // Invalid
      }

      // Reverse mapping (same formula for symmetric mapping)
      return (uint8)((TPS2HCS08_DEV_MAX - 1u) - slot);
  }

  // Build chain TX buffer
  static void BuildChainTxBuffer(const tTps2hcs08Frame frames[TPS2HCS08_DEV_MAX],
                                 uint8 deviceCount,
                                 uint8 *txBuffer)
  {
      for (uint8 devIdx = 0u; devIdx < deviceCount; devIdx++)
      {
          uint8 wireSlot = LogicalToWireSlot(devIdx);
          uint8 bufferOffset = wireSlot * TPS2HCS08_SPI_FRAME_LEN;

          txBuffer[bufferOffset + 0u] = frames[devIdx].byte[0];
          txBuffer[bufferOffset + 1u] = frames[devIdx].byte[1];
          txBuffer[bufferOffset + 2u] = frames[devIdx].byte[2];
      }
  }

  // Parse chain RX buffer
  static void ParseChainRxBuffer(const uint8 *rxBuffer,
                                 uint8 deviceCount,
                                 tTps2hcs08Frame frames[TPS2HCS08_DEV_MAX])
  {
      for (uint8 devIdx = 0u; devIdx < deviceCount; devIdx++)
      {
          uint8 wireSlot = LogicalToWireSlot(devIdx);
          uint8 bufferOffset = wireSlot * TPS2HCS08_SPI_FRAME_LEN;

          frames[devIdx].byte[0] = rxBuffer[bufferOffset + 0u];
          frames[devIdx].byte[1] = rxBuffer[bufferOffset + 1u];
          frames[devIdx].byte[2] = rxBuffer[bufferOffset + 2u];
      }
  }
  ```
- **검증**:
  - **CRITICAL**: 회로도에서 실제 IC 연결 순서 확인 필수
  - DB IC 컬럼 값과 매핑 일치 확인
  - Hardware test로 검증
- **Effort**: 4 hours (회로도 확인 시간 포함)
- **근거**: 검증체크리스트 2절, 데이터시트 p.24~25

---

**M-15: DB_PARSING 중복 여부 확인**
- **문제**: SETUP_SCN_DB_PARSING 단계와 VHAL_FR DBLoad의 역할 중복 가능성
- **조사 항목**:
  1. `SETUP_SCN_DB_PARSING`이 수행하는 작업
  2. `VHAL_FR DBLoad` 시점 및 수행 작업
  3. 두 단계의 역할 차이
- **판단 기준**:
  - 중복: DB parsing을 한 곳으로 통합
  - 비중복: 각각의 역할 주석으로 명시
- **Effort**: 2 hours (조사 + 수정)
- **근거**: 검증체크리스트 5절

---

**M-17: AUTO_LPM 진입/복귀 시퀀스**
- **구현**:
  ```c
  // LPM_PREPARE 상태
  case TPS2HCS08_RUN_LPM_PREPARE:
      // Step 1: Disable watchdog
      pCtx->devConfig.bits.WD_EN = 0u;
      if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_CONFIG,
                                          pCtx->devConfig.word) == E_OK)
      {
          // Step 2: Disable ADC (except ISNS)
          pCtx->adcConfig.bits.ADC_VBB_DIS = 1u;
          pCtx->adcConfig.bits.ADC_TSNS_DIS = 1u;
          pCtx->adcConfig.bits.ADC_VSNS_DIS = 1u;
          pCtx->adcConfig.bits.ADC_VDS_DIS = 1u;
          // ISNS remains enabled

          if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                              pCtx->adcConfig.word) == E_OK)
          {
              // Step 3: Verify AUTO_LPM_EXIT_CHx = 0
              if ((pCtx->lpm.bits.AUTO_LPM_EXIT_CH1 == 0u) &&
                  (pCtx->lpm.bits.AUTO_LPM_EXIT_CH2 == 0u))
              {
                  // Ready for LPM entry
                  exVioDbTps2hcs08RunState = TPS2HCS08_RUN_LPM_ENTRY;
              }
              else
              {
                  // Clear EXIT flags first
                  pCtx->lpm.bits.AUTO_LPM_EXIT_CH1 = 0u;
                  pCtx->lpm.bits.AUTO_LPM_EXIT_CH2 = 0u;
                  (void)ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                                        pCtx->lpm.word);
              }
          }
      }
      break;

  // LPM_RESTORE 상태 (복귀)
  case TPS2HCS08_RUN_LPM_RESTORE:
      // Step 1: Clear AUTO_LPM_ENTRY
      pCtx->lpm.bits.MANUAL_LPM_ENTRY = 0u;
      if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_LPM,
                                          pCtx->lpm.word) == E_OK)
      {
          // Step 2: Re-enable watchdog
          pCtx->devConfig.bits.WD_EN = 1u;
          if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_DEV_CONFIG,
                                              pCtx->devConfig.word) == E_OK)
          {
              // Step 3: Restore ADC configuration
              // (restore to initial setup values)
              pCtx->adcConfig.word = TPS2HCS08_RST_ADC_CONFIG;
              if (ExVioDb_WriteRegister_Tps2hcs08(devIdx, TPS2HCS08_REG_ADC_CONFIG,
                                                  pCtx->adcConfig.word) == E_OK)
              {
                  // LPM exit complete - return to ACTIVE
                  exVioDbTps2hcs08RunState = TPS2HCS08_RUN_ACTIVE;
              }
          }
      }
      break;
  ```
- **검증**: LPM 진입/복귀 시퀀스 단계별 확인
- **Effort**: 4 hours
- **근거**: 검증체크리스트 5절, 데이터시트 p.39~40

---

**M-18: 폴트 READ 및 POR 감지**
- **구현**:
  ```c
  void ExVioDb_EvalGlobalFaultLog_Tps2hcs08(uint8 devIdx)
  {
      tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];
      uint16 currentFault = pCtx->globalFault.word;
      uint16 previousFault = pCtx->logLatchGlobal;

      // Detect changes (rising/falling edges)
      uint16 risingEdge = (previousFault ^ currentFault) & currentFault;
      uint16 fallingEdge = (previousFault ^ currentFault) & previousFault;

      // Log rising edges (new faults)
      if (risingEdge != 0u)
      {
          if ((risingEdge & (1u << TPS2HCS08_GF_BIT_VBB_UVLO)) != 0u)
          {
              TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                  "[TPS2HCS08] dev=%d FAULT: VBB_UVLO\r\n", devIdx);
          }
          // ... (check all fault bits)
      }

      // Log falling edges (fault cleared)
      if (fallingEdge != 0u)
      {
          TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(TAG_EEVP_EXVIODB,
              "[TPS2HCS08] dev=%d FAULT CLEARED: 0x%04X\r\n", devIdx, fallingEdge);
      }

      // Check POR bit (critical - triggers re-configuration)
      if (pCtx->globalFault.bits.POR == 1u)
      {
          if (pCtx->porCleared == FALSE)
          {
              // First POR detection after boot/re-config
              TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
                  "[TPS2HCS08] dev=%d POR DETECTED (expected after init)\r\n", devIdx);
              pCtx->porCleared = TRUE;
          }
          else
          {
              // Unexpected POR (device reset during operation)
              TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                  "[TPS2HCS08] dev=%d UNEXPECTED POR -> RE-CONFIGURATION REQUIRED\r\n", devIdx);
              exVioDbTps2hcs08ReCfgReq = TRUE;
          }
      }

      // Update logLatch for next comparison
      pCtx->logLatchGlobal = currentFault;
  }
  ```
- **검증**:
  - 최초/변경/유지/해제 4케이스 로그 출력 확인
  - POR 재발생 시 ReCfgReq 트리거 확인
- **Effort**: 2 hours
- **근거**: 검증체크리스트 5절, 데이터시트 p.71~73

---

## Phase 2: VERIFICATION_REPORT Critical Issues (Week 3)

이 단계는 VERIFICATION_REPORT의 9개 Critical Issues를 해결합니다.
**대부분 Phase 1에서 이미 해결됨** (M-01, M-05, M-09, M-14 등과 중복)

추가로 필요한 작업만 정리:

### Issue #2 & #7: Retry Counter System (이미 TODO 리스트에 있음)

**구현**:
```c
// ExVioDb_Tps2hcs08.h
typedef struct
{
    uint8 configWrite;
    uint8 configVerify;
    uint8 devIdRead;
    uint8 diagRead;
} tTps2hcs08RetryCounters;

#define TPS2HCS08_MAX_RETRY_CONFIG_WRITE   (5u)
#define TPS2HCS08_MAX_RETRY_CONFIG_VERIFY  (10u)
#define TPS2HCS08_MAX_RETRY_DEV_ID_READ    (5u)
#define TPS2HCS08_MAX_RETRY_DIAG_READ      (3u)

// ExVioDb_Tps2hcs08.c
D_STATIC tTps2hcs08RetryCounters exVioDbTps2hcs08Retry[TPS2HCS08_DEV_MAX];
```

**적용 예시 (CONFIG_VERIFY)**:
```c
case TPS2HCS08_SETUP_SCN_CONFIG_VERIFY:
    if (ExVioDb_VerifyConfig_Tps2hcs08() == TPS2HCS08_COMPLETE)
    {
        // Success
        for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
        {
            exVioDbTps2hcs08Retry[devIdx].configVerify = 0u;
        }
        exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_DIAG_PULLDOWN;
    }
    else
    {
        // Failure - increment retry counter
        boolean allFailed = TRUE;

        for (devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
        {
            if (exVioDbTps2hcs08Ctx[devIdx].devPresent == TRUE)
            {
                exVioDbTps2hcs08Retry[devIdx].configVerify++;

                if (exVioDbTps2hcs08Retry[devIdx].configVerify < TPS2HCS08_MAX_RETRY_CONFIG_VERIFY)
                {
                    allFailed = FALSE;
                }
            }
        }

        if (allFailed == TRUE)
        {
            // All devices exceeded retry limit
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] CONFIG_VERIFY failed after %d retries\r\n",
                TPS2HCS08_MAX_RETRY_CONFIG_VERIFY);
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
        }
        else
        {
            // Retry
            exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CONFIG_WRITE;
        }
    }
    break;
```

**Effort**: 1 day

---

### Issue #3: SDO Header Validation (별도 작업 필요)

**구현**:
```c
D_STATIC void ExVioDb_ValidateSdoHeader_Tps2hcs08(uint8 devIdx, uint8 sdoHeader)
{
    // SDO header = GLOBAL_FAULT_TYPE[15:8]
    // Check bits [4:0] for critical faults

    if ((sdoHeader & 0x01u) != 0u)  // VBB_UVLO
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d SDO HEADER: VBB_UVLO detected\r\n", devIdx);
    }
    if ((sdoHeader & 0x02u) != 0u)  // VBB_UV_WRN
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d SDO HEADER: VBB_UV_WRN detected\r\n", devIdx);
    }
    if ((sdoHeader & 0x04u) != 0u)  // VDD_UVLO
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d SDO HEADER: VDD_UVLO detected\r\n", devIdx);
    }
    if ((sdoHeader & 0x08u) != 0u)  // WD_ERR
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d SDO HEADER: WD_ERR detected\r\n", devIdx);
    }
    if ((sdoHeader & 0x10u) != 0u)  // SPI_ERR
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] dev=%d SDO HEADER: SPI_ERR detected\r\n", devIdx);
    }
}

// Call after every SPI transaction
D_STATIC Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(
    uint8 devIdx,
    uint8 addr,
    uint16 payload)
{
    // ... existing code ...

    if (ExVioDb_Tps2hcs08_Port_SpiTransfer(devIdx, txBuf, rxBuf,
                                           TPS2HCS08_SPI_FRAME_LEN) == TRUE)
    {
        exVioDbTps2hcs08SdoHeader[devIdx] = rxBuf[0];

        // NEW: Validate SDO header immediately
        ExVioDb_ValidateSdoHeader_Tps2hcs08(devIdx, rxBuf[0]);

        UpdateShadowRegister(devIdx, addr, payload);
        retVal = E_OK;
    }

    // ...
}
```

**Benefit**: 100ms watchdog 주기를 기다리지 않고 즉시 fault 감지

**Effort**: 4 hours

---

### Issue #5 & #6: Timeout + ERROR State

**추가 구현** (M-05에서 ERROR state는 이미 추가):
```c
// Add timeout to all blocking states
D_STATIC uint16 exVioDbTps2hcs08StateTimeout;

#define TPS2HCS08_TICK_STATE_TIMEOUT  TPS2HCS08_MS_TO_TICK(1000u)  // 1 second

case TPS2HCS08_SETUP_SCN_WAIT_READY:
    if (exVioDbTps2hcs08StateTimeout >= TPS2HCS08_TICK_STATE_TIMEOUT)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] WAIT_READY timeout\r\n");
        exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_ERROR;
        exVioDbTps2hcs08StateTimeout = 0u;
    }
    else if (ExVioDb_WaitReadyDone_Tps2hcs08() == TPS2HCS08_COMPLETE)
    {
        exVioDbTps2hcs08StateTimeout = 0u;
        exVioDbTps2hcs08SetupScnState = TPS2HCS08_SETUP_SCN_CLEAR_POR;
    }
    else
    {
        exVioDbTps2hcs08StateTimeout++;
    }
    break;
```

**States requiring timeout**:
- WAIT_READY
- CLEAR_POR
- CONFIG_WRITE
- CONFIG_VERIFY
- DIAG_* (already have their own timers)

**Effort**: 1 day

---

### Issue #8: CSN Low Timing

**Porting Layer 수정**:
```c
// ExVioDb_Tps2hcs08_Port.c (가정)

void ExVioDb_Tps2hcs08_Port_WakeupSequence(uint8 devIdx)
{
    // Set CSN low
    GPIO_WritePin(TPS2HCS08_CSN_PORT[devIdx], TPS2HCS08_CSN_PIN[devIdx], GPIO_PIN_RESET);

    // Wait tREADY minimum (65µs typ)
    // Use 70µs for margin
    Delay_Us(70);

    // Set CSN high
    GPIO_WritePin(TPS2HCS08_CSN_PORT[devIdx], TPS2HCS08_CSN_PIN[devIdx], GPIO_PIN_SET);
}
```

**검증**: Oscilloscope로 CSN low pulse 폭 측정 ≥ 65µs

**Effort**: 2 hours (+ hardware verification)

---

### Issue #9: Execution Time Monitoring

**구현**:
```c
// ExVioDb_Tps2hcs08.h
typedef struct
{
    uint32 lastExecTime_us;
    uint32 maxExecTime_us;
    uint32 avgExecTime_us;
    uint32 execCount;
} tTps2hcs08ExecStats;

// ExVioDb_Tps2hcs08.c
D_STATIC tTps2hcs08ExecStats exVioDbTps2hcs08ExecStats;

void ExVioDb_RunScnTps2hcs08Reg(void)
{
    uint32 startTime = GetMicroseconds();

    // ... existing run scan logic ...

    uint32 execTime = GetMicroseconds() - startTime;

    // Update statistics
    exVioDbTps2hcs08ExecStats.lastExecTime_us = execTime;
    exVioDbTps2hcs08ExecStats.execCount++;

    if (execTime > exVioDbTps2hcs08ExecStats.maxExecTime_us)
    {
        exVioDbTps2hcs08ExecStats.maxExecTime_us = execTime;
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] New max exec time: %d us\r\n", execTime);
    }

    // Moving average
    exVioDbTps2hcs08ExecStats.avgExecTime_us =
        (exVioDbTps2hcs08ExecStats.avgExecTime_us * 7u + execTime) / 8u;
}
```

**Effort**: 4 hours

---

## Phase 3: GetPort/SetPort 호환성 구현 (Week 4)

### 목표

기존 Application이 사용하는 `GetPort()`/`SetPort()` API를 변경 없이 TPS2HCS08-Q1 지원

### 작업 단계

#### Step 1: DB Category 분기 추가

```c
// SWC_EXVIODB.c - RE_CS_SWC_EXVIODB_SetPort()

Std_ReturnType RE_CS_SWC_EXVIODB_SetPort(
    uint16 sigid,
    uint16 value,
    uint16 mode)
{
    // ... existing code ...

    switch (exVioDbRec[sigIndex].CAT_1)
    {
        case DB_CAT1_LOW_OUT:
        case DB_CAT1_LOW_MOTOR:
            result = ExVioDb_SetPortOutputDrv8912Reg(sigIndex, value, mode);
            break;

        case DB_CAT1_HIGH_OUT:
        case DB_CAT1_HIGH_MOTOR:
            result = ExVioDb_SetPortOutputMpq6620Reg(sigIndex, value, mode);
            break;

        case DB_CAT1_E_FUSE:
            result = ExVioDb_SetPortOutputVnfd1248Reg(sigIndex, value, mode);
            break;

        case DB_CAT1_E_FUSE_TPS2HCS08:  // NEW: TPS2HCS08 category
            result = ExVioDb_SetPortOutputTps2hcs08Reg(sigIndex, value, mode);
            break;

        default:
            result = E_NOT_OK;
            break;
    }

    return result;
}

// Similar for RE_CS_SWC_EXVIODB_GetPort()
```

**DB 정의 필요**:
```c
// ExVioDb.h
#define DB_CAT1_E_FUSE_TPS2HCS08  (6u)  // New category for TPS2HCS08
```

**Effort**: 1 hour

---

#### Step 2: SetPort Adapter 구현

```c
static uint8 ExVioDb_SetPortOutputTps2hcs08Reg(
    uint16 sigIndex,
    uint16 value,
    uint16 mode)
{
    uint8 result = E_NOT_OK;
    uint8 devIdx;
    uint8 chIdx;
    tTps2hcs08Ctx *pCtx;

    // Validate sigIndex
    if (sigIndex >= exVioDbMemCnt)
    {
        return E_NOT_OK;
    }

    // Extract device and channel from DB
    devIdx = (uint8)exVioDbRec[sigIndex].IC;

    if (devIdx >= TPS2HCS08_DEV_MAX)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] SetPort: invalid IC=%d (max=%d)\r\n",
            devIdx, TPS2HCS08_DEV_MAX);
        return E_NOT_OK;
    }

    if (exVioDbRec[sigIndex].PIN == 0u)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] SetPort: invalid PIN=0\r\n");
        return E_NOT_OK;
    }

    // Convert 1-based PIN to 0-based channel
    chIdx = (uint8)(exVioDbRec[sigIndex].PIN - 1u);

    if (chIdx >= TPS2HCS08_CH_MAX)
    {
        TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
            "[TPS2HCS08] SetPort: invalid channel=%d (max=%d)\r\n",
            chIdx, TPS2HCS08_CH_MAX);
        return E_NOT_OK;
    }

    pCtx = &exVioDbTps2hcs08Ctx[devIdx];

    // Check device presence
    if (pCtx->devPresent == FALSE)
    {
        return E_NOT_OK;
    }

    // Dispatch by mode
    switch (mode)
    {
        case 0u:  // Output state
            result = ExVioDb_SetPortOutputTps2hcs08State(pCtx, sigIndex, value, chIdx);
            break;

        case 1u:  // PWM duty
            result = ExVioDb_SetPortOutputTps2hcs08Duty(pCtx, sigIndex, value, chIdx);
            break;

        case 2u:  // Event clear
            result = ExVioDb_ClearPortEventTps2hcs08(pCtx, devIdx, chIdx);
            break;

        case 3u:  // Fault clear
            result = ExVioDb_ClearPortFaultTps2hcs08(pCtx, devIdx, chIdx);
            break;

        case 4u:  // PWM frequency
            result = ExVioDb_SetPortOutputTps2hcs08Freq(pCtx, sigIndex, value, chIdx);
            break;

        default:
            result = E_NOT_OK;
            break;
    }

    // Apply changes to hardware (if modified)
    if (result == E_OK)
    {
        // Write modified registers to IC
        result = ExVioDb_DownloadTps2hcs08Reg(pCtx, devIdx);
    }

    return result;
}
```

**Effort**: 4 hours

---

#### Step 3: State 변환 함수 구현

```c
static uint8 ExVioDb_SetPortOutputTps2hcs08State(
    tTps2hcs08Ctx *pCtx,
    uint16 sigIndex,
    uint16 state,
    uint8 chIdx)
{
    uint8 result = E_OK;

    switch (state)
    {
        case DB_DEF_IDLE:
            // Turn OFF
            pCtx->swState.bits.CH1_ON = (chIdx == TPS2HCS08_CH1) ? 0u : pCtx->swState.bits.CH1_ON;
            pCtx->swState.bits.CH2_ON = (chIdx == TPS2HCS08_CH2) ? 0u : pCtx->swState.bits.CH2_ON;
            break;

        case DB_DEF_ACTIVE:
            // Turn ON (Active High/Low handled via CAT_2)
            if (exVioDbRec[sigIndex].CAT_2 == DB_CAT2_ACTIVE_HIGH)
            {
                // Active High: ON = 1
                if (chIdx == TPS2HCS08_CH1)
                    pCtx->swState.bits.CH1_ON = 1u;
                else
                    pCtx->swState.bits.CH2_ON = 1u;
            }
            else if (exVioDbRec[sigIndex].CAT_2 == DB_CAT2_ACTIVE_LOW)
            {
                // Active Low: Not applicable for TPS2HCS08 (always sources current)
                // Log warning
                TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
                    "[TPS2HCS08] Active Low not supported for high-side switch\r\n");
                result = E_NOT_OK;
            }
            else
            {
                result = E_NOT_OK;
            }
            break;

        case DB_DEF_REVERSE:
            // Not applicable for non-motor outputs
            TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB,
                "[TPS2HCS08] REVERSE not supported\r\n");
            result = E_NOT_OK;
            break;

        default:
            result = E_NOT_OK;
            break;
    }

    return result;
}
```

**Effort**: 3 hours

---

#### Step 4: Duty/Frequency 변환 함수

```c
static uint8 ExVioDb_SetPortOutputTps2hcs08Duty(
    tTps2hcs08Ctx *pCtx,
    uint16 sigIndex,
    uint16 duty,
    uint8 chIdx)
{
    uint16 regDuty;

    // DB duty: 0~255 (8-bit)
    // TPS2HCS08 PWM_DTY_CHx: 0~255 (8-bit, bits [8:1])
    // Direct 1:1 mapping!

    if (duty > 0xFFu)
    {
        return E_NOT_OK;
    }

    regDuty = (uint16)duty;  // Direct mapping

    // Update shadow
    pCtx->pwmCh[chIdx].bits.PWM_DTY_CHx = (uint8)regDuty;

    return E_OK;
}

static uint8 ExVioDb_SetPortOutputTps2hcs08Freq(
    tTps2hcs08Ctx *pCtx,
    uint16 sigIndex,
    uint16 freq,
    uint8 chIdx)
{
    typedef struct
    {
        uint16 dbFreq;
        uint8 regCode;
    } tFreqMap;

    static const tFreqMap freqTable[] =
    {
        { DB_PWM_0P8Hz,   0x0u },  // 0.8 Hz
        { DB_PWM_50Hz,    0x1u },  // 50 Hz
        { DB_PWM_100Hz,   0x2u },  // 100 Hz
        { DB_PWM_200Hz,   0x3u },  // 200 Hz
        { DB_PWM_500Hz,   0x4u },  // 500 Hz
        { DB_PWM_1000Hz,  0x5u },  // 1000 Hz
        { DB_PWM_1500Hz,  0x6u },  // 1500 Hz
        { DB_PWM_1770Hz,  0x7u }   // 1770 Hz
    };

    for (uint8 i = 0u; i < (sizeof(freqTable) / sizeof(tFreqMap)); i++)
    {
        if (freqTable[i].dbFreq == freq)
        {
            pCtx->pwmCh[chIdx].bits.PWM_FREQ_CHx = freqTable[i].regCode;
            return E_OK;
        }
    }

    // Not found in table
    TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(TAG_EEVP_EXVIODB,
        "[TPS2HCS08] Unsupported PWM frequency: %d\r\n", freq);
    return E_NOT_OK;
}
```

**Effort**: 2 hours

---

#### Step 5: GetPort Adapter 구현

```c
static uint8 ExVioDb_GetPortOutputTps2hcs08Reg(
    uint16 sigIndex,
    uint16 *value,
    uint16 mode)
{
    uint8 result = E_NOT_OK;
    uint8 devIdx;
    uint8 chIdx;
    const tTps2hcs08Ctx *pCtx;

    // Parameter validation
    if ((value == NULL_PTR) || (sigIndex >= exVioDbMemCnt))
    {
        return E_NOT_OK;
    }

    devIdx = (uint8)exVioDbRec[sigIndex].IC;
    if ((devIdx >= TPS2HCS08_DEV_MAX) || (exVioDbRec[sigIndex].PIN == 0u))
    {
        return E_NOT_OK;
    }

    chIdx = (uint8)(exVioDbRec[sigIndex].PIN - 1u);
    if (chIdx >= TPS2HCS08_CH_MAX)
    {
        return E_NOT_OK;
    }

    pCtx = &exVioDbTps2hcs08Ctx[devIdx];

    if (pCtx->devPresent == FALSE)
    {
        return E_NOT_OK;
    }

    // Dispatch by mode
    switch (mode)
    {
        case 0u:  // Output state
            result = ExVioDb_GetPortOutputTps2hcs08State(pCtx, sigIndex, chIdx, value);
            break;

        case 1u:  // PWM duty
            result = ExVioDb_GetPortOutputTps2hcs08Duty(pCtx, chIdx, value);
            break;

        case 2u:  // Event status
            result = ExVioDb_GetPortEventTps2hcs08(pCtx, devIdx, chIdx, value);
            break;

        case 3u:  // Fault status
            result = ExVioDb_GetPortFaultTps2hcs08(pCtx, devIdx, chIdx, value);
            break;

        case 4u:  // PWM frequency
            result = ExVioDb_GetPortOutputTps2hcs08Freq(pCtx, chIdx, value);
            break;

        case 5u:  // Current measurement
            result = ExVioDb_GetPortCurrentTps2hcs08(pCtx, devIdx, chIdx, value);
            break;

        default:
            result = E_NOT_OK;
            break;
    }

    return result;
}

static uint8 ExVioDb_GetPortOutputTps2hcs08State(
    const tTps2hcs08Ctx *pCtx,
    uint16 sigIndex,
    uint8 chIdx,
    uint16 *value)
{
    // Return shadow (last commanded state)
    if (chIdx == TPS2HCS08_CH1)
        *value = (uint16)pCtx->swState.bits.CH1_ON;
    else
        *value = (uint16)pCtx->swState.bits.CH2_ON;

    return E_OK;
}

static uint8 ExVioDb_GetPortCurrentTps2hcs08(
    const tTps2hcs08Ctx *pCtx,
    uint8 devIdx,
    uint8 chIdx,
    uint16 *value)
{
    // Return last ADC ISNS result
    *value = pCtx->adcIsns[chIdx];

    // TODO: Consider converting to physical units (mA) if needed

    return E_OK;
}
```

**Effort**: 3 hours

---

#### Step 6: DB Parsing 초기화

```c
static void ExVioDb_ParsingOutputTps2hcs08(void)
{
    for (uint16 sigIndex = 0u; sigIndex < exVioDbMemCnt; sigIndex++)
    {
        if (exVioDbRec[sigIndex].CAT_1 == DB_CAT1_E_FUSE_TPS2HCS08)
        {
            uint8 devIdx = (uint8)exVioDbRec[sigIndex].IC;
            uint8 chIdx = (uint8)(exVioDbRec[sigIndex].PIN - 1u);

            if ((devIdx < TPS2HCS08_DEV_MAX) && (chIdx < TPS2HCS08_CH_MAX))
            {
                tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[devIdx];

                // Apply DEF_Value
                (void)ExVioDb_SetPortOutputTps2hcs08State(
                    pCtx, sigIndex, exVioDbRec[sigIndex].DEF_Value, chIdx);

                // Apply PWM_Duty
                (void)ExVioDb_SetPortOutputTps2hcs08Duty(
                    pCtx, sigIndex, exVioDbRec[sigIndex].PWM_Duty, chIdx);

                // Apply PWM_F
                (void)ExVioDb_SetPortOutputTps2hcs08Freq(
                    pCtx, sigIndex, exVioDbRec[sigIndex].PWM_F, chIdx);

                // Apply OCP (→ ILIMIT_SET_CHx, I2T_TRIP_CHx)
                // Apply SR (→ SLRT_CHx)
                // Apply CT (→ INRUSH_DURATION_CHx, CAP_CHRG_CHx)
                // Apply OLD (→ OL_SVBB_EN_CHx)
                // Apply MOC (→ PARALLEL_12)
                // ... (map all DB parameters to registers)
            }
        }
    }

    // Download all parsed configurations to hardware
    for (uint8 devIdx = 0u; devIdx < TPS2HCS08_DEV_MAX; devIdx++)
    {
        if (exVioDbTps2hcs08Ctx[devIdx].devPresent == TRUE)
        {
            (void)ExVioDb_DownloadTps2hcs08Reg(&exVioDbTps2hcs08Ctx[devIdx], devIdx);
        }
    }
}
```

**Effort**: 1 day (DB parameter mapping 복잡도에 따라)

---

## Phase 4: 통합 검증 및 테스트 (Week 5-6)

### Unit Testing

#### 테스트 프레임워크 설정

```bash
# Install Unity test framework
git clone https://github.com/ThrowTheSwitch/Unity.git test/Unity

# Create test directory structure
mkdir -p test/TPS2HCS08
mkdir -p test/mocks
```

#### 테스트 케이스 구현

**test_TPS2HCS08_Init.c**:
```c
#include "unity.h"
#include "ExVioDb_Tps2hcs08.h"

void setUp(void) {
    ExVioDb_InitRegValue_Tps2hcs08();
}

void tearDown(void) {
}

void test_ResetValues_Match_Datasheet(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    // Test M-01 fixes
    TEST_ASSERT_EQUAL_HEX16(0xFF80, pCtx->lpm.word);
    TEST_ASSERT_EQUAL_HEX16(0xF000, pCtx->pwmCh[0].word);
    TEST_ASSERT_EQUAL_HEX16(0xF000, pCtx->pwmCh[1].word);

    // Test all reset values
    TEST_ASSERT_EQUAL_HEX16(0xFF80, pCtx->faultMask.word);
    TEST_ASSERT_EQUAL_HEX16(0xFFFC, pCtx->swState.word);
    TEST_ASSERT_EQUAL_HEX16(0xF800, pCtx->devConfig.word);
    TEST_ASSERT_EQUAL_HEX16(0xFF3A, pCtx->adcConfig.word);
}

void test_ReservedBits_Are_Zero(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    // After DB parsing, reserved bits should remain as reset value
    // (Not forced to 0 - they preserve reset value)
    // This test verifies reset values include correct reserved bits

    TEST_ASSERT_EQUAL_HEX16(0xFF80, pCtx->faultMask.word & 0xFF80);  // Reserved [15:7]
    TEST_ASSERT_EQUAL_HEX16(0xFFFC, pCtx->swState.word & 0xFFFC);    // Reserved [15:2]
    TEST_ASSERT_EQUAL_HEX16(0xF800, pCtx->devConfig.word & 0xF800);  // Reserved [15:11]
}

void test_WD_TO_Is_400ms(void)
{
    tTps2hcs08Ctx *pCtx = &exVioDbTps2hcs08Ctx[0];

    // M-02: Verify WD_TO is set to 400ms (01b), not 400us (00b)
    TEST_ASSERT_EQUAL_UINT8(TPS2HCS08_WD_TO_400MS, pCtx->devConfig.bits.WD_TO);
    TEST_ASSERT_EQUAL_UINT8(0x1u, pCtx->devConfig.bits.WD_TO);
}
```

**test_TPS2HCS08_RegisterAddress.c**:
```c
void test_ValidAddress_Accepted(void)
{
    // Test M-09: Valid addresses
    TEST_ASSERT_TRUE(IsValidRegisterAddress(TPS2HCS08_REG_DEV_ID));
    TEST_ASSERT_TRUE(IsValidRegisterAddress(TPS2HCS08_REG_SW_STATE));
    TEST_ASSERT_TRUE(IsValidRegisterAddress(TPS2HCS08_REG_FLT_STAT_CH1));
}

void test_ReservedAddress_Rejected(void)
{
    // Test M-09: Reserved addresses should be rejected
    TEST_ASSERT_FALSE(IsValidRegisterAddress(0x06));  // Reserved
    TEST_ASSERT_FALSE(IsValidRegisterAddress(0x08));  // Reserved
    TEST_ASSERT_FALSE(IsValidRegisterAddress(0x0C));  // Reserved
    TEST_ASSERT_FALSE(IsValidRegisterAddress(0x1F));  // Reserved
    TEST_ASSERT_FALSE(IsValidRegisterAddress(0x7F));  // Reserved
}
```

**test_TPS2HCS08_DaisyChain.c**:
```c
void test_LogicalToWireSlot_Mapping(void)
{
    // Test M-10: Daisy chain slot mapping
    // Assumption: reverse order mapping

    TEST_ASSERT_EQUAL_UINT8(3, LogicalToWireSlot(0));
    TEST_ASSERT_EQUAL_UINT8(2, LogicalToWireSlot(1));
    TEST_ASSERT_EQUAL_UINT8(1, LogicalToWireSlot(2));
    TEST_ASSERT_EQUAL_UINT8(0, LogicalToWireSlot(3));

    // Invalid index
    TEST_ASSERT_EQUAL_UINT8(0xFF, LogicalToWireSlot(4));
}

void test_ChainBuffer_Build(void)
{
    tTps2hcs08Frame frames[4];
    uint8 txBuffer[12];  // 4 devices × 3 bytes

    // Set distinct patterns for each device
    frames[0].byte[0] = 0xAA; frames[0].byte[1] = 0xBB; frames[0].byte[2] = 0xCC;
    frames[1].byte[0] = 0x11; frames[1].byte[1] = 0x22; frames[1].byte[2] = 0x33;
    frames[2].byte[0] = 0xDD; frames[2].byte[1] = 0xEE; frames[2].byte[2] = 0xFF;
    frames[3].byte[0] = 0x44; frames[3].byte[1] = 0x55; frames[3].byte[2] = 0x66;

    BuildChainTxBuffer(frames, 4, txBuffer);

    // Verify wire slot order (reverse)
    // Wire slot 3 (device 0) should be first in buffer
    TEST_ASSERT_EQUAL_HEX8(0xAA, txBuffer[9]);  // Slot 3, byte 0
    TEST_ASSERT_EQUAL_HEX8(0xBB, txBuffer[10]); // Slot 3, byte 1
    TEST_ASSERT_EQUAL_HEX8(0xCC, txBuffer[11]); // Slot 3, byte 2

    // Wire slot 0 (device 3) should be last in buffer
    TEST_ASSERT_EQUAL_HEX8(0x44, txBuffer[0]);  // Slot 0, byte 0
    TEST_ASSERT_EQUAL_HEX8(0x55, txBuffer[1]);  // Slot 0, byte 1
    TEST_ASSERT_EQUAL_HEX8(0x66, txBuffer[2]);  // Slot 0, byte 2
}
```

**test_TPS2HCS08_GetSetPort.c**:
```c
void test_SetPort_State_IDLE(void)
{
    // Setup DB record
    exVioDbRec[0].CAT_1 = DB_CAT1_E_FUSE_TPS2HCS08;
    exVioDbRec[0].IC = 0;
    exVioDbRec[0].PIN = 1;  // CH1
    exVioDbRec[0].CAT_2 = DB_CAT2_ACTIVE_HIGH;

    exVioDbTps2hcs08Ctx[0].devPresent = TRUE;

    // Set to IDLE (OFF)
    uint8 result = ExVioDb_SetPortOutputTps2hcs08Reg(0, DB_DEF_IDLE, 0);

    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
    TEST_ASSERT_EQUAL_UINT8(0, exVioDbTps2hcs08Ctx[0].swState.bits.CH1_ON);
}

void test_SetPort_State_ACTIVE(void)
{
    exVioDbRec[0].CAT_1 = DB_CAT1_E_FUSE_TPS2HCS08;
    exVioDbRec[0].IC = 0;
    exVioDbRec[0].PIN = 2;  // CH2
    exVioDbRec[0].CAT_2 = DB_CAT2_ACTIVE_HIGH;

    exVioDbTps2hcs08Ctx[0].devPresent = TRUE;

    // Set to ACTIVE (ON)
    uint8 result = ExVioDb_SetPortOutputTps2hcs08Reg(0, DB_DEF_ACTIVE, 0);

    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
    TEST_ASSERT_EQUAL_UINT8(1, exVioDbTps2hcs08Ctx[0].swState.bits.CH2_ON);
}

void test_SetPort_PWM_Duty(void)
{
    exVioDbRec[0].CAT_1 = DB_CAT1_E_FUSE_TPS2HCS08;
    exVioDbRec[0].IC = 0;
    exVioDbRec[0].PIN = 1;

    exVioDbTps2hcs08Ctx[0].devPresent = TRUE;

    // Set duty to 128 (50%)
    uint8 result = ExVioDb_SetPortOutputTps2hcs08Reg(0, 128, 1);  // mode=1: duty

    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
    TEST_ASSERT_EQUAL_UINT8(128, exVioDbTps2hcs08Ctx[0].pwmCh[0].bits.PWM_DTY_CHx);
}

void test_GetPort_Returns_Shadow(void)
{
    exVioDbRec[0].CAT_1 = DB_CAT1_E_FUSE_TPS2HCS08;
    exVioDbRec[0].IC = 0;
    exVioDbRec[0].PIN = 1;

    exVioDbTps2hcs08Ctx[0].devPresent = TRUE;
    exVioDbTps2hcs08Ctx[0].swState.bits.CH1_ON = 1;

    uint16 value = 0;
    uint8 result = ExVioDb_GetPortOutputTps2hcs08Reg(0, &value, 0);  // mode=0: state

    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
    TEST_ASSERT_EQUAL_UINT16(1, value);
}
```

**테스트 실행**:
```bash
# Compile and run tests
gcc -DTEST -I Include -I test/Unity/src \
    test/Unity/src/unity.c \
    test/TPS2HCS08/*.c \
    Src/ExVioDb_Tps2hcs08.c \
    -o test_runner && ./test_runner
```

**Coverage 목표**: 90% line coverage

**Effort**: 1 week

---

### Hardware-in-Loop Testing

#### 테스트 시나리오

**Scenario 1: Normal Operation**
```
1. Power on
2. Verify DEV_ID = FFF0h (A버전)
3. Verify setup completes (SETUP_SCN_COMPLETE)
4. Verify RUN_ACTIVE state reached
5. SetPort(CH1, ACTIVE) → Verify output voltage
6. SetPort(CH1, IDLE) → Verify output off
7. Measure watchdog READ interval < 400ms
8. Verify fault logging on overcurrent injection
```

**Scenario 2: Missing Device**
```
1. Power on with device 2 disconnected
2. Verify device 2 marked devPresent=FALSE
3. Verify devices 0,1,3 still functional
4. Verify SetPort to device 2 returns E_NOT_OK
```

**Scenario 3: POR Detection**
```
1. Normal operation in RUN_ACTIVE
2. Inject power glitch (trigger POR)
3. Verify POR detection in next watchdog READ
4. Verify ReCfgReq triggered
5. Verify re-configuration from CLEAR_POR
6. Verify outputs restored to safe state
```

**Scenario 4: Daisy Chain Verification**
```
1. Connect 4 devices in daisy chain
2. SetPort to each device with distinct duty values
3. Measure output of each physical device
4. Verify logical devIdx maps to correct physical device
5. Verify M-10 slot mapping is correct
```

**Scenario 5: Retry and Timeout**
```
1. Normal operation
2. Disconnect SPI temporarily during CONFIG_WRITE
3. Verify retry counter increments
4. Verify ERROR state after max retries
5. Reconnect SPI
6. Call ResetError API
7. Verify recovery successful
```

**Scenario 6: GetPort/SetPort Compatibility**
```
1. Use existing Application code (unchanged)
2. Call SetPort(sigid, value, mode) for TPS2HCS08 signals
3. Verify correct device/channel addressed
4. Call GetPort(sigid, &value, mode)
5. Verify returned value matches shadow
```

**테스트 장비**:
- Logic analyzer (SPI timing 검증)
- Oscilloscope (CSN low pulse 65µs 검증)
- Power supply with glitch injection
- Electronic load (overcurrent 테스트)
- 4× TPS2HCS08-Q1 chips in daisy chain configuration

**Effort**: 1 week

---

## 작업 요약 및 우선순위 매트릭스

| ID | 작업 | 우선순위 | Week | Effort | 완료 조건 |
|----|------|----------|------|--------|-----------|
| M-01 | Reset값 수정 | S | 1 | 1h | LPM/PWM = 데이터시트값 |
| M-02 | WD_TO 검증 | S | 1 | 30m | WD_TO = 400ms 확인 |
| M-04 | I2T 안전성 | S | 1 | 2h | 초기 I2T_EN=0 |
| M-05 | RunState 수정 | S | 1 | 3h | RUN_INIT 상태 추가 |
| M-16 | DEV_ID 검증 | S | 1 | 1h | A/B 버전 구분 |
| M-03 | ADC_VBB 설정 | A | 1 | 30m | VBB 측정 정책 확정 |
| M-06 | Ctx 필드 추가 | A | 1 | 1h | adcVbb/crcConfig 추가 |
| M-07 | 필드명 통일 | A | 1 | 15m | VDS_SNS_DIS 주석 |
| M-09 | 주소 화이트리스트 | A | 1 | 2h | 28개 주소 검증 |
| M-11 | ROM 상수 테이블 | A | 1 | 4h | RST/MSK/VFY 정의 |
| M-12 | Read 0 채움 | A | 1 | 10m | txBuf[1,2]=0 |
| M-13 | CRC-4 통일 | A | 1 | 1h | "CRC-8" 제거 |
| M-14 | Shadow 갱신 | A | 1 | 3h | SPI 성공 시만 갱신 |
| M-08 | 비트필드 테스트 | B | 2 | 4h | 전 타입 테스트 |
| M-10 | Daisy Chain | B | 2 | 4h | Slot 매핑 + 회로도 확인 |
| M-15 | DB_PARSING 중복 | B | 2 | 2h | 역할 명확화 |
| M-17 | LPM 시퀀스 | B | 2 | 4h | 진입/복귀 순서 |
| M-18 | 폴트 READ | B | 2 | 2h | Edge 검출 + POR |
| VR-2,7 | Retry Counter | A | 3 | 1d | 재시도 상한 |
| VR-3 | SDO 검증 | A | 3 | 4h | Inline validation |
| VR-5,6 | Timeout+ERROR | A | 3 | 1d | 전 상태 timeout |
| VR-8 | CSN Timing | A | 3 | 2h | 70µs delay |
| VR-9 | Exec Time | A | 3 | 4h | 통계 수집 |
| GP-1 | DB Category | High | 4 | 1h | CAT_1 분기 |
| GP-2 | SetPort Adapter | High | 4 | 4h | Mode 0-4 구현 |
| GP-3 | State 변환 | High | 4 | 3h | IDLE/ACTIVE |
| GP-4 | Duty/Freq 변환 | High | 4 | 2h | 0-255 매핑 |
| GP-5 | GetPort Adapter | High | 4 | 3h | Mode 0-5 조회 |
| GP-6 | DB Parsing | High | 4 | 1d | 초기값 적용 |
| TEST-1 | Unit Test | Mandatory | 5 | 1w | 90% coverage |
| TEST-2 | HiL Test | Mandatory | 6 | 1w | 전 시나리오 PASS |

---

## 검증 완료 기준

### 검증 체크리스트 달성

- [ ] 1절: 초기값 및 설정값 검증 - 100% 완료
- [ ] 2절: 데이터 구조와 버퍼 검증 - 100% 완료
- [ ] 3절: TX 프레임 생성 검증 - 100% 완료
- [ ] 4절: RX 파싱 및 검증 - 100% 완료
- [ ] 5절: 전체 IC 동작 프로세스 검증 - 100% 완료
- [ ] 6절: 타이밍과 CSN 제어 검증 - 100% 완료
- [ ] 7절: 오류 처리와 복구 검증 - 100% 완료
- [ ] 8절: 동기 SPI 전제 검증 - 100% 완료
- [ ] 9절: 단위 테스트 항목 - 100% 완료

### VERIFICATION_REPORT 달성

- [ ] 40/40 checks PASS (100%)
- [ ] All 9 Critical Issues resolved
- [ ] All 16 Warnings addressed

### GetPort/SetPort 호환성 달성

- [ ] 기존 Application 코드 변경 없이 TPS2HCS08 지원
- [ ] Mode 0-5 모두 정상 동작
- [ ] DB parsing으로 초기값 적용 확인

---

## 리스크 관리

| 리스크 | 확률 | 영향 | 대응 방안 |
|--------|------|------|-----------|
| 회로도 Daisy Chain 순서 불일치 | Medium | High | 하드웨어 엔지니어 조기 확인, Configurable 매핑 |
| DB Category 추가 불가 | Low | Medium | 기존 Category 내 구분 로직 |
| WD_TO 400ms 불충분 | Low | High | 프로젝트 요구사항 재확인 |
| I2T 초기값 문제 | Medium | High | DB 덮어쓰기 순서 보장 |
| HiL 테스트 환경 부족 | Medium | High | 대체 검증 방법 (시뮬레이션) |

---

## 의존성 및 외부 요청사항

### 하드웨어팀 요청

- [ ] Daisy Chain 회로도 확인 (IC 연결 순서)
- [ ] CS 신호가 체인 전체 공유인지 개별인지 확인
- [ ] HiL 테스트 환경 구성 지원

### 시스템팀 요청

- [ ] DB Category 추가 가능 여부 확인
- [ ] WD_TO 400ms 요구사항 확인
- [ ] VBB 측정 사용 여부 확인

### BSW팀 요청

- [ ] SPI MCAL 설정 확인 (CPOL/CPHA, MSB-first, CS continuous)
- [ ] CSN low 70µs delay 지원 확인

---

**문서 소유자**: Development Team
**마지막 업데이트**: 2026-09-04
**다음 리뷰**: Phase 1 완료 후
