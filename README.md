# Whack-a-Rabbit on STM32H743I-EVAL

A touchscreen "whack-a-mole" style game running on the **STM32H743I-EVAL** evaluation board, written in C with the STM32 HAL and BSP drivers. Rabbits pop out of nine holes. Tap them with the hammer before they hide, and score as many points as you can in 60 seconds.

<!-- Add a photo or GIF of the board running the game: -->
<!-- ![Whack-a-Rabbit on STM32H743I-EVAL](docs/whack.jpg) -->

## Features

- Touchscreen-only control, no buttons needed
- 3 x 3 grid of holes, with the grid sized automatically to the LCD
- 60-second round with a live countdown
- Score, best score and time left shown in the top bar
- Difficulty rises with your score: rabbits appear faster, stay up for less time, and up to 3 can be up at once
- "BONK!" hammer animation when you hit a rabbit
- All graphics drawn with rectangles and circles, so no bitmaps or image files are needed
- Flicker-free: only the cell that changes is redrawn
- LED feedback: green LED (LED1) toggles on every hit, red LED (LED3) turns on at "TIME UP"

## Hardware

| Item | Details |
|---|---|
| Board | STM32H743I-EVAL (STM32H743XIH6 MCU) |
| Display | On-board LCD, landscape |
| Input | On-board touchscreen |
| Debugger | On-board ST-LINK (USB) |

## Software requirements

- STM32CubeIDE
- STM32H7 HAL and BSP drivers (included in the project's `Drivers` folder)

## Getting started

1. Clone or download this repository.
2. Open STM32CubeIDE and choose **File -> Import -> General -> Existing Projects into Workspace**.
3. Select the `touch_game/STM32CubeIDE` folder and click **Finish**.
4. Build the project (hammer icon). You want 0 errors.
5. Connect the board to your PC with the ST-LINK USB cable.
6. Click **Debug** (bug icon), then **Resume (F8)**.

## How to play

1. The title screen shows **WHACK-A-RABBIT**. Tap the screen to start.
2. Rabbits pop out of the holes. Tap a hole with a rabbit in it to hit it with the hammer (+1 point).
3. Rabbits that stay too long hide again, and you get no point for them.
4. When the 60 seconds run out, **TIME UP** appears with your score. Tap to play again.

| Action | Control |
|---|---|
| Start or restart | Tap the screen |
| Hit a rabbit | Tap its hole |

## Difficulty

Everything speeds up as your score grows:

| What | Start | Limit |
|---|---|---|
| Delay between spawns | 900 ms, minus 12 ms per point | 350 ms minimum |
| Time a rabbit stays up | 1200 ms, minus 15 ms per point | 550 ms minimum |
| Rabbits up at the same time | 1, plus 1 for every 10 points | 3 maximum |

These values are `#define` constants at the top of `game.c` (`GAME_SECONDS`, `SPAWN_START_MS`, `UP_START_MS` and so on), so you can tune the game easily.

## Project structure

```
touch_game/
├── Inc/
│   ├── game.h              # Game_Init(), Game_Run()
│   ├── main.h              # BSP includes, LED names
│   └── ...                 # HAL and BSP configuration headers
├── Src/
│   ├── main.c              # Clock, MPU, cache, LED, button and LCD setup; main loop
│   ├── game.c              # Game logic, drawing and touch input
│   ├── stm32h7xx_it.c      # SysTick and EXTI interrupt handlers
│   └── system_stm32h7xx.c  # System init
└── STM32CubeIDE/
    ├── STM32H743XIHX_FLASH.ld        # Linker script
    ├── Example/Startup/              # Startup assembly file
    ├── Drivers/                      # HAL, BSP, CMSIS
    └── touch_game.launch             # Debug configuration
```

Only `game.c` and `game.h` hold the game. The rest comes from ST's BSP example, adapted for this project. The game uses the same `Game_Init()` / `Game_Run()` interface as the Snake game in the other project, so `main.c`, `main.h` and `game.h` are shared between the two games.

## How it works

**Startup (`main.c`):** configure the MPU and caches, start the HAL, set the CPU to 400 MHz, initialise the LEDs, the TAMPER button and the LCD, call `Game_Init()`, then loop on `Game_Run()` every 5 ms.

**Game states (`game.c`):** a small state machine with three states: menu, play and over.

**Holes:** each of the 9 holes has a state (`H_EMPTY`, `H_UP` or `H_HIT`) and a time (`until`) when that state ends. `HAL_GetTick()` supplies the current time in milliseconds.

**Every loop in the play state:**
1. Read the touchscreen. A new touch counts as a tap, and samples in a short grace window (150 ms) after it can also hit.
2. Convert the touch point to a hole number: column = x / cell width, row = (y - top bar) / cell height.
3. If that hole has a rabbit up, mark it hit, draw the hammer, add a point and toggle the green LED.
4. Hide rabbits whose time ran out, and clear hits after 250 ms.
5. Spawn a new rabbit when it is time, up to the current maximum.
6. Update the countdown each second. At zero, call `GameOver()`.

**Drawing:** the drawing unit is one tenth of a cell height, so the rabbit, hole and hammer scale with the screen. The rabbit is built from rectangles (ears) and circles (head, eyes, nose) using the `UTIL_LCD_*` functions.

## Troubleshooting

| Problem | Fix |
|---|---|
| `undefined reference to Game_Init` | Make sure `game.c` is not excluded from the build (Properties -> C/C++ Build). |
| `multiple definition of SystemInit` | `system_stm32h7xx.c` is in the build twice. Remove one copy. |
| `No rule to make target ...` | A deleted file is still listed in the project. Remove it from the project tree. |
| Taps land in the wrong place | Check `gTS.Orientation` in `Game_Init()`. The game uses `TS_SWAP_NONE`, the same as ST's touchscreen demo. |
| Taps are missed or double-counted | Adjust `TAP_GRACE_MS` and `gTS.Accuracy` in `game.c`. |


## Author

Deniston
