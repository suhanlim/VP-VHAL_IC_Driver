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


