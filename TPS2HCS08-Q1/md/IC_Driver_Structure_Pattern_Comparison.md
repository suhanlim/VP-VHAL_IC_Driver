# IC 드라이버 구조체 설계 패턴 비교

## 1. 문서 목적

이 문서는 기존 `EX_VIO_OUTPUT` 모듈에 구현된 다음 IC 드라이버의 구조체 구성을 비교한다.

- MPQ6620A
- DRV8912
- VNFD1248F
- 현재 개발 중인 TPS2HCS08-Q1

주요 목적은 신규 TPS2HCS08-Q1 드라이버에서 다음 정보를 어떤 구조로 분리할지 판단하는 것이다.

1. 변경되지 않는 하드웨어 및 PB 설정
2. 초기 운용 레지스터 설정
3. 장치별 현재 및 이전 상태
4. Daisy Chain 전체의 SPI 통신 진행 상태

---

## 2. IC 드라이버에 필요한 정보의 종류

처음에는 다음 두 가지 정보만 생각하기 쉽다.

```text
const 초기 설정 정보
현재 진행 상태
```

이 구분은 올바른 출발점이지만, 실제 드라이버에서는 일반적으로 다음 네 영역이 필요하다.

```text
1. 무엇이 연결되어 있는가
   -> PB configuration / topology

2. 처음 무엇을 설정할 것인가
   -> Initial register configuration

3. 현재 IC 상태가 무엇인가
   -> Runtime state / register cache / previous status

4. 현재 어떤 SPI 통신을 수행 중인가
   -> Transport state / pending command / TX-RX buffer
```

각 영역의 수명과 변경 가능성은 다음과 같다.

| 영역 | 대표 내용 | 일반적인 저장 위치 | 실행 중 변경 |
|---|---|---|---|
| PB/topology | SPI 채널, CS, GPIO, 장치 수 | ROM/const | 하지 않음 |
| 초기 운용 설정 | 초기 레지스터값 | ROM/const | 원본은 변경하지 않음 |
| 장치 runtime | 현재 상태, 측정값, fault | RAM | 변경됨 |
| SPI transport | pending 명령, buffer, busy | RAM | 자주 변경됨 |

---

## 3. MPQ6620A 구조

MPQ6620A는 `현재/요청 상태`, `레지스터 설정`, `PB 연결 정보`를 분리한다.

### 3.1 현재 상태와 요청 상태

```c
typedef struct
{
    TE_EX_VIO_STATE cur_state;
    TE_EX_VIO_STATE req_state;
} TS_EX_VIO_OUTPUT_MPQ6620A_INFO;
```

- `cur_state`: 현재 실제 드라이버 상태
- `req_state`: 상위 소프트웨어가 요청한 다음 상태

예를 들어 다음과 같은 전환 중 상태를 표현할 수 있다.

```text
cur_state = RUN
req_state = SLEEP
```

이는 SLEEP 요청은 들어왔지만 아직 하드웨어 전환이 완료되지 않았다는 의미다.

### 3.2 레지스터 설정값

```c
typedef struct
{
    TU_EX_VIO_MPQ6620_REG_OUT    reg_out[];
    TU_EX_VIO_MPQ6620_REG_CTRL   reg_ctl[];
    TU_EX_VIO_MPQ6620_REG_PWM    reg_pwm[];
    TU_EX_VIO_MPQ6620_REG_FREQ   reg_frq[];
    TU_EX_VIO_MPQ6620_REG_FAULT  reg_flt[];
    TU_EX_VIO_MPQ6620_REG_SCD    reg_scd[];
    TU_EX_VIO_MPQ6620_REG_GFAULT reg_gft[];
    TU_EX_VIO_MPQ6620_REG_SLEEP  reg_slp[];
} TS_EX_VIO_OUTPUT_MPQ6620A_CONFIG;
```

IC에 적용하거나 관리할 레지스터 설정을 하나의 구조체에 모은다.

현재 TPS2HCS08-Q1의 다음 write shadow와 유사하다.

```c
faultMask
swState
devConfig
adcConfig
lpm
```

### 3.3 PB 채널 정보

```c
typedef struct
{
    const uint8 channelId;
    const uint8 csSelection;
    const uint8 connectedDeviceCount;

    const TS_EX_VIO_OUTPUT_MPQ6620A_PB_DEVICE_INFO *deviceInfo;

    TS_EX_VIO_OUTPUT_MPQ6620A_INFO   *info;
    TS_EX_VIO_OUTPUT_MPQ6620A_CONFIG *config;

    uint8 rxBuffer[];
    uint8 txBuffer[];
} TS_EX_VIO_OUTPUT_MPQ6620A_PB_CHANNEL_INFO;
```

개념적인 구조는 다음과 같다.

```text
PB 고정 설정
├─ 채널 번호
├─ CS 선택 방식
├─ 연결 장치 수
├─ 장치별 SPI/GPIO 정보
│
└─ RAM 객체 포인터
   ├─ 현재/요청 상태
   ├─ 레지스터 설정
   ├─ RX buffer
   └─ TX buffer
```

### 3.4 MPQ6620A 구조의 특징

장점:

- 현재 상태와 요청 상태가 명확히 분리된다.
- 레지스터 설정이 IC 전용 타입으로 표현된다.
- PB 연결 정보와 RAM 상태를 구분한다.
- 구조가 DRV8912보다 비교적 단순하다.

제한점:

- 이전 register 상태를 명시적으로 보관하는 구조가 약하다.
- 최신 read 값과 previous 값의 비교가 구조적으로 드러나지 않는다.
- Daisy Chain의 Wire slot 순서 처리는 DRV8912만큼 명확하지 않다.

---

## 4. DRV8912 구조

DRV8912는 Daisy Chain, 비동기 SPI, 초기값과 runtime 값의 분리를 가장 적극적으로 구현한다.

### 4.1 ROM 초기 설정

```c
static const TS_EX_VIO_DRV8912_REG_4
s_ex_vio_channel_0_daisy_chain_init;
```

초기화에 사용할 레지스터값이며 실행 중 원본을 변경하지 않는다.

```text
const init register
        ↓
초기화 시 Daisy Chain SPI Write
```

### 4.2 RAM runtime 레지스터

```c
static TS_EX_VIO_DRV8912_REG_4
s_ex_vio_channel_0_reg_data_tbl;
```

실행 중 변경되는 레지스터값과 SPI Read 결과를 저장한다.

```text
IC SPI Read 결과
        ↓
RAM runtime register table
```

### 4.3 이전 IC 상태

```c
uint8 *ic_pre_status;
```

새로운 status를 읽기 전에 현재값을 이전 상태에 복사한다.

```c
memcpy(ic_pre_status, currentStatus, deviceCount);
```

새로운 값이 수신되면 다음과 같이 새롭게 발생한 fault를 검출한다.

```c
newFault = (previous ^ current) & current;
```

| 이전 | 현재 | 결과 | 의미 |
|---:|---:|---:|---|
| 0 | 0 | 0 | 변화 없음 |
| 0 | 1 | 1 | 새 fault 발생 |
| 1 | 0 | 0 | fault 해제 |
| 1 | 1 | 0 | 기존 fault 유지 |

### 4.4 SPI 명령과 응답 저장 상태

```c
typedef struct
{
    uint8 *r_reg;
    TU_EX_VIO_DRV8912_REGS daisy_chain_tbl;
} TS_EX_VIO_DRV8912_CMD_N_STATUS;
```

- `r_reg`: 비동기 SPI Read 응답을 저장할 RAM 위치
- `daisy_chain_tbl`: 체인 장치별 command/register 데이터

Read 명령 전송 시 응답을 받을 register table 주소를 `r_reg`에 저장한다.

```c
cmd_n_status.r_reg = GetCrRgPtr(...);
```

SPI callback은 응답을 지정된 위치에 저장한다.

```c
r_reg[dev_no] = receivedData;
```

Write 명령처럼 저장할 Read 결과가 없으면 `r_reg = NULL_PTR`로 설정한다.

### 4.5 Daisy Chain 장치 수와 buffer

DRV8912 context는 실제 체인 장치 수를 보관한다.

```c
uint8 num_of_dev_in_daisy_chain;
```

SPI buffer 크기는 장치 수를 반영하여 계산한다.

```text
SPI buffer size = chain header + device count × payload size
```

따라서 IC 한 개 단위 buffer가 아니라 체인 전체 buffer를 사용한다.

### 4.6 Daisy Chain 수신 순서 변환

DRV8912 callback에 명시된 수신 순서는 다음과 같다.

```text
Sn ... S1, Header1, Header2, Rn ... R1
```

수신 register 결과가 `Rn ... R1` 순서이므로 논리 장치 배열에 역순으로 저장한다.

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

장치가 3개인 경우:

| RX buffer | 논리 배열 저장 위치 |
|---|---|
| `R3` | `r_reg[2]` |
| `R2` | `r_reg[1]` |
| `R1` | `r_reg[0]` |

### 4.7 DRV8912 채널 context

개념적으로 다음 정보가 하나의 채널 context에 연결된다.

```text
고정 topology
├─ SPI 정보
├─ 체인 장치 수
└─ 전체 buffer 크기

초기 설정
└─ const init register

runtime
├─ 현재 상태
├─ runtime register table
├─ 이전 IC 상태
├─ pending 명령 및 응답 위치
├─ SPI TX buffer
└─ SPI 오류 횟수
```

### 4.8 DRV8912 구조의 특징

장점:

- ROM 초기값, RAM runtime 값, 이전 상태가 분리된다.
- Daisy Chain 장치 수와 전체 buffer가 명시적이다.
- Wire 수신 순서를 논리 장치 index로 변환한다.
- 비동기 SPI callback 결과의 저장 위치를 관리한다.
- 여러 장치의 fault 변화를 배열로 비교하기 쉽다.

제한점:

- pointer, command index, register index, Wire slot을 함께 이해해야 한다.
- `r_reg`가 잘못 설정되면 다른 RAM 위치에 결과가 저장될 수 있다.
- 전체 runtime register table로 인해 RAM 사용량이 커진다.
- 범용 `uint8 *` 접근은 IC 전용 union보다 타입 안전성이 낮다.
- 각 cached 값의 valid, timestamp, stale 상태는 충분히 구조화되어 있지 않다.

---

## 5. VNFD1248F 구조

VNFD1248F는 DRV8912와 구현 방식은 다르지만 초기 설정과 runtime 상태 분리가 명확하다.

### 5.1 기본 레지스터 설정

```c
typedef struct
{
    uint16 w_data_len;
    const TS_EX_VIO_VNFD1248_WR_BITS *w_data;
} TS_EX_VIO_VNFD1248_DEF_CFG;
```

- `w_data_len`: 초기화할 register write 항목 수
- `w_data`: 변경되지 않는 초기 설정 배열

### 5.2 장치 PB 설정

```c
typedef struct
{
    TS_STD_SPI_INFO spi_info;

    uint16 port_hwlo;
    uint16 port_diag;
    uint16 port_din1;
    uint16 port_din2;
    uint16 port_outst1;
    uint16 port_outst2;

    uint16 dev_step_spi_ready;
    uint16 dev_step_wake_trig;
    uint16 dev_step_soft_reset;
    uint16 dev_step_read_all;
    uint16 dev_step_run;

    TS_EX_VIO_VNFD1248_DEF_CFG def_cfg;
} TS_EX_VIO_VNFD1248_PB_DEV;
```

PB 설정에는 다음 내용이 들어 있다.

- SPI 연결 정보
- 장치 GPIO 연결 정보
- 상태 머신의 단계 설정
- 기본 레지스터 설정

### 5.3 장치 runtime 정보

```c
typedef struct
{
    uint32 spi_trans_cnt;
    uint32 spi_error_cnt;

    uint8 spi_rx_buf[VNFD1248_SPI_PACKET_SIZE];

    uint8 cur_wd_trig;
    uint8 flg_wd_trig;

    uint16 dev_step;
    uint8 reg_gsb;

    uint32 pre_reg_devsr1;
    uint32 pre_reg_ch0sr1;
    uint32 pre_reg_ch1sr1;

    uint32 reg_value[VNFD1248_REG_MAX];
} TS_EX_VIO_VNFD1248_DEV_INFO;
```

각 멤버의 역할은 다음과 같다.

| 멤버 | 역할 |
|---|---|
| `spi_trans_cnt` | SPI 전송 횟수 |
| `spi_error_cnt` | SPI 오류 횟수 |
| `spi_rx_buf` | 최근 SPI 수신 원본 데이터 |
| `cur_wd_trig` | 현재 watchdog trigger 값 |
| `flg_wd_trig` | watchdog 처리 flag |
| `dev_step` | 상태 머신의 현재 단계 |
| `reg_gsb` | 현재 global status 정보 |
| `pre_reg_devsr1` | 이전 장치 status register |
| `pre_reg_ch0sr1` | 이전 채널 0 status register |
| `pre_reg_ch1sr1` | 이전 채널 1 status register |
| `reg_value[]` | 최신 전체 register cache |

### 5.4 VNFD1248F 구조의 특징

개념적인 구조는 다음과 같다.

```text
const PB configuration
├─ SPI/GPIO 연결
├─ 상태 머신 단계 설정
└─ 기본 레지스터 설정

runtime device info
├─ 현재 진행 단계
├─ 최근 RX buffer
├─ 최신 전체 register cache
├─ 이전 fault/status register
└─ 통신 횟수 및 오류 횟수
```

DRV8912와의 차이는 다음과 같다.

- DRV8912는 `r_reg`로 응답 저장 위치를 동적으로 지정한다.
- VNFD1248F는 `reg_value[]`라는 고정 register cache를 사용한다.
- DRV8912는 Daisy Chain 전체를 채널 단위로 관리한다.
- VNFD1248F는 장치별 runtime context가 더 직접적으로 보인다.

---

## 6. 기존 IC 드라이버 비교

| 구분 | MPQ6620A | DRV8912 | VNFD1248F |
|---|---|---|---|
| 고정 PB 설정 | 있음 | 있음 | 있음 |
| const 초기 레지스터 | 부분적으로 분리 | 명확히 분리 | `def_cfg`로 분리 |
| runtime 레지스터 | 설정 구조체 중심 | 전체 runtime table | `reg_value[]` |
| 현재 상태 | `cur_state` | `cur_state` | `dev_step` |
| 요청 상태 | `req_state` | command 상태로 관리 | 명시적 멤버 없음 |
| 이전 상태 | 명확하지 않음 | `ic_pre_status[]` | `pre_reg_*` |
| SPI RX buffer | 있음 | callback 수신값 사용 | context에 있음 |
| SPI TX buffer | 있음 | 있음 | 전송 함수 중심 |
| SPI 오류 횟수 | 확인 범위에서 없음 | `spi_error_cnt` | `spi_error_cnt` |
| Daisy Chain | 직접적인 순서 처리 약함 | 명시적으로 지원 | 장치별 context 중심 |
| Read 응답 저장 | RX buffer/개별 처리 | `r_reg` 동적 pointer | `reg_value[]` 고정 cache |

세 드라이버가 완전히 동일한 구조를 사용하지는 않는다. 하지만 다음 방향은 공통적이다.

```text
고정 연결 정보와 변경되는 상태를 분리한다.
초기 설정 원본과 runtime register 값을 분리한다.
현재 상태와 이전 상태를 필요에 따라 분리한다.
SPI 통신 buffer와 오류 상태를 runtime에서 관리한다.
```

---

## 7. 현재 TPS2HCS08-Q1 구조 평가

현재 TPS2HCS08-Q1 context는 설정 가능한 레지스터의 write shadow를 중심으로 한다.

```c
typedef struct
{
    /* shadow register: last written value */
    tTps2hcs08FaultMask faultMask;
    tTps2hcs08SwState   swState;
    tTps2hcs08DevConfig devConfig;
    tTps2hcs08AdcConfig adcConfig;
    tTps2hcs08Lpm       lpm;

    /* 다른 장치/채널 runtime 상태 */
    ...
} tTps2hcs08Ctx;
```

장점:

- IC register 이름과 bit field 의미가 분명하다.
- 설정값의 Read-Modify-Write가 쉽다.
- 장치별 context가 단순하다.
- 사용하지 않는 read-only register에 RAM을 쓰지 않는다.

개선이 필요한 부분:

- 데이터시트 Reset값과 프로젝트 초기 운용값의 역할 구분
- 변경되지 않는 초기 설정 원본과 runtime shadow 분리
- read-only register의 current/previous cache 정책
- 측정값의 valid/timestamp/stale 관리
- SPI 통신 횟수, 오류 횟수, pending 상태 관리
- Daisy Chain 전체의 공용 TX/RX buffer와 Wire slot 순서 관리

---

## 8. 신규 TPS2HCS08-Q1 권장 구조

TPS2HCS08-Q1은 레지스터별 명시적인 union 타입을 유지하면서 기존 드라이버의 장점을 조합하는 것이 적합하다.

### 8.1 고정 PB 및 topology

```c
typedef struct
{
    uint8 deviceCount;
    TS_STD_SPI_INFO spiInfo;

    uint8 logicalToWireSlot[TPS2HCS08_DEV_MAX];
    uint8 wireSlotToLogical[TPS2HCS08_DEV_MAX];
} tTps2hcs08ConstConfig;
```

이 구조체는 다음 정보를 표현한다.

- 하나의 CS에 연결된 실제 장치 수
- SPI 채널과 sequence 정보
- 논리 `devIdx`와 물리 Daisy Chain slot의 관계

실행 중 변경하지 않는다면 `const` 객체로 정의한다.

### 8.2 초기 운용 레지스터 설정

```c
typedef struct
{
    tTps2hcs08FaultMask faultMask;
    tTps2hcs08SwState   swState;
    tTps2hcs08DevConfig devConfig;
    tTps2hcs08AdcConfig adcConfig;
    tTps2hcs08Lpm       lpm;
} tTps2hcs08InitialConfig;
```

프로젝트 초기 운용값은 ROM에 둔다.

```c
static const tTps2hcs08InitialConfig
tps2hcs08InitialConfig[TPS2HCS08_DEV_MAX];
```

데이터시트 Reset값과 프로젝트 운용값은 용어와 상수로 구분하는 것이 좋다.

```c
#define TPS2HCS08_DEV_CONFIG_RESET_VALUE (0xF800u)

static const tTps2hcs08InitialConfig projectInitialConfig[];
```

### 8.3 장치별 runtime 상태

```c
typedef struct
{
    tTps2hcs08State state;

    /* 마지막으로 적용했거나 적용할 설정값 */
    tTps2hcs08InitialConfig configShadow;

    /* 최신 read-only register cache */
    uint16 registerCache[TPS2HCS08_REGISTER_COUNT];
    boolean registerValid[TPS2HCS08_REGISTER_COUNT];

    /* 이전 상태 비교 */
    uint16 previousGlobalFault;
    uint16 previousChannelFault[TPS2HCS08_CH_MAX];

    /* 측정값 */
    tTps2hcs08AdcResultVbb currentVbb;
    tTps2hcs08AdcResultVbb previousVbb;
    boolean vbbValid;
    uint32 vbbTimestamp;
} tTps2hcs08DeviceRuntime;
```

장치가 4개라면 장치별 runtime 객체도 네 개가 필요하다.

```c
static tTps2hcs08DeviceRuntime
tps2hcs08Device[TPS2HCS08_DEV_MAX];
```

### 8.4 Daisy Chain 전체의 SPI runtime

하나의 SPI I/F와 하나의 CS를 공유하므로 transport context는 장치별이 아니라 체인 전체에 하나가 적합하다.

```c
typedef struct
{
    boolean busy;

    uint8 pendingAddress[TPS2HCS08_DEV_MAX];
    boolean expectReadResult[TPS2HCS08_DEV_MAX];

    uint8 txBuffer[TPS2HCS08_CHAIN_BUFFER_SIZE];
    uint8 rxBuffer[TPS2HCS08_CHAIN_BUFFER_SIZE];

    uint32 transferCount;
    uint32 errorCount;
    uint32 sequence;
} tTps2hcs08ChainRuntime;
```

전체 관계는 다음과 같다.

```text
TPS2HCS08 Chain
├─ const topology
│  ├─ SPI 정보
│  ├─ deviceCount = 4
│  └─ logical/Wire slot mapping
│
├─ chain runtime 1개
│  ├─ 공용 TX/RX buffer
│  ├─ busy/pending 상태
│  └─ 통신 횟수 및 오류 횟수
│
└─ device runtime 4개
   ├─ device[0] config/cache/fault/measurement
   ├─ device[1] config/cache/fault/measurement
   ├─ device[2] config/cache/fault/measurement
   └─ device[3] config/cache/fault/measurement
```

### 8.5 모든 register cache가 반드시 필요한 것은 아님

VNFD1248F처럼 전체 register cache를 둘 수도 있지만, TPS2HCS08-Q1에서 실제 사용하는 read-only register만 저장할 수도 있다.

전체 cache 방식:

```c
uint16 registerCache[TPS2HCS08_REGISTER_COUNT];
```

장점:

- 전체 register dump가 쉽다.
- 진단과 read-back 검증이 편하다.
- 공통 read callback을 만들기 쉽다.

단점:

- RAM 사용량이 증가한다.
- register address가 연속적이지 않으면 mapping table이 필요하다.
- 어떤 값이 실제로 유효한지 별도 valid 상태가 필요하다.

선택 cache 방식:

```c
tTps2hcs08GlobalFault globalFault;
tTps2hcs08AdcResultVbb adcResultVbb;
tTps2hcs08AdcResultCh adcResultCh[TPS2HCS08_CH_MAX];
```

장점:

- 타입이 명확하다.
- RAM 사용량이 작다.
- 실제 요구사항만 구현한다.

단점:

- 새 register를 사용할 때마다 구조체를 변경해야 한다.
- 범용 register dump 구현이 어렵다.

TPS2HCS08-Q1에는 선택 cache 방식으로 시작하고, 전체 read-back 또는 진단 dump 요구가 확정되면 전체 cache를 추가하는 것이 현실적이다.

---

## 9. 권장 설계 원칙

### 9.1 const의 의미

`const`는 단순히 초기값이라는 뜻이 아니라 실행 중 변경되어서는 안 되는 정책 또는 topology라는 뜻으로 사용하는 것이 좋다.

적합한 대상:

- SPI channel 및 sequence
- CS/GPIO 연결 정보
- Daisy Chain 장치 수
- 논리 장치와 Wire slot mapping
- 프로젝트 초기 레지스터 설정 원본

부적합한 대상:

- 현재 상태
- 마지막 Read 결과
- 마지막 Write shadow
- SPI 오류 횟수
- pending transaction

### 9.2 초기 설정과 현재 설정 분리

```text
const initialConfig
        ↓ 복사 및 DB/정책 반영
runtime configShadow
        ↓ SPI Write
IC register
```

원본 초기 설정은 유지하고, 실제 변경되는 값은 runtime shadow에서 관리한다.

### 9.3 Write shadow와 Read cache 분리

```text
Write shadow
- MCU가 IC에 적용한 설정
- Read-Modify-Write 및 복원에 사용

Read cache
- IC가 생성한 상태/측정 결과
- 비교, 진단, 상위 전달에 사용
- SPI Write 경로에 전달하지 않음
```

### 9.4 current와 previous는 필요한 값에만 사용

모든 register에 previous 값을 둘 필요는 없다.

적합한 대상:

- fault 발생 edge 검출
- 전압/전류 변화량 계산
- watchdog 상태 변화
- 출력 상태 변화 진단

단순 조회만 필요한 설정 register에는 previous 값이 불필요할 수 있다.

### 9.5 장치 runtime과 chain runtime 분리

하나의 CS를 공유하는 Daisy Chain에서는 다음 구분이 중요하다.

```text
장치별 정보
- 설정값
- 측정값
- fault 상태
- 이전 상태

체인 공용 정보
- TX/RX buffer
- busy 상태
- pending command
- SPI sequence
- 체인 전체 오류
```

공용 buffer를 각 device context에 중복으로 두면 RAM 낭비와 동시성 문제가 발생할 수 있다.

---

## 10. 결론

기존 IC 드라이버들은 완전히 동일한 구조체 패턴을 사용하지 않지만 공통적으로 다음 네 가지 질문에 답하도록 구성되어 있다.

```text
무엇이 연결되어 있는가?
-> const PB/topology

처음 어떤 값을 설정할 것인가?
-> const initial register configuration

현재 IC 상태는 무엇인가?
-> device runtime/register cache/previous status

현재 어떤 통신이 진행 중인가?
-> chain transport/pending command/TX-RX buffer
```

- MPQ6620A는 현재 상태와 요청 상태, register 설정을 단순하게 분리한다.
- DRV8912는 Daisy Chain 전체 buffer, Wire 순서 변환, 비동기 응답 위치를 상세히 관리한다.
- VNFD1248F는 const 초기 설정과 전체 runtime register cache 및 이전 상태를 직관적으로 분리한다.

신규 TPS2HCS08-Q1 드라이버에는 다음 절충 구조가 적합하다.

```text
MPQ6620A의 명확한 현재/요청 상태
+ DRV8912의 Daisy Chain transport 및 순서 관리
+ VNFD1248F의 초기 설정/runtime/previous 분리
+ TPS2HCS08의 명시적인 register union 타입
```

즉 DRV8912 구조 전체를 그대로 복사하기보다, `const topology`, `const initial config`, `device runtime`, `chain runtime`의 네 계층으로 나누는 것이 가독성, 타입 안전성, Daisy Chain 지원 사이의 균형이 좋다.

