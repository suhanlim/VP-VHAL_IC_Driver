# TPS2HCS08-Q1과 DRV8912 드라이버 구조 및 Daisy Chain 비교

## 1. 문서 목적

이 문서는 다음 내용을 설명한다.

- 현재 `TPS2HCS08-Q1` 드라이버와 기존 `DRV8912` 드라이버의 구조적 차이
- 설정 레지스터, 읽기 결과, 이전 상태를 보관하는 일반적인 방법
- SPI Daisy Chain이 무엇이며 왜 프레임 순서 처리가 필요한지
- DRV8912 코드에 Daisy Chain 순서가 어떻게 구현되어 있는지
- 신규 TPS2HCS08-Q1 드라이버에 추가로 필요한 Daisy Chain 고려사항

이 문서에서 사용하는 `devIdx` 또는 `dev_no`는 소프트웨어상의 장치 번호이다. 이 번호가 물리적으로 MCU와 가장 가까운 IC인지, 가장 먼 IC인지는 회로 연결과 프로젝트 규칙으로 별도 정의해야 한다.

---

## 2. 먼저 이해해야 하는 SPI Daisy Chain

### 2.1 일반적인 독립 SPI 연결

여러 IC를 독립적으로 연결하면 MCU의 SCLK와 SDI/SDO는 공유할 수 있지만, 보통 IC별 CS 신호를 별도로 사용한다.

```text
                  +---- IC 0
MCU SCLK ---------+---- IC 1
                  +---- IC 2

MCU SDI ----------+---- 각 IC SDI
MCU SDO <---------+---- 각 IC SDO

MCU CS0 -------------- IC 0 CS
MCU CS1 -------------- IC 1 CS
MCU CS2 -------------- IC 2 CS
```

IC 1에 접근하려면 `CS1`만 활성화하고 IC 한 개 분량의 프레임을 전송한다.

```text
CS1 LOW
전송: [IC 1용 명령]
CS1 HIGH
```

장점은 구현과 장치 식별이 단순하다는 것이다. 단점은 장치 수만큼 CS 핀이 필요하다는 것이다.

### 2.2 Daisy Chain 연결

Daisy Chain에서는 앞 IC의 직렬 출력이 다음 IC의 직렬 입력으로 연결된다.

```text
MCU SDI ---> [IC 1] ---> [IC 2] ---> [IC 3] ---> MCU SDO
                SDO SDI     SDO SDI

MCU SCLK ---> 모든 IC의 SCLK
MCU CS   ---> 모든 IC의 CS
```

여러 IC가 하나의 긴 shift register처럼 동작한다. IC가 3개이고 IC 하나의 명령 프레임이 `F`비트라면 한 번의 체인 전송 길이는 일반적으로 다음과 같다.

```text
총 전송 비트 수 = F × 3
```

예를 들어 IC 하나가 24비트 프레임을 사용한다면 3개 체인의 전체 길이는 72비트, 즉 9바이트가 된다.

```text
[24-bit slot][24-bit slot][24-bit slot]
```

중요한 점은 첫 번째로 MCU가 넣은 slot이 첫 번째 IC에 남는다고 단정할 수 없다는 것이다. 데이터가 체인을 통과해 계속 다음 IC로 이동하므로, 일반적으로 먼저 전송한 slot은 더 먼 IC까지 이동한다. 정확한 명령 slot 순서는 해당 IC 데이터시트와 실제 배선 순서로 확정해야 한다.

### 2.3 왜 장치 순서 변환이 필요한가

소프트웨어는 보통 다음처럼 자연스러운 순서를 원한다.

```text
device[0], device[1], device[2]
```

하지만 SPI 버퍼에서는 물리적인 shift 순서 때문에 다음과 같은 순서로 송수신될 수 있다.

```text
전송 또는 수신 버퍼: device[2], device[1], device[0]
```

따라서 다음 두 순서를 구분해야 한다.

- 논리 순서: 애플리케이션과 DB가 사용하는 `devIdx`
- Wire 순서: SPI 버퍼에 실제로 들어가는 slot 순서

드라이버는 두 순서 사이의 매핑을 명시적으로 처리해야 한다.

```c
wireSlot = (deviceCount - 1u) - devIdx;
```

위 식은 흔한 예일 뿐이며, 실제 프로젝트 식은 데이터시트와 배선 확인 후 결정해야 한다.

### 2.4 Daisy Chain의 장점과 비용

장점:

- 여러 IC가 하나의 CS 신호를 공유할 수 있다.
- MCU 핀 수를 줄일 수 있다.
- 여러 장치의 명령과 상태를 한 번의 체인 전송으로 교환할 수 있다.

비용 및 주의사항:

- 장치가 늘어날수록 전체 SPI 프레임이 길어진다.
- 한 IC와의 배선 또는 통신 문제가 뒤쪽 체인에 영향을 줄 수 있다.
- 논리 장치 순서와 Wire slot 순서를 변환해야 한다.
- 특정 장치 하나만 변경해도 다른 slot에 NOP 또는 유지 명령을 채워야 할 수 있다.
- 수신 결과가 어느 장치의 값인지 정확히 역매핑해야 한다.
- 비동기 SPI라면 진행 중 명령과 수신 buffer의 소유권도 관리해야 한다.

---

## 3. TPS2HCS08-Q1 SPI에서 추가로 알아야 할 점

TPS2HCS08-Q1은 CRC를 사용하지 않을 때 IC 한 개당 24비트 프레임을 사용한다.

```text
SDI: [23]    R/W
     [22:16] Register Address RA6:RA0
     [15:0]  Data
```

SDO의 16비트 Data Out은 현재 명령이 아니라 이전 SPI 명령의 결과이다.

```text
명령 프레임 N 전송   -> 이전 명령 N-1의 결과 수신
명령 프레임 N+1 전송 -> 명령 N의 결과 수신
```

따라서 독립 연결에서도 register read는 현재 코드처럼 두 번의 transaction이 필요하다.

```text
Transaction 1: READ 명령 전송
Transaction 2: 후속 명령 전송, Transaction 1의 결과 회수
```

Daisy Chain에서는 이 pipeline 규칙이 장치 수만큼의 slot 전체에 적용된다. 장치가 `N`개라면 한 transaction은 단순 3바이트가 아니라 원칙적으로 `3 × N`바이트의 체인 프레임이 되어야 한다.

```text
Transaction 1: [N개 장치의 명령 slot]
Transaction 2: [N개 장치의 후속 slot] + 이전 결과 회수
```

각 slot의 정확한 전송 순서와 후속 명령으로 무엇을 사용할지는 TPS2HCS08-Q1 데이터시트의 Daisy Chain 규칙 및 실제 회로 연결을 기준으로 확정해야 한다.

---

## 4. 현재 TPS2HCS08-Q1 드라이버 구조

### 4.1 잘 구현된 부분

현재 코드는 최대 장치 수와 장치별 context를 정의한다.

```c
#define TPS2HCS08_DEV_MAX (4u)

static tTps2hcs08Ctx exVioDbTps2hcs08Ctx[TPS2HCS08_DEV_MAX];
```

Signal DB의 `IC` 값도 `devIdx`로 변환하여 장치별 설정을 분리한다.

```c
devIdx = (uint8)exVioDbRec[sigIndex].IC;
pCtx = &exVioDbTps2hcs08Ctx[devIdx];
```

따라서 다음 논리 계층은 이미 존재한다.

```text
DB IC 번호 -> devIdx -> 장치별 context
```

레지스터별 union 타입도 명확하다.

```c
tTps2hcs08FaultMask
tTps2hcs08DevConfig
tTps2hcs08AdcConfig
tTps2hcs08AdcResultVbb
```

설정 레지스터는 write shadow로 보관하여 Read-Modify-Write와 설정 복원에 유리하다.

### 4.2 현재 코드는 실제 Daisy Chain transfer를 구현하지 않음

현재 porting API는 다음 형태다.

```c
ExVioDb_Tps2hcs08_Port_SpiTransfer(
    uint8 devIdx,
    const uint8 *txData,
    uint8 *rxData,
    uint8 len);
```

Write 및 Read 함수는 항상 IC 한 개 분량인 3바이트만 전송한다.

```c
#define TPS2HCS08_SPI_FRAME_LEN (3u)

ExVioDb_Tps2hcs08_Port_SpiTransfer(
    devIdx,
    txBuf,
    rxBuf,
    TPS2HCS08_SPI_FRAME_LEN);
```

CS 제어도 `devIdx`를 인자로 받는다.

```c
ExVioDb_Tps2hcs08_Port_SetCsn(uint8 devIdx, boolean high);
```

이 인터페이스는 다음 모델에 가깝다.

```text
devIdx 선택 -> 해당 장치 CS 선택 -> 3바이트 전송
```

즉 `TPS2HCS08_DEV_MAX`의 주석과 DB의 IC index에는 Daisy Chain이라는 표현이 있지만, 현재 파일에서 확인되는 실제 전송 로직은 다음 기능을 가지고 있지 않다.

- `deviceCount × frameLength` 크기의 체인 TX/RX buffer
- 논리 `devIdx`에서 Wire slot으로 변환
- 장치별 명령 slot 조립
- 수신 slot의 역순 매핑
- 한 CS 구간에서 모든 장치 명령 전송
- Daisy Chain 전체에 대한 2단계 read pipeline 관리

Porting layer 내부가 별도로 chain frame을 확장한다고 가정하기도 어렵다. 상위 함수가 3바이트 buffer만 전달하기 때문에 다른 장치의 slot에 넣을 명령과 결과 저장 위치를 porting layer가 알 수 없기 때문이다.

따라서 현재 구현은 실질적으로 다음 중 하나로 판단된다.

1. IC별 CS를 사용하는 independent secondary 구조
2. Daisy Chain 구현이 아직 추가되지 않은 초기 구조

실제 하드웨어가 하나의 CS와 직렬 SDO-SDI 체인으로 연결되어 있다면 현재 3바이트 단위 전송만으로는 전체 체인을 올바르게 제어하기 어렵다.

---

## 5. DRV8912에 Daisy Chain 순서가 반영된 방법

### 5.1 체인 장치 수를 configuration으로 관리

DRV8912 채널 context에는 다음 값이 있다.

```c
uint8 num_of_dev_in_daisy_chain;
```

모든 장치 번호 검사도 이 값을 기준으로 한다.

```c
dev_no < num_of_dev_in_daisy_chain
```

고정 최대값만 사용하는 것이 아니라 실제 채널에 연결된 IC 수를 runtime configuration에서 알 수 있다.

### 5.2 체인 전체 SPI buffer를 사용

DRV8912는 체인 장치 수를 반영해 SPI buffer 크기를 계산한다.

```c
SPI_BUFFER_SIZE =
    DAISY_CHAIN_HEADER_SIZE +
    (NUM_OF_DEV_IN_DAISY_CHAIN * PAYLOAD_MUL);
```

그리고 채널별 TX/RX buffer를 별도로 둔다.

```c
static uint8 s_ex_vio_channel_0_drv8912_spi_buffer[SPI_BUFFER_SIZE];
```

이는 IC 한 개의 명령만 전송하는 것이 아니라 전체 체인 프레임을 한 번에 구성한다는 뜻이다.

### 5.3 논리 장치별 runtime register table

현재 읽은 레지스터값은 RAM runtime table에 저장한다.

```c
static TS_EX_VIO_DRV8912_REG_4 s_ex_vio_channel_0_reg_data_tbl;
```

초기 설정은 별도 `const` ROM 객체에 저장한다.

```c
static const TS_EX_VIO_DRV8912_REG_4 s_ex_vio_channel_0_daisy_chain_init;
```

따라서 다음 정보가 분리되어 있다.

```text
init_reg         : 초기 설정값
runtime reg table: 최근 SPI read 또는 runtime 상태
ic_pre_status    : 직전 상태
```

### 5.4 수신된 역순 데이터를 논리 장치 순서로 저장

DRV8912 callback의 주석은 실제 RX 순서를 다음과 같이 설명한다.

```text
Sn ... S1, Header1, Header2, Rn ... R1
```

수신 buffer의 register 결과 부분은 `Rn ... R1` 순서지만, software table은 장치 번호 순서로 사용해야 한다. 구현은 장치 index를 감소시키면서 buffer index를 증가시킨다.

```c
dev_cnt = num_of_dev_in_daisy_chain;
buf_idx = datalen - num_of_dev_in_daisy_chain;

while (dev_cnt > 0u)
{
    dev_cnt--;
    r_reg[dev_cnt] = data[buf_idx];
    buf_idx++;
}
```

장치가 3개라면 개념적으로 다음 변환이다.

| RX buffer 순서 | 저장 대상 |
|---|---|
| `R3` | `r_reg[2]` |
| `R2` | `r_reg[1]` |
| `R1` | `r_reg[0]` |

즉 Wire에서 수신된 역순을 software의 정방향 index로 복원한다.

### 5.5 Read 응답 저장 위치를 요청 시 지정

DRV8912는 비동기 SPI 응답이 도착했을 때 어느 레지스터 table에 저장할지 `r_reg` 포인터로 기억한다.

```c
typedef struct
{
    uint8 *r_reg;
    TU_EX_VIO_DRV8912_REGS daisy_chain_tbl;
} TS_EX_VIO_DRV8912_CMD_N_STATUS;
```

Read 요청이면 해당 runtime register의 주소를 설정한다.

```c
cmd_n_status.r_reg = GetCrRgPtr(...);
```

callback은 수신 결과를 `r_reg[dev_no]`에 저장한다. Write 요청처럼 저장할 Read 결과가 없으면 `r_reg = NULL_PTR`로 둔다.

### 5.6 이전 상태와 현재 상태 비교

새로운 IC status를 읽기 전에 현재 runtime 값을 이전 상태에 복사한다.

```c
memcpy(ic_pre_status, current_status, deviceCount);
```

새 응답 수신 후 다음 식으로 새롭게 발생한 fault만 검출한다.

```c
newFault = (previous ^ current) & current;
```

이 구조는 Daisy Chain에서 모든 장치의 이전/현재 상태를 배열 index로 비교하기에 적합하다.

---

## 6. TPS2HCS08과 DRV8912 구조 비교

| 항목 | 현재 TPS2HCS08 | DRV8912 |
|---|---|---|
| 장치 구분 | `devIdx`별 context | `dev_no`와 체인 장치 수 |
| 실제 전송 단위 | 장치당 3바이트 | 체인 전체 buffer |
| CS 모델 | `devIdx`별 CS 인터페이스 | 채널/체인 단위 CS |
| Wire slot 조립 | 없음 | 체인 수를 반영해 조립 |
| RX 역순 매핑 | 없음 | `Rn ... R1`을 index 역순으로 저장 |
| 초기 설정 | project default shadow | `const` init register ROM |
| 현재 read 값 | 주로 즉시 처리 또는 목적별 저장 | runtime register RAM table |
| 이전 상태 | fault latch 등 일부 목적별 | `ic_pre_status[]` 명시적 관리 |
| 비동기 응답 위치 | 없음 | `r_reg` 포인터로 지정 |
| 타입 가독성 | 레지스터별 union으로 높음 | 범용 table/pointer로 복잡함 |
| RAM 사용량 | 필요한 값 위주로 작음 | 전체 runtime table로 큼 |
| 확장성 | 독립 장치 접근에 단순 | 여러 체인 장치에 유리 |

### 6.1 현재 TPS2HCS08 구조의 장점

- 레지스터별 타입 이름과 bit field 의미가 명확하다.
- 설정 shadow를 이용한 Read-Modify-Write가 단순하다.
- 장치별 context가 분리되어 애플리케이션 코드가 이해하기 쉽다.
- 포인터 기반 비동기 routing이 적어 잘못된 RAM 위치에 응답을 저장할 위험이 낮다.
- 사용하지 않는 read-only 결과를 모두 저장하지 않으므로 RAM 사용량이 작다.

### 6.2 현재 TPS2HCS08 구조의 단점

- 데이터시트 Reset값과 프로젝트 운용 설정값의 역할이 일부 혼재한다.
- read-only 레지스터의 최신값, 이전값, valid, timestamp 관리 정책이 통일되어 있지 않다.
- 전체 장치 상태 snapshot 또는 register dump를 만들기 어렵다.
- 실제 Daisy Chain의 체인 frame 조립과 순서 변환이 없다.
- 현재 port API를 유지한 채 비동기 chain SPI로 바꾸기 어렵다.

### 6.3 DRV8912 구조의 장점

- ROM 초기값, RAM runtime 값, 이전 상태가 분리되어 있다.
- 체인 장치 수와 전체 buffer 크기가 명시적이다.
- Wire 역순 데이터를 논리 index로 변환하는 코드가 존재한다.
- 비동기 SPI callback 결과를 runtime register table로 routing한다.
- 모든 장치의 fault 변화 검출과 전체 상태 dump에 유리하다.

### 6.4 DRV8912 구조의 단점

- pointer, register index, command index, chain slot을 동시에 이해해야 한다.
- `r_reg`가 잘못 설정되거나 요청 도중 변경되면 잘못된 위치에 응답이 저장될 수 있다.
- 전체 register table을 저장하므로 RAM 사용량이 증가한다.
- 범용 `uint8 *` 접근은 TPS2HCS08의 명시적 union보다 타입 안전성이 낮다.
- current/previous는 있지만 각 값의 valid, timestamp, stale 상태는 충분히 구조화되어 있지 않다.

---

## 7. 신규 TPS2HCS08 Daisy Chain 드라이버 권장 구조

DRV8912를 그대로 복사하기보다는 TPS2HCS08의 명시적 타입 구조를 유지하면서 chain transport 계층을 추가하는 것이 적절하다.

### 7.1 물리 체인과 논리 장치 매핑 정의

다음 용어를 코드와 설계서에서 먼저 고정해야 한다.

```text
devIdx 0은 MCU에 가장 가까운 IC인가?
devIdx 0은 MCU에서 가장 먼 IC인가?
SDI 방향의 첫 번째 IC를 0이라고 하는가?
SDO 방향의 첫 번째 IC를 0이라고 하는가?
Signal DB의 IC 값은 어느 규칙을 따르는가?
```

권장 configuration 예시는 다음과 같다.

```c
typedef struct
{
    uint8 deviceCount;
    uint8 logicalToWireSlot[TPS2HCS08_DEV_MAX];
    uint8 wireSlotToLogical[TPS2HCS08_DEV_MAX];
} tTps2hcs08ChainConfig;
```

단순 역순이 확실하더라도 함수로 캡슐화하는 편이 좋다.

```c
static uint8 Tps2hcs08_LogicalToWireSlot(uint8 devIdx);
static uint8 Tps2hcs08_WireSlotToLogical(uint8 slot);
```

### 7.2 장치 명령과 체인 전송을 계층 분리

IC 하나의 24비트 명령 생성과 전체 체인 조립을 분리한다.

```c
typedef struct
{
    uint8 byte[3];
} tTps2hcs08Frame;

static void Tps2hcs08_BuildReadFrame(uint8 addr,
                                     tTps2hcs08Frame *frame);

static void Tps2hcs08_BuildWriteFrame(uint8 addr,
                                      uint16 data,
                                      tTps2hcs08Frame *frame);

static void Tps2hcs08_BuildChainTransfer(
    const tTps2hcs08Frame logicalFrames[],
    uint8 txBuffer[]);
```

이렇게 하면 register 의미와 Wire 순서가 서로 섞이지 않는다.

### 7.3 체인 전체 buffer 크기

CRC 미사용 시:

```c
#define TPS2HCS08_FRAME_SIZE_NO_CRC (3u)
#define TPS2HCS08_CHAIN_BUFFER_SIZE \
    (TPS2HCS08_FRAME_SIZE_NO_CRC * TPS2HCS08_DEV_MAX)
```

실제 연결 수가 최대 수보다 작다면 전송 길이는 `deviceCount`를 기준으로 계산한다.

```c
length = chainConfig.deviceCount * TPS2HCS08_FRAME_SIZE_NO_CRC;
```

CRC가 활성화되면 slot이 4바이트가 되므로 고정 3바이트 가정을 제거하거나 mode별 크기를 관리해야 한다.

### 7.4 특정 장치만 쓸 때도 모든 slot 채우기

Daisy Chain에서는 특정 장치 하나에 명령을 보낼 때도 체인의 다른 장치를 통과할 slot이 필요하다.

```text
목표: device 1의 DEV_CONFIG만 Write

Wire frame:
[device 2용 유지/NOP][device 1용 Write][device 0용 유지/NOP]
```

다른 slot에 어떤 명령을 넣어야 side effect가 없는지는 데이터시트로 확정해야 한다. 임의의 주소나 기존 Write 명령을 재사용하면 안 된다.

### 7.5 TPS2HCS08의 이전 프레임 응답 pipeline 관리

요청 context에 최소한 다음 정보를 보관하는 것이 좋다.

```c
typedef struct
{
    boolean active;
    uint8 requestedAddr[TPS2HCS08_DEV_MAX];
    boolean expectReadData[TPS2HCS08_DEV_MAX];
    uint32 sequence;
} tTps2hcs08PendingTransfer;
```

Transaction 1에서 보낸 read 요청과 Transaction 2에서 받은 data를 연결해야 한다. 비동기 SPI라면 callback이 현재 TX buffer가 아니라 이전 command context를 기준으로 결과를 해석해야 한다.

### 7.6 설정 shadow와 read cache 분리

```c
typedef struct
{
    tTps2hcs08FaultMask faultMask;
    tTps2hcs08SwState swState;
    tTps2hcs08DevConfig devConfig;
    tTps2hcs08AdcConfig adcConfig;
} tTps2hcs08ConfigShadow;

typedef struct
{
    tTps2hcs08AdcResultVbb currentVbb;
    tTps2hcs08AdcResultVbb previousVbb;
    boolean vbbValid;
    uint32 vbbTimestamp;
} tTps2hcs08MeasurementCache;
```

Read 성공 및 `VBB_RDY = 1`일 때만 다음처럼 갱신한다.

```c
previousVbb = currentVbb;
currentVbb = newVbb;
vbbValid = TRUE;
```

이 read cache는 SPI Write 경로에 전달하지 않는다.

### 7.7 동시성 및 오류 처리

비동기 chain SPI에서는 다음 항목이 필요하다.

- 동시에 두 요청이 pending되지 않도록 busy 상태 관리
- TX/RX buffer를 callback 완료 전까지 변경하지 않기
- 요청 sequence와 callback sequence 확인
- SPI timeout 및 CRC 오류 처리
- 장치별 응답 valid 관리
- 체인 전체 실패와 특정 장치 status fault 구분
- chain 재초기화 시 모든 read cache를 invalid 처리

---

## 8. 구현 전 필수 확인사항

다음 항목이 결정되기 전에는 Daisy Chain index 로직을 확정하면 안 된다.

1. 실제 회로에서 IC의 SDI/SDO 연결 순서
2. MCU에 가장 가까운 IC와 가장 먼 IC의 명칭
3. Signal DB의 `IC=0`이 가리키는 물리 IC
4. TPS2HCS08 데이터시트가 정의한 chain 명령 slot 순서
5. chain read 결과의 slot 순서
6. 목표 장치 외 slot에 사용할 안전한 명령
7. 한 CS 구간의 정확한 전체 clock 수
8. CRC 사용 여부와 CRC 계산 단위
9. TPS2HCS08의 previous-frame response가 chain에서 적용되는 방식
10. CS가 체인별 하나인지 IC별로 따로 존재하는지
11. SPI가 동기식인지 AUTOSAR 비동기 callback 방식인지
12. 일부 장치가 미실장된 경우 chain이 물리적으로 유지되는지

특히 `devIdx = 0`의 의미를 코드 주석만으로 결정하면 안 된다. 회로도, Signal DB, 데이터시트의 shift 방향을 함께 확인한 후 unit test의 예상 byte 배열로 고정해야 한다.

---

## 9. 권장 테스트 항목

### 9.1 Frame builder 단위 테스트

- Read/Write bit 위치
- `RA6:RA0` address mask
- 16비트 payload의 byte order
- CRC 활성/비활성 frame 길이

### 9.2 Chain ordering 단위 테스트

서로 구분되는 test pattern을 사용한다.

```text
device 0 payload = 0x1111
device 1 payload = 0x2222
device 2 payload = 0x3333
```

예상 TX buffer와 RX 역매핑 결과를 byte 단위로 검증한다. 같은 값을 모든 장치에 사용하면 순서 오류를 발견할 수 없다.

### 9.3 Read pipeline 테스트

- Transaction 1의 요청 결과가 Transaction 2에서 해당 장치 cache로 들어가는지
- 장치별 address가 섞이지 않는지
- 두 개 이상의 연속 read에서 한 transaction씩 밀리지 않는지

### 9.4 오류 테스트

- 잘못된 `devIdx`
- chain 장치 수 0 또는 최대 초과
- SPI callback timeout
- 짧거나 긴 RX 길이
- CRC 오류
- pending 요청 중 중복 요청
- 체인 중간 장치 통신 단절

---

## 10. 결론

현재 TPS2HCS08 코드에는 장치별 context와 `devIdx` 개념은 존재하지만, 이것만으로 Daisy Chain이 구현된 것은 아니다. 실제 Daisy Chain 지원에는 다음 핵심 로직이 추가되어야 한다.

```text
장치별 논리 명령 생성
        ↓
logical devIdx -> Wire slot 변환
        ↓
N개 slot의 chain TX buffer 조립
        ↓
한 CS 구간에서 전체 buffer 전송
        ↓
RX slot -> logical devIdx 역변환
        ↓
previous-frame 요청 context에 맞춰 결과 저장
```

DRV8912는 체인 장치 수, 체인 전체 buffer, RX 역순 저장, runtime register table, 이전 상태 비교를 구현하고 있으므로 좋은 참고 사례다. 다만 pointer와 범용 table 중심 구조를 그대로 복사하기보다, TPS2HCS08의 명시적인 레지스터 union 타입을 유지하고 chain transport와 read cache 계층만 추가하는 절충 구조가 적합하다.

