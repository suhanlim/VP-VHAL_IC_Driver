/*******************************************************************************
 *  File            : mock_SPI.h
 *  Description     : Mock SPI functions for TPS2HCS08 unit testing
 ******************************************************************************/
#ifndef MOCK_SPI_H
#define MOCK_SPI_H

#include "Std_Types.h"

/*==============================================================================
 *  MOCK CONTROL
 *============================================================================*/
typedef struct
{
    boolean     transferSuccess;    /* E_OK or E_NOT_OK */
    uint8       sdoHeaderToReturn;  /* SDO header byte (rxBuf[0]) */
    uint16      dataToReturn;       /* Data bytes (rxBuf[1:2]) */
    uint8       callCount;          /* Number of times called */
    uint8       lastDevIdx;         /* Last devIdx parameter */
    uint8       lastTxBuf[3];       /* Last transmitted data */
} MockSpiControl_t;

extern MockSpiControl_t mockSpiControl;

/*==============================================================================
 *  MOCK FUNCTIONS
 *============================================================================*/
void MockSpi_Reset(void);
void MockSpi_SetNextTransferResult(boolean success, uint8 sdoHeader, uint16 data);

/* Mocked SPI transfer function */
Std_ReturnType ExVioDb_Tps2hcs08_Port_SpiTransfer(
    uint8 devIdx,
    const uint8 *txBuf,
    uint8 *rxBuf,
    uint8 len);

#endif /* MOCK_SPI_H */
