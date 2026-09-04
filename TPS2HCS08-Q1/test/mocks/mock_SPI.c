/*******************************************************************************
 *  File            : mock_SPI.c
 *  Description     : Mock SPI implementation for TPS2HCS08 unit testing
 ******************************************************************************/
#include "mock_SPI.h"
#include <string.h>

/*==============================================================================
 *  MOCK STATE
 *============================================================================*/
MockSpiControl_t mockSpiControl;

/*==============================================================================
 *  MOCK CONTROL FUNCTIONS
 *============================================================================*/
void MockSpi_Reset(void)
{
    memset(&mockSpiControl, 0, sizeof(MockSpiControl_t));
    mockSpiControl.transferSuccess = TRUE;  /* Default: success */
}

void MockSpi_SetNextTransferResult(boolean success, uint8 sdoHeader, uint16 data)
{
    mockSpiControl.transferSuccess = success;
    mockSpiControl.sdoHeaderToReturn = sdoHeader;
    mockSpiControl.dataToReturn = data;
}

/*==============================================================================
 *  MOCK SPI TRANSFER
 *============================================================================*/
Std_ReturnType ExVioDb_Tps2hcs08_Port_SpiTransfer(
    uint8 devIdx,
    const uint8 *txBuf,
    uint8 *rxBuf,
    uint8 len)
{
    /* Record call parameters */
    mockSpiControl.callCount++;
    mockSpiControl.lastDevIdx = devIdx;

    if (len <= 3u)
    {
        memcpy(mockSpiControl.lastTxBuf, txBuf, len);
    }

    /* Return configured result */
    if (mockSpiControl.transferSuccess == TRUE)
    {
        if (len >= 1u)
        {
            rxBuf[0] = mockSpiControl.sdoHeaderToReturn;
        }
        if (len >= 3u)
        {
            rxBuf[1] = (uint8)((mockSpiControl.dataToReturn >> 8u) & 0xFFu);
            rxBuf[2] = (uint8)(mockSpiControl.dataToReturn & 0xFFu);
        }
        return E_OK;
    }
    else
    {
        return E_NOT_OK;
    }
}
