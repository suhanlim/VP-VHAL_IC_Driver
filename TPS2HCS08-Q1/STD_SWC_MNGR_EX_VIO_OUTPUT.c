#include <string.h>
#include "ExVioDb.h"

// VHAL_FR 코드 베이스 레이어에서 제공하는 SPI 전송 함수의 RTE 호출 인터페이스
extern Rte_Call_R_CS_STD_SWC_MNGR_SPI_Transmit(uint8 seqid, const uint8* data, uint16 datalen, TE_STD_SPI_RESULT* result);

Rte_Call_R_CS_STD_SWC_MNGR_SPI_Transmit(uint8 seqid, const uint8* data, uint16 datalen, TE_STD_SPI_RESULT* result)
{

}

D_STATIC Std_ReturnType ExVioDb_Tps2hcs08_Port_SpiTransfer(uint8 seqid, const uint8* txData, uint8* rxData, uint8 len)
{
	Std_ReturnType err = E_NOT_OK;
	TE_STD_SPI_RESULT spi_result = STD_SPI_NOT_OK;

	(void)rxData; /* RX가 BSW 설정 버퍼로 연결된다면 */

	/* Justify QAC, Rule-12.3-3417, DT090813, 20251120, This function is generated as macro by Vector AUTOSAR (Only MICROSAR) */
	/* Justify QAC, Dir-4.9-3469, DT090813, 20251120, This function is generated as macro by Vector AUTOSAR (Only MICROSAR) */
	(void)Rte_Call_R_CS_STD_SWC_MNGR_SPI_Transmit(seqid,
		txData,
		len,
		&spi_result);

	/* Check SPI Result */
	if (spi_result == STD_SPI_OK)
	{
		// 버퍼에서 읽은 값 기반으로 파싱 후 동작한다.
		// rxData는 BSW 설정에 따라 RTE에서 제공하는 버퍼로 연결되어 있으면 수동으로 채울 필요가 없다.
		// seqid 설정이 동기 방식이면 이곳에서 rxData 값을 기반으로 동작을 수행한다.
		// seqid 설정이 비동기 방식이면 callback 함수에서 동작을 수행한다.
		err = E_OK;
    }

    return err;
}

#  define Rte_TypeDef_TE_STD_SPI_TYPE
typedef unsigned char TE_STD_SPI_TYPE;

typedef struct
{
	TE_STD_SPI_TYPE spi_type;
	uint8 master_seq_id;
	uint8 slave_ch_id;
	uint8 dummy_padding;
} TS_STD_SPI_INFO;

static boolean s_Mngr_Ex_Vio_Output_Drv8912_SeqToCh(TE_STD_SPI_TYPE seq_id, uint8* ch)
{
	uint8   ch_id;
	boolean rtn = FALSE;

	if (ch != NULL_PTR)
	{
		for (ch_id = 0U; ch_id < gtc_ex_vio_output_drv8912_Pb_Setting.ex_vio_output_channel_count; ch_id++)
		{
			if (gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch_id].spi_info.master_seq_id == seq_id)
			{
				rtn = TRUE;

				*ch = ch_id;

				/* Exit Loop */
				break;
			}
		}
	}

	return rtn;
}

void RE_STD_SWC_MNGR_EX_VIO_OUTPUT_SpiReceived_Drv8912(const TS_STD_SPI_INFO* spiInfo, const uint8* data, uint16 datalen)
{
	/**********************************************************************************************************************
	 * DO NOT CHANGE THIS COMMENT!           << Start of runnable implementation >>             DO NOT CHANGE THIS COMMENT!
	 * Symbol: RE_STD_SWC_MNGR_EX_VIO_OUTPUT_SpiReceived_Drv8912
	 *********************************************************************************************************************/
	 /* Justify QAC, Rule-2.1-1503, DT071820, 20240927, This function is RTE runnable. If RTE interface port related this
	 runnable is connected in RTE composition, then this function will be called by RTE */
#if (EX_VIO_OUTPUT_CHIP_DEFINE_DRV8912 == EX_VIO_DEV_EXIST)

	uint8  ch;
	uint8  dev_cnt;
	uint16 buf_idx;

	if ((spiInfo != NULL_PTR) && (data != NULL_PTR) && (datalen > 0U))
	{
		if ((s_Mngr_Ex_Vio_Output_Drv8912_SeqToCh(spiInfo->master_seq_id, &ch) == TRUE) &&
			(gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch].spi_buffer_size == datalen) &&
			(gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch].cmd_n_status.r_reg != NULL_PTR))
		{
			// save data: rx data is in ordered as Sn-...-S1-HDR1-HDR2-Rn-...-R1 => save Rn-R1 (n = gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch].num_of_dev_in_daisy_chain)
			dev_cnt = gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch].num_of_dev_in_daisy_chain;
			buf_idx = datalen - gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch].num_of_dev_in_daisy_chain;

			/* Copy Data */
			while (dev_cnt > 0u)
			{
				dev_cnt--;
				gtc_ex_vio_output_drv8912_Pb_Setting.p_ex_vio_output_channel[ch].cmd_n_status.r_reg[dev_cnt] = data[buf_idx];
				buf_idx++;
			}
		}
	}

#endif
	/*********************************************************************************************************************
	 * DO NOT CHANGE THIS COMMENT!           << End of runnable implementation >>               DO NOT CHANGE THIS COMMENT!
	 *********************************************************************************************************************/
}

// RE_STD_SWC_MNGR_EX_VIO_OUTPUT_GetOutput_Vnfd1248
// RE_STD_SWC_MNGR_EX_VIO_OUTPUT_ReadRegister_Vnfd1248

FUNC(void, STD_SWC_MNGR_EX_VIO_OUTPUT_CODE) RE_STD_SWC_MNGR_EX_VIO_OUTPUT_ReadRegister_Vnfd1248(std_uint8 ch_no, std_uint8 dev_no, std_uint8 reg_addr, P2VAR(std_uint32, AUTOMATIC, RTE_STD_SWC_MNGR_EX_VIO_OUTPUT_APPL_VAR) r_data, std_uint8 opt, P2VAR(TE_EX_VIO_OUTPUT_RESULT, AUTOMATIC, RTE_STD_SWC_MNGR_EX_VIO_OUTPUT_APPL_VAR) result) /* PRQA S 0624, 3206 */ /* MD_Rte_0624, MD_Rte_3206 */
{
	/**********************************************************************************************************************
	 * DO NOT CHANGE THIS COMMENT!           << Start of runnable implementation >>             DO NOT CHANGE THIS COMMENT!
	 * Symbol: RE_STD_SWC_MNGR_EX_VIO_OUTPUT_ReadRegister_Vnfd1248
	 *********************************************************************************************************************/
	 /* Justify QAC, Rule-2.1-1503, DT090813, 20251119, This function is RTE runnable. If RTE interface port related this
	 runnable is connected in RTE composition, then this function will be called by RTE */
#if (EX_VIO_OUTPUT_CHIP_DEFINE_VNFD1248 == EX_VIO_DEV_EXIST)
	TE_EX_VIO_OUTPUT_RESULT tmp_result = EX_VIO_OUTPUT_E_OK;
	uint8 op_code;
	switch (opt)
	{
	case VNFD1248_READ_ONLY:
		op_code = VNFD1248_OP_CODE_READ_ONLY;
		break;
	case VNFD1248_READ_AND_CLEAR:
		op_code = VNFD1248_OP_CODE_READ_AND_CLEAR;
		break;
	case VNFD1248_READ_DEVICE_INFO:
		op_code = VNFD1248_OP_CODE_READ_DEVICE_INFO;
		break;
	default:
		tmp_result = EX_VIO_OUTPUT_E_INVALID_PARAM;
		break;
	}

	if (tmp_result == EX_VIO_OUTPUT_E_OK)
	{
		uint32 read_val = 0U;
		tmp_result = s_Mngr_Ex_Vio_Output_SpiTransmit_Vnfd1248(ch_no, dev_no, reg_addr, VNFD1248_MASK_DATA, &read_val, op_code);
		if ((tmp_result == EX_VIO_OUTPUT_E_OK) && (r_data != NULL_PTR))
		{
			*r_data = read_val;
		}
	}
	if (result != NULL_PTR)
	{
		*result = tmp_result;
	}
#endif
	/**********************************************************************************************************************
	 * DO NOT CHANGE THIS COMMENT!           << End of runnable implementation >>               DO NOT CHANGE THIS COMMENT!
	 *********************************************************************************************************************/
}

D_STATIC uint8 ExVioDb_GetPortOutputVhpReg(uint16 sigIndex, uint16* value, uint16 mode)
{
	uint8_t ret = E_OK;
	uint8_t err;
	uint8_t port_no, dev_no;

	switch (exVioDbRec[sigIndex].IC)
	{
	case DB_IC_1: // 0x0000, (0)     IC# 1
	case DB_IC_2: // 0x0002, (2)     IC# 2
	case DB_IC_3: // 0x0003, (3)     IC# 3
	case DB_IC_4: // 0x0004, (4)     IC# 4
		dev_no = exVioDbRec[sigIndex].IC;

		switch (exVioDbRec[sigIndex].PIN)
		{
			// Excluding PIN 0, starting from PIN 1
		case DB_IC_PIN_1: // 0x0001, (1)     PIN #1
		case DB_IC_PIN_2: // 0x0002, (2)     PIN #2
			if (NULL_PTR != value)
			{
				port_no = exVioDbRec[sigIndex].PIN - 1U;
				*value = 0xFFFF;

				switch (exVioDbRec[sigIndex].PWM)
				{
				case DB_PWM_O: // 0x0000, (0)
					IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
					ret = E_NOT_OK;
					break;
				case DB_PWM_C: // 0x0002, (2)
				case DB_PWM_X: // 0x0001, (1)
					switch (mode)
					{
					case 0: // get state
						err = ExVioDb_GetPortValue_Vnfd1248(dev_no, port_no, value);
						break;
					case 1: // get duty
						err = ExVioDb_GetPwmDuty_Vnfd1248(dev_no, port_no, value);
						break;
					case 2: // get event
						err = ExVioDb_GetPortEvent_Vnfd1248(dev_no, port_no, value);
						break;
					case 3: // get fault
						err = ExVioDb_GetPortFault_Vnfd1248(dev_no, port_no, value);
						break;
					case 4: // get freq
						err = ExVioDb_GetPwmFreq_Vnfd1248(dev_no, port_no, value);
						break;
					case 5: // get current
						err = ExVioDb_GetPortCurrent_Vnfd1248(dev_no, port_no, value);
						break;
					default:
						IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
						err = E_NOT_OK;
						break;
					}

					if (err != E_OK)
					{
						ret = E_NOT_OK;
					}
					break;
				default: // exVioDbRec[sigIndex].PWM
					IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
					ret = E_NOT_OK;
					break;
				}
			}
			break;
		default:
			IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %s(sigId%d) PIN = %d""\r\n", __func__, exVioDbRec[sigIndex].SIG_ID, exVioDbRec[sigIndex].PIN);
			break;
		}
		break;

	default:
		IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
		ret = E_NOT_OK;
		break;
	}

	return ret;
}


D_STATIC uint8_t ExVioDb_SetPortOutputVhpReg(uint16_t sigIndex, std_uint16 value, std_uint16 mode)
{
	uint8_t ret = E_OK;
	uint8_t err;
	uint8_t port_no, dev_no, ccm;
	std_uint16 pre_value;

	switch (exVioDbRec[sigIndex].IC)
	{
	case DB_IC_1: // 0x0000, (0)     IC# 1
	case DB_IC_2: // 0x0002, (2)     IC# 2
	case DB_IC_3: // 0x0003, (3)     IC# 3
	case DB_IC_4: // 0x0004, (4)     IC# 4
		dev_no = exVioDbRec[sigIndex].IC;

		switch (exVioDbRec[sigIndex].PIN)
		{
			// Excluding PIN 0, starting from PIN 1
		case DB_IC_PIN_1: // 0x0001, (1)     PIN #1
		case DB_IC_PIN_2: // 0x0002, (2)     PIN #2
			port_no = exVioDbRec[sigIndex].PIN - 1U;

			switch (exVioDbRec[sigIndex].PWM)
			{
			case DB_PWM_O: // 0x0000, (0)
				IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
				ret = E_NOT_OK;
				break;
			case DB_PWM_C: // 0x0002  (2)
			case DB_PWM_X: // 0x0001, (1)
				switch (mode)
				{
				case 0: // set state
					if ((value == (std_uint16)DB_DEF_ACTIVE) && (exVioDbRec[sigIndex].PWM == (uint8_t)DB_PWM_C))
					{
						err = ExVioDb_GetPortValue_Vnfd1248(dev_no, port_no, &pre_value);
						if (err == E_OK)
						{
							if (pre_value == (std_uint16)DB_DEF_IDLE)
							{
								ccm = STD_ON;
							}
							else
							{
								ccm = STD_OFF;
							}
						}
					}
					else
					{
						ccm = STD_OFF;
						err = E_OK;
					}
					if (err == E_OK)
					{
						err = ExVioDb_SetPortValue_Vnfd1248(dev_no, port_no, ccm, value);
					}
					break;
				case 1:	// set duty
					err = ExVioDb_SetPwmDuty_Vnfd1248(dev_no, port_no, value);
					break;
				case 2:	// clear event
					err = ExVioDb_ClearPortEvent_Vnfd1248(dev_no, port_no);
					break;
				case 3:	// clear fault
					err = ExVioDb_ClearPortFault_Vnfd1248(dev_no, port_no);
					break;
				case 4:	// set freq
					err = ExVioDb_SetPwmFreq_Vnfd1248(dev_no, port_no, value);
					break;
				default: // mode
					IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
					err = E_NOT_OK;
					break;
				}

				if (E_OK != err)
				{
					ret = E_NOT_OK;
				}
				break;
			default: // exVioDbRec[sigIndex].PWM
				IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
				ret = E_NOT_OK;
				break;
			}
			break;

			// Excluding PIN 0, starting from PIN 1
		case DB_IC_PIN_0: // 0x0000, (0)     PIN #0
		default:
			IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
			ret = E_NOT_OK;
			break;
		}
		break;
	default:
		IF_STD_SWC_MNGR_LOG_SHEL_LOG_W(TAG_EEVP_EXVIODB, "Warning!! Invalid value : %d %s""\r\n", __LINE__, __func__);
		ret = E_NOT_OK;
		break;
	}

	return ret;
}
