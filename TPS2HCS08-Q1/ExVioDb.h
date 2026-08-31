/*******************************************************************************
 *  File            : ExVioDb.h
 *  Component       : SWC_EXVIODB / EX_VIO_DB
 *  Description     : Component level (one layer above the device driver)
 *                    interface of the external vehicle IO database SWC.
 *
 *                    layer structure
 *                      RTE
 *                       |  RE_Swc_ExVioDb_Init() / RE_Swc_ExVioDb_Task_10ms()
 *                       v
 *                      ExVioDb.c            <-- this layer
 *                       |  ExVioDb_SetupScnXxxReg() / ExVioDb_RunScnXxxReg()
 *                       v
 *                      ExVioDb_Tps2hcs08.c  ( TIC12400 / DRV8912 / MPQ6620 /
 *                                             VNFD1248 device driver )
 *                       |  ExVioDb_Xxx_Port_...()
 *                       v
 *                      MCAL / board layer
 *
 *  NOTE : This header also collects the symbols the device drivers expect from
 *         "ExVioDb.h" ( exVioDbRec[] / exVioDbMemCnt / log macro ).
 *         When the file set is merged into an existing SWC_EXVIODB project,
 *         set EXVIODB_PROVIDE_BASE_LAYER to STD_OFF, see section 2.
 ******************************************************************************/
#ifndef EXVIODB_H
#define EXVIODB_H

/*==============================================================================
 *  INCLUDES
 *============================================================================*/
#include "Std_Types.h"          /* uint8 / uint16 / boolean / Std_ReturnType   */

/*==============================================================================
 *  RTE APPLICATION DATA TYPE FALLBACKS
 *      Rte_Type.h supplies these definitions in the production AUTOSAR build.
 *      Keep byte-sized aliases and matching enumerator values for stand-alone
 *      driver builds. The RTE-generated type guards prevent redefinition.
 *============================================================================*/
#ifndef Rte_TypeDef_TE_STD_SPI_RESULT
#define Rte_TypeDef_TE_STD_SPI_RESULT
typedef uint8 TE_STD_SPI_RESULT;
#endif

#ifndef Rte_TypeDef_TE_EX_VIO_OUTPUT_RESULT
#define Rte_TypeDef_TE_EX_VIO_OUTPUT_RESULT
typedef uint8 TE_EX_VIO_OUTPUT_RESULT;
#endif

#ifndef STD_SPI_OK
#define STD_SPI_OK                         ((TE_STD_SPI_RESULT)0u)
#define STD_SPI_NOT_OK                     ((TE_STD_SPI_RESULT)1u)
#define STD_SPI_TRANSMIT_FAIL              ((TE_STD_SPI_RESULT)2u)
#define STD_SPI_BUFFER_SETUP_FAIL          ((TE_STD_SPI_RESULT)3u)
#define STD_SPI_SEQ_FAIL                   ((TE_STD_SPI_RESULT)4u)
#define STD_SPI_CALLBACK_SETUP_FAIL        ((TE_STD_SPI_RESULT)5u)
#define STD_SPI_INVALID_DATA               ((TE_STD_SPI_RESULT)6u)
#define STD_SPI_INVALID_DATA_LEN           ((TE_STD_SPI_RESULT)7u)
#define STD_SPI_SYNC_BUSY                  ((TE_STD_SPI_RESULT)8u)
#define STD_SPI_INVALID_TASK               ((TE_STD_SPI_RESULT)9u)
#endif

#ifndef EX_VIO_OUTPUT_E_OK
#define EX_VIO_OUTPUT_E_OK                 ((TE_EX_VIO_OUTPUT_RESULT)0u)
#define EX_VIO_OUTPUT_E_NOT_OK             ((TE_EX_VIO_OUTPUT_RESULT)1u)
#define EX_VIO_OUTPUT_E_SPI_CONFIG_ERROR   ((TE_EX_VIO_OUTPUT_RESULT)2u)
#define EX_VIO_OUTPUT_E_ALREADY_INITIALIZED ((TE_EX_VIO_OUTPUT_RESULT)3u)
#define EX_VIO_OUTPUT_E_NOT_STOPPED        ((TE_EX_VIO_OUTPUT_RESULT)4u)
#define EX_VIO_OUTPUT_E_ALREADY_IN_IDLE    ((TE_EX_VIO_OUTPUT_RESULT)5u)
#define EX_VIO_OUTPUT_E_ALREADY_RUNNING    ((TE_EX_VIO_OUTPUT_RESULT)6u)
#define EX_VIO_OUTPUT_E_NOT_INITIALIZED    ((TE_EX_VIO_OUTPUT_RESULT)7u)
#define EX_VIO_OUTPUT_E_NOT_RUNNING        ((TE_EX_VIO_OUTPUT_RESULT)8u)
#define EX_VIO_OUTPUT_E_SPI_COMM_ERROR     ((TE_EX_VIO_OUTPUT_RESULT)9u)
#define EX_VIO_OUTPUT_E_SLEEP_PIN_ERROR    ((TE_EX_VIO_OUTPUT_RESULT)10u)
#define EX_VIO_OUTPUT_E_CONFIG_MISMATCH    ((TE_EX_VIO_OUTPUT_RESULT)11u)
#define EX_VIO_OUTPUT_E_FATAL_ERROR        ((TE_EX_VIO_OUTPUT_RESULT)12u)
#define EX_VIO_OUTPUT_E_INVALID_PARAM      ((TE_EX_VIO_OUTPUT_RESULT)13u)
#define EX_VIO_OUTPUT_E_IN_STOPPING        ((TE_EX_VIO_OUTPUT_RESULT)14u)
#define EX_VIO_OUTPUT_E_INIT_REG_ERROR     ((TE_EX_VIO_OUTPUT_RESULT)15u)
#endif

/*==============================================================================
 *  1. COMPILER / LOG ABSTRACTION
 *      The production project gets these from Compiler.h and from the standard
 *      SWC manager log header. The fallback below keeps the component
 *      compilable in a stand alone build.
 *============================================================================*/
#ifndef FUNC
#define FUNC(rettype, memclass)         rettype
#endif

#ifndef SWC_EXVIODB_CODE
#define SWC_EXVIODB_CODE                /* memory class of the SWC code       */
#endif

#ifndef D_STATIC
#define D_STATIC                        static
#endif

#ifndef TAG_EEVP_EXVIODB
#define TAG_EEVP_EXVIODB                (0)
#endif

#ifndef TF_STD_SWC_MNGR_LOG_SHEL_LOG_E
#include <stdio.h>
#define TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(tag, ...)    (void)printf(__VA_ARGS__)
#endif

#ifndef TF_STD_SWC_MNGR_LOG_SHEL_LOG_I
#include <stdio.h>
#define TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(tag, ...)    (void)printf(__VA_ARGS__)
#endif

/*==============================================================================
 *  2. COMPONENT CONFIGURATION
 *============================================================================*/
/* STD_ON  : ExVioDb.c provides the component base layer ( state sequence,
 *           signal DB instance, RTE write stub, sub device task stub ) so that
 *           the delivered file set builds stand alone.
 * STD_OFF : only the RE_Swc_ExVioDb_xxx runnables and the service API are
 *           compiled, every base layer symbol is taken from the existing
 *           SWC_EXVIODB project.                                              */
#ifndef EXVIODB_PROVIDE_BASE_LAYER
#define EXVIODB_PROVIDE_BASE_LAYER      STD_ON
#endif

/* device driver used by this standard controller                             */
#ifndef EXVIODB_USE_TIC12400
#define EXVIODB_USE_TIC12400            STD_ON
#endif
#ifndef EXVIODB_USE_DRV8912
#define EXVIODB_USE_DRV8912             STD_ON
#endif
#ifndef EXVIODB_USE_MPQ6620
#define EXVIODB_USE_MPQ6620             STD_ON
#endif
#ifndef EXVIODB_USE_VNFD1248
#define EXVIODB_USE_VNFD1248            STD_ON
#endif
#ifndef EXVIODB_USE_TPS2HCS08
#define EXVIODB_USE_TPS2HCS08           STD_ON
#endif

/* task period of RE_Swc_ExVioDb_Task_10ms()                                  */
#define EXVIODB_TASK_PERIOD_MS          (10u)

/* my standard controller ID (SC parameter of the vehicle IO signal DB)       */
#ifndef EXVIODB_MY_SC_ID
#define EXVIODB_MY_SC_ID                (1u)
#endif

/* maximum record count of the vehicle IO signal DB                           */
#ifndef EXVIODB_SIG_MAX
#define EXVIODB_SIG_MAX                 (256u)
#endif

/* invalid signal DB record index                                             */
#define EXVIODB_SIG_IDX_INVALID         (0xFFFFu)

/* RTE status event value of P_SR_EXVIODB_NOTI_StatusEvent                    */
#define EXVIODB_NOTI_STATUS_READY       (1u)

/*==============================================================================
 *  3. VEHICLE IO SIGNAL DB RECORD
 *      One record of the vehicle IO signal DB specification. The member names
 *      are identical to the column names of the specification because the
 *      device driver parsers access them directly.
 *============================================================================*/
typedef struct
{
    uint16  SIG_ID;         /* signal ID in the vehicle IO DB                */
    uint8   CAT_1;          /* IC category      ( 5 = e-Fuse )                */
    uint8   CAT_2;          /* output polarity  ( 0 = active high )           */
    uint8   SC;             /* standard controller ID                         */
    uint8   IC;             /* daisy chain device index                       */
    uint8   PIN;            /* IC output channel ( 1 = CH1, 2 = CH2 )         */
    uint8   USED;           /* channel assigned                               */
    uint8   MOC;            /* multi output channel ( parallel operation )    */
    uint8   OCP;            /* over current protection level                  */
    uint8   RT;             /* retry / reaction-time DB parameter             */
    uint8   PWM;            /* PWM type ( 0 none / 1 PWM_C / 2 _X / 3 _O )    */
    uint8   OLD;            /* off state open load detection                  */
    uint8   PWM_F;          /* PWM frequency                                  */
    uint8   CT;             /* capacitive / inrush charging time              */
    uint8   SR;             /* output slew rate                               */
    uint8   VOL_DET;        /* VOUT voltage sensing use                       */
    uint8   DEF_Value;      /* initial output level ( B+ always on )          */
    uint8   Wake;           /* wake-up configuration                          */
    uint8   PRE_Value;      /* previous output value                          */
    uint8   WC;             /* wire cross section ( -> ISWCL )                */
    uint8   Threshold_V;    /* input/output diagnostic threshold              */
    uint8   PWM_Duty;       /* PWM duty / inrush current limit                */

    uint16  AnaValue;       /* latest analog value                            */
    boolean flg_PRE_Value;  /* PRE_Value validity flag                        */
    uint16  frt_CT;         /* charging-time runtime counter                  */
    uint8   port_num[4];    /* mapped IC ports                                */
    uint8   port_cnt;       /* number of mapped IC ports                      */
} tExVioDbRec;

extern const tExVioDbRec    exVioDbRec[];
extern uint16               exVioDbMemCnt;

/*==============================================================================
 *  4. COMPONENT STATE SEQUENCE
 *============================================================================*/
typedef enum
{
    EXVIODB_STATE_SET_CMD = 0,
    EXVIODB_STATE_START,
    EXVIODB_STATE_READ_ZONE,
    EXVIODB_STATE_LOAD_DB,
    EXVIODB_STATE_EXVIO_SETUP,
    EXVIODB_STATE_RUN,
    EXVIODB_STATE_ERROR,
    EXVIODB_STATE_DISABLE
} T_exVioDbState;

/* Stand-alone source compatibility. The production SWC uses
 * T_exVioDbState directly. */
typedef T_exVioDbState tExVioDbStateSeq;

extern tExVioDbStateSeq     exVioDbStateSeq;

/* input change notification counter written to the RTE                       */
extern uint16               evdbNotiInputChanged;

/*==============================================================================
 *  5. SUB DEVICE DRIVER INTERFACE
 *      Provided by the device driver files of the project. Delete the block of
 *      a device when the symbols are already declared by an existing project
 *      header.
 *============================================================================*/
#if (EXVIODB_USE_TPS2HCS08 == STD_ON)
#include "ExVioDb_Tps2hcs08.h"
#endif

#if (EXVIODB_USE_TIC12400 == STD_ON)
#ifndef TIC_SETUP_SCN_COMPLETE
#define TIC_SETUP_SCN_COMPLETE          (0xFFu)
#endif
extern uint8    exVioDbTicSetupScnState;
extern void     ExVioDb_SetupScnTicMSDIReg(void);
extern void     ExVioDb_ReadTic12400FaultReg(void);
#endif

#if (EXVIODB_USE_DRV8912 == STD_ON)
#ifndef DRV_SETUP_SCN_COMPLETE
#define DRV_SETUP_SCN_COMPLETE          (0xFFu)
#endif
extern uint8    exVioDbDrvSetupScnState;
extern void     ExVioDb_SetupScnDrv8912Reg(void);
#endif

#if (EXVIODB_USE_MPQ6620 == STD_ON)
#ifndef MPQ_SETUP_SCN_COMPLETE
#define MPQ_SETUP_SCN_COMPLETE          (0xFFu)
#endif
extern uint8    exVioDbMpqSetupScnState;
extern void     ExVioDb_SetupScnMpq6620Reg(void);
#endif

#if (EXVIODB_USE_VNFD1248 == STD_ON)
#ifndef VNFD_SETUP_SCN_COMPLETE
#define VNFD_SETUP_SCN_COMPLETE         (0xFFu)
#endif
extern uint8    exVioDbVnfdSetupScnState;
extern void     ExVioDb_SetupScnVnfd1248Reg(void);
#endif

/* cyclic sub tasks of EXVIODB_STATE_RUN                                      */
extern void     ExVioDb_TaskSigDbUpdate(void);
extern void     ExVioDb_TaskInputReg(void);
extern void     ExVioDb_TaskOutputLpReg(void);
extern void     ExVioDb_TaskOutputHpReg(void);
extern void     ExVioDb_TaskOutputVhpReg(void);

/*==============================================================================
 *  6. RTE INTERFACE
 *      Declared by Rte_SwcExVioDb.h in the production project.
 *============================================================================*/
extern Std_ReturnType Rte_Write_P_SR_EXVIODB_NOTI_StatusEvent(uint8 status);
extern Std_ReturnType Rte_Write_P_SR_EXVIODB_NOTI_InputChanged(sint16 changed);

/*==============================================================================
 *  7. SWC PUBLIC API  ( RTE runnable entity )
 *============================================================================*/
/* component initialisation, called once before the cyclic task starts        */
extern FUNC(void, SWC_EXVIODB_CODE) RE_Swc_ExVioDb_Init(void);

/* 10ms cyclic runnable : setup scan and run scan of every external IO device */
extern FUNC(void, SWC_EXVIODB_CODE) RE_Swc_ExVioDb_Task_10ms(void);

/*==============================================================================
 *  8. COMPONENT LEVEL SERVICE API  ( for application SW )
 *      Device independent entry point. The target device driver is selected
 *      from the CAT_1 parameter of the vehicle IO signal DB record.
 *============================================================================*/
/* current component state sequence                                           */
extern tExVioDbStateSeq ExVioDb_GetStateSeq(void);

/* TRUE when the setup scan of every used device is finished                  */
extern boolean          ExVioDb_IsSetupComplete(void);

/* output ON / OFF request by vehicle IO signal DB record index               */
extern Std_ReturnType   ExVioDb_SetOutput(uint16 sigIdx, boolean onOff);

/* ADC monitoring value request by vehicle IO signal DB record index          */
extern Std_ReturnType   ExVioDb_GetAdcValue(uint16 sigIdx,
                                            uint16 *isns,
                                            uint16 *tsns,
                                            uint16 *vds);

/* setup scan open load / short to VBB diagnostic result                      */
extern Std_ReturnType   ExVioDb_GetDiagResult(uint16 sigIdx, uint8 *diagResult);

/* sleep / wake-up request from the vehicle power mode manager                */
extern void             ExVioDb_ReqSleep(boolean req);
extern void             ExVioDb_ReqWakeUp(void);

/* record index search helper ( EXVIODB_SIG_IDX_INVALID when not found )      */
extern uint16           ExVioDb_FindSigIdx(uint8 cat1, uint8 ic, uint8 pin);

#endif /* EXVIODB_H */
