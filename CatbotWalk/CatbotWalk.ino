// CatbotWalk - a tiny top-down world on the M5Stack StopWatch (C152).
// The Catbot (model: Catbot_____1body.step) wanders around a small round
// island on the 466x466 round AMOLED. Made to be watched through the
// acrylic dome of the StopWatch ball housing.
// Controls: tap screen or KEY A = jump, KEY B = day / night.
// Sprites: 8 directions pre-rendered from the 3D model (render_sprites.py).
#include <M5Unified.h>
#include <math.h>
#include "catbot_sprites.h"

static constexpr uint16_t C(int r, int g, int b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

struct Palette {
  uint16_t water, sparkle, sand, grass, grassDark, pond, pondLight,
           trunk, canopy1, canopy2, canopy3, pine, rock, rockLight, shadow;
};
static const Palette PAL_DAY = {
  C(38,116,184), C(96,168,220), C(233,216,160), C(104,190,84), C(80,158,62),
  C(96,178,224), C(150,210,242), C(122,82,44),
  C(52,148,66), C(66,168,80), C(44,132,58), C(38,116,70),
  C(150,156,160), C(178,184,188), C(84,160,66)
};
static const Palette PAL_NIGHT = {
  C(14,40,82), C(48,88,138), C(118,106,82), C(34,82,50), C(26,64,40),
  C(28,68,108), C(44,92,134), C(66,46,24),
  C(24,74,42), C(30,88,50), C(20,64,36), C(18,60,38),
  C(84,90,96), C(108,114,120), C(26,64,40)
};
static const uint16_t FLOWER_COLS[5] = {
  C(255,110,160), C(255,214,64), C(255,150,70), C(190,140,250), C(250,250,245)
};
static const uint16_t FIREFLY = C(196,255,150);

static const int SCR = 466, CX = 233;

// ===== 視点調整（ドーム越しの見え方に合わせて変える場所） =====
// アクリル半球はレンズとして効くので、実機で見ながらこの4つを触る想定です。
static const float VIEW_SCALE = 0.40f;  // ズーム（FOV）: 大きいほど世界が大きく・狭く見える
static const float VIEW_TILT = 0.56f;   // 視点の傾き: 1.0=真上、 小さいほど斜め上から見た感じ（地面が縦に潰れる）
static const int VIEW_CENTER_Y = 292;   // 画面の中でキャラの足元が来る高さ（px）
static const float BOT_SPR_SCALE = 0.85f;  // キャラの表示サイズ（世界とのバランス調整用）
// ※キャラ絵そのものの見下ろし角度は render_sprites.py の EL（初期値52度）で、
//   VIEW_TILT を大きく変えたらそちらも合わせて再生成すると馴染みます。
static const float ISLAND_R = 950.0f;
static const float POND_X = -320.0f, POND_Y = 210.0f, POND_RX = 210.0f, POND_RY = 130.0f;

M5Canvas canvas(&M5.Display);

// ---------- deterministic world generation ----------
static uint32_t rngState = 20261117u;
static float frand() {  // 0..1
  rngState = rngState * 1664525u + 1013904223u;
  return (rngState >> 8) * (1.0f / 16777216.0f);
}
static float frange(float a, float b) { return a + frand() * (b - a); }

enum ItemType : uint8_t { T_TREE, T_PINE, T_ROCK, T_FLOWER, T_TUFT, T_BOT };
struct WorldItem { float x, y; uint8_t type; uint8_t var; float s; };
static WorldItem items[170];
static int itemCount = 0;
static float speckles[70][2];
static float sparkles[46][2];

struct DrawEnt { float sx, sy; uint8_t type; uint8_t var; float s; };
static DrawEnt drawList[180];
static float botSwayX = 0;

// explicit prototypes (keeps Arduino's auto-prototype generation from
// breaking on the struct types above)
static void genWorld();
static void updateBot(float dt, float t);
static void blitBot(int spr, int feetX, int feetY);
static void drawItem(const DrawEnt &e, const Palette &pal, float bobY, float jumpY, int sprIdx);
static void drawFrame(float t);

static bool inPond(float x, float y, float m) {
  float dx = (x - POND_X) / (POND_RX * m), dy = (y - POND_Y) / (POND_RY * m);
  return dx * dx + dy * dy < 1.0f;
}
static void addItem(uint8_t type) {
  for (int tries = 0; tries < 24; tries++) {
    float a = frand() * 6.2831853f, r = sqrtf(frand()) * 890.0f;
    float x = cosf(a) * r, y = sinf(a) * r;
    if (inPond(x, y, type == T_FLOWER ? 1.02f : 1.18f)) continue;
    if (x * x + y * y < 95.0f * 95.0f) continue;  // spawn clearing
    WorldItem &it = items[itemCount++];
    it.x = x; it.y = y; it.type = type;
    it.var = (uint8_t)(frand() * 256.0f);
    it.s = frange(0.85f, 1.25f);
    return;
  }
}
static void genWorld() {
  for (int i = 0; i < 15; i++) addItem(T_TREE);
  for (int i = 0; i < 8; i++) addItem(T_PINE);
  for (int i = 0; i < 10; i++) addItem(T_ROCK);
  for (int i = 0; i < 44; i++) addItem(T_FLOWER);
  for (int i = 0; i < 80; i++) addItem(T_TUFT);
  for (int i = 0; i < 70; i++) {
    float a = frand() * 6.2831853f, r = sqrtf(frand()) * 925.0f;
    speckles[i][0] = cosf(a) * r; speckles[i][1] = sinf(a) * r;
  }
  for (int i = 0; i < 46; i++) {
    float a = frand() * 6.2831853f, r = frange(1010.0f, 1500.0f);
    sparkles[i][0] = cosf(a) * r; sparkles[i][1] = sinf(a) * r;
  }
}

// ---------- bot state ----------
static float botX = 0, botY = 0;
static float heading = -1.5707963f;   // facing south (toward viewer)
static float headTarget = heading;
static float camX = 0, camY = 0;
static bool walking = true;
static float aiTimer = 3.0f;
static float curSpeed = 0;      // ramped speed (units/s)
static float stepPhase = 0;     // advances with distance travelled: 2*pi = 2 steps
static float jumpT = -1;              // <0 = grounded
static bool night = false;

static float angDiff(float a, float b) {
  float d = a - b;
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  return d;
}

static void updateBot(float dt, float t) {
  aiTimer -= dt;
  float d = sqrtf(botX * botX + botY * botY);
  if (aiTimer <= 0) {
    walking = !walking;
    aiTimer = walking ? frange(2.0f, 4.5f) : frange(0.7f, 1.8f);
    if (walking) headTarget = heading + frange(-1.15f, 1.15f);
  }
  if (d > 740.0f) headTarget = atan2f(-botY, -botX) + frange(-0.35f, 0.35f);
  if (inPond(botX, botY, 1.30f))
    headTarget = atan2f(botY - POND_Y, botX - POND_X);
  // speed ramp: ease in/out so starts and stops read as steps, not gliding
  float tgtSpeed = walking ? 62.0f : 0.0f;
  if (walking && aiTimer < 0.3f) tgtSpeed *= aiTimer / 0.3f;  // decelerate into the stop
  float accel = 300.0f * dt;
  curSpeed += tgtSpeed > curSpeed ? fminf(accel, tgtSpeed - curSpeed)
                                  : -fminf(accel, curSpeed - tgtSpeed);
  // turning is stronger while stepping (feet planted = body turns in steps)
  float turn = angDiff(headTarget, heading);
  float maxTurn = (1.1f + 1.7f * (curSpeed / 62.0f)) * dt;
  heading += turn > maxTurn ? maxTurn : (turn < -maxTurn ? -maxTurn : turn);
  if (curSpeed > 0.05f) {
    // pulsed advance within each step cycle + phase locked to distance,
    // so the feet never slide against the ground
    float dist = curSpeed * dt * (0.72f + 0.28f * cosf(2.0f * stepPhase));
    botX += cosf(heading) * dist;
    botY += sinf(heading) * dist;
    stepPhase += dist / 34.0f * 6.2831853f;   // 34 world units per 2-step cycle
    float dd = sqrtf(botX * botX + botY * botY);
    if (dd > 885.0f) { botX *= 885.0f / dd; botY *= 885.0f / dd; }
  }
  float k = 1.0f - expf(-3.2f * dt);
  camX += (botX - camX) * k;
  camY += (botY - camY) * k;
  (void)t;
}

// ---------- drawing ----------
static inline float toSx(float wx) { return CX + (wx - camX) * VIEW_SCALE; }
static inline float toSy(float wy) { return VIEW_CENTER_Y - (wy - camY) * VIEW_SCALE * VIEW_TILT; }

static void blitBot(int spr, int feetX, int feetY) {
  const float SC = BOT_SPR_SCALE;
  const int dw = (int)(CATBOT_SPR_W * SC), dh = (int)(CATBOT_SPR_H * SC);
  const int ox = feetX - (int)(CATBOT_ANCHOR_X * SC);
  const int oy = feetY - (int)(CATBOT_ANCHOR_Y * SC);
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const uint16_t *rgb = catbot_spr_rgb[spr];
  const uint8_t *alp = catbot_spr_a[spr];
  for (int dy = 0; dy < dh; dy++) {
    int y = oy + dy;
    if (y < 0 || y >= SCR) continue;
    int si0 = (int)(dy / SC) * CATBOT_SPR_W;
    uint16_t *row = buf + y * SCR;
    for (int dx = 0; dx < dw; dx++) {
      int x = ox + dx;
      if (x < 0 || x >= SCR) continue;
      int si = si0 + (int)(dx / SC);
      uint8_t a = pgm_read_byte(alp + si);
      if (a == 0) continue;
      uint16_t fg = pgm_read_word(rgb + si);
      if (a >= 250) { row[x] = fg; continue; }
      uint16_t bg = row[x];
      int r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a)) / 255;
      int g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
      int b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
      row[x] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
}

static void drawItem(const DrawEnt &e, const Palette &pal, float bobY, float jumpY, int sprIdx) {
  int sx = (int)e.sx, sy = (int)e.sy;
  float s = e.s;
  switch (e.type) {
    case T_TREE: {
      const uint16_t can = e.var % 3 == 0 ? pal.canopy1 : (e.var % 3 == 1 ? pal.canopy2 : pal.canopy3);
      canvas.fillEllipse(sx, sy + 2, (int)(27 * s), (int)(8 * s), pal.shadow);
      canvas.fillRect(sx - (int)(3 * s), sy - (int)(24 * s), (int)(7 * s), (int)(24 * s), pal.trunk);
      canvas.fillCircle(sx, sy - (int)(52 * s), (int)(30 * s), can);
      canvas.fillCircle(sx - (int)(22 * s), sy - (int)(36 * s), (int)(20 * s), can);
      canvas.fillCircle(sx + (int)(22 * s), sy - (int)(38 * s), (int)(21 * s), can);
      break;
    }
    case T_PINE: {
      canvas.fillEllipse(sx, sy + 2, (int)(18 * s), (int)(6 * s), pal.shadow);
      canvas.fillRect(sx - (int)(2 * s), sy - (int)(14 * s), (int)(5 * s), (int)(14 * s), pal.trunk);
      canvas.fillTriangle(sx, sy - (int)(58 * s), sx - (int)(20 * s), sy - (int)(20 * s), sx + (int)(20 * s), sy - (int)(20 * s), pal.pine);
      canvas.fillTriangle(sx, sy - (int)(78 * s), sx - (int)(14 * s), sy - (int)(44 * s), sx + (int)(14 * s), sy - (int)(44 * s), pal.pine);
      break;
    }
    case T_ROCK: {
      canvas.fillEllipse(sx, sy - (int)(5 * s), (int)(16 * s), (int)(10 * s), pal.rock);
      canvas.fillEllipse(sx - (int)(4 * s), sy - (int)(9 * s), (int)(8 * s), (int)(5 * s), pal.rockLight);
      break;
    }
    case T_FLOWER: {
      canvas.drawLine(sx, sy, sx, sy - (int)(9 * s), pal.grassDark);
      canvas.fillCircle(sx, sy - (int)(11 * s), (int)(4.5f * s) + 1, FLOWER_COLS[e.var % 5]);
      canvas.fillCircle(sx, sy - (int)(11 * s), 1, C(255, 240, 180));
      break;
    }
    case T_TUFT: {
      uint16_t col = pal.grassDark;
      canvas.drawLine(sx, sy, sx - 3, sy - (int)(9 * s), col);
      canvas.drawLine(sx, sy, sx, sy - (int)(11 * s), col);
      canvas.drawLine(sx, sy, sx + 3, sy - (int)(8 * s), col);
      break;
    }
    case T_BOT: {
      float jf = jumpY > 0 ? jumpY / 30.0f : 0;
      canvas.fillEllipse(sx, sy + 2, (int)(27 * (1.0f - 0.35f * jf)), (int)(8 * (1.0f - 0.35f * jf)), pal.shadow);
      blitBot(sprIdx, sx + (int)botSwayX, sy - (int)(bobY + jumpY));
      break;
    }
  }
}

static void drawFrame(float t) {
  const Palette &pal = night ? PAL_NIGHT : PAL_DAY;
  canvas.fillScreen(pal.water);
  // island (sand rim + grass)
  float ix = toSx(0), iy = toSy(0);
  canvas.fillEllipse((int)ix, (int)iy, (int)((ISLAND_R + 34) * VIEW_SCALE), (int)((ISLAND_R + 34) * VIEW_SCALE * VIEW_TILT), pal.sand);
  canvas.fillEllipse((int)ix, (int)iy, (int)(ISLAND_R * VIEW_SCALE), (int)(ISLAND_R * VIEW_SCALE * VIEW_TILT), pal.grass);
  // pond
  float px = toSx(POND_X), py = toSy(POND_Y);
  canvas.fillEllipse((int)px, (int)py, (int)(POND_RX * VIEW_SCALE), (int)(POND_RY * VIEW_SCALE * VIEW_TILT), pal.pond);
  canvas.fillEllipse((int)px - 8, (int)py - 3, (int)(POND_RX * 0.68f * VIEW_SCALE), (int)(POND_RY * 0.62f * VIEW_SCALE * VIEW_TILT), pal.pondLight);
  // grass speckles + water sparkles (world-anchored)
  for (int i = 0; i < 70; i++) {
    float sx = toSx(speckles[i][0]), sy = toSy(speckles[i][1]);
    if (sx < -4 || sx > SCR + 4 || sy < -4 || sy > SCR + 4) continue;
    canvas.fillCircle((int)sx, (int)sy, 2, pal.grassDark);
  }
  for (int i = 0; i < 46; i++) {
    float sx = toSx(sparkles[i][0]), sy = toSy(sparkles[i][1]);
    if (sx < -4 || sx > SCR + 4 || sy < -4 || sy > SCR + 4) continue;
    canvas.fillCircle((int)sx, (int)sy, 2, pal.sparkle);
  }
  // build draw list (decorations + bot), painter-sorted by screen y
  int n = 0;
  for (int i = 0; i < itemCount; i++) {
    float sx = toSx(items[i].x), sy = toSy(items[i].y);
    if (sx < -90 || sx > SCR + 90 || sy < -110 || sy > SCR + 40) continue;
    drawList[n++] = { sx, sy, items[i].type, items[i].var, items[i].s };
  }
  float bobY, jumpY = 0;
  if (jumpT >= 0) jumpY = sinf(3.14159265f * (jumpT / 0.55f)) * 30.0f;
  bobY = curSpeed > 4.0f ? fabsf(sinf(stepPhase)) * 4.2f : sinf(t * 2.2f) * 0.7f;
  botSwayX = curSpeed > 4.0f ? sinf(stepPhase) * 2.2f : 0.0f;
  float deg = heading * 57.29578f;
  int sprIdx = ((int)floorf(deg + 90.0f + 22.5f) / 45) % 8;
  if (sprIdx < 0) sprIdx += 8;
  drawList[n++] = { toSx(botX), toSy(botY), T_BOT, 0, 1.0f };
  for (int i = 1; i < n; i++) {  // insertion sort by sy
    DrawEnt key = drawList[i];
    int j = i - 1;
    while (j >= 0 && drawList[j].sy > key.sy) { drawList[j + 1] = drawList[j]; j--; }
    drawList[j + 1] = key;
  }
  for (int i = 0; i < n; i++) drawItem(drawList[i], pal, bobY, jumpY, sprIdx);
  // fireflies at night
  if (night) {
    for (int i = 0; i < 12; i++) {
      float fx = 233 + sinf(t * 0.23f + i * 2.4f) * 170.0f + sinf(t * 0.9f + i) * 22.0f;
      float fy = 250 + cosf(t * 0.31f + i * 1.7f) * 150.0f + cosf(t * 1.3f + i * 2.0f) * 16.0f;
      canvas.fillCircle((int)fx, (int)fy, 2, FIREFLY);
    }
  }
  canvas.pushSprite(0, 0);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setBrightness(200);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.createSprite(SCR, SCR);
  genWorld();
}

void loop() {
  M5.update();
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  float dt = lastMs ? (now - lastMs) / 1000.0f : 0.016f;
  lastMs = now;
  if (dt > 0.05f) dt = 0.05f;
  float t = now / 1000.0f;

  if (M5.Touch.getCount() > 0) {
    auto d = M5.Touch.getDetail(0);
    if (d.wasClicked() && jumpT < 0) jumpT = 0;
  }
  if (M5.BtnA.wasPressed() && jumpT < 0) jumpT = 0;
  if (M5.BtnB.wasPressed()) night = !night;
  if (jumpT >= 0) { jumpT += dt; if (jumpT > 0.55f) jumpT = -1; }

  updateBot(dt, t);
  drawFrame(t);
}
