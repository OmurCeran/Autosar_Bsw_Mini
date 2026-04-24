/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "usb_host.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Std_Types.h"
#include "Mini_SchM.h"
#include "Mini_Com.h"
#include "Mini_Dem.h"
#include "Mini_Rte.h"
#include "Mini_Timestamp.h"
#include "Mini_Timestamp.h"
#include "Mini_EcuM.h"
#include "Mini_Nvm.h"
#include "Mini_Dcm.h"
#include "Mini_FaultInj.h"
#include "SEGGER_SYSVIEW.h"
#include "App_Swc.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
typedef StaticQueue_t osStaticMessageQDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEMO_PHASE_DURATION_MS      5000U
#define DEMO_COOLDOWN_MS            500U
#define DEBUG_AUTOSAR               1U
#define STACKSIZE_TASK_1MS          (512U * 4)
#define STACKSIZE_TASK_10MS         (512U * 4)
#define STACKSIZE_TASK_100MS        (512U * 4)
#define STACKSIZE_DEFAULT_TASK      (2048U * 4)

/* DMA RX buffer - circular buffer for incoming UDS frames */
#define UART_RX_BUFFER_SIZE   64U
static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t uart_rx_old_pos = 0U;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_tx;
DMA_HandleTypeDef hdma_usart2_rx;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
uint32_t defaultTaskBuffer[ 1024 ];
osStaticThreadDef_t defaultTaskControlBlock;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .cb_mem = &defaultTaskControlBlock,
  .cb_size = sizeof(defaultTaskControlBlock),
  .stack_mem = &defaultTaskBuffer[0],
  .stack_size = sizeof(defaultTaskBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for task1ms */
osThreadId_t task1msHandle;
uint32_t task1msStack[ 2048 ];
osStaticThreadDef_t task1msControlBlock;
const osThreadAttr_t task1ms_attributes = {
  .name = "task1ms",
  .cb_mem = &task1msControlBlock,
  .cb_size = sizeof(task1msControlBlock),
  .stack_mem = &task1msStack[0],
  .stack_size = sizeof(task1msStack),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for task10ms */
osThreadId_t task10msHandle;
uint32_t task10msStack[ 2048 ];
osStaticThreadDef_t task10msStackControlBlock;
const osThreadAttr_t task10ms_attributes = {
  .name = "task10ms",
  .cb_mem = &task10msStackControlBlock,
  .cb_size = sizeof(task10msStackControlBlock),
  .stack_mem = &task10msStack[0],
  .stack_size = sizeof(task10msStack),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for task100ms */
osThreadId_t task100msHandle;
uint32_t task100msStack[ 2048 ];
osStaticThreadDef_t task100msControlBlock;
const osThreadAttr_t task100ms_attributes = {
  .name = "task100ms",
  .cb_mem = &task100msControlBlock,
  .cb_size = sizeof(task100msControlBlock),
  .stack_mem = &task100msStack[0],
  .stack_size = sizeof(task100msStack),
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for DiagnosticTask */
osThreadId_t DiagnosticTaskHandle;
uint32_t diagnosticTaskHBuffer[ 256 ];
osStaticThreadDef_t diagnosticTaskHControlBlock;
const osThreadAttr_t DiagnosticTask_attributes = {
  .name = "DiagnosticTask",
  .cb_mem = &diagnosticTaskHControlBlock,
  .cb_size = sizeof(diagnosticTaskHControlBlock),
  .stack_mem = &diagnosticTaskHBuffer[0],
  .stack_size = sizeof(diagnosticTaskHBuffer),
  .priority = (osPriority_t) osPriorityNormal4,
};
/* Definitions for DcMuartRxQueue */
osMessageQueueId_t DcMuartRxQueueHandle;
uint8_t DcMuartRxQueueBuffer[ 128 * sizeof( uint8_t ) ];
osStaticMessageQDef_t DcMuartRxQueueControlBlock;
const osMessageQueueAttr_t DcMuartRxQueue_attributes = {
  .name = "DcMuartRxQueue",
  .cb_mem = &DcMuartRxQueueControlBlock,
  .cb_size = sizeof(DcMuartRxQueueControlBlock),
  .mq_mem = &DcMuartRxQueueBuffer,
  .mq_size = sizeof(DcMuartRxQueueBuffer)
};
/* USER CODE BEGIN PV */

/*Static semaphore and mutex usage*/
/* Static control block buffers — no malloc needed */
static StaticSemaphore_t printfMutexBuffer;
static StaticSemaphore_t dmaTxSemBuffer;

osMutexId_t printf_mutex;
osSemaphoreId_t dma_tx_sem;

/* Attributes with static buffer references */
static const osMutexAttr_t printf_mutex_attr = {
    .name    = "printfMutex",
    .attr_bits = osMutexPrioInherit,       /* priority inheritance */
    .cb_mem  = &printfMutexBuffer,
    .cb_size = sizeof(printfMutexBuffer),
};

static const osSemaphoreAttr_t dma_tx_sem_attr = {
    .name    = "dmaTxSem",
    .cb_mem  = &dmaTxSemBuffer,
    .cb_size = sizeof(dmaTxSemBuffer),
};
/*Safety-critical software doesn't use malloc. I migrated my mini AUTOSAR project from dynamic to static allocation, 
* following ISO 26262 and MISRA-C:2012 Rule 21.3 guidance.*/
/* DMA Printf buffer */
uint8_t dma_tx_buffer[256];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_CAN1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);
void task1msHandleFunction(void *argument);
void task10msHandleFunction(void *argument);
void task100msHandleFunction(void *argument);
void DiagnosticTask_Function(void *argument);

/* USER CODE BEGIN PFP */
void DMA_Printf(const char *format, ...);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_DMA_Init();
  MX_CAN1_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  
  /* UART RX — DMA circular mode with IDLE line detection */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart2, uart_rx_buffer, UART_RX_BUFFER_SIZE);
  __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);  /* Disable half-transfer IRQ (don't need it) */

  /* Logging initialization is inside EcuM_StartupSequence */
    EcuM_StartupSequence();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /*printf_mutex = osMutexNew(dmaMutex_attributes);*/
  printf_mutex = osMutexNew(&printf_mutex_attr);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  dma_tx_sem = osSemaphoreNew(1, 1, &dma_tx_sem_attr);
  /*dma_tx_sem = osSemaphoreNew(1, 1, NULL);*/
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of DcMuartRxQueue */
  DcMuartRxQueueHandle = osMessageQueueNew (128, sizeof(uint8_t), &DcMuartRxQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of task1ms */
  task1msHandle = osThreadNew(task1msHandleFunction, NULL, &task1ms_attributes);

  /* creation of task10ms */
  task10msHandle = osThreadNew(task10msHandleFunction, NULL, &task10ms_attributes);

  /* creation of task100ms */
  task100msHandle = osThreadNew(task100msHandleFunction, NULL, &task100ms_attributes);

  /* creation of DiagnosticTask */
  DiagnosticTaskHandle = osThreadNew(DiagnosticTask_Function, NULL, &DiagnosticTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */

    /* Check for all task creation , otherwise it might be silently fail */
  if (defaultTaskHandle == NULL ||
      task1msHandle     == NULL ||
      task10msHandle    == NULL ||
      task100msHandle   == NULL)
  {
      Log_Raw("\r\n!!! TASK CREATION FAILED !!!\r\n");
      Log_Raw("defaultTask: %s\r\n", defaultTaskHandle ? "OK" : "FAIL");
      Log_Raw("task1ms    : %s\r\n", task1msHandle     ? "OK" : "FAIL");
      Log_Raw("task10ms   : %s\r\n", task10msHandle    ? "OK" : "FAIL");
      Log_Raw("task100ms  : %s\r\n", task100msHandle   ? "OK" : "FAIL");
      Error_Handler();
  }
      Log_Raw("[INIT] All tasks created (static allocation)\r\n");

  Log_Raw("[INIT] All tasks created, starting scheduler...\r\n");
  Log_Raw("\r\n");
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
    /*SystemView configuration and startup*/
    #ifdef DEBUG_SEGGER_SYSVIEW
    SEGGER_SYSVIEW_Conf();
    #endif
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 16;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_1TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_1TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  /* DMA1_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

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

  /* Enable UART2 clock */
  __HAL_RCC_USART2_CLK_ENABLE();

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PDM_OUT_Pin */
  GPIO_InitStruct.Pin = PDM_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PDM_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : I2S3_WS_Pin */
  GPIO_InitStruct.Pin = I2S3_WS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(I2S3_WS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD6_Pin
                           Audio_RST_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : I2S3_MCK_Pin I2S3_SCK_Pin I2S3_SD_Pin */
  GPIO_InitStruct.Pin = I2S3_MCK_Pin|I2S3_SCK_Pin|I2S3_SD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : Audio_SCL_Pin Audio_SDA_Pin */
  GPIO_InitStruct.Pin = Audio_SCL_Pin|Audio_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief Thread-safe ve DMA based printf implementation for debugging over UART2.
  */
void DMA_Printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    if (osKernelGetState() == osKernelRunning)
    {
        /* RTOS = MUTEX and SEMAPHORE for thread safety */
        if (osMutexAcquire(printf_mutex, pdMS_TO_TICKS(50)) == osOK)
        {
            /* Wait for DMA transfer to complete */
            if (osSemaphoreAcquire(dma_tx_sem, pdMS_TO_TICKS(50)) == osOK)
            {
                int len = vsnprintf((char*)dma_tx_buffer, sizeof(dma_tx_buffer), format, args);
                if (len > 0)
                {
                    HAL_UART_Transmit_DMA(&huart2, dma_tx_buffer, len);
                }
                else
                {
                    osSemaphoreRelease(dma_tx_sem); /* If there is an error, release the semaphore */
                }
            }
            osMutexRelease(printf_mutex);
        }
    }
    else
    {
        /* If RTOS is not ready, use polling */
        int len = vsnprintf((char*)dma_tx_buffer, sizeof(dma_tx_buffer), format, args);
        if (len > 0)
        {
            HAL_UART_Transmit(&huart2, dma_tx_buffer, len, HAL_MAX_DELAY);
        }
    }
    va_end(args);
}

/**
  * @brief UART DMA transfer completed callback - semaphore release for next transfer
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        osSemaphoreRelease(dma_tx_sem);
    }
}

/**
 * @brief UART RX event callback — called on IDLE line detection or DMA complete
 *
 * Called in three scenarios (HAL tells us which via the Size parameter):
 *   - IDLE line: frame finished, Size = bytes received since last call
 *   - HT/TC: DMA buffer half-full / full (circular wrap-around)
 *
 * We just forward new bytes to DCM frame assembler.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2)
    {
        /* Calculate how many new bytes arrived */
        if (Size != uart_rx_old_pos)
        {
            uint16_t newDataLen;

            if (Size > uart_rx_old_pos)
            {
                /* Linear growth — bytes are contiguous in the buffer */
                newDataLen = Size - uart_rx_old_pos;
                for (uint16_t i = 0U; i < newDataLen; i++)
                {
                    uint8_t rxByte = uart_rx_buffer[uart_rx_old_pos + i];
                    osMessageQueuePut(DcMuartRxQueueHandle, &rxByte, 0U, 0U); // ISR-Safe Call
                }
            }
            else
            {
                /* Buffer wrap-around — read to end, then from start */
                uint16_t tailLen = UART_RX_BUFFER_SIZE - uart_rx_old_pos;
                for (uint16_t i = 0U; i < tailLen; i++)
                {
                    uint8_t rxByte = uart_rx_buffer[uart_rx_old_pos + i];
                    osMessageQueuePut(DcMuartRxQueueHandle, &rxByte, 0U, 0U); // ISR-Safe Call 
                }
                for (uint16_t i = 0U; i < Size; i++)
                {
                    uint8_t rxByte = uart_rx_buffer[i];
                    osMessageQueuePut(DcMuartRxQueueHandle, &rxByte, 0U, 0U); // ISR-Safe Call 
                }
            }

            uart_rx_old_pos = Size;

            /* Handle buffer wrap */
            if (uart_rx_old_pos >= UART_RX_BUFFER_SIZE)
            {
                uart_rx_old_pos = 0U;
            }
        }
    }
}

/**
 * @brief UART error callback — re-arm RX on error
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* Clear error flags */
        __HAL_UART_CLEAR_FLAG(huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE);

        /* Re-arm RX */
        HAL_UARTEx_ReceiveToIdle_DMA(huart, uart_rx_buffer, UART_RX_BUFFER_SIZE);
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);

        uart_rx_old_pos = 0U;
    }
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Demo Task — toggles between EXPLICIT and IMPLICIT RTE access
  *         modes and reports the race condition count.
  *
  *         In EXPLICIT mode: shared data is accessed directly in the global
  *         buffer. Preemption during a multi-field write causes the reader
  *         task to see inconsistent data → dozens of errors per 5s window.
  *
  *         In IMPLICIT mode: RTE takes a task-boundary snapshot. Data stays
  *         consistent throughout the task → zero errors.
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_HOST */
  MX_USB_HOST_Init();
  /* USER CODE BEGIN 5 */
  #ifdef DEBUG_SEGGER_SYSVIEW
  SEGGER_SYSVIEW_Start();
  #endif
  uint32_t cycle = 0U;
  /* Notify EcuM that scheduler has started */
  EcuM_OnSchedulerStart();

  
  /* Give the periodic tasks a moment to start running */
  osDelay(1000);

  Log_Banner("RACE CONDITION DEMO — STARTING");

  for (;;)
  {
    cycle++;

    /* ============ EXPLICIT MODE (dangerous) ============ */
    Log_Separator();
    Log_Write("DEMO ", "Cycle %lu | Phase 1: EXPLICIT mode (5s window)", cycle);
    Log_Separator();

    Rte_SetAccessMode(RTE_ACCESS_EXPLICIT);
    Rte_ResetInconsistencyCount();

    osDelay(DEMO_PHASE_DURATION_MS);

    uint32_t explicitErrors = Rte_GetInconsistencyCount();
    Log_Write("DEMO ", "EXPLICIT: %lu inconsistencies detected", explicitErrors);

    if (explicitErrors > 0U)
    {
      Log_Write("DEMO ", " Result                   : RACE CONDITION");
    }
    Log_Raw("");

    osDelay(DEMO_COOLDOWN_MS);

    /* ============ IMPLICIT MODE (safe) ============ */
    Log_Separator();
    Log_Write("DEMO ", "Cycle %lu | Phase 2: IMPLICIT mode (5s window)", cycle);
    Log_Separator();

    Rte_SetAccessMode(RTE_ACCESS_IMPLICIT);
    Rte_ResetInconsistencyCount();

    osDelay(DEMO_PHASE_DURATION_MS);

    uint32_t implicitErrors = Rte_GetInconsistencyCount();
    Log_Write("DEMO ", "IMPLICIT: %lu inconsistencies detected", implicitErrors);
    Log_Write("DEMO ", " Torque miscalculations   : %lu%s\r\n",
               implicitErrors, (implicitErrors == 0U) ? "  [OK]" : "  [FAIL]");
    if (implicitErrors == 0U)
    {
      Log_Write("DEMO ", " Result                   : AUTOSAR RTE snapshot works");
    }
    Log_Raw("\r\n");

    /* Summary */
    Log_Write("DEMO ", "==> Cycle %lu summary: EXPLICIT=%lu, IMPLICIT=%lu\r\n",
               cycle, explicitErrors, implicitErrors);
    Log_Raw("\r\n");

    osDelay(2000);

    /* ============ DEM LIFECYCLE DEMO ============ */
    Log_Banner("DEM LIFECYCLE DEMO — Fault Injection Scenario");
    Log_Write("DEMO ", "Running ECU in normal state for 2 seconds...");
    osDelay(2000);

    Log_Separator();
    Log_Write("DEMO ", "Step 1: Injecting torque sensor fault");
    Log_Separator();
    FaultInj_Inject(FAULT_INJ_TORQUE_OUT_OF_RANGE);

    /* Wait for debounce to mature (5 counts x 100ms = 500ms minimum) */
    osDelay(1000);

    Log_Separator();
    Log_Write("DEMO ", "Step 2: Observing NvM async write completion");
    Log_Separator();
    osDelay(500);  /* Wait for NvM write to finish (3x100ms) */

    Log_Separator();
    Log_Write("DEMO ", "Step 3: Reading DTC state via Dem_GetDtcStatusByte()");
    Log_Separator();
    uint8 dtcStatus = Dem_GetDtcStatusByte(DEM_EVENT_TORQUE_SENSOR_FAULT);
    Log_Write("DEMO ", "DTC 0x4A12 status byte: 0x%02X (tester would read this via UDS 0x19)",
              (uint32)dtcStatus);

    Dem_FreezeFrameType ff;
    if (Dem_GetFreezeFrame(DEM_EVENT_TORQUE_SENSOR_FAULT, &ff) == E_OK)
    {
        Log_Write("DEMO ", "Freeze frame: torque=%u speed=%u angle=%u t=%lums",
                  (uint32)ff.torque_input, (uint32)ff.vehicle_speed,
                  (uint32)ff.steering_angle, ff.timestamp_ms);
    }

    osDelay(2000);

    Log_Separator();
    Log_Write("DEMO ", "Step 4: Removing fault, observing healing");
    Log_Separator();
    FaultInj_Clear();

    /* Wait for debounce to heal (5 counts x 100ms = 500ms minimum) */
    osDelay(1000);

    Log_Separator();
    Log_Write("DEMO ", "Step 5: Clearing DTC (simulates UDS 0x14 ClearDTC)");
    Log_Separator();
    Dem_ClearDtc(DEM_EVENT_TORQUE_SENSOR_FAULT);
    Log_Write("DEMO ", "DTC cleared, status now: 0x%02X",
              (uint32)Dem_GetDtcStatusByte(DEM_EVENT_TORQUE_SENSOR_FAULT));

    osDelay(3000);

    Log_Banner("DEMO CYCLE COMPLETE");
    Log_Raw("");
    Log_Raw("System is now IDLE. Send UDS requests from the terminal.");
    Log_Raw("");
    Log_Raw("Example commands:");
    Log_Raw("  10 03         - Diagnostic Session Control (Extended)");
    Log_Raw("  22 F1 90      - Read VIN");
    Log_Raw("  22 F1 91      - Read Hardware Version");
    Log_Raw("  19 02 09      - Read DTCs");
    Log_Raw("  14 FF FF FF   - Clear all DTCs");
    Log_Raw("  3E 00         - Tester Present");
    Log_Raw("");
    Log_Raw("Demo will restart in 60 seconds...");
    Log_Raw("");

    osDelay(60000);   /* 60 saniye beklemek = UDS test için rahat süre */
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_task1msHandleFunction */
/**
* @brief Function implementing the task1ms thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_task1msHandleFunction */
void task1msHandleFunction(void *argument)
{
  /* USER CODE BEGIN task1msHandleFunction */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {
    Rte_Task_Begin();
    App_Runnable_TorqueCalc_1ms();
    SchM_MainFunction_1ms();
    Rte_Task_End();
    osDelay(1);
  }
  /* USER CODE END task1msHandleFunction */
}

/* USER CODE BEGIN Header_task10msHandleFunction */
/**
* @brief Function implementing the task10ms thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_task10msHandleFunction */
void task10msHandleFunction(void *argument)
{
  /* USER CODE BEGIN task10msHandleFunction */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {
    Rte_Task_Begin();
    App_Runnable_SensorUpdate_10ms();
    SchM_MainFunction_10ms();
    Rte_Task_End();
    osDelay(10);
  }
  /* USER CODE END task10msHandleFunction */
}

/* USER CODE BEGIN Header_task100msHandleFunction */
/**
* @brief Function implementing the task100ms thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_task100msHandleFunction */
void task100msHandleFunction(void *argument)
{
  /* USER CODE BEGIN task100msHandleFunction */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {
    Rte_Task_Begin();
    App_Runnable_DiagMonitor_100ms();
    SchM_MainFunction_100ms();
    Rte_Task_End();
    osDelay(100);
  }
  /* USER CODE END task100msHandleFunction */
}

/* USER CODE BEGIN Header_DiagnosticTask_Function */
/**
* @brief Function implementing the DiagnosticTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_DiagnosticTask_Function */
void DiagnosticTask_Function(void *argument)
{
  /* USER CODE BEGIN DiagnosticTask_Function */
  uint8_t rxByte;
  /* Infinite loop */
  for(;;)
    {
      /* If there are not data in queue , cpu sleep, zero cpu load */
      if (osMessageQueueGet(DcMuartRxQueueHandle, &rxByte, NULL, osWaitForever) == osOK)
      {
        /* Delete CRLF from Received Bytes */
          Dcm_FeedRxByte(rxByte);
        }
    }
  /* USER CODE END DiagnosticTask_Function */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
     ex: DMA_Printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
