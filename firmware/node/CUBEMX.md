<p align="center">
  <img src="../../NIC-Quake.svg" width="170"/>
</p>

★ N.I.C. ★

# Node board layer — STM32H503 (CubeMX integration)

This directory is the **hardware-bound** layer: `board.c` implements the
callbacks the portable `nq-*` libraries expect, `main_app.c` composes them into
the node runtime. CubeMX generates the clock/peripheral init and the real
`main.c`; these two files slot on top.

> **Status — written, not yet built or validated.** No ARM toolchain or ST
> headers were available here, so this layer is **not compiled** and **not
> host-tested** (unlike everything above it — 7 passing suites). The routine
> peripherals are standard ST HAL; every line tagged `VALIDATE ON HW` (the clock
> switch, CLKIN/EXT_CLK routing, flash programming) must be confirmed on a real
> board with a scope. Treat it as the starting skeleton, per the agreed plan
> (bootstrap in CubeMX → clean to bare-metal C).

---

## CubeMX peripherals

| Peripheral | Use | Notes |
|---|---|---|
| **SPI1 / SPI2 / SPI3** | ICM-42688 / ADXL355 / SCL3300 | mode 0, 8-bit, master; CS as plain GPIO |
| **USART1** | RS-485 data pair | enable hardware **Driver Enable (DE)**; idle-line detection |
| **USART2** | clock pair (becomes HSE_IN) | its RX pin is remapped to the HSE bypass input at sync |
| **TIM1** | sensor clocks | CH1 = 1.024 MHz (ADXL EXT_CLK), CH2 = 40.96 kHz (ICM CLKIN), PWM |
| **IWDG** | watchdog | ~1 s timeout |
| **EXTI** | DRDY interrupts | one line per sensor INT pin → `board_drdy_isr(id)` |
| **FLASH** | calibration page | reserve one page at `CAL_FLASH_ADDR` |

## Required pin / macro labels (must match `board.c`)

Set these **User Labels** in CubeMX so the generated `main.h` defines the macros:

```
CS_ICM, CS_ADXL355, CS_SCL3300        (GPIO_Output, idle high)
HSE_IN                                (USART2 RX pin / HSE bypass input)
```

and reserve `CAL_FLASH_ADDR` (a free flash page) in your project.

## Clock tree

- **HSE = external clock (bypass)**, fed from the master's distributed 8.192 MHz
  (or 4.096 MHz after the optional /2 flip-flop). PLL → core clock per your
  budget (the node can run slow — seismics are <20 Hz).
- TIM1 must be clocked so CH1 divides to exactly **1.024 MHz** and CH2 to
  **40.96 kHz** — both phase-locked because they come off the HSE-locked PLL.
- Fill `op_select_hse()` in `board.c` with the PLL M/N/P CubeMX computes for the
  external-clock input.

## Wiring it up

In the CubeMX `main.c`, after `MX_*_Init()`:

```c
#include "board.h"
extern void nq_app_run(void);   /* from main_app.c */
...
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init(); MX_SPI1_Init(); MX_SPI2_Init(); MX_SPI3_Init();
    MX_USART1_UART_Init(); MX_USART2_UART_Init();
    MX_TIM1_Init(); MX_IWDG_Init();
    nq_app_run();               /* never returns */
}
```

In the EXTI callback (`HAL_GPIO_EXTI_Callback`), route each sensor's DRDY to
`board_drdy_isr(NQ_SENSOR_xxx)`.

## Clock-loss recovery

Two layers, both already wired in `board.c` / `main_app.c`:

- **CSS (Clock Security System)** — `board_init()` calls `HAL_RCC_EnableCSS()`.
  If the master clock on the HSE dies, the CSS fires an NMI and hardware
  auto-switches SYSCLK to the internal HSI. For the callback to run, the
  CubeMX `NMI_Handler` (in `stm32h5xx_it.c`) must call `HAL_RCC_NMI_IRQHandler()`;
  it dispatches to `HAL_RCC_CSSCallback()` (in board.c), which latches the loss.
  The runtime then emits `NIC_EV_CLOCK_LOST` → `nic_clocksync_recover()` (remap
  HSE_IN back to UART2 RX, re-init UART2, formally select RC) → back to LISTEN.
- **IWDG (~1 s)** — the ultimate backstop: kicked each main-loop pass, so a true
  hang resets the MCU, which reboots on the RC oscillator and re-listens.

## Build

Add `firmware/lib/nq-*` and `firmware/node/*.c` to the CubeIDE/CMake project and
build with `arm-none-eabi-gcc`. The `nq-*` libraries are plain portable C and
compile unchanged for the target; only this `node/` layer pulls in the ST HAL.
