/*******************************************************************************
 *  File            : ExVioDb_Tps2hcs08.h
 *  Component       : SWC_EXVIODB / EX_VIO_DB
 *  Target Device   : Texas Instruments TPS2HCS08-Q1 (SLVSHR0 - MAY 2025)
 *                    8.9mOhm Automotive Dual-Channel SPI Controlled High-Side
 *                    Switch with I2T Wire Protection and Low Power Mode
 *  Description     : Register map / bit-field / type definition header.
 *                    All register related structure and type definitions of the
 *                    TPS2HCS08-Q1 driver are collected in this single header.
 *
 *  NOTE : Bit-fields are declared LSB first (little endian bit packing).
 *         A register is always transferred over SPI through the ".word" member.
 ******************************************************************************/
#ifndef EXVIODB_TPS2HCS08_H
#define EXVIODB_TPS2HCS08_H

/*==============================================================================
 *  INCLUDES
 *============================================================================*/
#include "Std_Types.h"          /* uint8 / uint16 / boolean / Std_ReturnType   */
#include <stdbool.h>

#ifndef TRUE
#define TRUE  ((boolean)true)
#endif

#ifndef FALSE
#define FALSE ((boolean)false)
#endif

#ifndef E_OK
#define E_OK     ((Std_ReturnType)0u)
#endif

#ifndef E_NOT_OK
#define E_NOT_OK ((Std_ReturnType)1u)
#endif

/*==============================================================================
 *  1. DEVICE / CHANNEL CONFIGURATION
 *============================================================================*/
#define TPS2HCS08_DEV_MAX                 (4u)   /* daisy-chain device count   */
#define TPS2HCS08_CH_MAX                  (2u)   /* CH1, CH2                   */

/* M-10: Daisy Chain Slot Mapping
 *
 * Physical Connection (datasheet p.24-25):
 *   MCU SDI → [Dev N-1] → ... → [Dev 1] → [Dev 0] → MCU SDO
 *
 * For TPS2HCS08_DEV_MAX = 4:
 *   MCU SDI → [Dev 3] → [Dev 2] → [Dev 1] → [Dev 0] → MCU SDO
 *
 * TX Buffer Order: [Slot 3][Slot 2][Slot 1][Slot 0]
 *   - First transmitted slot reaches last device (Dev 3)
 *   - Last transmitted slot reaches first device (Dev 0)
 *
 * Mapping Formula:
 *   wireSlot = (TPS2HCS08_DEV_MAX - 1) - devIdx
 *
 * Example:
 *   devIdx=0 → wireSlot=3 (transmitted first, reaches last device)
 *   devIdx=3 → wireSlot=0 (transmitted last, reaches first device)
 *
 * CRITICAL: Verify with actual hardware circuit diagram!
 * Match with Vehicle IO Signal DB IC column values.
 *
 * M-08: TODO - Bitfield layout verification
 * Add compile-time or unit tests to verify struct bit ordering:
 *   - Test: swState.word=0x0001 → CH1_ON=1, CH2_ON=0
 *   - Test: swState.word=0x0002 → CH1_ON=0, CH2_ON=1
 * Ensures compiler bit-field packing matches datasheet.
 */

// Mock SeqId TODO BSW Configuration generate
#define SeqId = 0u;

#define TPS2HCS08_CH1                     (0u)
#define TPS2HCS08_CH2                     (1u)

/* Device version (DEV_ID register 0h) */
#define TPS2HCS08_DEV_ID_VER_A            (0xFFF0u)
#define TPS2HCS08_DEV_ID_VER_B            (0xFFF1u)

/* M-16: Project target version - only Ver A is supported.
 * Ver B has different register behavior (e.g., CHx_ON control).
 * Reject Ver B chips explicitly during initialization.
 */
#define TPS2HCS08_TARGET_VERSION          TPS2HCS08_DEV_ID_VER_A

/*==============================================================================
 *  2. SPI FRAME DEFINITION  (24bit frame, CRC disabled)
 *      SDI  : [23]=R/W  [22:16]=ADDR[6:0]  [15:0]=DATA
 *      SDO  : [23:16]=GLOBAL_FAULT_TYPE[15:8]  [15:0]=DATA of PREVIOUS frame
 *      -> a register read always needs 2 transactions (Figure 8-8)
 *
 * M-13: CRC Mode (when CRC_EN=1):
 *      - Algorithm: CRC-4-ITU (NOT CRC-8!)
 *      - Polynomial: X^4 + X + 1
 *      - Initial: 0xF
 *      - Input: 24-bit frame (CMD + ADDR + DATA)
 *      - Output: 4-bit CRC appended as byte[3][3:0], byte[3][7:4]=0
 *        (clocks 25-28 = 0000 fixed, clocks 29-32 = CRC3..CRC0, Fig 8-10/8-11)
 *      - Frame length: 32-bit (4 bytes)
 *      - Reference: Datasheet p.26 (algorithm), p.27 (frame placement)
 *============================================================================*/
#define TPS2HCS08_SPI_FRAME_LEN           (3u)   /* 24bit = 3 byte             */
#define TPS2HCS08_SPI_FRAME_LEN_CRC       (4u)   /* 32bit = 4 byte (CRC_EN=1)  */

#define TPS2HCS08_SPI_CMD_READ            (0x00u)
#define TPS2HCS08_SPI_CMD_WRITE           (0x80u)
#define TPS2HCS08_SPI_ADDR_MASK           (0x7Fu)

/*==============================================================================
 *  3. REGISTER OFFSET (Table 8-13)
 *============================================================================*/
#define TPS2HCS08_REG_DEV_ID              (0x00u)
#define TPS2HCS08_REG_CRC_CONFIG          (0x01u)
#define TPS2HCS08_REG_SLEEP               (0x02u)
#define TPS2HCS08_REG_LPM                 (0x03u)
#define TPS2HCS08_REG_GLOBAL_FAULT_TYPE   (0x04u)
#define TPS2HCS08_REG_FAULT_MASK          (0x05u)
#define TPS2HCS08_REG_SW_STATE            (0x07u)
#define TPS2HCS08_REG_DEV_CONFIG          (0x09u)
#define TPS2HCS08_REG_ADC_CONFIG          (0x0Au)
#define TPS2HCS08_REG_ADC_RESULT_VBB      (0x0Bu)

/* --- Channel 1 base ------------------------------------------------------- */
#define TPS2HCS08_REG_FLT_STAT_CH1        (0x0Du)
#define TPS2HCS08_REG_PWM_CH1             (0x0Eu)
#define TPS2HCS08_REG_ILIM_CONFIG_CH1     (0x0Fu)
#define TPS2HCS08_REG_CH1_CONFIG          (0x10u)
#define TPS2HCS08_REG_ADC_RESULT_CH1_I    (0x11u)
#define TPS2HCS08_REG_ADC_RESULT_CH1_T    (0x12u)
#define TPS2HCS08_REG_ADC_RESULT_CH1_V    (0x13u)
#define TPS2HCS08_REG_ADC_RESULT_CH1_VDS  (0x14u)
#define TPS2HCS08_REG_I2T_CONFIG_CH1      (0x15u)

/* --- Channel 2 base ( = CH1 + 0x09 ) -------------------------------------- */
#define TPS2HCS08_REG_CH_OFFSET           (0x09u)
#define TPS2HCS08_REG_FLT_STAT_CH2        (0x16u)
#define TPS2HCS08_REG_PWM_CH2             (0x17u)
#define TPS2HCS08_REG_ILIM_CONFIG_CH2     (0x18u)
#define TPS2HCS08_REG_CH2_CONFIG          (0x19u)
#define TPS2HCS08_REG_ADC_RESULT_CH2_I    (0x1Au)
#define TPS2HCS08_REG_ADC_RESULT_CH2_T    (0x1Bu)
#define TPS2HCS08_REG_ADC_RESULT_CH2_V    (0x1Cu)
#define TPS2HCS08_REG_ADC_RESULT_CH2_VDS  (0x1Du)
#define TPS2HCS08_REG_I2T_CONFIG_CH2      (0x1Eu)

/* Channel indexed register address helper ( chIdx : 0=CH1, 1=CH2 )           */
#define TPS2HCS08_CH_REG(ch1Addr, chIdx) \
            ((uint8)((ch1Addr) + ((uint8)(chIdx) * TPS2HCS08_REG_CH_OFFSET)))

/* Dummy address used for wake-up / dummy read (does not exist in the device) */
#define TPS2HCS08_REG_DUMMY               (0x7Fu)

/*==============================================================================
 *  4. REGISTER BIT-FIELD DEFINITION
 *============================================================================*/

/*--- 0h : DEV_ID -----------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned DEVICE_ID              : 16;   /* [15:0]   */
    } bits;
} tTps2hcs08DevId;

/*--- 1h : CRC_CONFIG -------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned CRC_EN                 : 1;    /* [0]      */
        unsigned RESERVED               : 15;   /* [15:1]   */
    } bits;
} tTps2hcs08CrcConfig;

/*--- 2h : SLEEP ------------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned SLEEP                  : 1;    /* [0]      */
        unsigned RESERVED               : 15;   /* [15:1]   */
    } bits;
} tTps2hcs08Sleep;

/*--- 3h : LPM --------------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned MANUAL_LPM_ENTRY       : 1;    /* [0]      */
        unsigned AUTO_LPM_EXIT_CH1      : 1;    /* [1]      */
        unsigned AUTO_LPM_EXIT_CH2      : 1;    /* [2]      */
        unsigned MAN_LPM_EXIT_CURR_CH1  : 2;    /* [4:3]    */
        unsigned MAN_LPM_EXIT_CURR_CH2  : 2;    /* [6:5]    */
        unsigned RESERVED               : 9;    /* [15:7]   */
    } bits;
} tTps2hcs08Lpm;

/*--- 4h : GLOBAL_FAULT_TYPE ------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned VBB_UVLO               : 1;    /* [0]  RC  */
        unsigned VBB_UV_WRN             : 1;    /* [1]  RC  */
        unsigned VDD_UVLO               : 1;    /* [2]  RC  */
        unsigned WD_ERR                 : 1;    /* [3]  RC  */
        unsigned SPI_ERR                : 1;    /* [4]  RC  */
        unsigned LPM_STATUS_1           : 1;    /* [5]  RC  */
        unsigned POR                    : 1;    /* [6]  RC  */
        unsigned LIMPHOME_STAT          : 1;    /* [7]  W1C */
        unsigned GLOBAL_ERR_WRN         : 1;    /* [8]      */
        unsigned OL_SHRT_VBB_OFF_FLT    : 1;    /* [9]      */
        unsigned CHAN_OCP_I2T_TSD       : 1;    /* [10]     */
        unsigned LPM_STATUS             : 1;    /* [11]     */
        unsigned CH1_FLT                : 1;    /* [12]     */
        unsigned CH2_FLT                : 1;    /* [13]     */
        unsigned RESERVED               : 2;    /* [15:14]  */
    } bits;
} tTps2hcs08GlobalFaultType;

/* bit position of GLOBAL_FAULT_TYPE (log latch / one shot output control)     */
#define TPS2HCS08_GF_BIT_VBB_UVLO         (0u)
#define TPS2HCS08_GF_BIT_VBB_UV_WRN       (1u)
#define TPS2HCS08_GF_BIT_VDD_UVLO         (2u)
#define TPS2HCS08_GF_BIT_WD_ERR           (3u)
#define TPS2HCS08_GF_BIT_SPI_ERR          (4u)
#define TPS2HCS08_GF_BIT_LPM_STATUS_1     (5u)
#define TPS2HCS08_GF_BIT_POR              (6u)
#define TPS2HCS08_GF_BIT_LIMPHOME_STAT    (7u)
#define TPS2HCS08_GF_BIT_GLOBAL_ERR_WRN   (8u)
#define TPS2HCS08_GF_BIT_OL_SHRT_VBB_OFF  (9u)
#define TPS2HCS08_GF_BIT_CHAN_OCP_I2T_TSD (10u)
#define TPS2HCS08_GF_BIT_LPM_STATUS       (11u)
#define TPS2HCS08_GF_BIT_CH1_FLT          (12u)
#define TPS2HCS08_GF_BIT_CH2_FLT          (13u)

/*--- 5h : FAULT_MASK -------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned MASK_VBB_UVLO          : 1;    /* [0]      */
        unsigned MASK_WD_ERR            : 1;    /* [1]      */
        unsigned MASK_SPI_ERR           : 1;    /* [2]      */
        unsigned RESERVED_3             : 1;    /* [3]      */
        unsigned MASK_OL_OFF            : 1;    /* [4]      */
        unsigned MASK_SHRT_VBB          : 1;    /* [5]      */
        unsigned RESERVED_6             : 1;    /* [6]      */
        unsigned RESERVED               : 9;    /* [15:7]   */
    } bits;
} tTps2hcs08FaultMask;

/*--- 7h : SW_STATE ---------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned CH1_ON                 : 1;    /* [0]      */
        unsigned CH2_ON                 : 1;    /* [1]      */
        unsigned RESERVED               : 14;   /* [15:2]   */
    } bits;
} tTps2hcs08SwState;

/*--- 9h : DEV_CONFIG -------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned FLT_LTCH_DIS           : 1;    /* [0]      */
        unsigned WD_TO                  : 2;    /* [2:1]    */
        unsigned WD_EN                  : 1;    /* [3]      */
        unsigned PARALLEL_12            : 1;    /* [4]      */
        unsigned AUTO_LPM_ENTRY         : 1;    /* [5]      */
        unsigned PWM_SHIFT_DIS          : 1;    /* [6]      */
        unsigned CH1_LH_IN              : 2;    /* [8:7]    */
        unsigned CH2_LH_IN              : 2;    /* [10:9]   */
        unsigned RESERVED               : 5;    /* [15:11]  */
    } bits;
} tTps2hcs08DevConfig;

/*--- Ah : ADC_CONFIG -------------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned ADC_DIS                : 1;    /* [0]      */
        unsigned ADC_VBB_DIS            : 1;    /* [1]      */
        unsigned ADC_ISNS_DIS           : 1;    /* [2]      */
        unsigned ADC_TSNS_DIS           : 1;    /* [3]      */
        unsigned ADC_VSNS_DIS           : 1;    /* [4]      */
        unsigned ADC_VDS_DIS            : 1;    /* [5]      */
        unsigned ADC_ISNS_SAMPLE_CONFIG : 2;    /* [7:6]    */
        unsigned RESERVED               : 8;    /* [15:8]   */
    } bits;
} tTps2hcs08AdcConfig;

/*--- Bh : ADC_RESULT_VBB ---------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned ADC_RESULT_VBB         : 10;   /* [9:0]    */
        unsigned VBB_RDY                : 1;    /* [10]     */
        unsigned RESERVED               : 5;    /* [15:11]  */
    } bits;
} tTps2hcs08AdcResultVbb;

/*--- Dh / 16h : FLT_STAT_CHx -----------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned THERMAL_WRN_CHx        : 1;    /* [0]  RC  */
        unsigned RESERVED_1             : 1;    /* [1]      */
        unsigned OL_OFF_CHx             : 1;    /* [2]  RC  */
        unsigned SHRT_VBB_CHx           : 1;    /* [3]  RC  */
        unsigned ILIMIT_CHx             : 1;    /* [4]  RC  */
        unsigned THERMAL_SD_CHx         : 1;    /* [5]  RC  */
        unsigned LPM_WAKE_CHx           : 1;    /* [6]  RC  */
        unsigned I2T_FLT_CHx            : 1;    /* [7]  RC  */
        unsigned VOUT_ERR_CHx           : 1;    /* [8]      */
        unsigned SW_STATE_STAT_CHx      : 1;    /* [9]      */
        unsigned FLT_CHx                : 1;    /* [10]     */
        unsigned LATCH_STAT_CHx         : 1;    /* [11]     */
        unsigned I2T_MOD_CHx            : 1;    /* [12]     */
        unsigned RESERVED               : 3;    /* [15:13]  */
    } bits;
} tTps2hcs08FltStatCh;

/* bit position of FLT_STAT_CHx (log latch / one shot output control)          */
#define TPS2HCS08_FS_BIT_THERMAL_WRN      (0u)
#define TPS2HCS08_FS_BIT_OL_OFF           (2u)
#define TPS2HCS08_FS_BIT_SHRT_VBB         (3u)
#define TPS2HCS08_FS_BIT_ILIMIT           (4u)
#define TPS2HCS08_FS_BIT_THERMAL_SD       (5u)
#define TPS2HCS08_FS_BIT_LPM_WAKE         (6u)
#define TPS2HCS08_FS_BIT_I2T_FLT          (7u)
#define TPS2HCS08_FS_BIT_VOUT_ERR         (8u)
#define TPS2HCS08_FS_BIT_SW_STATE_STAT    (9u)
#define TPS2HCS08_FS_BIT_FLT_CH           (10u)
#define TPS2HCS08_FS_BIT_LATCH_STAT       (11u)
#define TPS2HCS08_FS_BIT_I2T_MOD          (12u)

/*--- Eh / 17h : PWM_CHx ----------------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned PWM_EN_CHx             : 1;    /* [0]      */
        unsigned PWM_DTY_CHx            : 8;    /* [8:1]    */
        unsigned PWM_FREQ_CHx           : 3;    /* [11:9]   */
        unsigned RESERVED               : 4;    /* [15:12]  */
    } bits;
} tTps2hcs08PwmCh;

/*--- Fh / 18h : ILIM_CONFIG_CHx --------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned ILIMIT_SET_CHx         : 4;    /* [3:0]    */
        unsigned INRUSH_LIMIT_CHx       : 4;    /* [7:4]    */
        unsigned INRUSH_DURATION_CHx    : 3;    /* [10:8]   */
        unsigned I2T_EN_CHx             : 1;    /* [11]     */
        unsigned CAP_CHRG_CHx           : 2;    /* [13:12]  */
        unsigned RESERVED               : 2;    /* [15:14]  */
    } bits;
} tTps2hcs08IlimConfigCh;

/*--- 10h / 19h : CHx_CONFIG ------------------------------------------------*/
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
        /* M-07: Datasheet has typo - p.86 says "VDS_SNS_DIS_CH1", p.99 says "VDSSNS_DIS_CH2".
         * Both refer to same bit[14], same function. Using unified name VDS_SNS_DIS_CHx.
         * CH1/CH2 share this type definition - verified identical bit layout.
         */
        unsigned VDS_SNS_DIS_CHx        : 1;    /* [14]     */
        unsigned VSNS_DIS_CHx           : 1;    /* [15]     */
    } bits;
} tTps2hcs08ChConfig;

/*--- 11h / 1Ah : ADC_RESULT_CHx_I ------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned ADC_RESULT_CHx_I       : 10;   /* [9:0]    */
        unsigned ISNS_RDY_CHx           : 1;    /* [10]     */
        unsigned ISNS_SCALE_EFF_CHx     : 1;    /* [11]     */
        unsigned RESERVED               : 4;    /* [15:12]  */
    } bits;
} tTps2hcs08AdcResultChI;

/*--- 12h/1Bh : _T , 13h/1Ch : _V , 14h/1Dh : _VDS --------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned ADC_RESULT_CHx         : 10;   /* [9:0]    */
        unsigned RDY_CHx                : 1;    /* [10]     */
        unsigned RESERVED               : 5;    /* [15:11]  */
    } bits;
} tTps2hcs08AdcResultCh;

/*--- 15h / 1Eh : I2T_CONFIG_CHx --------------------------------------------*/
typedef union
{
    uint16 word;
    struct
    {
        unsigned NOM_CUR_CHx            : 3;    /* [2:0]    */
        unsigned I2T_TRIP_CHx           : 4;    /* [6:3]    */
        unsigned ISWCL_CHx              : 2;    /* [8:7]    */
        unsigned SWCL_DLY_TMR_CHx       : 2;    /* [10:9]   */
        unsigned RESERVED               : 3;    /* [13:11]  */
        unsigned TCLDN_CHx              : 2;    /* [15:14]  */
    } bits;
} tTps2hcs08I2tConfigCh;

/*==============================================================================
 *  5. REGISTER FIELD SETTING VALUE
 *============================================================================*/
/* DEV_CONFIG.CHx_LH_IN */
#define TPS2HCS08_LH_IN_BY_DI_PIN         (0x0u)
#define TPS2HCS08_LH_IN_KEEP_CHx_ON       (0x1u)  /* << project default        */
#define TPS2HCS08_LH_IN_OFF               (0x2u)
#define TPS2HCS08_LH_IN_ON                (0x3u)

/* DEV_CONFIG.WD_TO - CRITICAL: Watch the unit difference!
 * Datasheet p.28 Table 8-4: 00b=400µs / 01b=400ms
 * Unit trap: microseconds vs milliseconds - easy to confuse!
 */
#define TPS2HCS08_WD_TO_400US             (0x0u)  /* 400 microseconds (NOT ms!) */
#define TPS2HCS08_WD_TO_400MS             (0x1u)  /* 400 milliseconds - project default */
#define TPS2HCS08_WD_TO_800MS             (0x2u)
#define TPS2HCS08_WD_TO_1200MS            (0x3u)

/* CHx_CONFIG.OL_SVBB_EN */
#define TPS2HCS08_OL_SVBB_DISABLE         (0x0u)
#define TPS2HCS08_OL_SVBB_PULLDOWN        (0x1u)  /* short-to-VBB detect       */
#define TPS2HCS08_OL_SVBB_PULLUP          (0x2u)  /* open-load / STB detect    */
#define TPS2HCS08_OL_SVBB_COMP_ONLY       (0x3u)

/* CHx_CONFIG.OL_SVBB_BLANK */
#define TPS2HCS08_OL_BLANK_0P4MS          (0x0u)
#define TPS2HCS08_OL_BLANK_1P0MS          (0x1u)
#define TPS2HCS08_OL_BLANK_2P0MS          (0x2u)
#define TPS2HCS08_OL_BLANK_4P0MS          (0x3u)  /* << project default        */
#define TPS2HCS08_OL_BLANK_TIME_MS        (4u)

/* CHx_CONFIG.OL_PU_STR */
#define TPS2HCS08_OL_PU_STR_26U5A         (0x0u)
#define TPS2HCS08_OL_PU_STR_60UA          (0x1u)
#define TPS2HCS08_OL_PU_STR_127UA         (0x2u)
#define TPS2HCS08_OL_PU_STR_260UA         (0x3u)

/* ILIM_CONFIG_CHx.CAP_CHRG */
#define TPS2HCS08_CAP_CHRG_NONE           (0x0u)  /* IOCP only                 */
#define TPS2HCS08_CAP_CHRG_CUR_REG        (0x2u)  /* current limit regulation  */

/* I2T_CONFIG_CHx.TCLDN */
#define TPS2HCS08_TCLDN_INDEFINITE        (0x0u)
#define TPS2HCS08_TCLDN_0P8S              (0x1u)
#define TPS2HCS08_TCLDN_2P0S              (0x2u)
#define TPS2HCS08_TCLDN_4P0S              (0x3u)

/* SW_STATE / LPM control value */
#define TPS2HCS08_CH_OFF                  (0u)
#define TPS2HCS08_CH_ON                   (1u)

/* register field max value (range check of the DB mapping result)             */
#define TPS2HCS08_MAX_ILIMIT_SET          (0x0Au)  /* 10A  ~ 55A               */
#define TPS2HCS08_MAX_INRUSH_LIMIT_OCP    (0x0Au)  /* CAP_CHRG = 00            */
#define TPS2HCS08_MAX_INRUSH_LIMIT_REG    (0x0Cu)  /* CAP_CHRG = 10            */
#define TPS2HCS08_MAX_INRUSH_DURATION     (0x07u)  /* 0ms  ~ 100ms             */
#define TPS2HCS08_MAX_PWM_FREQ            (0x07u)  /* 0.8Hz ~ 1770Hz           */
#define TPS2HCS08_MAX_SLRT                (0x03u)  /* 0.25 ~ 0.55 V/us         */
#define TPS2HCS08_MAX_NOM_CUR             (0x07u)  /* 4.0A ~ 15.0A             */
#define TPS2HCS08_MAX_I2T_TRIP            (0x0Fu)  /* 8.8A2s ~ 350A2s          */
#define TPS2HCS08_MAX_ISWCL               (0x03u)  /* 19.55A ~ 13.3A           */

/*==============================================================================
 *  6. VEHICLE IO SIGNAL DB PARAMETER DEFINITION
 *============================================================================*/
/* CAT_1 : reuse the production DB's E-FUSE category (DB_CAT1_E_FUSE = 5).   */
#define DB_CAT1_E_FUSE_TPS2HCS08          (5u)
/* CAT_2 : TPS2HCS08-Q1 assigned signal uses ID 0 (Active High)               */
#define DB_CAT2_ACTIVE_HIGH               (0u)

/* PIN : IC output channel                                                    */
#define DB_PIN_IC_PIN_1                   (1u)    /* -> CH1 */
#define DB_PIN_IC_PIN_2                   (2u)    /* -> CH2 */

/* PWM parameter type ( decides how PWM_Duty is interpreted )                 */
typedef enum
{
    DB_PWM_TYPE_NONE = 0,     /* PWM not used                                 */
    DB_PWM_TYPE_C,            /* PWM_C : Duty -> INRUSH_LIMIT (CAP_CHRG = 10) */
    DB_PWM_TYPE_X,            /* PWM_X : Duty -> INRUSH_LIMIT (CAP_CHRG = 00) */
    DB_PWM_TYPE_O             /* PWM_O : Duty -> PWM_DTY_CHx (1:1)            */
} tDbPwmType;

/* Mapping table entry : DB parameter ID -> IC register field value           */
typedef struct
{
    uint8   dbId;             /* vehicle IO signal DB parameter ID            */
    uint8   regValue;         /* TPS2HCS08-Q1 register field value            */
} tTps2hcs08MapEntry;

/*==============================================================================
 *  7. CHANNEL CONFIGURATION (result of DB parsing)
 *============================================================================*/
typedef struct
{
    boolean     used;                 /* USED     : channel is assigned       */
    uint16      sigIdx;               /* vehicle IO signal DB record index    */
    boolean     bPlusAlways;          /* B+ always on channel (process #8)    */
    boolean     defValueOn;           /* DEF_Value: initial output level      */
    boolean     parallel;             /* MOC      : CH1/CH2 parallel operation*/
    boolean     volDetUse;            /* VOL_DET  : VOUT sensing use          */
    boolean     oldUse;               /* OLD      : off-state open load detect*/
    tDbPwmType  pwmType;              /* PWM      : PWM_C / PWM_X / PWM_O     */
    uint8       pwmFreq;              /* PWM_F    -> PWM_FREQ_CHx    [11:9]   */
    uint8       pwmDuty;              /* PWM_Duty -> PWM_DTY_CHx     [8:1]    */
    uint8       inrushLimit;          /* PWM_Duty -> INRUSH_LIMIT_CHx[7:4]    */
    uint8       inrushDuration;       /* CT       -> INRUSH_DURATION [10:8]   */
    uint8       capChrg;              /* CT       -> CAP_CHRG_CHx    [13:12]  */
    uint8       ilimitSet;            /* OCP      -> ILIMIT_SET_CHx  [3:0]    */
    uint8       slewRate;             /* SR       -> SLRT_CHx        [1:0]    */
    uint8       iswcl;                /* OCP      -> ISWCL_CHx       [8:7]    */
    uint8       i2tTrip;              /* OCP      -> I2T_TRIP_CHx    [6:3]    */
    uint8       nomCur;               /* OCP      -> NOM_CUR_CHx     [2:0]    */
} tTps2hcs08ChCfg;

/*==============================================================================
 *  8. DIAGNOSTIC RESULT
 *============================================================================*/
typedef enum
{
    TPS2HCS08_DIAG_NOT_EXECUTED = 0,
    TPS2HCS08_DIAG_NONE,              /* no fault                             */
    TPS2HCS08_DIAG_OPEN_LOAD,         /* off-state open load                  */
    TPS2HCS08_DIAG_SHORT_VBB          /* off-state short to VBB               */
} tTps2hcs08DiagResult;

/*==============================================================================
 *  9. STATE MACHINE DEFINITION
 *============================================================================*/
/* setup scan state (used in EXVIODB_STATE_EXVIO_SETUP)                       */
typedef enum
{
    TPS2HCS08_SETUP_SCN_SET_DEF = 0,      /*     register default value set   */
    TPS2HCS08_SETUP_SCN_DB_PARSING,       /*     vehicle IO signal DB parsing */
    TPS2HCS08_SETUP_SCN_WAKEUP,           /* #2,#3 SLEEP -> INIT&ABIST(CSN=0) */
    TPS2HCS08_SETUP_SCN_WAIT_READY,       /* #4  tREADY(65us) wait -> CONFIG  */
    TPS2HCS08_SETUP_SCN_CLEAR_POR,        /* #4  GLOBAL_FAULT_TYPE read/clear */
    TPS2HCS08_SETUP_SCN_CONFIG_WRITE,     /* #5  register write               */
    TPS2HCS08_SETUP_SCN_CONFIG_VERIFY,    /* #5  register read back verify    */
    TPS2HCS08_SETUP_SCN_DIAG_PULLDOWN,    /* #6  Open/Short diag : discharge  */
    TPS2HCS08_SETUP_SCN_DIAG_PULLUP,      /* #6  Open/Short diag : pull-up    */
    TPS2HCS08_SETUP_SCN_DIAG_JUDGE_OL,    /* #6  Open/Short diag : OL judge   */
    TPS2HCS08_SETUP_SCN_DIAG_JUDGE_STB,   /* #6  Open/Short diag : STB judge  */
    TPS2HCS08_SETUP_SCN_DIAG_REPORT,      /* #6  diagnostic report output     */
    TPS2HCS08_SETUP_SCN_ACTIVE_ENTRY,     /* #8  B+ always on channel ON      */
    TPS2HCS08_SETUP_SCN_COMPLETE,         /*     setup scan complete          */
    TPS2HCS08_SETUP_SCN_ERROR             /* Phase 2: Issue #5 & #6 - Fatal error state */
} tTps2hcs08SetupScnState;

/* run state (used in EXVIODB_STATE_RUN)                                      */
typedef enum
{
    TPS2HCS08_RUN_INIT = 0,               /* M-05: Initial state after reset/power-on */
    TPS2HCS08_RUN_ACTIVE,                 /* #9,#10 normal ACTIVE operation   */
    TPS2HCS08_RUN_LPM_PREPARE,            /* #11 sleep entry preparation      */
    TPS2HCS08_RUN_LPM_ENTRY,              /* #12 AUTO_LPM_ENTRY = 1           */
    TPS2HCS08_RUN_LPM_WAIT_STATUS,        /* #13,#14 LPM_STATUS = 1 wait      */
    TPS2HCS08_RUN_LPM_ACTIVE,             /* #15,#16 AUTO_LPM standby         */
    TPS2HCS08_RUN_LPM_EXIT,               /* #17 AUTO_LPM_EXIT_CHx = 1        */
    TPS2HCS08_RUN_LPM_RESTORE             /* #18,#19 restore then goto ACTIVE */
} tTps2hcs08RunState;

/* setup scan complete return value (same usage as VNFD1248_COMPLETE)         */
#define TPS2HCS08_COMPLETE                (1u)
#define TPS2HCS08_BUSY                    (0u)

/*==============================================================================
 * 10. RETRY COUNTER (Phase 2: Issue #2 & #7)
 *============================================================================*/
/* Retry counters for robustness against transient SPI errors.
 * Each setup scan phase tracks retry attempts per device.
 * Exceeding max retries transitions to ERROR state.
 */
typedef struct
{
    uint8 configWrite;      /* Write configuration registers to chip */
    uint8 configVerify;     /* Read-back verification of config */
    uint8 devIdRead;        /* DEV_ID register read during detection */
    uint8 diagRead;         /* Diagnostic register read failures */
} tTps2hcs08RetryCounters;

/* Maximum retry limits before declaring failure */
#define TPS2HCS08_MAX_RETRY_CONFIG_WRITE   (5u)   /* Config write attempts */
#define TPS2HCS08_MAX_RETRY_CONFIG_VERIFY  (10u)  /* Verification attempts */
#define TPS2HCS08_MAX_RETRY_DEV_ID_READ    (5u)   /* DEV_ID read attempts */
#define TPS2HCS08_MAX_RETRY_DIAG_READ      (3u)   /* Diagnostic read attempts */

/* Phase 2: Issue #9 - Execution time monitoring statistics.
 * Tracks RunScan execution time to detect performance issues.
 * NOTE: Requires GetMicroseconds() function from BSW/HAL layer.
 */
typedef struct
{
    uint32 lastExecTime_us;  /* Last execution time in microseconds */
    uint32 maxExecTime_us;   /* Maximum execution time ever recorded */
    uint32 avgExecTime_us;   /* Moving average (exponential, factor 7/8) */
    uint32 execCount;        /* Total execution count */
} tTps2hcs08ExecStats;

/*==============================================================================
 * 11. DEVICE CONTEXT
 *============================================================================*/
typedef struct
{
    uint16 signalId;

    /* Vehicle IO DB metadata */
    uint8 cat1;
    uint8 cat2;
    uint8 sc;
    uint8 ic;
    uint8 pin;

    /* State required by dependent mappings */
    uint8 used;
    uint8 pwmMode[TPS2HCS08_CH_MAX];

    /* --- shadow register (last written value) ---------------------------- */
    tTps2hcs08DevId             devId;
    tTps2hcs08CrcConfig         crcConfig;
    tTps2hcs08Sleep             sleep;
    tTps2hcs08Lpm               lpm;

    /* --- last read status ------------------------------------------------ */
    tTps2hcs08GlobalFaultType   globalFault;
    /* M-06: SDO header from last SPI transaction.
     * Contains GLOBAL_FAULT_TYPE[15:8] latched at CS falling edge (datasheet p.26).
     * Updated on every SPI transaction for immediate fault detection.
     */
    uint8                       sdoHeader;
    tTps2hcs08FaultMask         faultMask;
    tTps2hcs08SwState           swState;
    tTps2hcs08DevConfig         devConfig;
    tTps2hcs08AdcConfig         adcConfig;
    tTps2hcs08AdcResultVbb      adcResultVbb;

    /* --- channel status -------------------------------------------------- */
    tTps2hcs08FltStatCh         fltStatCh[TPS2HCS08_CH_MAX];
    tTps2hcs08PwmCh             pwmCh[TPS2HCS08_CH_MAX];
    tTps2hcs08IlimConfigCh      ilimCfgCh[TPS2HCS08_CH_MAX];
    tTps2hcs08ChConfig          chConfig[TPS2HCS08_CH_MAX];
    tTps2hcs08AdcResultChI      adcResultChI[TPS2HCS08_CH_MAX];
    tTps2hcs08AdcResultCh       adcResultChT[TPS2HCS08_CH_MAX];
    tTps2hcs08AdcResultCh       adcResultChV[TPS2HCS08_CH_MAX];
    tTps2hcs08AdcResultCh       adcResultChVDS[TPS2HCS08_CH_MAX];
    tTps2hcs08I2tConfigCh       i2tCfgCh[TPS2HCS08_CH_MAX];
    
    /* Mock read-register image for static verification */
    uint16                      mockReadReg[0x20u];

    /* Mock/static verification */
    uint32 writeCount;
    boolean lastWriteValid;
    uint8 lastSeqid;
    uint8 lastAddr;
    uint16 lastPayload;

#pragma region TOOD Check
    /* --- DB parsing result ----------------------------------------------- */
    tTps2hcs08ChCfg             chCfg[TPS2HCS08_CH_MAX];

    /* --- diagnostic result ----------------------------------------------- */
    tTps2hcs08DiagResult        diagResult[TPS2HCS08_CH_MAX];

    /* --- ADC monitoring value (for application SW) ----------------------- */
    uint16                      adcIsns[TPS2HCS08_CH_MAX];
    uint16                      adcTsns[TPS2HCS08_CH_MAX];
    uint16                      adcVsns[TPS2HCS08_CH_MAX];
    uint16                      adcVds[TPS2HCS08_CH_MAX];

    /* --- one shot log latch ---------------------------------------------- */
    uint16                      logLatchGlobal;
    uint16                      logLatchCh[TPS2HCS08_CH_MAX];
    boolean                     porCleared;
    boolean                     lpmStatus1Cleared;

    boolean                     devPresent;
#pragma endregion
} tTps2hcs08Ctx;

/* Shared by the DB driver and mapping layer; storage is owned by the DB. */
extern tTps2hcs08Ctx exVioDbTps2hcs08Ctx[TPS2HCS08_DEV_MAX];

/* These APIs perform chain-wide I/O using a common payload/read result. */
extern Std_ReturnType ExVioDb_WriteRegister_Tps2hcs08(
    uint8 seqid, uint8 addr, uint16 payload);
extern Std_ReturnType ExVioDb_ReadRegister_Tps2hcs08(
    uint8 seqid, uint8 addr, uint16 *readValue);

#define TPS2HCS08_SPI_FRAME_LEN_MAX      TPS2HCS08_SPI_FRAME_LEN

#define TPS2HCS08_CHAIN_BUF_LEN_MAX \
    (TPS2HCS08_SPI_FRAME_LEN_MAX * TPS2HCS08_DEV_MAX)

typedef struct
{
    uint8 txData[TPS2HCS08_CHAIN_BUF_LEN_MAX];
    uint8 rxData[TPS2HCS08_CHAIN_BUF_LEN_MAX];
    uint8 frameLen;
    uint8 deviceCount;
} tTps2hcs08SpiRuntime;

/*==============================================================================
 * 12. PUBLIC API
 *============================================================================*/
extern void  ExVioDb_InitRegValue_LoadDb(void);
extern void  ExVioDb_InitRegValue_Tps2hcs08(void);
extern void  ExVioDb_SetupScnTps2hcs08Reg(void);
extern void  ExVioDb_RunScnTps2hcs08Reg(void);
extern uint8 ExVioDb_GetSetupScnState_Tps2hcs08(void);

/* channel output control interface for application SW / signal DB            */
extern Std_ReturnType ExVioDb_SetChannelOutput_Tps2hcs08(uint8 devIdx,
                                                         uint8 chIdx,
                                                         boolean onOff);

/* sleep / wake-up request interface from the vehicle power mode manager       */
extern void ExVioDb_ReqSleep_Tps2hcs08(boolean req);
extern void ExVioDb_ReqWakeUp_Tps2hcs08(void);

/* ADC monitoring value read interface for application SW                      */
extern Std_ReturnType ExVioDb_GetAdcValue_Tps2hcs08(uint8 devIdx,
                                                    uint8 chIdx,
                                                    uint16 *isns,
                                                    uint16 *tsns,
                                                    uint16 *vds);

#endif /* EXVIODB_TPS2HCS08_H */
