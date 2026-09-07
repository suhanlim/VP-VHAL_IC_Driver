# TPS2HCS08-Q1 Current Code Verification

- 대상: `modules/IC_Driver/TPS2HCS08-Q1`
- 기준일: 2026-08-31
- 검증 범위: `ExVioDb_Tps2hcs08.c`, `ExVioDb_Tps2hcs08.h`, `ExVioDb.c`, `ExVioDb.h`, `TPS2HCS08-Q1_Spec.md`
- 검증 방법: 정적 코드 검토, 기존 소스 단일 오브젝트 컴파일 확인
- 테스트 코드 생성 여부: 생성하지 않음

## Executive Summary

현재 코드는 TPS2HCS08-Q1의 기본 레지스터 shadow, DB 파싱, 초기화/진단/AUTO_LPM 상태 흐름의 골격은 갖추고 있다. 그러나 요청 기준의 "드라이버 로직 및 프로토콜 단위 검증 완료"로 보기에는 부족하다.

가장 큰 미충족 항목은 다음이다.

- Daisy chain이 이름과 인덱스 수준으로만 존재하고, 체인 전체 TX/RX 버퍼 및 `frame_size * device_count` 전송이 없다.
- SPI 응답 검증이 `Port_SpiTransfer() == TRUE` 확인에 거의 한정되어 있고, 무응답 패턴, opcode/address echo, command error, CRC/parity 검증이 없다.
- CRC 지원은 프레임 길이 define만 있고 실제 frame 생성/계산/검증 로직이 없다.
- 초기화 실패 재시도 횟수 제한이 없고, setup 전체 timeout 외에는 상태별 timeout/error 전이가 부족하다.
- `ExVioDb.c`가 C99 기준으로 컴파일 실패한다. `ExVioDb_GetDiagResult_Tps2hcs08()` 선언/정의가 없다.

## Compile Check

명령:

```powershell
gcc -std=c99 -Wall -Wextra -IInclude -Imodules/IC_Driver/TPS2HCS08-Q1 -c modules/IC_Driver/TPS2HCS08-Q1/ExVioDb_Tps2hcs08.c
gcc -std=c99 -Wall -Wextra -IInclude -Imodules/IC_Driver/TPS2HCS08-Q1 -c modules/IC_Driver/TPS2HCS08-Q1/ExVioDb.c
```

결과:

- `ExVioDb_Tps2hcs08.c`: 단일 오브젝트 컴파일 성공
- `ExVioDb.c`: 실패

실패 내용:

```text
ExVioDb.c:489:26: error: implicit declaration of function 'ExVioDb_GetDiagResult_Tps2hcs08'
```

근거:

- `ExVioDb.c:470-490`에서 `ExVioDb_GetDiagResult_Tps2hcs08()` 호출
- `ExVioDb_Tps2hcs08.h:579-597` public API 목록에는 해당 함수 선언 없음
- `ExVioDb_Tps2hcs08.c`에도 해당 함수 정의 없음

## 1. 초기값 및 설정값 검증

판정: 부분 충족

충족 근거:

- RAM shadow 초기화는 `ExVioDb_InitRegValue_Tps2hcs08()`에서 장치별로 수행된다. 근거: `ExVioDb_Tps2hcs08.c:394-503`
- 일부 reset/reserved 기반 초기값이 코드 주석과 값으로 반영되어 있다. 예: `FAULT_MASK=0xFF80`, `SW_STATE=0xFFFC`, `DEV_CONFIG=0xF800`, `ADC_CONFIG=0xFF3A`. 근거: `ExVioDb_Tps2hcs08.c:403-429`
- 차량 IO DB 값은 별도 파싱 단계에서 shadow에 덮어쓴다. 근거: `ExVioDb_Tps2hcs08.c:546-777`
- 초기화 write 순서는 구현되어 있다. `SW_STATE -> LPM -> FAULT_MASK -> DEV_CONFIG -> ADC_CONFIG -> channel registers`. 근거: `ExVioDb_Tps2hcs08.c:902-1012`
- 일부 read-back 검증이 있다. `DEV_CONFIG`, `ILIM_CONFIG_CHx`만 검증한다. 근거: `ExVioDb_Tps2hcs08.c:1019-1079`

미충족/위험:

- IC Power-on Reset 값, 드라이버 RAM 초기값, 차량 요구 설정값이 표로 분리되어 있지 않다. 코드 주석에 일부 reset 값이 섞여 있으나 완전하지 않다.
- reserved bit가 모든 write register에서 데이터시트 요구값으로 유지되는지 검증하는 공통 정책이 없다. 일부 값은 `word` 초기값으로 맞추지만 DB 파싱 후 write 전 mask 강제는 없다.
- read-back 검증 대상이 제한적이다. `FAULT_MASK`, `SW_STATE`, `LPM`, `ADC_CONFIG`, `PWM_CHx`, `CHx_CONFIG`, `I2T_CONFIG_CHx`는 write 하지만 verify 대상이 아니다.
- 초기화 실패 시 개별 register 또는 state retry count가 없다. 실패하면 `BUSY`로 남거나 `CONFIG_VERIFY` 실패 시 `CONFIG_WRITE`로 무한 반복한다. 근거: `ExVioDb_Tps2hcs08.c:1408-1426`
- 재초기화는 POR 감지 시 `CLEAR_POR`부터 재진입하므로 DB parsing/default 초기화가 다시 수행되지 않는다. 이전 shadow/skip/latch 값이 남을 수 있다. 근거: `ExVioDb_Tps2hcs08.c:1599-1605`, `1922-1936`

## 2. 데이터 구조와 버퍼 검증

판정: 미충족에 가까운 부분 충족

충족 근거:

- 최대 장치 수는 `TPS2HCS08_DEV_MAX = 4u`로 정의되어 있다. 근거: `ExVioDb_Tps2hcs08.h:25`
- IC 1개당 frame 크기는 CRC off 3바이트, CRC on 4바이트 define이 있다. 근거: `ExVioDb_Tps2hcs08.h:41-42`
- 장치별 runtime/shadow context는 존재한다. 근거: `ExVioDb_Tps2hcs08.h:538-574`
- 장치 ID와 채널 범위 검사는 일부 API와 DB 파싱에서 수행한다. 근거: `ExVioDb_Tps2hcs08.c:572-578`, `2066-2092`, `2112-2133`
- NULL 포인터 검사는 read API 일부에서 수행한다. 근거: `ExVioDb_Tps2hcs08.c:350-383`, `2112-2133`

미충족/위험:

- 체인 전체 TX/RX 버퍼가 없다. 실제 `txBuf`/`rxBuf`는 함수 stack의 3바이트 단일 장치 버퍼다. 근거: `ExVioDb_Tps2hcs08.c:318-329`, `352-368`
- 전체 버퍼 크기 `frame_size * TPS2HCS08_DEV_MAX` 계산과 실제 전송 길이 `frame_size * current_device_count` 계산이 없다.
- 실제 daisy chain device count 변수가 없다. `devPresent`는 장치별 존재 flag일 뿐 체인 길이/순서/offset 계산에 쓰이지 않는다.
- `device_count == 0`에 대한 명시 정책이 없다. 현재 DB에 장치가 없으면 대부분 loop가 skip되어 setup이 완료될 수 있다.
- register address 범위 검사가 없다. `addr & 0x7F`로 masking하므로 잘못된 주소가 조용히 다른 주소로 전송될 수 있다. 근거: `ExVioDb_Tps2hcs08.c:324`, `358`
- daisy chain 장치 순서와 buffer offset 매핑이 없다. `devIdx`가 port API 인자로 전달될 뿐이다.
- 오류 카운터, retry count, spi error count, previous_fault, register cache 같은 독립 runtime 진단 구조가 없다.

## 3. TX 프레임 생성 검증

판정: 단일 장치 CRC off 기본 frame만 부분 충족

충족 근거:

- Write frame은 `[23]=1`, `[22:16]=addr`, `[15:0]=payload` 형식으로 생성된다. 근거: `ExVioDb_Tps2hcs08.c:316-329`
- Read frame은 `[23]=0`, `[22:16]=addr`, data 0으로 생성된다. 근거: `ExVioDb_Tps2hcs08.c:350-368`
- byte order는 header, data MSB, data LSB 순서다. 근거: `ExVioDb_Tps2hcs08.c:324-326`, `358-360`
- read는 previous-frame response를 고려해 2회 전송한다. 근거: `ExVioDb_Tps2hcs08.c:346-371`

미충족/위험:

- CRC/parity 위치, 계산 방식, known-answer 검증이 없다.
- `TPS2HCS08_SPI_FRAME_LEN_CRC`는 define만 있고 사용되지 않는다. 근거: `ExVioDb_Tps2hcs08.h:42`
- device address/ID field가 protocol frame에 포함되지 않는다. 이 IC의 protocol에 device ID field가 없다면 문서화되어야 하나, 현재 코드는 설명이 부족하다.
- reserved bit 고정값은 frame 생성 단계에서 검증하지 않는다.
- NOP/Broadcast frame 생성 API가 없다. `TPS2HCS08_REG_DUMMY=0x7F` define은 있으나 실제 read dummy에서는 같은 read command를 반복한다. 근거: `ExVioDb_Tps2hcs08.h:89-90`, `ExVioDb_Tps2hcs08.c:366-368`
- daisy chain 대상 외 IC에 넣는 frame, 전체 frame 장치 배열 순서, CSN 한 번에 전체 `frame_size * device_count` 전송이 구현되어 있지 않다.
- 데이터시트 예제 frame과 코드 결과를 비교하는 테스트가 없다.

## 4. RX 응답 파싱 및 검증

판정: 미충족

충족 근거:

- SPI 전송 성공 여부는 `Port_SpiTransfer() == TRUE`로 확인한다. 근거: `ExVioDb_Tps2hcs08.c:328-333`, `363-372`
- SDO header byte를 저장한다. 근거: `ExVioDb_Tps2hcs08.c:331-333`, `370-371`
- read data 16비트 추출은 수행한다. 근거: `ExVioDb_Tps2hcs08.c:371`

미충족/위험:

- RX 무응답 패턴 `0x00/0xFF` 검사가 없다.
- CRC/parity 검증이 없다.
- 응답 opcode/address echo 검증이 없다. TPS2HCS08 SDO가 address echo를 제공하지 않는 구조라면 그 제약과 대체 검증이 문서화되어야 한다.
- command/frame error와 IC fault bit를 통신 오류와 구분해 결과값으로 반환하지 않는다.
- 한 장치 응답 오류가 다른 장치 결과에 영향을 주지 않도록 하는 체인 단위 분리 로직이 없다.
- 검증 실패 시 기존 정상값을 덮어쓰지 않는 정책이 불완전하다. read 성공이면 응답 의미 검증 없이 cache를 갱신한다. 근거: `ExVioDb_Tps2hcs08.c:1563-1595`, `1617-1633`, `1725-1729`
- 결과 enum이 `E_OK/E_NOT_OK`와 `BUSY/COMPLETE` 수준이라 `SPI_ERROR`, `CRC_ERROR`, `RESPONSE_MISMATCH`, `COMMAND_ERROR`, `DEVICE_FAULT`를 구분하지 못한다.

## 5. 전체 IC 동작 프로세스 검증

판정: 상태 흐름 골격은 충족, 실패 경로는 부족

충족 근거:

- setup state가 `SET_DEF -> DB_PARSING -> WAKEUP -> WAIT_READY -> CLEAR_POR -> CONFIG_WRITE -> CONFIG_VERIFY -> DIAG -> ACTIVE_ENTRY -> COMPLETE` 순서로 구현되어 있다. 근거: `ExVioDb_Tps2hcs08.h:501-517`, `ExVioDb_Tps2hcs08.c:1364-1513`
- run state가 `ACTIVE -> LPM_PREPARE -> LPM_ENTRY -> LPM_WAIT_STATUS -> LPM_ACTIVE -> LPM_EXIT -> LPM_RESTORE -> ACTIVE`로 구현되어 있다. 근거: `ExVioDb_Tps2hcs08.h:520-529`, `ExVioDb_Tps2hcs08.c:1920-2049`
- Watchdog read는 100ms 주기로 `GLOBAL_FAULT_TYPE`, `FLT_STAT_CH1/2`, 필요 시 `ADC_RESULT_CHx_V`를 읽는다. 근거: `ExVioDb_Tps2hcs08.c:45-46`, `1548-1597`, `1941-1952`
- fault log는 latch로 최초 발생/해제 후 재발생 중심 출력 구조를 가진다. 근거: `ExVioDb_Tps2hcs08.c:1787-1910`

미충족/위험:

- OFF/SLEEP 자체를 독립 state로 관리하지 않는다. setup은 wakeup부터 시작한다.
- INIT/ABIST 완료는 DEV_ID read 성공으로 간접 판단하며 ABIST/NVM 상태 bit 확인은 없다. 근거: `ExVioDb_Tps2hcs08.c:809-849`
- 이전 단계 실패 시 다음 단계로 넘어가지 않는 구조는 일부 있으나, 실패 원인/횟수/timeout state가 없다.
- setup 내부의 실패는 `BUSY` 반복이고 장치별 Error 상태 전이가 없다. 전체 component timeout 10초만 있다. 근거: `ExVioDb.c:212-249`
- `DIAG` read 실패 반환을 무시하는 구간이 있다. 근거: `ExVioDb_Tps2hcs08.c:1177-1183`, `1234-1239`
- AUTO_LPM timeout은 log만 반복하고 Error 전이나 restore 강제 조건이 없다. 근거: `ExVioDb_Tps2hcs08.c:1984-1994`

## 6. 타이밍과 CSN 제어 검증

판정: 부분 충족

충족 근거:

- 10ms task tick 기반 시간 관리가 있다. 근거: `ExVioDb_Tps2hcs08.c:35-48`
- `tREADY=65us`는 10ms 1 tick 대기로 모델링한다. 근거: `ExVioDb_Tps2hcs08.c:39-40`, `815-818`
- 진단 discharge/blanking wait는 tick으로 구현되어 busy-wait가 아니다. 근거: `ExVioDb_Tps2hcs08.c:1429-1460`, `1478-1488`
- AUTO_LPM status wait timeout은 5초 tick으로 구현되어 busy-wait가 아니다. 근거: `ExVioDb_Tps2hcs08.c:47-48`, `1984-1994`

미충족/위험:

- CSN low 65us 이상 조건 구현이 불명확하다. wakeup에서 `SetCsn(FALSE)` 직후 `SetCsn(TRUE)`를 호출해 실제 low pulse 폭을 보장하지 않는다. 근거: `ExVioDb_Tps2hcs08.c:787-798`
- CS setup/hold time, SPI clock min/max, frame 사이 delay, POR 이후 첫 명령까지 대기 시간은 port/BSW에 위임되어 있고 코드에서 검증되지 않는다.
- `TICK_WD_READ=100ms`는 구현되어 있으나 runnable 내 실제 SPI 전송 횟수 최악값 산정이 없다.
- read 한 번이 SPI 전송 2회이므로 watchdog scan은 장치/채널 수에 따라 한 10ms runnable에서 많은 sync transfer를 수행할 수 있다. 최악 시 `GLOBAL_FAULT_TYPE` 4개 + `FLT_STAT` 8개 + `VOL_DET` 8개 = read 20회 = SPI transfer 40회 가능하다.
- BSW 통합 항목인 CPOL/CPHA, baud rate, sequence/job/channel, actual RX buffer 연결은 검증되지 않았다.

## 7. 오류 처리와 복구 검증

판정: 미충족에 가까운 부분 충족

충족 근거:

- 잘못된 DB `CAT_2`, `IC`, `PIN`, mapping table miss는 log 후 skip 처리한다. 근거: `ExVioDb_Tps2hcs08.c:555-589`, `624-776`
- public API의 `devIdx`, `chIdx`, NULL 포인터 범위 검사는 일부 있다. 근거: `ExVioDb_Tps2hcs08.c:2066-2133`
- setup 전체 timeout 후 component error state로 전이한다. 근거: `ExVioDb.c:212-249`
- POR fault 감지 시 재설정 요청이 있다. 근거: `ExVioDb_Tps2hcs08.c:1599-1605`, `1922-1936`

미충족/위험:

- SPI busy와 SPI fail이 boolean false 하나로만 처리된다.
- CRC/parity 실패, 응답 불일치, command error, timeout, device fault가 결과값으로 분리되지 않는다.
- 연속 실패 횟수, register별 retry 횟수 제한, 장치별 error 상태가 없다.
- 오류 발생 시 안전 출력 상태로 강제 전이하는 정책이 없다. 특히 run 중 SPI 실패 시 기존 `swState` shadow 유지 외 안전 동작이 없다.
- error counter overflow 방지 로직이 없다. counter 자체가 없다.
- fault 해제 시 latch clear는 일부 있으나 fault class별 복구 정책은 없다.

## 8. 동기 SPI 전제 검증

판정: 부분 충족

충족 근거:

- `Port_SpiTransfer()` 반환 후 즉시 RX를 해석하므로 동기 SPI 전제로 작성되어 있다. 근거: `ExVioDb_Tps2hcs08.c:328-333`, `363-372`
- SPI 성공일 때만 read output 값을 갱신한다. 근거: `ExVioDb_Tps2hcs08.c:363-372`
- TPS2HCS08 previous-frame response 특성을 인지해 read를 2 transaction으로 처리한다. 근거: `ExVioDb_Tps2hcs08.h:36-40`, `ExVioDb_Tps2hcs08.c:346-371`

미충족/위험:

- 동일 frame response인지 pipeline response인지에 대한 요청 context 추적이 없다. read 함수 내부에서만 2회 전송하고 전체 체인/비동기 확장에는 취약하다.
- dummy/NOP frame이 별도 opcode/API로 관리되지 않는다.
- 요청 중복 방지나 SPI busy 재진입 방지가 없다.
- 한 runnable 내 sync transfer 수 제한이 없다. 초기 config write/verify/diag와 watchdog read가 장치 수에 따라 길어질 수 있다.

## 9. 단위 테스트 항목 검증

판정: 미충족

현재 폴더에는 TPS2HCS08 드라이버용 단위 테스트 harness가 없다. 테스트 코드는 요청 조건에 따라 생성하지 않았다.

필수 테스트 대비 현재 상태:

- 최소/최대 register address: 없음
- 최소/최대 data: 없음
- read/write frame 생성 비교: 없음
- 정상 응답 파싱: 없음
- CRC/parity 오류: 코드 기능도 없음
- 응답 주소 불일치: 코드 기능도 없음
- 전체 `0x00`, 전체 `0xFF` 응답: 없음
- SPI 실패: 부분적으로 수동 확인 가능하나 자동 테스트 없음
- timeout: setup 전체 timeout만 있고 단위 테스트 없음
- daisy chain 1개/최대 개수: 체인 전송 기능 없음
- 잘못된 Device ID: `WAIT_READY`에서 검출하나 테스트 없음
- NOP/Broadcast frame: 코드 기능 없음
- 데이지 체인 장치별 RX 분리: 코드 기능 없음
- Fault 최초 발생/변경/유지/해제: latch 로직은 있으나 테스트 없음
- 상태 전이 정상/실패 경로: 없음
- 재시도 최대 횟수: 코드 기능 없음

## 최종 체크리스트

- [~] 초기값, Reset 값, 차량 설정값 및 초기화 순서: shadow/default/DB/write 순서는 있으나 reset/target/readback/retry 분리가 부족
- [ ] TX/RX 버퍼, 런타임 구조체 및 데이지 체인 크기/순서: 체인 버퍼와 offset 없음
- [~] 데이터시트 기반 TX 프레임 생성: CRC off 단일 frame만 있음
- [ ] 데이터시트 기반 RX 파싱 및 CRC/Parity/응답 정합성 검증: 대부분 없음
- [~] IC 전체 상태 머신과 요구사항 간 추적성: 정상 흐름은 있으나 실패/error/ABIST 추적 부족
- [~] CSN, NVM, ABIST, 진단, Watchdog 관련 타이밍: tick 기반 일부 구현, CSN/BSW/ABIST 부족
- [ ] 오류 검출, 재시도, Timeout 및 안전 상태 전이: setup 전체 timeout 외 부족
- [~] Sync SPI 사용 전제와 Runnable 최악 실행시간: sync 전제는 있으나 최악 실행시간 제한 없음
- [ ] 정상·오류·경계값 단위 테스트: 없음

## 권장 조치 우선순위

1. `ExVioDb_GetDiagResult_Tps2hcs08()` 선언/정의를 추가하거나 `ExVioDb_GetDiagResult()` 경로를 제거해 컴파일 오류를 먼저 해소한다.
2. TPS2HCS08 transport 계층을 분리해 단일 장치 SPI와 daisy chain SPI를 명확히 구분한다.
3. `deviceCount`, logical-to-wire slot mapping, chain TX/RX buffer, actual transfer length 계산을 추가한다.
4. register address range check와 result enum을 추가해 invalid param, SPI error, CRC/parity error, response mismatch, timeout, command error, device fault를 분리한다.
5. write register 전체에 대해 reserved bit mask와 read-back 대상 목록을 명시한다.
6. setup/run state별 retry counter와 timeout/error transition을 추가한다.
7. RTE/BSW stub 기반 단위 테스트를 별도 working tree에서 작성해 frame 생성, pipeline read, RX 오류, fault latch, state transition을 검증한다.

## 완료 기준 제안

현재 단계는 "요구사항 대비 정적 검증 및 주요 gap 식별 완료"로 보는 것이 맞다. 아직 "드라이버 로직 및 프로토콜 단위 검증 완료"는 아니다.

SPI 통합 검증에서 별도로 남는 항목:

- 실제 CPOL/CPHA 및 baud rate
- CSN timing
- Sequence/Job/Channel 연결
- 실제 RX buffer 연결
- RTE port 연결
- 실물 IC 통신과 통합 timing
