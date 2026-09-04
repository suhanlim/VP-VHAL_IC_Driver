/*******************************************************************************
 *  File            : mock_Logger.c
 *  Description     : Mock logging functions for TPS2HCS08 unit testing
 ******************************************************************************/
#include "Std_Types.h"
#include <stdarg.h>
#include <stdio.h>

/* Mock logging function - just suppress output during testing */
void TF_STD_SWC_MNGR_LOG_SHEL_LOG_I(uint32 tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
    /* Suppress info logs during testing */
}

void TF_STD_SWC_MNGR_LOG_SHEL_LOG_W(uint32 tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
    /* Suppress warning logs during testing */
}

void TF_STD_SWC_MNGR_LOG_SHEL_LOG_E(uint32 tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
    /* Suppress error logs during testing */
}

/* Define tag */
uint32 TAG_EEVP_EXVIODB = 0x12345678;
