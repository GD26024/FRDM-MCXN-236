#include <stdbool.h>
#include <stdint.h>

#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_lpadc.h"
#include "fsl_spc.h"
#include "fsl_vref.h"
#include "board.h"
#include "temperature.h"

#define TEMP_ADC_BASE ADC0
#define TEMP_ADC_CHANNEL 26U
#define TEMP_ADC_COMMAND_ID 1U
#define TEMP_ADC_VREF_SOURCE kLPADC_ReferenceVoltageAlt2
#define TEMP_ADC_SAMPLE_MODE kLPADC_SampleChannelDiffBothSide
#define TEMP_ADC_OFFSET_A 0xAU
#define TEMP_ADC_OFFSET_B 0xAU

static volatile bool s_conversion_complete;
static volatile float s_temperature_celsius;

static float convert_sensor_samples(uint16_t vbe1, uint16_t vbe8)
{
    const float slope = FSL_FEATURE_LPADC_TEMP_PARAMETER_A;
    const float offset = FSL_FEATURE_LPADC_TEMP_PARAMETER_B;
    const float alpha = FSL_FEATURE_LPADC_TEMP_PARAMETER_ALPHA;
    const float difference = (float)vbe8 - (float)vbe1;
    const float denominator = (float)vbe8 + (alpha * difference);

    if (denominator == 0.0f)
    {
        return -273.15f;
    }

    return (slope * (alpha * difference / denominator)) - offset;
}

void ADC0_IRQHandler(void)
{
    lpadc_conv_result_t result;
    uint16_t vbe1;
    uint16_t vbe8;

#if defined(FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE) && (FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE == 4U)
    (void)LPADC_GetConvResult(TEMP_ADC_BASE, &result, 0U);
    (void)LPADC_GetConvResult(TEMP_ADC_BASE, &result, 0U);
#endif

    if (LPADC_GetConvResult(TEMP_ADC_BASE, &result, 0U))
    {
        vbe1 = (uint16_t)result.convValue;
        if (LPADC_GetConvResult(TEMP_ADC_BASE, &result, 0U))
        {
            vbe8 = (uint16_t)result.convValue;
            s_temperature_celsius = convert_sensor_samples(vbe1, vbe8);
        }
    }

    s_conversion_complete = true;
    SDK_ISR_EXIT_BARRIER;
}

static void configure_temperature_adc(void)
{
    lpadc_config_t adc_config;
    lpadc_conv_command_config_t command_config;
    lpadc_conv_trigger_config_t trigger_config;

    LPADC_GetDefaultConfig(&adc_config);
    adc_config.enableAnalogPreliminary = true;
#if defined(FSL_FEATURE_LPADC_HAS_CFG_PWRSEL) && (FSL_FEATURE_LPADC_HAS_CFG_PWRSEL == 1U)
    adc_config.powerLevelMode = kLPADC_PowerLevelAlt4;
#endif
    adc_config.referenceVoltageSource = TEMP_ADC_VREF_SOURCE;
#if defined(FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE)
#if defined(FSL_FEATURE_LPADC_FIFO_COUNT) && (FSL_FEATURE_LPADC_FIFO_COUNT == 2U)
    adc_config.FIFO0Watermark = FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE - 1U;
    adc_config.FIFO1Watermark = 0U;
#else
    adc_config.FIFOWatermark = FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE - 1U;
#endif
#endif
    LPADC_Init(TEMP_ADC_BASE, &adc_config);

#if defined(FSL_FEATURE_LPADC_FIFO_COUNT) && (FSL_FEATURE_LPADC_FIFO_COUNT == 2U)
    LPADC_DoResetFIFO0(TEMP_ADC_BASE);
#else
    LPADC_DoResetFIFO(TEMP_ADC_BASE);
#endif

#if defined(FSL_FEATURE_LPADC_HAS_CTRL_CALOFS) && FSL_FEATURE_LPADC_HAS_CTRL_CALOFS
#if defined(FSL_FEATURE_LPADC_OFSTRIM_COUNT) && (FSL_FEATURE_LPADC_OFSTRIM_COUNT == 2U)
    LPADC_SetOffsetValue(TEMP_ADC_BASE, TEMP_ADC_OFFSET_A, TEMP_ADC_OFFSET_B);
#elif defined(FSL_FEATURE_LPADC_OFSTRIM_COUNT) && (FSL_FEATURE_LPADC_OFSTRIM_COUNT == 1U)
    LPADC_SetOffsetValue(TEMP_ADC_BASE, TEMP_ADC_OFFSET_A);
#endif
#endif

#if defined(FSL_FEATURE_LPADC_HAS_CTRL_CAL_REQ) && FSL_FEATURE_LPADC_HAS_CTRL_CAL_REQ
    SDK_DelayAtLeastUs(1U, SystemCoreClock);
    LPADC_DoAutoCalibration(TEMP_ADC_BASE);
#endif
#if defined(FSL_FEATURE_LPADC_HAS_CFG_CALOFS) && FSL_FEATURE_LPADC_HAS_CFG_CALOFS
    SDK_DelayAtLeastUs(1U, SystemCoreClock);
    LPADC_DoAutoCalibration(TEMP_ADC_BASE);
#endif

    LPADC_GetDefaultConvCommandConfig(&command_config);
    command_config.channelNumber = TEMP_ADC_CHANNEL;
    command_config.sampleChannelMode = TEMP_ADC_SAMPLE_MODE;
    command_config.sampleTimeMode = kLPADC_SampleTimeADCK131;
    command_config.hardwareAverageMode = kLPADC_HardwareAverageCount128;
#if defined(FSL_FEATURE_LPADC_HAS_CMDL_MODE) && FSL_FEATURE_LPADC_HAS_CMDL_MODE
    command_config.conversionResolutionMode = kLPADC_ConversionResolutionHigh;
#endif
#if defined(FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE)
    command_config.loopCount = FSL_FEATURE_LPADC_TEMP_SENS_BUFFER_SIZE - 1U;
#endif
    LPADC_SetConvCommandConfig(TEMP_ADC_BASE, TEMP_ADC_COMMAND_ID, &command_config);

    LPADC_GetDefaultConvTriggerConfig(&trigger_config);
    trigger_config.targetCommandId = TEMP_ADC_COMMAND_ID;
    LPADC_SetConvTriggerConfig(TEMP_ADC_BASE, 0U, &trigger_config);

#if defined(FSL_FEATURE_LPADC_FIFO_COUNT) && (FSL_FEATURE_LPADC_FIFO_COUNT == 2U)
    LPADC_EnableInterrupts(TEMP_ADC_BASE, kLPADC_FIFO0WatermarkInterruptEnable);
#else
    LPADC_EnableInterrupts(TEMP_ADC_BASE, kLPADC_FIFOWatermarkInterruptEnable);
#endif
    EnableIRQ(ADC0_IRQn);
}

void temperature_init(void)
{
    vref_config_t vref_config;

    CLOCK_AttachClk(kFRO_HF_to_ADC0);
    CLOCK_SetClkDiv(kCLOCK_DivAdc0Clk, 1U);
    SPC0->ACTIVE_CFG1 |= 0x1U;

    VREF_GetDefaultConfig(&vref_config);
    VREF_Init(VREF0, &vref_config);
    VREF_SetTrim21Val(VREF0, 8U);

    configure_temperature_adc();
    s_conversion_complete = false;
    s_temperature_celsius = -273.15f;
    (void)temperature_read_celsius();
}

float temperature_read_celsius(void)
{
    s_conversion_complete = false;
    LPADC_DoSoftwareTrigger(TEMP_ADC_BASE, 1U);

    while (!s_conversion_complete)
    {
    }

    return s_temperature_celsius;
}