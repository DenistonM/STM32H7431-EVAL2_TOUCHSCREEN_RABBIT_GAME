/**
  * Whack-a-Rabbit for STM32H743I-EVAL, touchscreen only.
  *
  * Nine holes in a 3 x 3 grid. Rabbits pop out for a short time.
  * Tap a rabbit to hit it with the hammer (+1 score).
  * You have GAME_SECONDS seconds. The longer you play, the faster they come.
  * Tap the screen to start / restart.
  *
  * Everything is drawn with rectangles and circles only (no bitmaps), and only
  * the cell that changes is redrawn, so there is no flicker.
  *
  * Uses the same Game_Init() / Game_Run() names as the other games, so
  * main.c, main.h and game.h stay unchanged.
  */
#include "main.h"
#include "game.h"
#include <stdlib.h>

#define HUD_H          30
#define COLS           3
#define ROWS           3
#define HOLES          (COLS * ROWS)
#define GAME_SECONDS   60
#define HIT_SHOW_MS    250        /* how long the hammer stays after a hit */
#define TAP_GRACE_MS   150        /* samples this soon after a touch starts can still hit */
#define SPAWN_START_MS 900
#define SPAWN_MIN_MS   350
#define UP_START_MS    1200
#define UP_MIN_MS      550

typedef enum { ST_MENU, ST_PLAY, ST_OVER } State_t;
typedef enum { H_EMPTY, H_UP, H_HIT } HoleState_t;

typedef struct {
  HoleState_t st;
  uint32_t    until;              /* time (ms) when this state ends */
} Hole_t;

static uint32_t W, H;
static int      cw, ch, u;         /* cell width, cell height, drawing unit = ch / 10 */
static State_t  state;
static Hole_t   holes[HOLES];

static int      score, best, timeLeft;
static uint32_t startMs, nextSpawn, touchStart;

/* own touch handles */
static TS_Init_t  gTS;
static TS_State_t gTSState;
static uint8_t    touchWasDown = 0;

/* size in tenths of a unit: UU(46) = 4.6 units */
#define UU(t)  ((t) * u / 10)

/* ---------- drawing helpers ---------- */
static void DrawHud(void)
{
  char buf[48];
  UTIL_LCD_FillRect(0, 0, W, HUD_H, UTIL_LCD_COLOR_BLUE);
  UTIL_LCD_SetFont(&Font20);
  UTIL_LCD_SetBackColor(UTIL_LCD_COLOR_BLUE);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
  sprintf(buf, "Score:%d  Best:%d  Time:%d", score, best, timeLeft);
  UTIL_LCD_DisplayStringAt(8, 5, (uint8_t *)buf, LEFT_MODE);
}

static void DrawCentered(const char *l1, const char *l2)
{
  UTIL_LCD_SetFont(&Font24);
  UTIL_LCD_SetBackColor(UTIL_LCD_COLOR_BLACK);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_YELLOW);
  UTIL_LCD_DisplayStringAt(0, H / 2 - 30, (uint8_t *)l1, CENTER_MODE);
  UTIL_LCD_SetFont(&Font16);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
  UTIL_LCD_DisplayStringAt(0, H / 2 + 5, (uint8_t *)l2, CENTER_MODE);
}

static void CellOrigin(int i, int *x0, int *y0)
{
  *x0 = (i % COLS) * cw;
  *y0 = HUD_H + (i / COLS) * ch;
}

/* Grass cell with an empty hole. */
static void DrawHoleEmpty(int i)
{
  int x0, y0, hx, hy;
  CellOrigin(i, &x0, &y0);
  hx = x0 + cw / 2;
  hy = y0 + UU(70);

  UTIL_LCD_FillRect(x0, y0, cw, ch, UTIL_LCD_COLOR_DARKGREEN);
  UTIL_LCD_DrawRect(x0, y0, cw, ch, UTIL_LCD_COLOR_GREEN);
  UTIL_LCD_FillCircle(hx, hy, UU(28), UTIL_LCD_COLOR_BROWN);   /* rim */
  UTIL_LCD_FillCircle(hx, hy, UU(24), UTIL_LCD_COLOR_BLACK);   /* hole */
}

/* A rabbit coming out of the hole. hit = 0: alert rabbit. hit = 1: hammer on its head. */
static void DrawRabbit(int i, int hit)
{
  int x0, y0, hx;
  CellOrigin(i, &x0, &y0);
  hx = x0 + cw / 2;

  DrawHoleEmpty(i);

  /* ears (outer white, inner pink) */
  UTIL_LCD_FillRect(hx - UU(13), y0 + UU(4), UU(8), UU(28), UTIL_LCD_COLOR_WHITE);
  UTIL_LCD_FillRect(hx + UU(5),  y0 + UU(4), UU(8), UU(28), UTIL_LCD_COLOR_WHITE);
  UTIL_LCD_FillRect(hx - UU(11), y0 + UU(8), UU(4), UU(18), UTIL_LCD_COLOR_LIGHTMAGENTA);
  UTIL_LCD_FillRect(hx + UU(7),  y0 + UU(8), UU(4), UU(18), UTIL_LCD_COLOR_LIGHTMAGENTA);

  /* head */
  UTIL_LCD_FillCircle(hx, y0 + UU(46), UU(18), UTIL_LCD_COLOR_WHITE);

  if (!hit)
  {
    /* eyes, nose, mouth line */
    UTIL_LCD_FillCircle(hx - UU(7), y0 + UU(42), UU(3), UTIL_LCD_COLOR_BLACK);
    UTIL_LCD_FillCircle(hx + UU(7), y0 + UU(42), UU(3), UTIL_LCD_COLOR_BLACK);
    UTIL_LCD_FillCircle(hx, y0 + UU(52), UU(3), UTIL_LCD_COLOR_LIGHTRED);
    UTIL_LCD_DrawHLine(hx - UU(6), y0 + UU(58), UU(12), UTIL_LCD_COLOR_GRAY);
  }
  else
  {
    /* dizzy yellow eyes below the hammer, nose */
    UTIL_LCD_FillCircle(hx - UU(8), y0 + UU(52), UU(4), UTIL_LCD_COLOR_YELLOW);
    UTIL_LCD_FillCircle(hx + UU(8), y0 + UU(52), UU(4), UTIL_LCD_COLOR_YELLOW);
    UTIL_LCD_FillCircle(hx, y0 + UU(60), UU(3), UTIL_LCD_COLOR_LIGHTRED);

    /* hammer: handle from the top of the cell, head across the rabbit's forehead */
    UTIL_LCD_FillRect(hx - UU(3), y0, UU(6), UU(32), UTIL_LCD_COLOR_ORANGE);
    UTIL_LCD_FillRect(hx - UU(22), y0 + UU(32), UU(44), UU(16), UTIL_LCD_COLOR_DARKGRAY);
    UTIL_LCD_DrawRect(hx - UU(22), y0 + UU(32), UU(44), UU(16), UTIL_LCD_COLOR_BLACK);

    UTIL_LCD_SetFont(&Font16);
    UTIL_LCD_SetBackColor(UTIL_LCD_COLOR_DARKGREEN);
    UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_YELLOW);
    UTIL_LCD_DisplayStringAt(x0 + 4, y0 + 4, (uint8_t *)"BONK!", LEFT_MODE);
  }
}

/* ---------- game logic ---------- */
static int ActiveRabbits(void)
{
  int i, n = 0;
  for (i = 0; i < HOLES; i++) if (holes[i].st == H_UP) n++;
  return n;
}

static void SpawnRabbit(uint32_t now)
{
  int tries, idx;
  int upMs = UP_START_MS - score * 15;
  if (upMs < UP_MIN_MS) upMs = UP_MIN_MS;

  for (tries = 0; tries < 12; tries++)
  {
    idx = rand() % HOLES;
    if (holes[idx].st == H_EMPTY)
    {
      holes[idx].st    = H_UP;
      holes[idx].until = now + (uint32_t)upMs;
      DrawRabbit(idx, 0);
      return;
    }
  }
}

static void NewGame(void)
{
  int i;
  score    = 0;
  timeLeft = GAME_SECONDS;
  startMs  = HAL_GetTick();
  nextSpawn = startMs + 500;

  UTIL_LCD_Clear(UTIL_LCD_COLOR_BLACK);
  DrawHud();
  for (i = 0; i < HOLES; i++)
  {
    holes[i].st = H_EMPTY;
    holes[i].until = 0;
    DrawHoleEmpty(i);
  }
  BSP_LED_Off(LED_RED);
  state = ST_PLAY;
}

static void GameOver(void)
{
  char buf[40];
  BSP_LED_On(LED_RED);
  DrawHud();
  sprintf(buf, "Score %d.  Tap to play again", score);
  DrawCentered("TIME UP", buf);
  state = ST_OVER;
}

/* ---------- input ---------- */
/* Returns 1 on a new touch (finger just went down), 2 while held, 0 if none */
static uint8_t ReadTouch(uint32_t *x, uint32_t *y)
{
  BSP_TS_GetState(0, &gTSState);
  if (gTSState.TouchDetected)
  {
    *x = gTSState.TouchX;
    *y = gTSState.TouchY;
    if (!touchWasDown) { touchWasDown = 1; return 1; }
    return 2;
  }
  touchWasDown = 0;
  return 0;
}

static void HandleTap(uint32_t px, uint32_t py, uint32_t now)
{
  int col, row, idx;

  if ((int)py < HUD_H) return;

  col = (int)px / cw;   if (col >= COLS) col = COLS - 1;
  row = ((int)py - HUD_H) / ch;   if (row >= ROWS) row = ROWS - 1;
  idx = row * COLS + col;

  if (holes[idx].st == H_UP)
  {
    holes[idx].st    = H_HIT;
    holes[idx].until = now + HIT_SHOW_MS;
    DrawRabbit(idx, 1);
    score++;
    if (score > best) best = score;
    DrawHud();
    BSP_LED_Toggle(LED_GREEN);
  }
}

/* ---------- public ---------- */
void Game_Init(void)
{
  BSP_LCD_GetXSize(0, &W);
  BSP_LCD_GetYSize(0, &H);

  cw = (int)W / COLS;
  ch = ((int)H - HUD_H) / ROWS;
  u  = ch / 10;

  gTS.Width       = W;
  gTS.Height      = H;
  gTS.Orientation = TS_SWAP_NONE;     /* same as the ST touchscreen demo */
  gTS.Accuracy    = 5;
  BSP_TS_Init(0, &gTS);

  srand(HAL_GetTick());
  best  = 0;
  score = 0;
  state = ST_MENU;

  UTIL_LCD_Clear(UTIL_LCD_COLOR_BLACK);
  DrawCentered("WHACK-A-RABBIT", "Tap the rabbits! Tap to start");
}

void Game_Run(void)
{
  uint32_t tx = 0, ty = 0;
  uint8_t  t = ReadTouch(&tx, &ty);
  uint32_t now = HAL_GetTick();
  int i;

  switch (state)
  {
    case ST_MENU:
    case ST_OVER:
      if (t == 1) { NewGame(); }
      break;

    case ST_PLAY:
      if (t == 1) touchStart = now;

      /* first sample of a touch, plus any sample in the short grace window after it */
      if (t == 1 || (t == 2 && (now - touchStart) < TAP_GRACE_MS))
        HandleTap(tx, ty, now);

      /* rabbits that stayed too long hide again; hit rabbits disappear */
      for (i = 0; i < HOLES; i++)
      {
        if ((holes[i].st == H_UP || holes[i].st == H_HIT) && now >= holes[i].until)
        {
          holes[i].st = H_EMPTY;
          DrawHoleEmpty(i);
        }
      }

      /* spawn a new rabbit */
      if (now >= nextSpawn)
      {
        int maxUp = 1 + score / 10;
        int gap   = SPAWN_START_MS - score * 12;
        if (maxUp > 3) maxUp = 3;
        if (gap < SPAWN_MIN_MS) gap = SPAWN_MIN_MS;
        if (ActiveRabbits() < maxUp) SpawnRabbit(now);
        nextSpawn = now + (uint32_t)gap + (uint32_t)(rand() % 200);
      }

      /* countdown */
      {
        int remaining = GAME_SECONDS - (int)((now - startMs) / 1000);
        if (remaining < 0) remaining = 0;
        if (remaining != timeLeft)
        {
          timeLeft = remaining;
          DrawHud();
        }
        if (remaining == 0) { GameOver(); }
      }
      break;

    default:
      break;
  }
}
