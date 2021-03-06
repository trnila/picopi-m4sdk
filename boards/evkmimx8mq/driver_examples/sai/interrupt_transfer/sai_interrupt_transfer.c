/*
 * The Clear BSD License
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted (subject to the limitations in the disclaimer below) provided
 * that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "board.h"
#include "music.h"
#include "fsl_sai.h"
#include "fsl_debug_console.h"

#include "fsl_wm8524.h"
#include "pin_mux.h"
#include "clock_config.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define DEMO_SAI (I2S2)
#define DEMO_SAI_CLK_FREQ  \
        CLOCK_GetPllFreq(kCLOCK_SystemPll1Ctrl) / (CLOCK_GetRootPreDivider(kCLOCK_RootSai2)) / \
        (CLOCK_GetRootPostDivider(kCLOCK_RootSai2)) / 6
#define DEMO_CODEC_WM8524 (1)
#define CODEC_USEGPIO (1)
#define CODEC_BUS_PIN (NULL)
#define CODEC_BUS_PIN_NUM (0)
#define CODEC_MUTE_PIN (GPIO1)
#define CODEC_MUTE_PIN_NUM (8)
#define OVER_SAMPLE_RATE (384U)

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
sai_handle_t txHandle = {0};
static volatile bool isFinished = false;
#if defined(DEMO_CODEC_WM8960)
wm8960_handle_t codecHandle = {0};
#elif defined(DEMO_CODEC_DA7212)
da7212_handle_t codecHandle = {0};
#elif defined(DEMO_CODEC_WM8524)
wm8524_handle_t codecHandle = {0};
#else
sgtl_handle_t codecHandle = {0};
#endif

#ifndef CODEC_USEGPIO
#if defined(FSL_FEATURE_SOC_LPI2C_COUNT) && (FSL_FEATURE_SOC_LPI2C_COUNT)
lpi2c_master_handle_t i2cHandle = {0};
#else
i2c_master_handle_t i2cHandle = {{0, 0, kI2C_Write, 0, 0, NULL, 0}, 0, 0, NULL, NULL};
#endif
#endif /* CODEC_USEGPIO */

/*******************************************************************************
 * Code
 ******************************************************************************/
static void callback(I2S_Type *base, sai_handle_t *handle, status_t status, void *userData)
{
    isFinished = true;
}

/*!
 * @brief Main function
 */
int main(void)
{
    sai_config_t config;
    uint32_t mclkSourceClockHz = 0U;
    sai_transfer_format_t format;
    sai_transfer_t xfer;
#ifndef CODEC_USEGPIO
#if defined(FSL_FEATURE_SOC_LPI2C_COUNT) && (FSL_FEATURE_SOC_LPI2C_COUNT)
    lpi2c_master_config_t i2cConfig = {0};
#else
    i2c_master_config_t i2cConfig = {0};
#endif
    uint32_t i2cSourceClock;
#endif /* CODEC_USEGPIO */
    uint32_t temp = 0;
    uint32_t delayCycle = 500000;

    /* Board specific RDC settings */
    BOARD_RdcInit();
    
    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();
    BOARD_InitMemory();

    CLOCK_SetRootMux(kCLOCK_RootSai2, kCLOCK_SaiRootmuxSysPll1Div6); /* Set SAI source to SYS PLL1 Div6 133MHZ */
    CLOCK_SetRootDivider(kCLOCK_RootSai2, 1U, 3U);                /* Set root clock to 133MHZ / 2 = 66MHZ */

    PRINTF("SAI example started!\n\r");

    /*
     * config.masterSlave = kSAI_Master;
     * config.mclkSource = kSAI_MclkSourceSysclk;
     * config.protocol = kSAI_BusLeftJustified;
     * config.syncMode = kSAI_ModeAsync;
     * config.mclkOutputEnable = true;
     */
    SAI_TxGetDefaultConfig(&config);
#if defined DEMO_CODEC_WM8524
    config.protocol = kSAI_BusI2S;
#endif
    SAI_TxInit(DEMO_SAI, &config);

    /* Configure the audio format */
    format.bitWidth = kSAI_WordWidth16bits;
    format.channel = 0U;
    format.sampleRate_Hz = kSAI_SampleRate16KHz;
#if (defined FSL_FEATURE_SAI_HAS_MCLKDIV_REGISTER && FSL_FEATURE_SAI_HAS_MCLKDIV_REGISTER) || \
    (defined FSL_FEATURE_PCC_HAS_SAI_DIVIDER && FSL_FEATURE_PCC_HAS_SAI_DIVIDER)
    format.masterClockHz = OVER_SAMPLE_RATE * format.sampleRate_Hz;
#else
    format.masterClockHz = DEMO_SAI_CLK_FREQ;
#endif
    format.protocol = config.protocol;
    format.stereo = kSAI_Stereo;
    format.isFrameSyncCompact = false;
#if defined(FSL_FEATURE_SAI_FIFO_COUNT) && (FSL_FEATURE_SAI_FIFO_COUNT > 1)
    format.watermark = FSL_FEATURE_SAI_FIFO_COUNT / 2U;
#endif

#ifndef CODEC_USEGPIO
    /* Configure Codec I2C */
    codecHandle.base = DEMO_I2C;
    codecHandle.i2cHandle = &i2cHandle;

    i2cSourceClock = DEMO_I2C_CLK_FREQ;

#if defined(FSL_FEATURE_SOC_LPI2C_COUNT) && (FSL_FEATURE_SOC_LPI2C_COUNT)
    /*
     * i2cConfig.debugEnable = false;
     * i2cConfig.ignoreAck = false;
     * i2cConfig.pinConfig = kLPI2C_2PinOpenDrain;
     * i2cConfig.baudRate_Hz = 100000U;
     * i2cConfig.busIdleTimeout_ns = 0;
     * i2cConfig.pinLowTimeout_ns = 0;
     * i2cConfig.sdaGlitchFilterWidth_ns = 0;
     * i2cConfig.sclGlitchFilterWidth_ns = 0;
     */
    LPI2C_MasterGetDefaultConfig(&i2cConfig);
    LPI2C_MasterInit(DEMO_I2C, &i2cConfig, i2cSourceClock);
    LPI2C_MasterTransferCreateHandle(DEMO_I2C, &i2cHandle, NULL, NULL);
#else
    /*
     * i2cConfig.baudRate_Bps = 100000U;
     * i2cConfig.enableStopHold = false;
     * i2cConfig.glitchFilterWidth = 0U;
     * i2cConfig.enableMaster = true;
     */
    I2C_MasterGetDefaultConfig(&i2cConfig);
    I2C_MasterInit(DEMO_I2C, &i2cConfig, i2cSourceClock);
    I2C_MasterTransferCreateHandle(DEMO_I2C, &i2cHandle, NULL, NULL);
#endif
#endif /* CODEC_USEGPIO */

#if defined(DEMO_CODEC_WM8960)
    WM8960_Init(&codecHandle, NULL);
    WM8960_ConfigDataFormat(&codecHandle, format.masterClockHz, format.sampleRate_Hz, format.bitWidth);
#elif defined(DEMO_CODEC_DA7212)
    DA7212_Init(&codecHandle, NULL);
    DA7212_ConfigAudioFormat(&codecHandle, format.sampleRate_Hz, format.masterClockHz, format.bitWidth);
    DA7212_ChangeOutput(&codecHandle, kDA7212_Output_HP);
#elif defined(DEMO_CODEC_WM8524)
    wm8524_config_t codecConfig = {0};
    codecConfig.busPinNum = CODEC_BUS_PIN_NUM;
    codecConfig.busPin = CODEC_BUS_PIN;
    codecConfig.mutePin = CODEC_MUTE_PIN;
    codecConfig.mutePinNum = CODEC_MUTE_PIN_NUM;
    codecConfig.protocol = kWM8524_ProtocolI2S;
    WM8524_Init(&codecHandle, &codecConfig);
#else
    /* Use default settings for sgtl5000 */
    SGTL_Init(&codecHandle, NULL);
    /* Configure codec format */
    SGTL_ConfigDataFormat(&codecHandle, format.masterClockHz, format.sampleRate_Hz, format.bitWidth);
#endif

#if defined(CODEC_CYCLE)
    delayCycle = CODEC_CYCLE;
#endif
    while (delayCycle)
    {
        __ASM("nop");
        delayCycle--;
    }

    SAI_TransferTxCreateHandle(DEMO_SAI, &txHandle, callback, NULL);
    mclkSourceClockHz = DEMO_SAI_CLK_FREQ;
    SAI_TransferTxSetFormat(DEMO_SAI, &txHandle, &format, mclkSourceClockHz, format.masterClockHz);

    /*  xfer structure */
    temp = (uint32_t)music;
    xfer.data = (uint8_t *)temp;
    xfer.dataSize = MUSIC_LEN;
    SAI_TransferSendNonBlocking(DEMO_SAI, &txHandle, &xfer);
    /* Wait until finished */
    while (isFinished != true)
    {
    }

    PRINTF("\n\r SAI example finished!\n\r ");
    while (1)
    {
    }
}
