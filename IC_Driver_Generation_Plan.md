# IC Chip Driver 생성 계획 및 구현 프롬프트

## Summary

- 목표: 각 IC 칩 PDF/Markdown 스펙을 기반으로 `Init` Runnable과 10ms Task Runnable에서 칩별 동작 프로세스를 수행하는 AUTOSAR C 스타일 드라이버 구조를 생성한다.
- 대상 구조: 여러 칩 공통 아키텍처를 우선 설계하고, 칩별 구현은 `ExVioDb_{ChipName}.c/.h`로 분리한다.
- 현재 확인된 스펙: `VNF9Q20F_Spec_KR.md` 기준으로 Sleep -> Fail-safe -> DB 기반 레지스터 설정 -> 초기 진단 -> Normal -> WDT/PWM Sync/Run -> Pre-standby/Standby 흐름을 상태 머신화한다.
- Reference match: `Reference Code/VAHL_SWC/Src/PowerWindow.c`의 `static state + Init reset + 10ms switch/case + RTE read/write` 패턴을 따른다.
- Exemple match: `Exemple Code/.../App_Mode.c`의 AUTOSAR `FUNC`, RTE 호출, 모드 전이 Runnable 스타일을 따른다.

## Key Changes

- 공통 타입/상태:
  - 전체 SWC 상태는 기존 `EXVIODB_STATE_EXVIO_SETUP`, `EXVIODB_STATE_RUN`, `EXVIODB_STATE_ERROR`를 유지한다.
  - 칩별 세부 프로세스는 `ExVioDb_{ChipName}ProcStateType` enum으로 정의한다.
  - 예: `VNF9Q20F_PROC_SLEEP`, `FAILSAFE_ENTER`, `WRITE_DB_CONFIG`, `INITIAL_DIAG`, `NORMAL_ENTER_UNLOCK`, `NORMAL_ENTER_ENABLE`, `WATCHDOG`, `PWM_SYNC`, `RUN_OUTPUT`, `PRE_STANDBY`, `STANDBY`, `COMPLETE`, `ERROR`.
- 파일 구조:
  - `ExVioDb_{ChipName}.h`: 칩별 register address, bit mask, field shift/width, enum, register image struct, public API 선언.
  - `ExVioDb_{ChipName}.c`: 칩별 Init/Setup/Run/Diagnosis/Sleep process 구현.
  - `ExVioDb_Helper.h/.c`: SPI read/write wrapper, bitfield set/get, masked write, state completion check, retry/timer, register write verification, common fault logging helper.
- Public API:
  - `void ExVioDb_{ChipName}_Init(void);`
  - `void ExVioDb_{ChipName}_Setup10ms(void);`
  - `void ExVioDb_{ChipName}_Run10ms(void);`
  - `void ExVioDb_{ChipName}_PrepareSleep10ms(void);`
  - `boolean ExVioDb_{ChipName}_IsSetupComplete(void);`
  - `boolean ExVioDb_{ChipName}_HasError(void);`
- Runnable integration:
  - `RE_Swc_ExVioDb_Init()`에서 공통 상태와 모든 칩 상태를 초기화하고 `exVioDbStateSeq = EXVIODB_STATE_EXVIO_SETUP`로 진입한다.
  - `RE_Swc_ExVioDb_Task_10ms()`의 `EXVIODB_STATE_EXVIO_SETUP`에서 각 칩의 `Setup10ms()`를 호출하고, 모든 칩 `IsSetupComplete()`가 `TRUE`이면 RTE notification write 후 `EXVIODB_STATE_RUN`으로 전이한다.
  - `EXVIODB_STATE_RUN`에서는 기존 DB/Input/Output update 호출 후 칩별 `Run10ms()`를 호출한다.
  - 칩 오류 발생 시 해당 칩은 `ERROR` 상태로 고정하고, 시스템 정책에 따라 전체 `EXVIODB_STATE_ERROR` 또는 degraded run으로 전이한다. 기본값은 전체 `EXVIODB_STATE_ERROR`.

## VNF9Q20F Mapping

- Init:
  - register image를 default 값으로 초기화한다.
  - `STDBY_NOT` 제어 준비, WDT toggle state, SPI retry counter, 진단 완료 flag, 최초 GSB/RSTB ignore flag를 초기화한다.
- Setup 10ms:
  - Sleep 상태에서 시작한다고 가정한다.
  - `STDBY_NOT = HIGH`로 Fail-safe 진입.
  - 차량 IO Signal DB를 읽어 `DUTYCRx`, `SLOPECRx`, `CHPHAx`, `PWMFCYx`, `CCR`, `PARAL`, `INOM`, `TNOM` register image에 매핑한다.
  - 정의되지 않은 DB parameter는 해당 register write를 skip하고 signal id/parameter error log를 남긴다.
  - 초기 진단에서 `ADCLSRx`, `ADCMSRx`, `ADCHSRx`, `ITSTx`, channel open/short status를 읽고 정상 범위를 벗어나면 1회 report한다.
  - Normal mode 진입은 `UNLOCK=1` write 후 `EN=1`, `GOSTBY=0` write 순서로 수행한다.
  - PWM Sync는 `PWM SYNC=1` write 완료 시 setup complete 처리한다.
- Run 10ms:
  - Watchdog spec은 5ms이므로 10ms Runnable에서는 매 호출마다 2회 토글하지 않는다. 기본 계획은 5ms Runnable이 없으면 "10ms마다 1회 토글"로 구현하고, 5ms Runnable 존재 시 `Run5ms()` API를 별도 추가한다.
  - 앱SW output request 기준으로 channel output register를 갱신한다.
  - WDT toggle 시 GSB 및 RAM fault register를 읽어 조건 만족 field를 1회 logging한다.
  - `RSTB=1` 또는 SPI retry 10회 실패 정책 발생 시 VNF9Q20F process를 DB config 단계부터 재시작하거나 SW reset 요청으로 연결한다.
- Sleep transition:
  - ACC OFF 및 sleep 조건 만족 시 앱SW channel disable register를 먼저 write한다.
  - `UNLOCK=1, EN=1` 후 `EN=0, GOSTBY=1` 순서로 Pre-standby 진입.
  - `STDBY_NOT = LOW`로 Standby 진입.
  - Wake condition 발생 시 `STDBY_NOT = HIGH` 후 초기 진단 단계부터 반복한다.

## Test Plan

- 정적 확인:
  - enum 상태가 스펙 동작 프로세스 1~15단계와 1:1로 추적 가능한지 확인한다.
  - register address/mask/shift가 PDF/Markdown 표와 일치하는지 리뷰한다.
  - helper 함수가 칩별 파일에 종속되지 않고 address/value/mask 기반으로 재사용되는지 확인한다.
- 단위 테스트 또는 스텁 테스트:
  - Init 후 모든 칩 process state가 첫 setup 단계로 초기화되는지 확인한다.
  - Setup 단계에서 SPI write/read 성공 시 `IsSetupComplete()`가 `TRUE`로 변하는지 확인한다.
  - DB parameter 미정의 입력 시 register write skip 및 error log가 발생하는지 확인한다.
  - VNF9Q20F 초기 진단 정상/비정상 범위별 report 동작을 확인한다.
  - `RSTB=1`, `SPIE=1`, SPI retry 10회 실패 시 재시작 또는 error 전이가 발생하는지 확인한다.
- 빌드/품질:
  - AUTOSAR compiler abstraction `FUNC`, memory section, RTE include, `Std_ReturnType` cast 스타일을 기존 프로젝트 규칙과 맞춘다.
  - MISRA 관점에서 macro 다중 평가, signed/unsigned 비교, magic number, unused return, enum default 처리를 점검한다.

## Code Generation Prompt

```text
너는 AUTOSAR C 기반 ASW/SWC 드라이버 코드를 작성하는 시니어 임베디드 엔지니어다.

목표:
IC chip PDF/Markdown 스펙을 기반으로 ExVioDb IC driver 코드를 생성한다. 코드는 10ms Runnable과 Init Runnable에서 동작하며, 칩별 동작 프로세스는 enum 상태 머신으로 구현한다.

입력:
1. 기존 Runnable 예시:
   - RE_Swc_ExVioDb_Init()
   - RE_Swc_ExVioDb_Task_10ms()
   - EXVIODB_STATE_EXVIO_SETUP
   - EXVIODB_STATE_RUN
   - EXVIODB_STATE_ERROR
2. 칩 스펙 문서:
   - VNF9Q20F_Spec_KR.md 또는 각 칩별 PDF에서 register address, bit field, 동작 프로세스, 진단 조건을 추출한다.
3. 코딩 스타일:
   - AUTOSAR C 스타일
   - FUNC(void, SWC_EXVIODB_CODE)
   - Rte_Read/Rte_Write return은 `(void)` cast
   - 상태 변수는 static 또는 기존 ExVioDb module variable 패턴 사용
   - MISRA/QAC 위험이 낮은 명시적 cast, enum default, magic number macro화를 적용한다.

생성할 파일:
1. ExVioDb_{ChipName}.h
   - include guard
   - register address macro
   - bit mask/shift macro
   - register field enum
   - chip process state enum
   - register image struct
   - public API 선언:
     void ExVioDb_{ChipName}_Init(void);
     void ExVioDb_{ChipName}_Setup10ms(void);
     void ExVioDb_{ChipName}_Run10ms(void);
     void ExVioDb_{ChipName}_PrepareSleep10ms(void);
     boolean ExVioDb_{ChipName}_IsSetupComplete(void);
     boolean ExVioDb_{ChipName}_HasError(void);

2. ExVioDb_{ChipName}.c
   - Init에서 process state, retry counter, timer, register image, diagnosis flags 초기화
   - Setup10ms에서 칩 스펙의 단계별 동작을 switch-case 상태 머신으로 구현
   - Run10ms에서 watchdog, output update, fault read/report 수행
   - PrepareSleep10ms에서 output disable, pre-standby, standby 진입 수행
   - 각 SPI write/read는 helper API를 통해 호출
   - 각 단계는 한 번에 하나의 side-effect만 수행하고 다음 10ms tick에서 다음 단계로 전이

3. ExVioDb_Helper.h/.c
   - masked bitfield set/get
   - SPI read/write wrapper
   - retry counter 처리
   - register write verification
   - common state completion check
   - common fault/report helper

Runnable 통합:
- RE_Swc_ExVioDb_Init()에서 모든 칩 Init API를 호출한다.
- RE_Swc_ExVioDb_Task_10ms()의 EXVIODB_STATE_EXVIO_SETUP에서 모든 칩 Setup10ms()를 호출한다.
- 모든 칩 IsSetupComplete()가 TRUE이면 기존 RTE notification write 후 EXVIODB_STATE_RUN으로 전이한다.
- EXVIODB_STATE_RUN에서 기존 DB/Input/Output task 호출 후 모든 칩 Run10ms()를 호출한다.
- 어떤 칩이 HasError() TRUE이면 기본 정책으로 EXVIODB_STATE_ERROR로 전이한다.

VNF9Q20F 필수 구현:
- Sleep mode 시작
- STDBY_NOT HIGH로 Fail-safe mode 진입
- 차량 IO Signal DB 기반 register image 생성 및 write
- 초기 진단:
  ADCLSRx 71~91 정상
  ADCMSRx 293~357 정상
  ADCHSRx 819~961 정상
  ITSTx == 111b 정상
  Open/Short 진단 프로세스 수행
- Normal mode 진입:
  UNLOCK=1
  EN=1 AND GOSTBY=0
- Watchdog:
  WDTB toggle
  GSB/RAM fault read 및 조건 만족 field 1회 logging
- PWM sync:
  PWM SYNC=1
- Sleep 준비:
  channel disable
  UNLOCK=1 AND EN=1
  EN=0 AND GOSTBY=1
  STDBY_NOT LOW

주의:
- PDF에 정의되지 않은 DB parameter는 register write를 하지 말고 error log 처리한다.
- 칩별 register 값 계산 로직은 칩별 파일에 둔다.
- bit 조작, SPI retry, write verify 같은 중복 로직은 helper로 분리한다.
- 기존 RTE generated API는 직접 생성하지 말고, 필요한 API 이름은 TODO 또는 wrapper로 명시한다.
- 컴파일 가능한 C 코드를 우선하되 실제 SPI/RTE API 이름이 미확정이면 adapter 함수로 격리한다.
```

## Assumptions

- 현재 폴더에 기존 `ExVioDb` 소스가 없으므로, 실제 구현 시 기존 SWC 저장소 위치를 먼저 확인한 뒤 같은 naming/include/MemMap 규칙에 맞춘다.
- 여러 칩 공통 구조를 기본으로 하되, 현재 상세 매핑은 `VNF9Q20F`부터 완성하고 TIC/DRV/MPQ 등은 각 PDF 확보 후 같은 interface로 확장한다.
- Watchdog 5ms 요구는 현재 요청의 10ms Runnable과 충돌 가능성이 있으므로, 5ms Runnable이 없으면 10ms 동작으로 임시 구현하고 TODO로 남긴다.
