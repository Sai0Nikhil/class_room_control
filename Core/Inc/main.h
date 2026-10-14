/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for Smart Classroom Controller (v1.0 Pro)
  *                   Project: classroomcontrol
  *                   Target : STM32F446RE (Nucleo-64)
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

// ADC Sensor Reads
uint32_t read_ldr(void);
float read_temperature_lm35(void);

// Actuator & Lighting Controls
void set_lighting_mode(uint8_t duty_front, uint8_t duty_back);
void set_fan_state(bool on);
void buzzer_alert_chime(void);

// Servo Window Blind
void servo_init(void);
void servo_set_pulse_us(uint16_t pulse_us);
void servo_set_angle(float angle);
void set_window_state(bool close_window);

// Dynamic Calibration & Mode Control
void calibrate_ambient_light(void);
bool is_manual_switch_active(void);
bool is_room_occupied(void);
void check_recalibration_button(void);

// LCD Display Driver
void lcd_init(void);
void lcd_send_cmd(uint8_t cmd);
void lcd_send_data(uint8_t data);
void lcd_send_string(const char *str);
void lcd_put_cursor(int row, int col);
void lcd_clear(void);
void lcd_update_dashboard(uint32_t ldr_val, float temp_c, bool is_proj, bool is_occupied, bool fan_on);

// UART Telemetry & Commands
void uart_send_string(const char *str);
void process_uart_commands(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Hardware Pin Definitions --------------------------------------------------*/
// Sensors
#define LDR_ADC_Pin               GPIO_PIN_0
#define LDR_ADC_GPIO_Port          GPIOA
#define LM35_ADC_Pin              GPIO_PIN_1
#define LM35_ADC_GPIO_Port         GPIOA
#define PIR_MOTION_Pin            GPIO_PIN_4
#define PIR_MOTION_GPIO_Port       GPIOA

// Actuators
#define SERVO_PWM_Pin             GPIO_PIN_6
#define SERVO_PWM_GPIO_Port        GPIOA
#define BUZZER_Pin                GPIO_PIN_7
#define BUZZER_GPIO_Port           GPIOA
#define LIGHT1_FRONT_Pin          GPIO_PIN_0
#define LIGHT1_FRONT_GPIO_Port     GPIOB
#define LIGHT2_MID_Pin            GPIO_PIN_1
#define LIGHT2_MID_GPIO_Port       GPIOB
#define LIGHT3_BACK_Pin           GPIO_PIN_2
#define LIGHT3_BACK_GPIO_Port      GPIOB
#define FAN_RELAY_Pin             GPIO_PIN_5
#define FAN_RELAY_GPIO_Port        GPIOB

// Controls & Indicators
#define MANUAL_SWITCH_Pin         GPIO_PIN_4
#define MANUAL_SWITCH_GPIO_Port    GPIOB
#define B1_USER_BTN_Pin           GPIO_PIN_13
#define B1_USER_BTN_GPIO_Port      GPIOC
#define LD2_Pin                   GPIO_PIN_5
#define LD2_GPIO_Port              GPIOA

// UART & LCD
#define USART_TX_Pin              GPIO_PIN_2
#define USART_TX_GPIO_Port         GPIOA
#define USART_RX_Pin              GPIO_PIN_3
#define USART_RX_GPIO_Port         GPIOA
#define I2C1_SCL_Pin              GPIO_PIN_8
#define I2C1_SCL_GPIO_Port         GPIOB
#define I2C1_SDA_Pin              GPIO_PIN_9
#define I2C1_SDA_GPIO_Port         GPIOB

/* Configuration Constants ---------------------------------------------------*/
#define LCD_ADDR                  (0x27 << 1)

#define CALIBRATION_SAMPLES       20
#define CALIBRATION_INTERVAL_MS   50
#define THRESHOLD_OFFSET          400
#define PROJECTOR_PRE_ON_GUARD    2800
#define FALLBACK_BASELINE         1500
#define FALLBACK_THRESHOLD        2200

#define WINDOW_CLOSED_ANGLE       90.0f
#define WINDOW_OPEN_ANGLE         0.0f
#define SERVO_MIN_PULSE_US        1000
#define SERVO_MAX_PULSE_US        2000
#define SG90_EXT_MIN_US           500
#define SG90_EXT_MAX_US           2500

#define FAN_TEMP_THRESHOLD_C      28.0f // Turn on fan if temperature exceeds 28.0 deg C
#define AUTO_SLEEP_TIMEOUT_MS     30000 // 30 seconds of no motion enters ECO SLEEP

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
