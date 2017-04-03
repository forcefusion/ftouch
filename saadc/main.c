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

//#include <stdbool.h>
//#include <stdint.h>
//#include <stdio.h>
#include <math.h>
//#include "nrf.h"
#include "nrf_drv_saadc.h"
//#include "nrf_drv_ppi.h"
//#include "nrf_drv_timer.h"
#include "boards.h"
#include "app_error.h"
#include "nrf_delay.h"
//#include "app_util_platform.h"
//#include <string.h>
#include "app_timer.h"

#define NRF_LOG_MODULE_NAME "APP"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

typedef struct
{
    uint32_t           frame_id;
		float							 x;
		float							 y;
		float							 z;
} touch_event_t;

#define APP_TIMER_PRESCALER             0                                           /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /**< Size of timer operation queues. */
#define SENSOR_SCAN_INTERVAL APP_TIMER_TICKS(1000 / SCAN_RATE, APP_TIMER_PRESCALER)

#define SCAN_RATE	100		// (Hz)
#define OFFSET_VALUE 65
#define ROWS 16
#define COLS 24
#define TACT_BUF_SZ ROWS * COLS
#define TOUCH_SQR_SZ 5	// odd number only, minimum = 3
#define REPORT_SCALE 10

#define DOUT_LINES COLS
#define STCP ARDUINO_8_PIN
#define SHCP ARDUINO_9_PIN
#define DS ARDUINO_10_PIN
#define LOW 0
#define HIGH 1

#define OFFSET(x, y) (x - y > 0 ? x - y : 0)

#define FLOATING_BUF_SIZE 2
static uint16_t tact_buf[COLS][ROWS];
static nrf_saadc_value_t floating_buf[FLOATING_BUF_SIZE][COLS][ROWS];
static uint8_t floating_buf_idx = 0;
static uint16_t touch_sqr_buf[TOUCH_SQR_SZ][TOUCH_SQR_SZ];
static uint32_t scan_counter = 0;

touch_event_t last_touch = {
	.frame_id = 0,
	.x = 0,
	.y = 0,
	.z = 0
};

touch_event_t last_move = {
	.frame_id = 0,
	.x = 0,
	.y = 0,
	.z = 0
};

#define SAMPLES_IN_BUFFER 1
APP_TIMER_DEF(m_app_timer_id);


void saadc_callback(nrf_drv_saadc_evt_t const * p_event)
{

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
}

void exp_io_init() 
{
		// Output IO Init
		nrf_gpio_range_cfg_output(ARDUINO_4_PIN, ARDUINO_12_PIN);

		// 4067 chip enable pin
		nrf_gpio_pin_toggle(ARDUINO_11_PIN);		// ARDUINO_A1_PIN read
		//nrf_gpio_pin_toggle(ARDUINO_12_PIN);		// ARDUINO_A0_PIN read
}


void exp_io_out_sel(int line)
{
 
	if (line > DOUT_LINES -1) return;
	int l = line < 12 ? 11 - line : line;
	
  nrf_gpio_pin_write(SHCP, LOW);
  nrf_gpio_pin_write(STCP, LOW);

  for (int i = 0; i < DOUT_LINES; i++) {
    nrf_gpio_pin_write(DS, i == l);
		nrf_delay_us(1);
		nrf_gpio_pin_write(SHCP, HIGH);
		nrf_delay_us(1);
		nrf_gpio_pin_write(SHCP, LOW);
		nrf_delay_us(1);
  }

  nrf_gpio_pin_write(STCP, HIGH);
}


void exp_io_in_inc(int sel_start, int sel_end)
{
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


bool is_touch_center(uint16_t* ptr, int i, int j)
{
		bool ret = true;
		int16_t offset = (TOUCH_SQR_SZ - 1) / 2;
		int16_t cur_pos = (i * ROWS) + j;
		int16_t lowest_corner = cur_pos - offset - (ROWS * offset);
		uint16_t cur_node_val = *(ptr + cur_pos);

		memset(touch_sqr_buf, 0, sizeof(touch_sqr_buf));
	
		for (int m = 0; m < TOUCH_SQR_SZ; m++) {
			for (int n = 0; n < TOUCH_SQR_SZ; n++) {
				int16_t comp_node = lowest_corner + (m * ROWS) + n;

				if (comp_node < 0 || comp_node == cur_pos) continue;
				if (comp_node >= TACT_BUF_SZ) break;
				
				uint16_t comp_node_val = *(ptr + comp_node);
				ret &= comp_node < cur_pos ? cur_node_val > comp_node_val : cur_node_val >= comp_node_val;

				if (ret) { touch_sqr_buf[m][n] = comp_node_val; }
				else { return false; }
			}
		}
		
		touch_sqr_buf[(TOUCH_SQR_SZ -  1) / 2][(TOUCH_SQR_SZ - 1) / 2] = cur_node_val;
		return true;
}


void scan_sensors()
{
	int j;
	bool col_sampled[COLS];
	memset(col_sampled, 0, sizeof(col_sampled) / 8);

	//if (scan_counter % SCAN_RATE == 0) NRF_LOG_RAW_INFO("SCAN-%d: \r\n", scan_counter);
	
	// frame sampling
	for (int i = 0; i < COLS; i++) {
		j = 0;
		exp_io_out_sel(i);

		while (j < ROWS) {
			nrf_drv_saadc_sample();
			nrf_drv_saadc_sample_convert(0, &floating_buf[floating_buf_idx % FLOATING_BUF_SIZE][i][j]);

			nrf_saadc_value_t floating_buf_sum = 0;
			for (int x = 0; x < FLOATING_BUF_SIZE; x++) { floating_buf_sum += floating_buf[x][i][j]; }
			tact_buf[i][j] = OFFSET(floating_buf_sum / FLOATING_BUF_SIZE, OFFSET_VALUE);
			col_sampled[i] |= tact_buf[i][j] > 0;

			/*
			if (tact_buf[i][j] > 0 && scan_counter % SCAN_RATE == 0) {
				NRF_LOG_RAW_INFO("(%d, %d) = %d\r\n", i, j, tact_buf[i][j]);
			}
			*/
			exp_io_in_inc(ARDUINO_4_PIN, ARDUINO_7_PIN);
			j++;
		}
	}


	// process frame samples
	for (int i = 0; i < COLS; i++) {
		if (col_sampled[i]) {				// skip column without samples
			for (int j = 0; j < ROWS; j++) {
				if (tact_buf[i][j] > 0) {
					if (is_touch_center(&tact_buf[0][0], i, j)) {
							uint16_t total_force = 0;
							uint16_t prev_neighbor_force = 0, next_neighbor_force = 0;
							float hDelta = 0, vDelta = 0;

							// calculating force center 
							for (int m = 0; m < TOUCH_SQR_SZ; m++) {
								uint16_t vSum = 0, hSum = 0;
								for (int n = 0; n < TOUCH_SQR_SZ; n++) {
									total_force += touch_sqr_buf[m][n];
									hSum += touch_sqr_buf[m][n];
									vSum += touch_sqr_buf[n][m];
								}

								hDelta += 1.0 * hSum * m / (TOUCH_SQR_SZ - 1);
								vDelta += 1.0 * vSum * m / (TOUCH_SQR_SZ - 1);
								if (m == (TOUCH_SQR_SZ - 1) / 2 - 1) prev_neighbor_force = hSum;
								else if (m == (TOUCH_SQR_SZ - 1) / 2 + 1) next_neighbor_force = hSum;
							}						

							float hDeviation = hDelta / total_force * 2 - 1;
							float vDeviation = vDelta / total_force * 2 - 1;
							float posx = (i + hDeviation);
							float posy = (j + vDeviation);
							
							uint16_t center_force = touch_sqr_buf[(TOUCH_SQR_SZ - 1) / 2][(TOUCH_SQR_SZ - 1) / 2];
							if (hDeviation > 0) {
								center_force += hDeviation * (center_force - prev_neighbor_force) / 2;
							}
							else if (hDeviation < 0) {
								center_force -= hDeviation * (center_force - next_neighbor_force) / 2;
							}

							float distX = 0, distY = 0;
							
							if (scan_counter - last_move.frame_id <= 10) {
								distX = posx - last_move.x;
								distY = posy - last_move.y;
							}
							else if (scan_counter - last_touch.frame_id <= 5) {
								distX = posx - last_touch.x;
								distY = posy - last_touch.y;
							}
							float dist = pow((pow(distX, 2) + pow(distY, 2)), 0.5);

							NRF_LOG_RAW_INFO("Frame(%d): ", scan_counter);
							NRF_LOG_RAW_INFO("(%d, %d) / (" NRF_LOG_FLOAT_MARKER ", " NRF_LOG_FLOAT_MARKER ")", i, j, NRF_LOG_FLOAT(posx), NRF_LOG_FLOAT(posy));
							NRF_LOG_RAW_INFO(" / (" NRF_LOG_FLOAT_MARKER ", " NRF_LOG_FLOAT_MARKER ")", NRF_LOG_FLOAT(distX), NRF_LOG_FLOAT(distY));

							if (dist > 0) {
								int16_t moveX, moveY;

								moveX = dist * distX * (scan_counter - last_move.frame_id);
								moveY = dist * distY * (scan_counter - last_move.frame_id);
									
								NRF_LOG_RAW_INFO(",\tMOVE: ", scan_counter);
								NRF_LOG_RAW_INFO("%d, %d", moveX, moveY);
								
								//if (moveX != 0 || moveY !=0) mouse_movement_send(moveX, -moveY);

								last_move.frame_id = scan_counter;
								last_move.x = posx;
								last_move.y = posy;
								last_move.z = center_force;							
							}
							NRF_LOG_RAW_INFO("\r\n");

							last_touch.frame_id = scan_counter;
							last_touch.x = posx;
							last_touch.y = posy;
							last_touch.z = center_force;							
					}
				}
			}
		}
	}
	//if (scan_counter % SCAN_RATE == 0) NRF_LOG_RAW_INFO("\r\n");

	floating_buf_idx++;
	scan_counter++;
}

void timer_timeout_handler(void * p_context) 
{
	scan_sensors();
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
    err_code = app_timer_start(m_app_timer_id, SENSOR_SCAN_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);
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
		exp_io_init();
	
		application_timer_init();
		application_timers_start();
	
    while (1)
    {
        __WFE();
        NRF_LOG_FLUSH();
    }
}


/** @} */
