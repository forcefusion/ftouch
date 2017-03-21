/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/** @file
 * @defgroup nrf_adc_example main.c
 * @{
 * @ingroup nrf_adc_example
 * @brief ADC Example Application main file.
 *
 * This file contains the source code for a sample application using ADC.
 *
 * @image html example_board_setup_a.jpg "Use board setup A for this example."
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "nrf.h"
#include "nrf_drv_saadc.h"
#include "nrf_drv_ppi.h"
#include "nrf_drv_timer.h"
#include "boards.h"
#include "app_error.h"
#include "nrf_delay.h"
#include "app_util_platform.h"
#include <string.h>
#include "app_timer.h"

#define NRF_LOG_MODULE_NAME "APP"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#define SCAN_RATE	40		// (Hz)
#define APP_TIMER_PRESCALER             0                                           /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /**< Size of timer operation queues. */
#define TIMER_INTERVAL APP_TIMER_TICKS(1000 / SCAN_RATE, APP_TIMER_PRESCALER)

APP_TIMER_DEF(m_app_timer_id);

#define DOUT_LINES 24
#define STCP ARDUINO_8_PIN
#define SHCP ARDUINO_9_PIN
#define DS ARDUINO_10_PIN
#define LOW 0
#define HIGH 1

nrf_saadc_value_t tact[24][16] = {0};

#define SAMPLES_IN_BUFFER 1
volatile uint8_t state = 1;

static const nrf_drv_timer_t m_timer = NRF_DRV_TIMER_INSTANCE(0);
//static nrf_saadc_value_t     m_buffer_pool[2][SAMPLES_IN_BUFFER];
static nrf_ppi_channel_t     m_ppi_channel;
static uint32_t              m_adc_evt_counter;

static uint16_t m_scan_times = 0;

#define MEASURE_SCANTIME
#define PRINT_SCAN_BUFFER
//#define SINGLE_SHOT_SCAN

void timer_handler(nrf_timer_event_t event_type, void * p_context)
{

}


void saadc_sampling_event_init(void)
{
    ret_code_t err_code;

    err_code = nrf_drv_ppi_init();
    APP_ERROR_CHECK(err_code);

    nrf_drv_timer_config_t timer_cfg = NRF_DRV_TIMER_DEFAULT_CONFIG;
    timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
    err_code = nrf_drv_timer_init(&m_timer, &timer_cfg, timer_handler);
    APP_ERROR_CHECK(err_code);

    /* setup m_timer for compare event every 400ms */
    uint32_t ticks = nrf_drv_timer_ms_to_ticks(&m_timer, 400);
    nrf_drv_timer_extended_compare(&m_timer,
                                   NRF_TIMER_CC_CHANNEL0,
                                   ticks,
                                   NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
                                   false);
    nrf_drv_timer_enable(&m_timer);

    uint32_t timer_compare_event_addr = nrf_drv_timer_compare_event_address_get(&m_timer,
                                                                                NRF_TIMER_CC_CHANNEL0);
    uint32_t saadc_sample_task_addr   = nrf_drv_saadc_sample_task_get();

    /* setup ppi channel so that timer compare event is triggering sample task in SAADC */
    err_code = nrf_drv_ppi_channel_alloc(&m_ppi_channel);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_ppi_channel_assign(m_ppi_channel,
                                          timer_compare_event_addr,
                                          saadc_sample_task_addr);
    APP_ERROR_CHECK(err_code);
}


void saadc_sampling_event_enable(void)
{
    ret_code_t err_code = nrf_drv_ppi_channel_enable(m_ppi_channel);

    APP_ERROR_CHECK(err_code);
}


void saadc_callback(nrf_drv_saadc_evt_t const * p_event)
{
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
    {
        ret_code_t err_code;

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, SAMPLES_IN_BUFFER);
        APP_ERROR_CHECK(err_code);

        int i;
        NRF_LOG_INFO("ADC event number: %d\r\n", (int)m_adc_evt_counter);

        for (i = 0; i < SAMPLES_IN_BUFFER; i++)
        {
            NRF_LOG_INFO("%d\r\n", p_event->data.done.p_buffer[i]);
        }
        m_adc_evt_counter++;
    }
}


void saadc_init(void)
{
    ret_code_t err_code;

		nrf_drv_saadc_config_t saadc_config = NRF_DRV_SAADC_DEFAULT_CONFIG;
		saadc_config.resolution = NRF_SAADC_RESOLUTION_12BIT;
		//saadc_config.oversample = NRF_SAADC_OVERSAMPLE_4X;
		err_code = nrf_drv_saadc_init(&saadc_config, saadc_callback);
    APP_ERROR_CHECK(err_code);

    nrf_saadc_channel_config_t channel_config =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN2);
		channel_config.gain = NRF_SAADC_GAIN1_4;
		//channe_config.burst = NRF_SAADC_BURST_ENABLED;
    err_code = nrf_drv_saadc_channel_init(0, &channel_config);
    APP_ERROR_CHECK(err_code);

    //err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[0], SAMPLES_IN_BUFFER);
    //APP_ERROR_CHECK(err_code);

    //err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[1], SAMPLES_IN_BUFFER);
    //APP_ERROR_CHECK(err_code);

}

void exp_io_init() {
		// Output IO Init
		nrf_gpio_range_cfg_output(ARDUINO_4_PIN, ARDUINO_12_PIN);

		// 4067 chip enable pin
		nrf_gpio_pin_toggle(ARDUINO_11_PIN);		// ARDUINO_A1_PIN read
		//nrf_gpio_pin_toggle(ARDUINO_12_PIN);		// ARDUINO_A0_PIN read
}


void exp_io_out_sel(int line) {
 
	if (line > DOUT_LINES -1) return;
	int l = line < 12 ? 11 - line : line;
	
  nrf_gpio_pin_write(SHCP, LOW);
  nrf_gpio_pin_write(STCP, LOW);

  for (int i = 0; i < DOUT_LINES; i++) {
    nrf_gpio_pin_write(DS, i == l);
		nrf_gpio_pin_write(SHCP, HIGH);
		nrf_gpio_pin_write(SHCP, LOW);
  }

  nrf_gpio_pin_write(STCP, HIGH);
}


void exp_io_in_inc(int sel_start, int sel_end) {
	if (!nrf_gpio_pin_out_read(sel_start)) {
		nrf_gpio_pin_toggle(sel_start);
	}
	else if (sel_start < sel_end) {
		nrf_gpio_pin_toggle(sel_start++);
		exp_io_in_inc(sel_start, sel_end);
	}
	else {
		nrf_gpio_pin_toggle(sel_end);
	}
}

void scan_sensors() {
#ifdef MEASURE_SCANTIME
		int x = app_timer_cnt_get();
#endif
	
	int j;
	
	for (int i = 0; i < 24; i++) {
		j = 0;
		exp_io_out_sel(i);

		while (j < 16) {
			nrf_saadc_value_t val;
			nrf_drv_saadc_sample();
			nrf_drv_saadc_sample_convert(0, &val);
			val -=95;
			tact[i][j] = val < 0 ? 0 : val;
			exp_io_in_inc(ARDUINO_4_PIN, ARDUINO_7_PIN);
			j++;
		}
	}
#ifdef SINGLE_SHOT_SCAN
#ifdef MEASURE_SCANTIME
		int y = app_timer_cnt_get();
		NRF_LOG_RAW_INFO("Scan Time: (%d - %d) = %d (%d ms)\r\n", y, x, x - y, (y - x)/32);
#endif
#endif

#ifdef SINGLE_SHOT_SCAN	
#ifdef PRINT_SCAN_BUFFER
		for (int j = 16; j >= 0; j--) {
			if (j > 15) {
				NRF_LOG_RAW_INFO("--\t");
			}
			else {
				NRF_LOG_RAW_INFO("%d\t", j);
			}

			for (int i = 0; i < 24; i++) {
				if (j > 15) {
					NRF_LOG_RAW_INFO("%d\t", i);
				}
				else {
					NRF_LOG_RAW_INFO("%d\t", tact[i][j]);
				}
			}
			NRF_LOG_RAW_INFO("\r\n");
		        NRF_LOG_FLUSH();
		}
		NRF_LOG_RAW_INFO("\r\n");
    NRF_LOG_FLUSH();
#endif
#endif
}

void timer_timeout_handler(void * p_context) {
#ifndef SINGLE_SHOT_SCAN
	scan_sensors();
	NRF_LOG_INFO("Scan times = %d\r\n", m_scan_times++);
#endif
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void application_timer_init(void)
{
		NRF_CLOCK->TASKS_LFCLKSTART = 1;
		while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0);

    // Initialize timer module.
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);

    // Create timers.
		uint32_t err_code;
    err_code = app_timer_create(&m_app_timer_id, APP_TIMER_MODE_REPEATED, timer_timeout_handler);
    APP_ERROR_CHECK(err_code);
}

static void application_timers_start(void)
{
		uint32_t err_code;
    err_code = app_timer_start(m_app_timer_id, TIMER_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);
}


void task_timer_event_handler(nrf_timer_event_t event_type, void* p_context)
{
    switch (event_type)
    {
        case NRF_TIMER_EVENT_COMPARE0:
            scan_sensors();
            break;

        default:
            //Do nothing.
            break;
    }
}


/**
 * @brief Function for main application entry.
 */
int main(void)
{
    uint32_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
	
    NRF_LOG_INFO("SAADC HAL simple example.\r\n");
    NRF_LOG_FLUSH();
    saadc_init();
    //saadc_sampling_event_init();
    //saadc_sampling_event_enable();
		exp_io_init();
	
		application_timer_init();
		application_timers_start();

#ifdef SINGLE_SHOT_SCAN
		scan_sensors();
#endif
	
    while (1)
    {
        __WFE();
        NRF_LOG_FLUSH();
    }
}


/** @} */
