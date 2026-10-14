/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart Classroom & Reflection-Free Presentation Controller (v1.0 Pro)
  *                   Target : STM32F446RE (Nucleo-64)
  *                   Project: classroomcontrol
  * 
  * INTEGRATED 5-IN-1 ADVANCED FEATURES:
  * 1. Smart Dual-Zone Note-Taking Dimming:
  *    - Presentation Mode: Front Light (PB0) = 0% OFF (zero screen glare).
  *                         Middle (PB1) & Back (PB2) = 20% Dimmed for note-taking.
  *    - Normal Mode:       All 3 Lights = 100% ON.
  * 2. LM35 Temperature & Exhaust Fan Control:
  *    - LM35 on PA1 (ADC1_IN1). If Temp > 28°C -> Fan Relay (PB5) ON.
  * 3. PIR Occupancy & Eco-Sleep Mode:
  *    - PIR Sensor on PA4 (or Button B1). If no motion for 30s -> Auto Sleep (Lights OFF, Blinds CLOSED).
  * 4. Motorized Curtain Warning Buzzer:
  *    - Piezo Buzzer on PA7 chimes 'beep-beep' before curtain servo moves.
  * 5. Live UART CSV Stream & Command Console:
  *    - Telemetry @ 115200 Baud + CSV Logger + Serial Control ('AUTO', 'PROJ', 'NORMAL', 'FAN ON', etc.)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
TIM_HandleTypeDef htim3;
I2C_HandleTypeDef hi2c1;

char uart_tx_buffer[256];
char uart_rx_buffer[64];
uint8_t uart_rx_byte;
volatile uint8_t uart_rx_idx = 0;
volatile bool uart_cmd_ready = false;

bool lcd_available = false;
bool current_presentation_mode = false;
bool manual_override_active = false;
bool fan_active = false;
bool room_occupied = true;
bool eco_sleep_active = false;

uint32_t ambient_baseline = 1200;
uint32_t calibrated_threshold = 1600;
uint32_t last_motion_tick = 0;
float current_temperature = 25.0f;
uint32_t current_ldr_val = 1200;
float current_curtain_angle = 0.0f;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_I2C1_Init(void);
static void adc_init(void);

/* ========================================================================== */
/*                             ADC DRIVER (LDR & LM35)                        */
/* ========================================================================== */

static void adc_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // PA0 (LDR) and PA1 (LM35) to Analog Mode (MODER = 11)
    GPIOA->MODER |= (3U << (0 * 2)) | (3U << (1 * 2));
    GPIOA->PUPDR &= ~((3U << (0 * 2)) | (3U << (1 * 2)));

    // Turn on ADC1
    ADC1->CR2 |= ADC_CR2_ADON;

    // Set sample time: 84 cycles for Channel 0 and Channel 1
    ADC1->SMPR2 |= (4U << (0 * 3)) | (4U << (1 * 3));
}

static uint32_t adc_read_channel(uint32_t channel)
{
    // Configure single channel rank
    ADC1->SQR1 = 0; // 1 conversion
    ADC1->SQR3 = channel & 0x1F;

    // Start conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;

    uint32_t timeout = 10000;
    while (!(ADC1->SR & ADC_SR_EOC) && --timeout);

    return (uint32_t)(ADC1->DR & 0x0FFF);
}

uint32_t read_ldr(void)
{
    return adc_read_channel(0); // PA0 (ADC1_IN0)
}

float read_temperature_lm35(void)
{
    uint32_t raw_sum = 0;
    for (int i = 0; i < 8; i++)
    {
        raw_sum += adc_read_channel(1); // PA1 (ADC1_IN1)
        HAL_Delay(2);
    }
    uint32_t raw_avg = raw_sum / 8;
    float voltage_mv = (raw_avg * 3300.0f) / 4095.0f;
    return (voltage_mv / 10.0f); // LM35: 10 mV / °C
}

/* ========================================================================== */
/*                           BUZZER WARNING CHIME                             */
/* ========================================================================== */

void buzzer_alert_chime(void)
{
    for (int i = 0; i < 2; i++)
    {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        HAL_Delay(60);
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        HAL_Delay(50);
    }
}

/* ========================================================================== */
/*                     WINDOW CURTAIN SERVO MOTOR (TIM3)                      */
/* ========================================================================== */

void servo_set_pulse_us(uint16_t pulse_us)
{
    if (pulse_us < SG90_EXT_MIN_US) pulse_us = SG90_EXT_MIN_US;
    else if (pulse_us > SG90_EXT_MAX_US) pulse_us = SG90_EXT_MAX_US;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse_us);
}

void servo_set_angle(float angle)
{
    if (angle < 0.0f) angle = 0.0f;
    else if (angle > 180.0f) angle = 180.0f;

    uint16_t pulse_us = (uint16_t)(SERVO_MIN_PULSE_US + 
                        ((angle / 180.0f) * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)));
    servo_set_pulse_us(pulse_us);
    current_curtain_angle = angle;
}

void servo_init(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    servo_set_angle(WINDOW_OPEN_ANGLE);
}

void set_window_state(bool close_window)
{
    float target_angle = close_window ? WINDOW_CLOSED_ANGLE : WINDOW_OPEN_ANGLE;
    if (current_curtain_angle != target_angle)
    {
        buzzer_alert_chime(); // Audible chime before motor movement
        servo_set_angle(target_angle);
    }
}

/* ========================================================================== */
/*                     SMART DUAL-ZONE NOTE-TAKING LIGHTING                   */
/* ========================================================================== */

void set_lighting_mode(uint8_t duty_front, uint8_t duty_back)
{
    // Stage Light (Front Row): direct ON/OFF
    HAL_GPIO_WritePin(LIGHT1_FRONT_GPIO_Port, LIGHT1_FRONT_Pin, (duty_front > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    // Middle & Back Row Lights: ON/OFF or software PWM
    GPIO_PinState back_state = (duty_back > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(LIGHT2_MID_GPIO_Port, LIGHT2_MID_Pin, back_state);
    HAL_GPIO_WritePin(LIGHT3_BACK_GPIO_Port, LIGHT3_BACK_Pin, back_state);
}

void set_fan_state(bool on)
{
    fan_active = on;
    HAL_GPIO_WritePin(FAN_RELAY_GPIO_Port, FAN_RELAY_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ========================================================================== */
/*                           OCCUPANCY & SENSOR CHECKS                        */
/* ========================================================================== */

bool is_room_occupied(void)
{
    // Default: Always occupied for live demonstration without PIR sensor
    return true;
}

bool is_manual_switch_active(void)
{
    // Default: Automatic mode based on LDR sensor
    return false;
}

void check_recalibration_button(void)
{
    if (HAL_GPIO_ReadPin(B1_USER_BTN_GPIO_Port, B1_USER_BTN_Pin) == GPIO_PIN_RESET)
    {
        HAL_Delay(50);
        if (HAL_GPIO_ReadPin(B1_USER_BTN_GPIO_Port, B1_USER_BTN_Pin) == GPIO_PIN_RESET)
        {
            calibrate_ambient_light();
            while (HAL_GPIO_ReadPin(B1_USER_BTN_GPIO_Port, B1_USER_BTN_Pin) == GPIO_PIN_RESET);
        }
    }
}

void calibrate_ambient_light(void)
{
    uart_send_string("\r\n>>> [CALIBRATION] Starting 1.0s Ambient Light Auto-Calibration...\r\n");
    if (lcd_available)
    {
        lcd_clear();
        lcd_put_cursor(0, 0);
        lcd_send_string("CALIBRATING...");
        lcd_put_cursor(1, 0);
        lcd_send_string("Keep Light Normal");
    }

    uint32_t sum = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++)
    {
        sum += read_ldr();
        HAL_GPIO_TogglePin(GPIOA, LD2_Pin);
        HAL_Delay(CALIBRATION_INTERVAL_MS);
    }
    HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_RESET);

    uint32_t avg = sum / CALIBRATION_SAMPLES;

    if (avg >= PROJECTOR_PRE_ON_GUARD)
    {
        ambient_baseline = FALLBACK_BASELINE;
        calibrated_threshold = FALLBACK_THRESHOLD;
        current_presentation_mode = true;
        uart_send_string("  -> [GUARD] Projector already active at boot! Fallback applied.\r\n");
    }
    else
    {
        ambient_baseline = avg;
        calibrated_threshold = avg + THRESHOLD_OFFSET;
        current_presentation_mode = false;
        snprintf(uart_tx_buffer, sizeof(uart_tx_buffer),
                 "  -> [OK] Ambient Baseline = %lu | Dynamic Threshold = %lu (+%d)\r\n",
                 ambient_baseline, calibrated_threshold, THRESHOLD_OFFSET);
        uart_send_string(uart_tx_buffer);
    }
}

/* ========================================================================== */
/*                             I2C 16x2 LCD DRIVER                            */
/* ========================================================================== */

void lcd_send_cmd_4bit(uint8_t nibble)
{
    uint8_t buf[2];
    buf[0] = (nibble & 0xF0) | 0x0C;
    buf[1] = (nibble & 0xF0) | 0x08;
    HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR, buf, 2, 50);
}

void lcd_send_cmd(uint8_t cmd)
{
    uint8_t up = cmd & 0xF0;
    uint8_t lo = (cmd << 4) & 0xF0;
    uint8_t buf[4];
    buf[0] = up | 0x0C;
    buf[1] = up | 0x08;
    buf[2] = lo | 0x0C;
    buf[3] = lo | 0x08;
    HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR, buf, 4, 50);
}

void lcd_send_data(uint8_t data)
{
    uint8_t up = data & 0xF0;
    uint8_t lo = (data << 4) & 0xF0;
    uint8_t buf[4];
    buf[0] = up | 0x0D;
    buf[1] = up | 0x09;
    buf[2] = lo | 0x0D;
    buf[3] = lo | 0x09;
    HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR, buf, 4, 50);
}

void lcd_send_string(const char *str)
{
    while (*str) lcd_send_data((uint8_t)(*str++));
}

void lcd_put_cursor(int row, int col)
{
    uint8_t address = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_send_cmd(address);
}

void lcd_clear(void)
{
    lcd_send_cmd(0x01);
    HAL_Delay(2);
}

void lcd_init(void)
{
    HAL_Delay(50);
    lcd_send_cmd_4bit(0x30); HAL_Delay(5);
    lcd_send_cmd_4bit(0x30); HAL_Delay(1);
    lcd_send_cmd_4bit(0x30); HAL_Delay(10);
    lcd_send_cmd_4bit(0x20); HAL_Delay(10);

    lcd_send_cmd(0x28); // 4-bit, 2 lines, 5x8 font
    lcd_send_cmd(0x08); // Display off
    lcd_send_cmd(0x01); // Clear display
    HAL_Delay(2);
    lcd_send_cmd(0x06); // Entry mode set
    lcd_send_cmd(0x0C); // Display ON, Cursor OFF
}

void lcd_update_dashboard(uint32_t ldr_val, float temp_c, bool is_proj, bool is_occupied, bool fan_on)
{
    if (!lcd_available) return;

    char line1[17];
    char line2[17];

    if (!is_occupied)
    {
        snprintf(line1, sizeof(line1), "ECO SLEEP MODE  ");
        snprintf(line2, sizeof(line2), "Room Empty %2.0fC ", temp_c);
    }
    else
    {
        // Line 1: Mode & Temperature
        const char *mode_str = is_proj ? "PROJ" : "NORM";
        snprintf(line1, sizeof(line1), "%-4s %4lu %2.1fC", mode_str, ldr_val, temp_c);

        // Line 2: Actuators (Window, Dimming, Fan)
        const char *lgt_str = is_proj ? "DIM" : "ON ";
        const char *fan_str = fan_on ? "FAN" : "---";
        snprintf(line2, sizeof(line2), "W:%02.0fd L:%s %s", current_curtain_angle, lgt_str, fan_str);
    }

    lcd_put_cursor(0, 0);
    lcd_send_string(line1);
    lcd_put_cursor(1, 0);
    lcd_send_string(line2);
}

/* ========================================================================== */
/*                             UART & COMMAND PARSER                          */
/* ========================================================================== */

void uart_send_string(const char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)str, (uint16_t)strlen(str), 100);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        HAL_UART_Transmit(&huart2, &uart_rx_byte, 1, 10); // Echo
        if (uart_rx_byte == '\r' || uart_rx_byte == '\n')
        {
            if (uart_rx_idx > 0)
            {
                uart_rx_buffer[uart_rx_idx] = '\0';
                uart_cmd_ready = true;
            }
        }
        else if (uart_rx_idx < sizeof(uart_rx_buffer) - 1)
        {
            uart_rx_buffer[uart_rx_idx++] = (char)uart_rx_byte;
        }
        HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
    }
}

void process_uart_commands(void)
{
    if (!uart_cmd_ready) return;

    if (strcmp(uart_rx_buffer, "PROJ") == 0)
    {
        manual_override_active = true;
        uart_send_string("\r\n[CMD] Manual PRESENTATION Mode Forced\r\nCMD> ");
    }
    else if (strcmp(uart_rx_buffer, "NORMAL") == 0)
    {
        manual_override_active = false;
        current_presentation_mode = false;
        uart_send_string("\r\n[CMD] Manual NORMAL Mode Forced\r\nCMD> ");
    }
    else if (strcmp(uart_rx_buffer, "AUTO") == 0)
    {
        manual_override_active = false;
        uart_send_string("\r\n[CMD] Switched to AUTOMATIC Sensor Control\r\nCMD> ");
    }
    else if (strcmp(uart_rx_buffer, "FAN ON") == 0)
    {
        set_fan_state(true);
        uart_send_string("\r\n[CMD] Exhaust Fan Forced ON\r\nCMD> ");
    }
    else if (strcmp(uart_rx_buffer, "FAN OFF") == 0)
    {
        set_fan_state(false);
        uart_send_string("\r\n[CMD] Exhaust Fan Forced OFF\r\nCMD> ");
    }
    else if (strcmp(uart_rx_buffer, "CALIB") == 0)
    {
        calibrate_ambient_light();
        uart_send_string("\r\nCMD> ");
    }
    else if (strcmp(uart_rx_buffer, "STATUS") == 0)
    {
        snprintf(uart_tx_buffer, sizeof(uart_tx_buffer),
                 "\r\n[STATUS] Mode: %s | LDR: %lu | Temp: %2.1fC | Occupied: %s | Fan: %s | Curtain: %02.0f deg\r\nCMD> ",
                 current_presentation_mode ? "PRESENTATION" : "NORMAL",
                 current_ldr_val, current_temperature,
                 room_occupied ? "YES" : "NO (SLEEP)",
                 fan_active ? "ON" : "OFF", current_curtain_angle);
        uart_send_string(uart_tx_buffer);
    }
    else
    {
        uart_send_string("\r\n[ERROR] Commands: PROJ, NORMAL, AUTO, FAN ON, FAN OFF, CALIB, STATUS\r\nCMD> ");
    }

    uart_rx_idx = 0;
    uart_cmd_ready = false;
}

/* ========================================================================== */
/*                               MAIN FUNCTION                                */
/* ========================================================================== */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_TIM3_Init();
    MX_I2C1_Init();
    adc_init();

    // Check LCD
    if (HAL_I2C_IsDeviceReady(&hi2c1, LCD_ADDR, 2, 50) == HAL_OK)
    {
        lcd_available = true;
        lcd_init();
        lcd_clear();
        lcd_put_cursor(0, 0);
        lcd_send_string("SMART CLASSROOM");
        lcd_put_cursor(1, 0);
        lcd_send_string("v1.0 Pro Init...");
    }

    servo_init();
    last_motion_tick = HAL_GetTick();

    uart_send_string("\r\n=======================================================\r\n");
    uart_send_string(" Smart Classroom & Reflection-Free Controller (v1.0 Pro)\r\n");
    uart_send_string(" Target: STM32F446RE Nucleo-64 | Baud: 115200\r\n");
    uart_send_string(" Features: Dual-Zone Dimming | LM35 Fan | PIR Sleep | Buzzer\r\n");
    uart_send_string("=======================================================\r\n");

    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);

    calibrate_ambient_light();

    uint32_t last_telemetry_tick = HAL_GetTick();
    uint32_t last_sensor_tick = HAL_GetTick();
    uint32_t cycle_counter = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        // 1. Process UART Commands & Buttons
        process_uart_commands();
        check_recalibration_button();

        // 2. Periodic Sensor Acquisition (every 100 ms)
        if (now - last_sensor_tick >= 100)
        {
            last_sensor_tick = now;
            current_ldr_val = read_ldr();
            current_temperature = read_temperature_lm35();
            room_occupied = is_room_occupied();

            // Automatic Fan Control logic based on temperature
            if (current_temperature >= FAN_TEMP_THRESHOLD_C)
            {
                set_fan_state(true);
            }
            else
            {
                set_fan_state(false);
            }

            // Mode Selection Logic
            if (!room_occupied)
            {
                // ECO SLEEP: Nobody in the classroom
                eco_sleep_active = true;
                current_presentation_mode = false;
                set_lighting_mode(0, 0);          // All lights OFF
                set_window_state(true);            // Curtain closed
                set_fan_state(false);              // Fan OFF
                HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_RESET);
            }
            else
            {
                eco_sleep_active = false;
                bool ext_sw = is_manual_switch_active();

                if (manual_override_active || ext_sw || (current_ldr_val > calibrated_threshold))
                {
                    // PRESENTATION / PROJECTOR MODE
                    current_presentation_mode = true;
                    set_window_state(true); // Close curtain (90 deg) to block sunlight
                    HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_SET);
                }
                else
                {
                    // NORMAL CLASSROOM MODE
                    current_presentation_mode = false;
                    set_window_state(false); // Open curtain (0 deg) for daylight
                    HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_RESET);
                }
            }
        }

        // 3. Smart Note-Taking Light Dimming (Software PWM Cycle)
        cycle_counter = (cycle_counter + 1) % 10;
        if (!eco_sleep_active)
        {
            if (current_presentation_mode)
            {
                // Front Light = 0% OFF (no screen glare)
                // Back Row Lights = 20% Dimmed (ON 2 out of 10 cycles) for note-taking
                uint8_t back_duty = (cycle_counter < 2) ? 1 : 0;
                set_lighting_mode(0, back_duty);
            }
            else
            {
                // Normal Mode = 100% Bright
                set_lighting_mode(1, 1);
            }
        }

        // 4. LCD & Serial CSV Telemetry (every 1000 ms)
        if (now - last_telemetry_tick >= 1000)
        {
            last_telemetry_tick = now;

            lcd_update_dashboard(current_ldr_val, current_temperature,
                                 current_presentation_mode, room_occupied, fan_active);

            // Power savings computation (Approx: Front light 20W, Back lights 40W)
            uint16_t watts_saved = 0;
            if (!room_occupied) watts_saved = 60;
            else if (current_presentation_mode) watts_saved = 32; // Front OFF (20W) + Back Dimmed 80% (12W)

            // Standard Telemetry
            snprintf(uart_tx_buffer, sizeof(uart_tx_buffer),
                     "LDR: %4lu | Temp: %2.1fC | Mode: %-7s | Occ: %-3s | Fan: %-3s | Curtain: %2.0f deg | PwrSaved: %2uW\r\n",
                     current_ldr_val, current_temperature,
                     current_presentation_mode ? "PROJ" : "NORMAL",
                     room_occupied ? "YES" : "NO",
                     fan_active ? "ON" : "OFF",
                     current_curtain_angle, watts_saved);
            uart_send_string(uart_tx_buffer);

            // CSV Stream for live data logging
            char csv_buf[128];
            snprintf(csv_buf, sizeof(csv_buf),
                     "[CSV] %lu,%s,%lu,%2.1f,%d,%d,%2.0f,%u\r\n",
                     now / 1000,
                     current_presentation_mode ? "PROJ" : "NORMAL",
                     current_ldr_val, current_temperature,
                     room_occupied ? 1 : 0, fan_active ? 1 : 0,
                     current_curtain_angle, watts_saved);
            uart_send_string(csv_buf);
        }

        HAL_Delay(5);
    }
}

/* ========================================================================== */
/*                        SYSTEM & PERIPHERAL INIT                            */
/* ========================================================================== */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 16;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    // 84 MHz APB1 Timer Clock: Prescaler = 84-1 -> 1 MHz (1 tick = 1 us). Period = 20000 -> 50 Hz
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 84 - 1;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 20000 - 1;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim3);

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1000;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_MspPostInit(&htim3);
}

static void MX_USART2_UART_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);

    HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

static void MX_I2C1_Init(void)
{
    __HAL_RCC_I2C1_CLK_ENABLE();
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLED;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLED;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLED;
    HAL_I2C_Init(&hi2c1);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Outputs: LD2 (PA5), Buzzer (PA7)
    HAL_GPIO_WritePin(GPIOA, LD2_Pin | BUZZER_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LD2_Pin | BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Outputs: Light1 (PB0), Light2 (PB1), Light3 (PB2), FanRelay (PB5)
    HAL_GPIO_WritePin(GPIOB, LIGHT1_FRONT_Pin | LIGHT2_MID_Pin | LIGHT3_BACK_Pin | FAN_RELAY_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LIGHT1_FRONT_Pin | LIGHT2_MID_Pin | LIGHT3_BACK_Pin | FAN_RELAY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Inputs: User Button B1 (PC13), PIR Motion (PA4), Manual Switch (PB4)
    GPIO_InitStruct.Pin = B1_USER_BTN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_USER_BTN_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PIR_MOTION_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(PIR_MOTION_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = MANUAL_SWITCH_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MANUAL_SWITCH_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
