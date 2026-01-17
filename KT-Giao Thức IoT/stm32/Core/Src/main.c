/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (DHT22 + UART command receive)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
// Include CDC interface for debug output
extern uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
char msg[120];

uint16_t soil_value = 0;
float lux = 0;
uint8_t bh1750_data[2];

// DHT22 buffer and variables
uint8_t dht_bits[5];
float dht_temperature = 0.0f;
float dht_humidity = 0.0f;

// Actuator states
uint8_t pump_state = 0;  // 0=OFF, 1=ON
uint8_t led_state = 0;   // 0=OFF, 1=ON
uint8_t servo_pos = 0;   // 0-100 degrees

// Boot time filter - ignore ESP32 boot messages for first 5 seconds
uint32_t boot_complete_time = 0;

// UART receive (interrupt)
static uint8_t rx_byte;
static char rx_buf[256];  // Increased from 64 to 256
static volatile uint8_t rx_idx = 0;
static volatile uint32_t rx_byte_count = 0;  // Track bytes received
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void BH1750_Init(void);
float BH1750_Read(void);
uint16_t Read_Soil_ADC(void);
void DHT22_Read(void);
void Send_Sensor_Data(void);
void process_uart_command(uint8_t cmd);
void start_uart_rx_interrupt(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* BH1750 (địa chỉ 0x23) */
void BH1750_Init() {
    uint8_t cmd = 0x01; // power on
    HAL_I2C_Master_Transmit(&hi2c1, (0x23<<1), &cmd, 1, 100);
    cmd = 0x10; // continous high res
    HAL_I2C_Master_Transmit(&hi2c1, (0x23<<1), &cmd, 1, 100);
}

float BH1750_Read() {
    if (HAL_I2C_Master_Receive(&hi2c1, (0x23<<1) | 1, bh1750_data, 2, 100) == HAL_OK) {
        uint16_t raw = (bh1750_data[0] << 8) | bh1750_data[1];
        return raw / 1.2f;
    }
    return -1.0f;
}

/* ADC soil */
uint16_t Read_Soil_ADC(void)
{
    uint16_t value = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
    {
        value = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return value;
}

/* DHT22 Read - 1-wire protocol implementation */
// DHT22 timing constants (in microseconds)
#define DHT22_START_SIGNAL_DURATION 18000  // 18ms pull down
#define DHT22_RESPONSE_TIMEOUT 200         // 200us timeout for response
#define DHT22_BIT_TIMEOUT 100              // 100us timeout for bit read

// Helper function: Set PA0 low (drain, simulating open-drain)
static void DHT22_Pin_Low(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
}

// Helper function: Set PA0 high (let pull-up handle it)
static void DHT22_Pin_High(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
}

// Helper function: Read PA0 state
static uint8_t DHT22_Pin_Read(void)
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
}

// Helper function: Delay in microseconds (approximate)
static void DHT22_Delay_us(uint32_t us)
{
    // For STM32F103 at ~72MHz, rough approximation
    // Adjust multiplier based on actual clock speed
    uint32_t count = (us * 72) / 9;  // Empirical value for 1us delay
    while (count--) __NOP();
}

// Helper function: Wait for pin to reach desired state with timeout
static uint8_t DHT22_Wait_For_Pin(uint8_t state, uint32_t timeout_us)
{
    uint32_t timeout = timeout_us;
    while (timeout--) {
        if (DHT22_Pin_Read() == state) {
            return 1;  // Success
        }
        DHT22_Delay_us(1);
    }
    return 0;  // Timeout
}

/* DHT22 Read - Actual protocol implementation */
void DHT22_Read(void)
{
    uint8_t data[5] = {0};
    
    // Reset DHT22 by releasing it
    DHT22_Pin_High();
    HAL_Delay(1);
    
    // Send start signal: pull low for 18ms
    DHT22_Pin_Low();
    HAL_Delay(18);
    DHT22_Pin_High();
    
    // Wait for DHT22 response: it pulls low for ~80us then high for ~80us
    if (!DHT22_Wait_For_Pin(0, DHT22_RESPONSE_TIMEOUT)) {
        dht_temperature = 25.0f;  // Default on timeout
        dht_humidity = 60.0f;
        return;
    }
    
    if (!DHT22_Wait_For_Pin(1, DHT22_RESPONSE_TIMEOUT)) {
        dht_temperature = 25.0f;
        dht_humidity = 60.0f;
        return;
    }
    
    // Read 40 bits of data
    for (int byte_idx = 0; byte_idx < 5; byte_idx++) {
        for (int bit_idx = 7; bit_idx >= 0; bit_idx--) {
            // Wait for start of bit (pin goes low)
            if (!DHT22_Wait_For_Pin(0, DHT22_BIT_TIMEOUT)) {
                dht_temperature = 25.0f;
                dht_humidity = 60.0f;
                return;
            }
            
            // Wait for pin to go high
            if (!DHT22_Wait_For_Pin(1, DHT22_BIT_TIMEOUT)) {
                dht_temperature = 25.0f;
                dht_humidity = 60.0f;
                return;
            }
            
            // Measure high time (~26us for 0, ~70us for 1)
            uint32_t high_time = 0;
            while (DHT22_Pin_Read() && high_time < DHT22_BIT_TIMEOUT) {
                DHT22_Delay_us(1);
                high_time++;
            }
            
            // If high_time > 50us, it's a 1; otherwise 0
            if (high_time > 50) {
                data[byte_idx] |= (1 << bit_idx);
            }
        }
    }
    
    // Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        // Checksum failed, use previous values or defaults
        return;
    }
    
    // Parse humidity (first 2 bytes)
    dht_humidity = (float)((data[0] << 8) | data[1]) / 10.0f;
    
    // Parse temperature (next 2 bytes)
    int16_t temp_raw = ((data[2] & 0x7F) << 8) | data[3];
    if (data[2] & 0x80) {  // Negative temperature
        temp_raw = -temp_raw;
    }
    dht_temperature = (float)temp_raw / 10.0f;
}

/* Send sensor data to ESP32 via UART */
void Send_Sensor_Data(void)
{
    char tx_buf[128];
    
    // Read all sensors
    DHT22_Read();
    lux = BH1750_Read();
    soil_value = Read_Soil_ADC();
    
    // Format: T:23.4,H:55.1,L:120.3,Soil:512,Pump:1,Servo:90,LED:1
    int len = snprintf(tx_buf, sizeof(tx_buf), 
                      "T:%.1f,H:%.1f,L:%.1f,Soil:%u,Pump:%u,Servo:%u,LED:%u\r\n",
                      dht_temperature, dht_humidity, lux, soil_value,
                      pump_state, servo_pos, led_state);
    
    if (len > 0 && len < sizeof(tx_buf)) {
        // Send ONLY sensor data to UART (for ESP32)
        HAL_UART_Transmit(&huart1, (uint8_t*)tx_buf, len, 200);
        
        // Debug info to USB CDC only
        char debug_buf[160];
        int debug_len = snprintf(debug_buf, sizeof(debug_buf), 
                                "[SENSOR] %s", tx_buf);
        if (debug_len > 0) {
            CDC_Transmit_FS((uint8_t*)debug_buf, debug_len);
        }
    }
}

/* UART receive interrupt start */
void start_uart_rx_interrupt(void)
{
    rx_idx = 0;
    memset(rx_buf, 0, sizeof(rx_buf));
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    
    // Debug only to USB CDC (not UART to avoid flooding ESP32)
    char msg_dbg[64];
    int len = snprintf(msg_dbg, sizeof(msg_dbg), "[UART] RX interrupt armed\r\n");
    if (len > 0) {
        CDC_Transmit_FS((uint8_t*)msg_dbg, len);
    }
}

/* Parse HEX commands:
   0x01 = Pump ON
   0x02 = Pump OFF
   0x03 = LED ON
   0x04 = LED OFF
   0x80-0xE4 = Servo 0-100 (0x80=0, 0xB2=50, 0xE4=100)
*/
void process_uart_command(uint8_t cmd)
{
    char debug_msg[128];
    int debug_len;
    
    // Debug only to USB CDC (not UART)
    debug_len = snprintf(debug_msg, sizeof(debug_msg), "[CMD] 0x%02X\r\n", cmd);
    CDC_Transmit_FS((uint8_t*)debug_msg, debug_len);

    // Parse hex commands
    if (cmd == 0x01) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
        pump_state = 1;  // Update state
        debug_len = snprintf(debug_msg, sizeof(debug_msg), "[ACTION] PUMP=ON\r\n");
        CDC_Transmit_FS((uint8_t*)debug_msg, debug_len);
    }
    else if (cmd == 0x02) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
        pump_state = 0;  // Update state
        debug_len = snprintf(debug_msg, sizeof(debug_msg), "[ACTION] PUMP=OFF\r\n");
        CDC_Transmit_FS((uint8_t*)debug_msg, debug_len);
    }
    else if (cmd == 0x03) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
        led_state = 1;  // Update state
        debug_len = snprintf(debug_msg, sizeof(debug_msg), "[ACTION] LED=ON\r\n");
        CDC_Transmit_FS((uint8_t*)debug_msg, debug_len);
    }
    else if (cmd == 0x04) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
        led_state = 0;  // Update state
        debug_len = snprintf(debug_msg, sizeof(debug_msg), "[ACTION] LED=OFF\r\n");
        CDC_Transmit_FS((uint8_t*)debug_msg, debug_len);
    }
    else if (cmd >= 0x80 && cmd <= 0xE4) {
        // Servo: 0x80=0°, 0xB2=50°, 0xE4=100°
        // Linear mapping: servo_value = (cmd - 0x80) * 100 / (0xE4 - 0x80)
        int servo_deg = (int)((cmd - 0x80) * 100 / (0xE4 - 0x80));
        if (servo_deg < 0) servo_deg = 0;
        if (servo_deg > 100) servo_deg = 100;
        
        servo_pos = (uint8_t)servo_deg;  // Update state
        uint32_t ccr = 50 + (servo_deg * 50 / 100);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);
        debug_len = snprintf(debug_msg, sizeof(debug_msg), "[ACTION] SERVO=%d\r\n", servo_deg);
        CDC_Transmit_FS((uint8_t*)debug_msg, debug_len);
    }

    // Send ACK
    uint8_t ack = 0x06;  // ACK = 0x06 (standard ASCII ACK)
    HAL_UART_Transmit(&huart1, &ack, 1, 100);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  // Initialize devices
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); // Pump OFF
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); // LED OFF
  pump_state = 0;
  led_state = 0;
  servo_pos = 0;
  
  // Initialize sensors
  BH1750_Init();
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  
  // Send boot message
  const char *boot_msg = "STM32 READY\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t*)boot_msg, strlen(boot_msg), 100);
  CDC_Transmit_FS((uint8_t*)boot_msg, strlen(boot_msg));
  
  // Set boot complete time - ignore ESP32 boot messages for 5 seconds
  boot_complete_time = HAL_GetTick() + 5000;
  
  HAL_Delay(500);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  
  uint32_t last_send_time = 0;
  const uint32_t SEND_INTERVAL = 2000; // Send sensor data every 2 seconds
  
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // Parse HEX commands - 1 byte at a time (non-blocking)
    uint8_t rx_byte_local;
    if (HAL_UART_Receive(&huart1, &rx_byte_local, 1, 50) == HAL_OK) {
      // Ignore boot messages during first 5 seconds
      if (HAL_GetTick() > boot_complete_time) {
        // Only process valid command bytes (0x01-0x04, 0x80-0xE4)
        if (rx_byte_local == 0x01 || rx_byte_local == 0x02 || 
            rx_byte_local == 0x03 || rx_byte_local == 0x04 || 
            (rx_byte_local >= 0x80 && rx_byte_local <= 0xE4)) {
          process_uart_command(rx_byte_local);
        }
        // Silently ignore other bytes (boot messages, text, etc.)
      }
    }
    
    // Send sensor data periodically
    uint32_t current_time = HAL_GetTick();
    if (current_time - last_send_time >= SEND_INTERVAL) {
      last_send_time = current_time;
      Send_Sensor_Data();
    }
    
    HAL_Delay(10); // Small delay to prevent busy-waiting
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV4;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_OC_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA0 (DHT22 - Open Drain) */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;  // Open Drain for DHT22
  GPIO_InitStruct.Pull = GPIO_PULLUP;          // Pull-up for DHT22 1-wire
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
