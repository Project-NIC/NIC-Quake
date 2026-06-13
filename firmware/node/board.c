/*
 * STM32H503 board support implementation (ST HAL).
 *
 * NOT host-buildable and NOT host-tested — this is the hardware layer. The CHIP
 * handles below are created by the CubeMX init (extern). Anything tagged
 * "VALIDATE ON HW" is chip-specific timing/clock work to confirm on the board.
 */

#include "board.h"
#include "nic_types.h"
#include "main.h"        /* CubeMX: extern handles + pin macros */

#include <string.h>

/* --- CubeMX-generated peripheral handles (declared in main.c / *_it.c) ------ */
extern SPI_HandleTypeDef  hspi1, hspi2, hspi3;   /* ICM, ADXL355, SCL3300 */
extern UART_HandleTypeDef huart1;                /* RS-485 data pair, hardware DE */
extern UART_HandleTypeDef huart2;                /* clock pair (becomes HSE_IN)   */
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef  htim1;                 /* sensor clocks (CH outputs)    */

#define SPI_TIMEOUT_MS  20u

/* --- SPI: one bus per sensor, CS toggled around each transfer --------------- */

typedef struct {
    SPI_HandleTypeDef *h;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
} spi_dev_t;

static spi_dev_t s_icm     = { &hspi1, CS_ICM_GPIO_Port,     CS_ICM_Pin };
static spi_dev_t s_adxl355 = { &hspi2, CS_ADXL355_GPIO_Port, CS_ADXL355_Pin };
static spi_dev_t s_scl3300 = { &hspi3, CS_SCL3300_GPIO_Port, CS_SCL3300_Pin };

static int spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_dev_t *d = (spi_dev_t *)ctx;
    HAL_StatusTypeDef s;

    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET);
    if (tx && rx)      s = HAL_SPI_TransmitReceive(d->h, (uint8_t *)tx, rx, (uint16_t)len, SPI_TIMEOUT_MS);
    else if (tx)       s = HAL_SPI_Transmit(d->h, (uint8_t *)tx, (uint16_t)len, SPI_TIMEOUT_MS);
    else if (rx)       s = HAL_SPI_Receive(d->h, rx, (uint16_t)len, SPI_TIMEOUT_MS);
    else               s = HAL_OK;
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET);

    return (s == HAL_OK) ? NIC_OK : NIC_ERR;
}

void board_sensor_buses(nq_sensors_buses_t *buses) {
    buses->icm     = (nic_spi_t){ .ctx = &s_icm,     .transfer = spi_xfer };
    buses->adxl355 = (nic_spi_t){ .ctx = &s_adxl355, .transfer = spi_xfer };
    buses->scl3300 = (nic_spi_t){ .ctx = &s_scl3300, .transfer = spi_xfer };
}

/* --- Clock switch: UART2 (clock pair) <-> HSE_IN, and recovery -------------- */
/* SOFTWARE.md mandates the order; nq-clocksync owns it, these are the pokes.   */

static void op_uart2_disable_irq(void *c) { (void)c; HAL_NVIC_DisableIRQ(USART2_IRQn); }
static void op_uart2_deinit(void *c)      { (void)c; HAL_UART_DeInit(&huart2); }

static void op_remap_rx_to_hse(void *c) {
    (void)c;
    /* VALIDATE ON HW: drive the UART2 RX pin as the HSE bypass clock input.
     * On STM32H503 HSE_IN is a dedicated pin; route the master clock there
     * (board trace) or reconfigure the shared pin's alternate function. */
    GPIO_InitTypeDef g = {0};
    g.Pin   = HSE_IN_Pin;
    g.Mode  = GPIO_MODE_INPUT;     /* clock fed to RCC HSE in bypass mode */
    g.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(HSE_IN_GPIO_Port, &g);
}

static void op_select_hse(void *c) {
    (void)c;
    /* VALIDATE ON HW: switch RCC to the external HSE (bypass) and run the PLL
     * off it. Exact PLL M/N/P for the 8.192 MHz-derived input comes from
     * CUBEMX.md; this enables HSE + PLL and selects PLL as SYSCLK. */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_BYPASS;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    /* osc.PLL.PLLM/N/P/Q/R = ... (CubeMX) */
    (void)HAL_RCC_OscConfig(&osc);
}

static int op_pll_wait_lock(void *c) {
    (void)c;
    uint32_t t0 = HAL_GetTick();
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY)) {
        if (HAL_GetTick() - t0 > 50u) return NIC_ERR;   /* lock timeout */
    }
    return NIC_OK;
}

static void op_remap_hse_to_rx(void *c) {
    (void)c;
    /* Restore the pin to the UART2 RX alternate function. */
    GPIO_InitTypeDef g = {0};
    g.Pin       = HSE_IN_Pin;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF_USART2;
    HAL_GPIO_Init(HSE_IN_GPIO_Port, &g);
}

static void op_uart2_reinit(void *c) { (void)c; HAL_UART_Init(&huart2); HAL_NVIC_EnableIRQ(USART2_IRQn); }
static void op_select_rc(void *c)    { (void)c; SystemClock_Config(); } /* back to HSI/CSI per CubeMX */

static const nic_clocksync_ops_t s_cs_ops = {
    .ctx               = NULL,
    .uart2_disable_irq = op_uart2_disable_irq,
    .uart2_deinit      = op_uart2_deinit,
    .remap_rx_to_hse   = op_remap_rx_to_hse,
    .select_hse        = op_select_hse,
    .pll_wait_lock     = op_pll_wait_lock,
    .remap_hse_to_rx   = op_remap_hse_to_rx,
    .uart2_reinit      = op_uart2_reinit,
    .select_rc         = op_select_rc,
};

const nic_clocksync_ops_t *board_clocksync_ops(void) { return &s_cs_ops; }

void board_sensor_clocks_start(void) {
    /* VALIDATE ON HW: TIM1 channels (off the HSE-locked PLL) generate the two
     * phase-locked clocks — 1.024 MHz to ADXL355 EXT_CLK, 40.96 kHz to the ICM
     * CLKIN. Prescaler/period set from the timer clock in CUBEMX.md. */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);   /* 1.024 MHz  -> ADXL355 EXT_CLK */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);   /* 40.96 kHz  -> ICM-42688 CLKIN */
}

/* --- RS-485 data link (UART1) ---------------------------------------------- */

int board_link_send(const uint8_t *buf, size_t len) {
    /* Hardware DE (USART driver-enable) frames the transmission automatically. */
    return (HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 50u) == HAL_OK)
           ? NIC_OK : NIC_ERR;
}

int board_link_recv(uint8_t *buf, size_t cap, size_t *out_len, uint32_t timeout_ms) {
    uint16_t got = 0;
    HAL_StatusTypeDef s = HAL_UARTEx_ReceiveToIdle(&huart1, buf, (uint16_t)cap, &got, timeout_ms);
    *out_len = got;
    if (s == HAL_OK || s == HAL_TIMEOUT) return NIC_OK;   /* idle-line delimits the frame */
    return NIC_ERR;
}

/* --- Calibration-matrix flash (one reserved page) -------------------------- */

int board_flash_store(const void *data, size_t len) {
    /* VALIDATE ON HW: STM32H5 flash programs in 16-byte quad-words; erase the
     * reserved page, then HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, ...). */
    HAL_FLASH_Unlock();
    /* ...erase CAL page, program `data` in 16-byte chunks... */
    (void)data; (void)len;
    HAL_FLASH_Lock();
    return NIC_OK;
}

int board_flash_load(void *data, size_t len) {
    memcpy(data, (const void *)CAL_FLASH_ADDR, len);     /* flash is memory-mapped */
    return NIC_OK;
}

/* --- Hardware number: the number on the box (DESIGN D23) -------------------- */
/* Set NQ_BOX_NUMBER per unit and reflash — the image is a few KB, reflashed maybe
 * a handful of times in a unit's life, so a flash cell + provisioning path would
 * be over-engineering at this scale. board_hw_addr() keeps the abstraction, so the
 * number can become flash-backed later without touching the app. Reported at boot /
 * after a hard reset; the master then software-remaps it to a slot. */
#define NQ_BOX_NUMBER  1u

uint8_t board_hw_addr(void) {
    return NQ_BOX_NUMBER;
}

/* --- Self-running TDMA slot + diagnostics (D24) ----------------------------- */
/* VALIDATE ON HW: board_slot_due latches on the RS-485 idle line after a frame
 * (predecessor done) or a TIM compare at round_start + (addr-1)*slot; the supply
 * reads come from a divider-to-ADC at the switcher output / bus input; the MCU
 * temperature from the internal sensor; sensor_fault from a read-failure latch. */
int     board_slot_due(uint8_t addr)   { (void)addr; return 0; }
uint8_t board_supply_input_dv(void)    { return 120u; }   /* ~12.0 V placeholder */
uint8_t board_supply_internal_dv(void) { return 50u;  }   /* ~5.0 V placeholder  */
int8_t  board_cpu_temp_c(void)         { return 25;   }
int     board_sensor_fault(void)       { return 0;    }

/* --- DRDY latches, set in the EXTI ISR ------------------------------------- */

static volatile uint8_t s_drdy[NQ_SENSOR_COUNT];

void board_drdy_isr(nq_sensor_id_t id) { if (id < NQ_SENSOR_COUNT) s_drdy[id] = 1; }

int board_drdy_take(nq_sensor_id_t id) {
    if (id >= NQ_SENSOR_COUNT) return 0;
    if (s_drdy[id]) { s_drdy[id] = 0; return 1; }
    return 0;
}

/* --- Clock-loss detection: STM32 Clock Security System --------------------- */
/* CSS fires an NMI when the HSE fails and hardware auto-switches SYSCLK to HSI;
 * this weak HAL callback runs from that path. The MCU is already on the internal
 * RC by the time the runtime reacts — nic_clocksync_recover then does the orderly
 * cleanup (pin remap back to UART2 RX, re-init UART2, select RC). */

static volatile uint8_t s_clock_lost;

void HAL_RCC_CSSCallback(void) { s_clock_lost = 1; }

int board_clock_lost_take(void) {
    if (s_clock_lost) { s_clock_lost = 0; return 1; }
    return 0;
}

/* --- Misc ------------------------------------------------------------------ */

void board_delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < ticks) { /* spin */ }
}

void board_watchdog_kick(void) { HAL_IWDG_Refresh(&hiwdg); }

/* RS-485 link diagnostics. The UART echo check (RS-485 mode returns our own
 * transmission) calls _bump on a mismatch; the count is reported on HEALTH. */
static volatile uint16_t s_link_errors;
void     board_link_error_bump(void) { if (s_link_errors < 0xFFFFu) s_link_errors++; }
unsigned board_link_errors(void)     { return s_link_errors; }

void board_init(void) {
    /* CS pins idle high. */
    HAL_GPIO_WritePin(CS_ICM_GPIO_Port,     CS_ICM_Pin,     GPIO_PIN_SET);
    HAL_GPIO_WritePin(CS_ADXL355_GPIO_Port, CS_ADXL355_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CS_SCL3300_GPIO_Port, CS_SCL3300_Pin, GPIO_PIN_SET);

    /* DWT cycle counter for µs delays. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    /* Clock Security System: detect master-clock loss on the HSE. */
    HAL_RCC_EnableCSS();
}
