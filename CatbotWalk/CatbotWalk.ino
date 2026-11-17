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
static const float VIEW_SCALE = 1.00f;  // ズーム（FOV）: 大きいほど世界が大きく・狭く見える
static const float VIEW_TILT = 0.56f;   // 視点の傾き: 1.0=真上、 小さいほど斜め上から見た感じ（地面が縦に潰れる）
static const int VIEW_CENTER_Y = 292;   // 画面の中でキャラの足元が来る高さ（px）
static const float BOT_SPR_SCALE = 0.85f;  // キャラの表示サイズ（世界とのバランス調整用）
// ※キャラ絵そのものの見下ろし角度は render_sprites.py の EL（初期値52度）で、
//   VIEW_TILT を大きく変えたらそちらも合わせて再生成すると馴染みます。
static const float WORLD = 5200.0f;      // 世界1枚の一辺。端どうしがつながっていて、どこまで歩いても途切れない
// wrap a coordinate difference into [-WORLD/2, WORLD/2): the world repeats endlessly (torus)
static inline float wd(float d) {
  d = fmodf(d + WORLD * 0.5f, WORLD);
  if (d < 0) d += WORLD;
  return d - WORLD * 0.5f;
}
static inline float wrapPos(float v) { return wd(v); }
static const float WALK_SPEED = 240.0f;  // 歩く速さ（world units/s）
static const float OBJ = VIEW_SCALE / 0.40f;          // 木・花など飾りの描画倍率（ズームに追従）
static const float BS = BOT_SPR_SCALE / 0.85f;        // キャラ周り（影・ジャンプ高さ）の倍率
static const float BOT_R = 40.0f;                     // キャラの当たり半径（world units）
struct Pond { float x, y, rx, ry; };
static const Pond PONDS[] = {
  {-520.0f, 380.0f, 230.0f, 140.0f}, {900.0f, -700.0f, 280.0f, 170.0f},
  {-1200.0f, -1100.0f, 200.0f, 130.0f}, {1300.0f, 1150.0f, 240.0f, 150.0f},
};
static const int NPONDS = sizeof(PONDS) / sizeof(PONDS[0]);

M5Canvas canvas(&M5.Display);

// ---------- deterministic world generation ----------
static uint32_t rngState = 20261117u;
static float frand() {  // 0..1
  rngState = rngState * 1664525u + 1013904223u;
  return (rngState >> 8) * (1.0f / 16777216.0f);
}
static float frange(float a, float b) { return a + frand() * (b - a); }

enum ItemType : uint8_t { T_TREE, T_PINE, T_ROCK, T_FLOWER, T_TUFT, T_BOT, T_BUG, T_PROP };
struct WorldItem {
  float x, y; uint8_t type; uint8_t var; float s;
  uint8_t fruit;    // trees: apples hanging (fruit trees only)
  bool thirsty;     // flowers: wilted, waiting for water
  float timer;      // trees: apple regrow countdown / flowers: bloom animation
};
static WorldItem items[1000];
static int itemCount = 0;
static inline bool isFruitTree(const WorldItem &it) { return it.type == T_TREE && ((it.var >> 2) % 3) == 0; }
static const int NSPECK = 1500;
static float speckles[NSPECK][2];

struct DrawEnt { float sx, sy; uint8_t type; uint8_t var; float s; int16_t idx; };
static DrawEnt drawList[360];

// explicit prototypes (keeps Arduino's auto-prototype generation from
// breaking on the struct types above)
static void genWorld();
struct Agent;
static void updateAgent(int idx, float dt);
static void updateTalks(float dt);
static void spawnNpcs();
static void blitBot(int spr, int feetX, int feetY, uint8_t color);
static void drawItem(const DrawEnt &e, const Palette &pal, float t);
static void drawBubble(int idx, float t);
static void drawFrame(float t);
static void drawFishWater(const Palette &pal);
static void drawProp(uint8_t var, const Palette &pal, float t);

static int inPond(float x, float y, float m) {  // returns pond index + 1, or 0
  for (int i = 0; i < NPONDS; i++) {
    float dx = wd(x - PONDS[i].x) / (PONDS[i].rx * m), dy = wd(y - PONDS[i].y) / (PONDS[i].ry * m);
    if (dx * dx + dy * dy < 1.0f) return i + 1;
  }
  return 0;
}
static void addItem(uint8_t type) {
  for (int tries = 0; tries < 24; tries++) {
    float x = frange(-WORLD * 0.5f, WORLD * 0.5f), y = frange(-WORLD * 0.5f, WORLD * 0.5f);
    if (inPond(x, y, type == T_FLOWER ? 1.02f : 1.18f)) continue;
    if (x * x + y * y < 260.0f * 260.0f) continue;  // spawn clearing
    WorldItem &it = items[itemCount++];
    it.x = x; it.y = y; it.type = type;
    it.var = (uint8_t)(frand() * 256.0f);
    it.s = frange(0.85f, 1.25f);
    it.fruit = isFruitTree(it) ? 3 : 0;
    it.thirsty = type == T_FLOWER && ((it.var >> 3) % 4) == 0;
    it.timer = 0;
    return;
  }
}
static void genWorld() {
  for (int i = 0; i < 110; i++) addItem(T_TREE);
  for (int i = 0; i < 60; i++) addItem(T_PINE);
  for (int i = 0; i < 70; i++) addItem(T_ROCK);
  for (int i = 0; i < 300; i++) addItem(T_FLOWER);
  for (int i = 0; i < 450; i++) addItem(T_TUFT);
  for (int i = 0; i < NSPECK; i++) {
    speckles[i][0] = frange(-WORLD * 0.5f, WORLD * 0.5f);
    speckles[i][1] = frange(-WORLD * 0.5f, WORLD * 0.5f);
  }
}

// ---------- bots (agent 0 = the player's Catbot, the rest are NPC friends) ----------
enum BotColor : uint8_t { COL_YELLOW, COL_WHITE, COL_BLACK, COL_SILVER, NCOLORS };
struct Agent {
  float x, y, heading, headTarget, speed, stepPhase, aiTimer;
  float jumpT;      // <0 = grounded
  float talkT;      // >0 = chatting with partner, seconds left
  float talkCool;   // >0 = won't start a new chat yet
  int8_t partner;
  bool walking;
  uint8_t color;
};
static const int NPC_COUNT = 45;        // NPC の数（島全体に散らばる）
static const int NAGENT = 1 + NPC_COUNT;
static Agent ag[NAGENT];
static const float NPC_SPEED_MUL = 0.55f;   // NPC はのんびり歩く
static const float TALK_DIST = 130.0f;       // この距離まで近づいたら会話開始
static float camX = 0, camY = 0;
static bool night = false;
static float gT = 0;   // seconds since boot (for animations)

// ---------- events: the player's Catbot sometimes goes off to do something ----------
// 虫取り / 果物探し / 花の水やり / 釣り。思いついたら吹き出しで知らせて、そこまで歩いて行ってやる。
enum ActKind : uint8_t { ACT_NONE, ACT_BUG, ACT_FRUIT, ACT_WATER, ACT_FISH, NACT };
enum ActPhase : uint8_t { PH_GOTO, PH_DO };
enum Icon : uint8_t { ICON_BUG, ICON_APPLE, ICON_FLOWER, ICON_FISH, ICON_BOOT, ICON_CAN };
struct Activity {
  uint8_t kind, phase, sub, slot;
  int16_t target;     // bug / item / pond index
  float tx, ty;       // where to walk to (fishing spot)
  float fx, fy;       // what to face (bug, tree, flower, bobber)
  float lx, ly;       // where the apple lands
  float t, dur, cool;
  bool success;
};
static Activity act = {};
static uint8_t lastKind = ACT_NONE;
static float actGoalDist = 1e9f;
static float popT = -1;  static uint8_t popKind = 0;   // "got it!" icon above the Catbot
static int16_t shakeIdx = -1; static float shakeT = 0;  // tree being shaken
static const float ACT_INTERVAL_MIN = 6.0f, ACT_INTERVAL_MAX = 12.0f;  // 次のイベントまでの間隔（秒）

// butterflies flutter around flowers (daytime only)
struct Bug { int16_t home; float ph, respawn; uint8_t col; bool alive; };
static const int NBUG = 40;
static Bug bugs[NBUG];
static const uint16_t BUG_COLS[4] = { C(255,236,110), C(250,250,255), C(140,200,255), C(255,160,80) };
static int16_t flowerIdx[400];
static int nFlower = 0;

static bool freeToChat(int i) {
  return ag[i].talkT <= 0 && ag[i].talkCool <= 0 && (i != 0 || act.kind == ACT_NONE);
}

static float angDiff(float a, float b) {
  float d = a - b;
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  return d;
}

// ---------- collision ----------
struct Obst { float x, y, r; };
static Obst obst[400];
static int nObst = 0;
static Obst nearObst[64];
static int nNear = 0;
static void buildObstacles() {
  for (int i = 0; i < itemCount && nObst < 400; i++) {
    const WorldItem &it = items[i];
    float r = it.type == T_TREE ? 34.0f : it.type == T_PINE ? 24.0f : it.type == T_ROCK ? 40.0f : 0.0f;
    if (r > 0) obst[nObst++] = { it.x, it.y, r * it.s };
  }
}
static void gatherNear(float x, float y, float range) {
  nNear = 0;
  for (int i = 0; i < nObst && nNear < 64; i++) {
    float dx = wd(obst[i].x - x), dy = wd(obst[i].y - y), rr = range + obst[i].r;
    if (dx * dx + dy * dy < rr * rr) nearObst[nNear++] = obst[i];
  }
}
// margin > 1 keeps a little clearance when planning ahead
static bool blockedAt(float x, float y, float margin) {
  if (inPond(x, y, 1.0f + 0.12f * margin)) return true;
  for (int i = 0; i < nNear; i++) {
    float dx = wd(x - nearObst[i].x), dy = wd(y - nearObst[i].y), rr = (nearObst[i].r + BOT_R) * margin;
    if (dx * dx + dy * dy < rr * rr) return true;
  }
  return false;
}
static const float LOOK_STEP = 45.0f;
static const int LOOK_N = 6;   // look 270 units ahead
static float lookMax = 1e9f;   // don't look past the place we're walking to (e.g. a pond edge)
static bool pathClear(float x, float y, float a) {
  float c = cosf(a), s = sinf(a);
  for (int i = 1; i <= LOOK_N && LOOK_STEP * i <= lookMax; i++)
    if (blockedAt(x + c * LOOK_STEP * i, y + s * LOOK_STEP * i, 1.15f)) return false;
  return true;
}

// other bots also count as obstacles while walking
static void addAgentObstacles(int self, float x, float y, float range) {
  for (int i = 0; i < NAGENT && nNear < 64; i++) {
    if (i == self) continue;
    // two bots that are both free to chat don't dodge each other: they'll meet and talk
    if (freeToChat(self) && freeToChat(i)) continue;
    float dx = wd(ag[i].x - x), dy = wd(ag[i].y - y), rr = range + BOT_R;
    if (dx * dx + dy * dy < rr * rr) nearObst[nNear++] = { ag[i].x, ag[i].y, BOT_R };
  }
}

static void updateAgent(int idx, float dt) {
  Agent &A = ag[idx];
  float &botX = A.x, &botY = A.y, &heading = A.heading, &headTarget = A.headTarget;
  float &curSpeed = A.speed, &stepPhase = A.stepPhase, &aiTimer = A.aiTimer;
  bool &walking = A.walking;
  const bool busy = idx == 0 && act.kind != ACT_NONE;   // walking/heading are driven by updateActivity()
  const float maxSpeed = idx == 0 ? WALK_SPEED : WALK_SPEED * NPC_SPEED_MUL;
  if (A.talkCool > 0) A.talkCool -= dt;
  if (A.jumpT >= 0) { A.jumpT += dt; if (A.jumpT > 0.55f) A.jumpT = -1; }
  if (A.talkT > 0) {
    // chatting: stand still and face the partner
    const Agent &B = ag[A.partner];
    headTarget = atan2f(wd(B.y - botY), wd(B.x - botX));
    walking = false;
    aiTimer = 0.4f;
  } else if (!busy) {
    aiTimer -= dt;
  }
  if (!busy && aiTimer <= 0 && A.talkT <= 0) {
    walking = !walking;
    aiTimer = walking ? frange(3.0f, 7.0f) : frange(0.5f, 1.3f);
    if (walking) headTarget = heading + frange(-1.15f, 1.15f);
  }
  // the player's Catbot likes company: head for the nearest friend who's free to chat
  if (idx == 0 && walking && freeToChat(0)) {
    int best = -1; float bd = 900.0f * 900.0f;
    for (int i = 1; i < NAGENT; i++) {
      if (!freeToChat(i)) continue;
      float dx = wd(ag[i].x - botX), dy = wd(ag[i].y - botY), d2 = dx * dx + dy * dy;
      if (d2 < bd) { bd = d2; best = i; }
    }
    if (best > 0) headTarget = atan2f(wd(ag[best].y - botY), wd(ag[best].x - botX));
  }
  // obstacle avoidance: if the way ahead is blocked, pick the nearest clear heading
  gatherNear(botX, botY, LOOK_STEP * LOOK_N + BOT_R);
  addAgentObstacles(idx, botX, botY, LOOK_STEP * LOOK_N + BOT_R);
  if (busy && act.kind == ACT_FRUIT) {   // don't steer around the very tree we're heading for
    const WorldItem &tr = items[act.target];
    for (int i = 0; i < nNear; i++)
      if (nearObst[i].x == tr.x && nearObst[i].y == tr.y) { nearObst[i] = nearObst[--nNear]; break; }
  }
  lookMax = (busy && act.phase == PH_GOTO) ? actGoalDist : 1e9f;
  bool aheadClear = true;
  if (walking) {
    if (!pathClear(botX, botY, headTarget)) {
      // try the side the bot is already turning toward first, so it doesn't dither
      float side = angDiff(heading, headTarget) >= 0 ? 1.0f : -1.0f;
      bool found = false;
      for (int k = 1; k <= 9 && !found; k++) {
        for (int j = 0; j < 2 && !found; j++) {
          float a = headTarget + (j == 0 ? side : -side) * k * 0.35f;
          if (pathClear(botX, botY, a)) { headTarget = a; found = true; }
        }
      }
      if (!found) headTarget = heading + 3.14159265f;
    }
    aheadClear = pathClear(botX, botY, heading);
  }
  lookMax = 1e9f;
  // speed ramp: ease in/out so starts and stops read as steps, not gliding
  float tgtSpeed = walking ? maxSpeed : 0.0f;
  if (walking && aiTimer < 0.3f) tgtSpeed *= aiTimer / 0.3f;  // decelerate into the stop
  if (!aheadClear) tgtSpeed *= 0.35f;                         // slow down while steering round
  float accel = 1200.0f * dt;
  curSpeed += tgtSpeed > curSpeed ? fminf(accel, tgtSpeed - curSpeed)
                                  : -fminf(accel, curSpeed - tgtSpeed);
  // turning is stronger while stepping (feet planted = body turns in steps)
  float turn = angDiff(headTarget, heading);
  float maxTurn = (1.1f + 1.7f * (curSpeed / maxSpeed)) * dt;
  heading += turn > maxTurn ? maxTurn : (turn < -maxTurn ? -maxTurn : turn);
  heading = angDiff(heading, 0.0f);           // keep angles bounded (they used to drift without limit)
  headTarget = heading + angDiff(headTarget, heading);
  if (curSpeed > 0.05f) {
    // pulsed advance within each step cycle + phase locked to distance,
    // so the feet never slide against the ground
    float dist = curSpeed * dt * (0.72f + 0.28f * cosf(2.0f * stepPhase));
    float nx = botX + cosf(heading) * dist, ny = botY + sinf(heading) * dist;
    // hard constraint: slide out of trees/rocks, never step into a pond
    for (int i = 0; i < nNear; i++) {
      float dx = wd(nx - nearObst[i].x), dy = wd(ny - nearObst[i].y), rr = nearObst[i].r + BOT_R;
      float d2 = dx * dx + dy * dy;
      if (d2 < rr * rr && d2 > 1e-4f) {
        float dd0 = sqrtf(d2);
        nx += dx / dd0 * rr - dx; ny += dy / dd0 * rr - dy;
      }
    }
    if (!inPond(nx, ny, 1.0f)) { botX = nx; botY = ny; }
    stepPhase += dist / 110.0f * 6.2831853f;   // 110 world units per 2-step cycle
    botX = wrapPos(botX); botY = wrapPos(botY);
  }
}

// two free bots that bump into each other stop and chat for a bit
static void updateTalks(float dt) {
  for (int i = 0; i < NAGENT; i++) {
    Agent &A = ag[i];
    if (A.talkT > 0) {
      A.talkT -= dt;
      if (A.talkT <= 0) {   // say bye: turn away and walk on
        const Agent &B = ag[A.partner];
        A.talkT = 0; A.talkCool = frange(6.0f, 10.0f);
        A.headTarget = atan2f(wd(A.y - B.y), wd(A.x - B.x)) + frange(-0.8f, 0.8f);
        A.walking = true; A.aiTimer = frange(3.0f, 6.0f);
      }
      continue;
    }
    if (!freeToChat(i)) continue;
    for (int j = i + 1; j < NAGENT; j++) {
      Agent &B = ag[j];
      if (!freeToChat(j)) continue;
      float dx = wd(B.x - A.x), dy = wd(B.y - A.y);
      if (dx * dx + dy * dy < TALK_DIST * TALK_DIST) {
        float len = frange(2.5f, 4.5f);
        A.talkT = B.talkT = len;
        A.partner = j; B.partner = i;
        A.jumpT = 0;   // a little "oh, hi!" hop
        break;
      }
    }
  }
}

static void spawnNpcs() {
  ag[0] = {};
  ag[0].heading = ag[0].headTarget = -1.5707963f;   // facing south (toward viewer)
  ag[0].aiTimer = 3.0f; ag[0].walking = true; ag[0].jumpT = -1; ag[0].color = COL_YELLOW;
  for (int i = 1; i < NAGENT; i++) {
    Agent &A = ag[i];
    A = {};
    for (int tries = 0; tries < 40; tries++) {
      A.x = frange(-WORLD * 0.5f, WORLD * 0.5f); A.y = frange(-WORLD * 0.5f, WORLD * 0.5f);
      if (A.x * A.x + A.y * A.y < 400.0f * 400.0f) continue;
      gatherNear(A.x, A.y, BOT_R * 2);
      if (!blockedAt(A.x, A.y, 1.3f)) break;
    }
    A.heading = A.headTarget = frand() * 6.2831853f;
    A.aiTimer = frange(0.5f, 5.0f); A.walking = frand() < 0.6f;
    A.jumpT = -1; A.talkCool = frange(0.0f, 3.0f);
    A.color = 1 + (i - 1) % (NCOLORS - 1);   // white / black / silver
  }
}

// ---------- events ----------
static void bugPos(int i, float t, float &x, float &y, float &z) {
  const Bug &b = bugs[i];
  const WorldItem &h = items[b.home];
  x = h.x + sinf(t * 0.7f + b.ph) * 55.0f + sinf(t * 1.9f + b.ph * 2.0f) * 15.0f;
  y = h.y + cosf(t * 0.6f + b.ph * 1.3f) * 45.0f;
  z = 46.0f + sinf(t * 3.1f + b.ph) * 9.0f;   // flight height in screen px
}
// pick a random flower, preferably one that isn't on screen right now
static int pickFlower(bool offscreen) {
  int f = flowerIdx[(int)(frand() * nFlower) % nFlower];
  for (int tries = 0; offscreen && tries < 12; tries++) {
    const WorldItem &it = items[f];
    if (fabsf(wd(it.x - camX)) > 320.0f || fabsf(wd(it.y - camY)) > 480.0f) break;
    f = flowerIdx[(int)(frand() * nFlower) % nFlower];
  }
  return f;
}
static void initEvents() {
  for (int i = 0; i < itemCount; i++)
    if (items[i].type == T_FLOWER && nFlower < 400) flowerIdx[nFlower++] = i;
  for (int i = 0; i < NBUG; i++) {
    bugs[i].home = pickFlower(false); bugs[i].ph = frand() * 6.2831853f;
    bugs[i].col = (uint8_t)(frand() * 4) & 3; bugs[i].alive = true;
  }
  act.kind = ACT_NONE; act.cool = 6.0f;
}

// apples grow back, watered flowers finish blooming, flowers slowly get thirsty again,
// caught butterflies are replaced somewhere else
static void updateWorld(float dt) {
  int nThirsty = 0;
  for (int i = 0; i < itemCount; i++) {
    WorldItem &it = items[i];
    if (isFruitTree(it) && it.fruit < 3) {
      it.timer -= dt;
      if (it.timer <= 0) { it.fruit++; it.timer = 45.0f; }
    } else if (it.type == T_FLOWER) {
      if (it.timer > 0) it.timer -= dt;
      if (it.thirsty) nThirsty++;
    }
  }
  static float thirstT = 15.0f;
  thirstT -= dt;
  if (thirstT <= 0) {
    thirstT = frange(10.0f, 20.0f);
    if (nThirsty < nFlower / 3) {
      WorldItem &it = items[pickFlower(true)];
      if (it.timer <= 0) it.thirsty = true;
    }
  }
  for (int i = 0; i < NBUG; i++) {
    Bug &b = bugs[i];
    if (b.alive) continue;
    b.respawn -= dt;
    if (b.respawn <= 0) { b.home = pickFlower(true); b.ph = frand() * 6.2831853f; b.alive = true; }
  }
  if (shakeT > 0) shakeT -= dt;
}

static bool findTarget(uint8_t kind) {
  const Agent &P = ag[0];
  float bd = 1100.0f * 1100.0f; int bi = -1;
  if (kind == ACT_BUG) {
    if (night) return false;
    for (int i = 0; i < NBUG; i++) {
      if (!bugs[i].alive) continue;
      float x, y, z; bugPos(i, gT, x, y, z);
      if (inPond(x, y, 1.15f)) continue;
      float dx = wd(x - P.x), dy = wd(y - P.y), d2 = dx * dx + dy * dy;
      if (d2 < bd) { bd = d2; bi = i; }
    }
  } else if (kind == ACT_FRUIT || kind == ACT_WATER) {
    for (int i = 0; i < itemCount; i++) {
      const WorldItem &it = items[i];
      bool ok = kind == ACT_FRUIT ? (it.type == T_TREE && it.fruit > 0) : (it.type == T_FLOWER && it.thirsty);
      if (!ok) continue;
      float dx = wd(it.x - P.x), dy = wd(it.y - P.y), d2 = dx * dx + dy * dy;
      if (d2 < bd) { bd = d2; bi = i; }
    }
  } else {   // fishing: walk to the near shore of the closest pond
    bd = 1500.0f * 1500.0f;
    for (int i = 0; i < NPONDS; i++) {
      const Pond &q = PONDS[i];
      float th = atan2f(wd(P.y - q.y) / q.ry, wd(P.x - q.x) / q.rx);
      float sx = q.x + cosf(th) * q.rx * 1.32f, sy = q.y + sinf(th) * q.ry * 1.32f;
      float dx = wd(sx - P.x), dy = wd(sy - P.y), d2 = dx * dx + dy * dy;
      if (d2 < bd) { bd = d2; bi = i; act.tx = wrapPos(sx); act.ty = wrapPos(sy); }
    }
  }
  if (bi < 0) return false;
  act.target = bi;
  return true;
}

static void tryStartActivity() {
  uint8_t order[4] = { ACT_BUG, ACT_FRUIT, ACT_WATER, ACT_FISH };
  for (int i = 3; i > 0; i--) {
    int j = (int)(frand() * (i + 1)); if (j > i) j = i;
    uint8_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
  }
  for (int i = 0; i < 3; i++)   // don't do the same thing twice in a row unless nothing else is around
    if (order[i] == lastKind) { order[i] = order[3]; order[3] = lastKind; break; }
  for (int k = 0; k < 4; k++) {
    if (findTarget(order[k])) {
      act.kind = order[k]; act.phase = PH_GOTO; act.t = 0; act.sub = 0;
      lastKind = act.kind;
      return;
    }
  }
  act.cool = 4.0f;
}

static bool actGoal(float &gx, float &gy, float &arrive) {
  switch (act.kind) {
    case ACT_BUG: {
      if (night || !bugs[act.target].alive) return false;
      float z; bugPos(act.target, gT, gx, gy, z);
      arrive = 75.0f; return true;
    }
    case ACT_FRUIT: {
      const WorldItem &it = items[act.target];
      if (!it.fruit) return false;
      gx = it.x; gy = it.y; arrive = 34.0f * it.s + BOT_R + 30.0f; return true;
    }
    case ACT_WATER: {
      const WorldItem &it = items[act.target];
      if (!it.thirsty) return false;
      gx = it.x; gy = it.y; arrive = 70.0f; return true;
    }
    case ACT_FISH: gx = act.tx; gy = act.ty; arrive = 55.0f; return true;
  }
  return false;
}

static void actBeginDo() {
  Agent &P = ag[0];
  act.phase = PH_DO; act.t = 0; act.sub = 0;
  switch (act.kind) {
    case ACT_BUG: {
      float z; bugPos(act.target, gT, act.fx, act.fy, z);
      bugs[act.target].alive = false; bugs[act.target].respawn = 25.0f;
      P.jumpT = 0;   // leap with the net
      break;
    }
    case ACT_FRUIT: {
      const WorldItem &it = items[act.target];
      act.fx = it.x; act.fy = it.y;
      float dx = wd(P.x - it.x), dy = wd(P.y - it.y), d = fmaxf(1.0f, sqrtf(dx * dx + dy * dy));
      float r = 34.0f * it.s + 16.0f;   // the apple drops between the trunk and the Catbot
      act.lx = wrapPos(it.x + dx / d * r); act.ly = wrapPos(it.y + dy / d * r);
      shakeIdx = act.target; shakeT = 1.2f;
      break;
    }
    case ACT_WATER: act.fx = items[act.target].x; act.fy = items[act.target].y; break;
    case ACT_FISH: {
      const Pond &q = PONDS[act.target];
      float th = atan2f(wd(P.y - q.y) / q.ry, wd(P.x - q.x) / q.rx);
      act.fx = wrapPos(q.x + cosf(th) * q.rx * 0.80f); act.fy = wrapPos(q.y + sinf(th) * q.ry * 0.80f);
      act.dur = 0.6f + frange(2.0f, 5.0f);   // when the fish bites
      act.success = frand() < 0.85f;          // otherwise: an old boot
      break;
    }
  }
}

static void actFinish(uint8_t icon) {
  popKind = icon; popT = 0;
  act.kind = ACT_NONE; act.cool = frange(ACT_INTERVAL_MIN, ACT_INTERVAL_MAX);
  ag[0].walking = false; ag[0].aiTimer = frange(1.0f, 2.0f);
}
static void actAbort() {
  act.kind = ACT_NONE; act.cool = 3.0f;
  ag[0].aiTimer = 0.5f;
}

static void updateActivity(float dt) {
  Agent &P = ag[0];
  if (popT >= 0) { popT += dt; if (popT > 1.8f) popT = -1; }
  if (act.kind == ACT_NONE) {
    if (P.talkT <= 0) { act.cool -= dt; if (act.cool <= 0) tryStartActivity(); }
    return;
  }
  act.t += dt;
  if (act.phase == PH_GOTO) {
    float gx, gy, arrive;
    if (!actGoal(gx, gy, arrive) || act.t > 25.0f) { actAbort(); return; }
    float dx = wd(gx - P.x), dy = wd(gy - P.y), d = sqrtf(dx * dx + dy * dy);
    actGoalDist = d;
    if (d < arrive) { actBeginDo(); return; }
    P.walking = true; P.aiTimer = 1.0f;
    P.headTarget = atan2f(dy, dx);
    return;
  }
  // PH_DO: stand and face whatever we're busy with
  P.walking = false; P.aiTimer = 1.0f;
  P.headTarget = atan2f(wd(act.fy - P.y), wd(act.fx - P.x));
  switch (act.kind) {
    case ACT_BUG:
      if (act.t > 1.0f) actFinish(ICON_BUG);
      break;
    case ACT_FRUIT: {
      WorldItem &it = items[act.target];
      if (act.sub == 0 && act.t > 1.2f) {          // an apple comes loose
        if (it.fruit > 0) it.fruit--;
        if (it.timer <= 0) it.timer = 45.0f;
        act.slot = it.fruit; act.sub = 1;
      } else if (act.sub == 1 && act.t > 2.1f) {   // hop and pick it up
        P.jumpT = 0; act.sub = 2;
      } else if (act.sub == 2 && act.t > 2.5f) {
        actFinish(ICON_APPLE);
      }
      break;
    }
    case ACT_WATER: {
      WorldItem &it = items[act.target];
      if (act.sub == 0 && act.t > 2.4f) { it.thirsty = false; it.timer = 1.5f; act.sub = 1; }
      else if (act.sub == 1 && act.t > 3.1f) actFinish(ICON_FLOWER);
      break;
    }
    case ACT_FISH:
      if (act.sub == 0 && act.t > act.dur + 0.9f) { act.sub = 1; P.jumpT = 0; }   // reel in!
      else if (act.sub == 1 && act.t > act.dur + 1.6f) actFinish(act.success ? ICON_FISH : ICON_BOOT);
      break;
  }
}

// ---------- drawing ----------
static inline float toSx(float wx) { return CX + wd(wx - camX) * VIEW_SCALE; }
static inline float toSy(float wy) { return VIEW_CENTER_Y - wd(wy - camY) * VIEW_SCALE * VIEW_TILT; }

// body-colour LUT: [color][shade 0..31] -> RGB565 (shade = red channel of the yellow body)
static uint16_t tintLut[NCOLORS][32];
static void buildTints() {
  static const uint8_t base[NCOLORS][3] = {
    {255, 255, 0}, {246, 246, 240}, {58, 58, 66}, {178, 186, 198} };
  for (int c = 0; c < NCOLORS; c++)
    for (int sh = 0; sh < 32; sh++) {
      float f = fminf(1.0f, sh / 27.0f);
      f = 0.25f + 0.75f * f;
      int r = (int)(base[c][0] * f), g = (int)(base[c][1] * f), b = (int)(base[c][2] * f);
      if (c == COL_SILVER && sh >= 27) { r = g = b = 236; }   // metallic highlight
      tintLut[c][sh] = C(r, g, b);
    }
}

// Catbot sprites are pre-rendered once per colour x direction into their own
// canvases through the M5GFX API (so the pixel format is the library's business,
// not ours), then stamped onto the frame with a transparent key colour.
static const uint16_t KEY = C(255, 0, 255);
static M5Canvas *botSpr[NCOLORS][8];
static void buildBotSprites() {
  const float SC = BOT_SPR_SCALE;
  const int dw = (int)(CATBOT_SPR_W * SC), dh = (int)(CATBOT_SPR_H * SC);
  for (int c = 0; c < NCOLORS; c++) {
    for (int k = 0; k < 8; k++) {
      M5Canvas *sp = new M5Canvas(&canvas);
      sp->setPsram(true);
      sp->setColorDepth(16);
      sp->createSprite(dw, dh);
      sp->fillSprite(KEY);
      const uint16_t *rgb = catbot_spr_rgb[k];
      const uint8_t *alp = catbot_spr_a[k];
      for (int y = 0; y < dh; y++) {
        int si0 = (int)(y / SC) * CATBOT_SPR_W;
        for (int x = 0; x < dw; x++) {
          int si = si0 + (int)(x / SC);
          if (alp[si] < 128) continue;
          uint16_t fg = rgb[si];
          if (c != COL_YELLOW) {   // recolour the yellow body, keep the red details
            int r5 = fg >> 11, g6 = (fg >> 5) & 63, b5 = fg & 31;
            int dg = g6 - 2 * r5;
            if (b5 <= 6 && r5 >= 5 && dg <= 8 && dg >= -8) fg = tintLut[c][r5];
          }
          if (fg == KEY) fg ^= 0x0020;
          sp->drawPixel(x, y, fg);
        }
      }
      botSpr[c][k] = sp;
    }
  }
}

static void blitBot(int spr, int feetX, int feetY, uint8_t color) {
  const float SC = BOT_SPR_SCALE;
  const int ox = feetX - (int)(CATBOT_ANCHOR_X * SC);
  const int oy = feetY - (int)(CATBOT_ANCHOR_Y * SC);
  botSpr[color][spr]->pushSprite(&canvas, ox, oy, KEY);
}

// ---------- event drawing ----------
static void drawSparkle(int x, int y, int r, uint16_t col) {
  canvas.drawLine(x - r, y, x + r, y, col); canvas.drawLine(x, y - r, x, y + r, col);
  canvas.drawLine(x - r / 2, y - r / 2, x + r / 2, y + r / 2, col);
  canvas.drawLine(x - r / 2, y + r / 2, x + r / 2, y - r / 2, col);
}
static void drawIcon(uint8_t k, int x, int y, float sc) {
  auto R = [sc](float v) { return (int)(v * sc + 0.5f); };
  switch (k) {
    case ICON_BUG:
      canvas.fillEllipse(x - R(6), y - R(3), R(6), R(5), C(255, 170, 60));
      canvas.fillEllipse(x + R(6), y - R(3), R(6), R(5), C(255, 170, 60));
      canvas.fillEllipse(x - R(5), y + R(4), R(4), R(3), C(255, 214, 90));
      canvas.fillEllipse(x + R(5), y + R(4), R(4), R(3), C(255, 214, 90));
      canvas.fillRoundRect(x - R(1.5f), y - R(6), R(3) + 1, R(13), 1, C(60, 40, 30));
      break;
    case ICON_APPLE:
      canvas.fillCircle(x, y + R(1), R(7), C(222, 40, 40));
      canvas.fillCircle(x - R(3), y - R(2), R(2), C(255, 150, 150));
      canvas.drawLine(x, y - R(5), x + R(2), y - R(10), C(110, 70, 30));
      canvas.fillEllipse(x + R(5), y - R(8), R(3), R(1.5f) + 1, C(70, 170, 60));
      break;
    case ICON_FLOWER:
      for (int i = 0; i < 5; i++) {
        float a = i * 1.2566371f - 1.5707963f;
        canvas.fillCircle(x + (int)(cosf(a) * R(5)), y + (int)(sinf(a) * R(5)), R(4), C(255, 110, 160));
      }
      canvas.fillCircle(x, y, R(3), C(255, 214, 64));
      break;
    case ICON_FISH:
      canvas.fillTriangle(x + R(7), y, x + R(14), y - R(6), x + R(14), y + R(6), C(70, 130, 210));
      canvas.fillEllipse(x, y, R(10), R(6), C(100, 166, 232));
      canvas.fillCircle(x - R(5), y - R(1), R(1.5f) + 1, C(255, 255, 255));
      canvas.fillCircle(x - R(5), y - R(1), 1, C(20, 20, 30));
      break;
    case ICON_BOOT:
      canvas.fillRect(x - R(5), y - R(9), R(8), R(13), C(112, 82, 52));
      canvas.fillRoundRect(x - R(5), y + R(1), R(14), R(6), R(2), C(112, 82, 52));
      canvas.drawLine(x - R(5), y - R(9), x + R(3), y - R(9), C(70, 50, 30));
      break;
    case ICON_CAN:
      canvas.drawCircle(x, y - R(6), R(5), C(70, 150, 180));
      canvas.fillRoundRect(x - R(7), y - R(5), R(14), R(11), R(2), C(90, 180, 210));
      canvas.drawLine(x + R(6), y, x + R(13), y - R(7), C(90, 180, 210));
      canvas.drawLine(x + R(6), y + 1, x + R(13), y - R(6), C(90, 180, 210));
      break;
  }
}

// where the player's Catbot holds things (screen px)
static void handPos(float &hx, float &hy, float &fdx, float &fdy) {
  const Agent &P = ag[0];
  float c = cosf(P.heading), s = sinf(P.heading);
  float jumpY = P.jumpT >= 0 ? sinf(3.14159265f * (P.jumpT / 0.55f)) * 30.0f * BS : 0.0f;
  hx = toSx(P.x) + c * 26.0f * VIEW_SCALE;
  hy = toSy(P.y) - s * 26.0f * VIEW_SCALE * VIEW_TILT - 42.0f * BS - jumpY;
  fdx = c; fdy = -s * VIEW_TILT;
  float l = sqrtf(fdx * fdx + fdy * fdy);
  fdx /= l; fdy /= l;
}
// fish (or boot) flying from the bobber to the Catbot
static bool fishFly(float &x, float &y) {
  if (act.kind != ACT_FISH || act.phase != PH_DO || act.sub != 1) return false;
  float u = (act.t - (act.dur + 0.9f)) / 0.6f;
  if (u < 0 || u > 1) return false;
  float x0 = toSx(act.fx), y0 = toSy(act.fy) - 4;
  float x1 = toSx(ag[0].x), y1 = toSy(ag[0].y) - 100.0f * BS;
  x = x0 + (x1 - x0) * u; y = y0 + (y1 - y0) * u - 70.0f * sinf(3.14159265f * u);
  return true;
}
// bobber, line end (screen px); during the cast it flies in an arc from the rod tip
static void bobberPos(float tipx, float tipy, float &x, float &y) {
  float bx = toSx(act.fx), by = toSy(act.fy) - 3;
  if (act.t < 0.6f) {
    float u = act.t / 0.6f;
    x = tipx + (bx - tipx) * u; y = tipy + (by - tipy) * u - 40.0f * sinf(3.14159265f * u);
  } else {
    x = bx; y = by;
    if (act.sub == 0 && act.t < act.dur) y += sinf(gT * 3.0f) * 1.5f;
    else if (act.sub == 0) y += ((int)((act.t - act.dur) * 9.0f) % 2) ? 3 : 0;   // nibble!
  }
}

// bobber and ripples on the water surface (drawn right after the ponds)
static void drawFishWater(const Palette &pal) {
  if (act.kind != ACT_FISH || act.phase != PH_DO || act.t < 0.6f) return;
  int bx = (int)toSx(act.fx), by = (int)toSy(act.fy);
  const uint16_t ripple = night ? C(110, 150, 190) : C(225, 242, 255);
  if (act.sub == 0 && act.t < act.dur) {
    canvas.drawEllipse(bx, by + 1, 12, 4, ripple);
  } else if (act.sub == 0) {
    float b = act.t - act.dur;
    for (int k = 0; k < 2; k++) {
      float rr = fmodf(b * 40.0f + k * 14.0f, 28.0f) + 6.0f;
      canvas.drawEllipse(bx, by + 1, (int)rr, (int)(rr * 0.4f) + 1, ripple);
    }
  } else {
    float rr = 8.0f + (act.t - act.dur - 0.9f) * 60.0f;
    canvas.drawEllipse(bx, by + 1, (int)rr, (int)(rr * 0.4f) + 1, ripple);
    canvas.drawEllipse(bx, by + 1, (int)(rr * 0.6f), (int)(rr * 0.24f) + 1, ripple);
    return;   // bobber is out of the water
  }
  float x, y; bobberPos(0, 0, x, y);
  canvas.fillCircle((int)x, (int)y, 4, C(230, 50, 50));
  canvas.fillCircle((int)x, (int)y - 3, 2, C(255, 255, 255));
}

// things held by the Catbot (net / watering can / rod) and the falling apple
static void drawProp(uint8_t var, const Palette &pal, float t) {
  if (var == 1) {   // apple: falls from the canopy, bounces, waits to be picked up
    const WorldItem &tr = items[act.target];
    static const int8_t FR[3][2] = { {-12, -44}, {9, -60}, {17, -33} };
    float s = tr.s * OBJ;
    float x0 = toSx(tr.x) + FR[act.slot][0] * s, y0 = toSy(tr.y) + FR[act.slot][1] * s;
    float x1 = toSx(act.lx), y1 = toSy(act.ly) - 7;
    float u = (act.t - 1.2f) / 0.5f, x, y;
    if (u < 1) { x = x0 + (x1 - x0) * u; y = y0 + (y1 - y0) * u * u; }
    else { x = x1; y = y1 - (u < 1.5f ? 12.0f * sinf(3.14159265f * (u - 1.0f) * 2.0f) : 0.0f); }
    canvas.fillEllipse((int)x1, (int)y1 + 7, 7, 3, pal.shadow);
    drawIcon(ICON_APPLE, (int)x, (int)y, 1.3f);
    return;
  }
  float hx, hy, fdx, fdy; handPos(hx, hy, fdx, fdy);
  switch (act.kind) {
    case ACT_BUG: {   // swing the net from overhead down to the front
      float a = 1.5f - 2.0f * fminf(1.0f, act.t / 0.45f);
      float L = 46.0f;
      float tx = hx + fdx * L * cosf(a), ty = hy + fdy * L * cosf(a) - L * sinf(a);
      for (int o = -1; o <= 1; o++) canvas.drawLine((int)hx + o, (int)hy, (int)tx + o, (int)ty, C(150, 110, 60));
      float cx = hx + (tx - hx) * 1.32f, cy = hy + (ty - hy) * 1.32f;
      canvas.fillCircle((int)cx, (int)cy, 13, C(214, 236, 226));
      canvas.drawCircle((int)cx, (int)cy, 13, C(250, 250, 250));
      canvas.drawCircle((int)cx, (int)cy, 12, C(250, 250, 250));
      if (act.t > 0.4f) drawIcon(ICON_BUG, (int)cx, (int)cy, 0.8f);
      break;
    }
    case ACT_WATER: {
      float fx = toSx(act.fx), fy = toSy(act.fy) - 12.0f * OBJ;
      float sgn = fx >= hx ? 1.0f : -1.0f;
      int x = (int)hx, y = (int)hy;
      canvas.drawCircle(x, y - 8, 6, C(70, 150, 180));
      canvas.fillRoundRect(x - 9, y - 7, 18, 14, 3, C(90, 180, 210));
      float spx = hx + sgn * 20.0f, spy = hy - 12.0f;
      canvas.drawLine(x + (int)(sgn * 8), y, (int)spx, (int)spy, C(90, 180, 210));
      canvas.drawLine(x + (int)(sgn * 8), y + 1, (int)spx, (int)spy + 1, C(90, 180, 210));
      if (act.t < 2.4f) {
        for (int k = 0; k < 6; k++) {
          float u = fmodf(t * 1.8f + k / 6.0f, 1.0f);
          float dx = spx + (fx - spx) * u, dy = spy + (fy - spy) * u - 16.0f * sinf(3.14159265f * u);
          canvas.fillCircle((int)dx, (int)dy, 2, C(120, 196, 255));
        }
      }
      break;
    }
    case ACT_FISH: {
      float tipx = hx + fdx * 26.0f, tipy = hy + fdy * 26.0f - 48.0f;
      if (act.sub == 1) { tipx = hx - fdx * 6.0f; tipy = hy - 60.0f; }   // pulled back while reeling
      for (int o = 0; o <= 1; o++) canvas.drawLine((int)hx + o, (int)hy, (int)tipx + o, (int)tipy, C(130, 90, 50));
      float ex, ey;
      if (!fishFly(ex, ey)) {
        if (act.sub == 1) break;
        bobberPos(tipx, tipy, ex, ey);
      }
      float mx = (tipx + ex) * 0.5f, my = (tipy + ey) * 0.5f + 8.0f;   // a little sag
      canvas.drawLine((int)tipx, (int)tipy, (int)mx, (int)my, C(235, 235, 235));
      canvas.drawLine((int)mx, (int)my, (int)ex, (int)ey, C(235, 235, 235));
      if (act.t < 0.6f) canvas.fillCircle((int)ex, (int)ey, 4, C(230, 50, 50));
      break;
    }
  }
}

// "got it!" icon rising over the Catbot
static void drawEventOverlay(float t) {
  float fx, fy;
  if (fishFly(fx, fy)) drawIcon(act.success ? ICON_FISH : ICON_BOOT, (int)fx, (int)fy, 1.4f);
  if (popT >= 0) {
    int x = (int)toSx(ag[0].x), y = (int)(toSy(ag[0].y) - 128.0f * BS - popT * 28.0f);
    float sc = 1.8f * fminf(1.0f, 0.4f + popT / 0.2f);
    drawIcon(popKind, x, y, sc);
    if (popKind != ICON_BOOT) {
      for (int k = 0; k < 3; k++) {
        float a = t * 2.5f + k * 2.094f;
        drawSparkle(x + (int)(cosf(a) * 26), y + (int)(sinf(a) * 18), 4, C(255, 246, 170));
      }
    } else {   // a sweat drop
      canvas.fillCircle(x + 22, y - 6, 4, C(140, 200, 255));
      canvas.fillTriangle(x + 18, y - 7, x + 26, y - 7, x + 22, y - 15, C(140, 200, 255));
    }
  }
}

static void drawItem(const DrawEnt &e, const Palette &pal, float t) {
  int sx = (int)e.sx, sy = (int)e.sy;
  float s = e.s * OBJ;
  switch (e.type) {
    case T_TREE: {
      const uint16_t can = e.var % 3 == 0 ? pal.canopy1 : (e.var % 3 == 1 ? pal.canopy2 : pal.canopy3);
      canvas.fillEllipse(sx, sy + 2, (int)(27 * s), (int)(8 * s), pal.shadow);
      canvas.fillRect(sx - (int)(3 * s), sy - (int)(24 * s), (int)(7 * s), (int)(24 * s), pal.trunk);
      const int cx = sx + ((e.idx == shakeIdx && shakeT > 0) ? (int)(sinf(t * 45.0f) * 5.0f * fminf(1.0f, shakeT)) : 0);
      canvas.fillCircle(cx, sy - (int)(52 * s), (int)(30 * s), can);
      canvas.fillCircle(cx - (int)(22 * s), sy - (int)(36 * s), (int)(20 * s), can);
      canvas.fillCircle(cx + (int)(22 * s), sy - (int)(38 * s), (int)(21 * s), can);
      static const int8_t FR[3][2] = { {-12, -44}, {9, -60}, {17, -33} };   // apple spots (same as drawProp)
      for (int f = 0; f < items[e.idx].fruit; f++) {
        int ax = cx + (int)(FR[f][0] * s), ay = sy + (int)(FR[f][1] * s);
        canvas.fillCircle(ax, ay, (int)(3.6f * s) + 1, C(222, 40, 40));
        canvas.fillCircle(ax - (int)(1.2f * s), ay - (int)(1.2f * s), (int)(1.0f * s), C(255, 150, 150));
      }
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
      const WorldItem &it = items[e.idx];
      if (it.thirsty) {   // wilted: drooping stem, small faded head
        canvas.drawLine(sx, sy, sx + (int)(3 * s), sy - (int)(7 * s), pal.grassDark);
        canvas.fillCircle(sx + (int)(4.5f * s), sy - (int)(6.5f * s), (int)(2.8f * s) + 1, C(168, 140, 96));
        break;
      }
      float hs = s;
      if (it.timer > 0) hs = s * (1.0f + 0.5f * sinf(3.14159265f * (1.0f - it.timer / 1.5f)));   // bloom pop
      canvas.drawLine(sx, sy, sx, sy - (int)(9 * s), pal.grassDark);
      canvas.fillCircle(sx, sy - (int)(11 * s), (int)(4.5f * hs) + 1, FLOWER_COLS[e.var % 5]);
      canvas.fillCircle(sx, sy - (int)(11 * s), (int)(1.2f * hs), C(255, 240, 180));
      if (it.timer > 0) {
        for (int k = 0; k < 4; k++) {
          float a = t * 3.0f + k * 1.5707963f, r = 9.0f * s * (1.6f - it.timer / 1.5f);
          drawSparkle(sx + (int)(cosf(a) * r), sy - (int)(11 * s) + (int)(sinf(a) * r * 0.7f), 4, C(255, 250, 190));
        }
      }
      break;
    }
    case T_BUG: {
      const Bug &b = bugs[e.idx];
      int y = sy - (int)e.s;
      canvas.fillEllipse(sx, sy, 5, 2, pal.shadow);
      int w = 2 + (int)(6.0f * fabsf(sinf(t * 15.0f + b.ph)));
      uint16_t col = BUG_COLS[b.col];
      canvas.fillEllipse(sx - w, y - 2, w, 5, col);
      canvas.fillEllipse(sx + w, y - 2, w, 5, col);
      canvas.fillEllipse(sx - w * 2 / 3, y + 4, w * 2 / 3 + 1, 3, col);
      canvas.fillEllipse(sx + w * 2 / 3, y + 4, w * 2 / 3 + 1, 3, col);
      canvas.fillRect(sx - 1, y - 5, 2, 11, C(60, 40, 30));
      break;
    }
    case T_PROP: drawProp(e.var, pal, t); break;
    case T_TUFT: {
      uint16_t col = pal.grassDark;
      int w = (int)(3 * s);
      for (int o = -1; o <= 1; o++) {  // a bit thicker when zoomed in
        canvas.drawLine(sx + o, sy, sx - w + o, sy - (int)(9 * s), col);
        canvas.drawLine(sx + o, sy, sx + o, sy - (int)(11 * s), col);
        canvas.drawLine(sx + o, sy, sx + w + o, sy - (int)(8 * s), col);
      }
      break;
    }
    case T_BOT: {
      const Agent &A = ag[e.var];
      float jumpY = A.jumpT >= 0 ? sinf(3.14159265f * (A.jumpT / 0.55f)) * 30.0f * BS : 0.0f;
      float bobY, swayX = 0;
      if (A.speed > 4.0f) {
        bobY = fabsf(sinf(A.stepPhase)) * 4.2f * BS;
        swayX = sinf(A.stepPhase) * 2.2f * BS;
      } else if (A.talkT > 0) {
        // talking: whoever has the turn bounces a bit while "speaking"
        bool speaking = ((int)(A.talkT / 0.7f) + (e.var < A.partner ? 0 : 1)) % 2 == 0;
        bobY = speaking ? fabsf(sinf(t * 11.0f)) * 3.5f * BS : sinf(t * 2.2f) * 0.7f * BS;
        swayX = speaking ? sinf(t * 5.5f) * 1.5f * BS : 0.0f;
      } else {
        bobY = sinf(t * 2.2f + e.var) * 0.7f * BS;
      }
      if (e.var == 0 && act.kind == ACT_FRUIT && act.phase == PH_DO && act.t < 1.2f)
        swayX = sinf(t * 38.0f) * 3.0f * BS;   // shaking the tree
      if (e.var == 0 && act.kind == ACT_WATER && act.phase == PH_DO && act.t < 2.4f)
        bobY = fabsf(sinf(t * 4.0f)) * 1.5f * BS;
      float deg = A.heading * 57.29578f;
      int sprIdx = ((int)floorf((deg + 90.0f + 22.5f) / 45.0f)) % 8;
      if (sprIdx < 0) sprIdx += 8;
      float jf = jumpY > 0 ? jumpY / (30.0f * BS) : 0;
      canvas.fillEllipse(sx, sy + 2, (int)(27 * BS * (1.0f - 0.35f * jf)), (int)(8 * BS * (1.0f - 0.35f * jf)), pal.shadow);
      blitBot(sprIdx, sx + (int)swayX, sy - (int)(bobY + jumpY), A.color);
      break;
    }
  }
}

// speech bubble with animated dots above whoever is "speaking"
static void drawBubble(int idx, float t) {
  const Agent &A = ag[idx];
  if (idx == 0 && act.kind != ACT_NONE && act.phase == PH_GOTO && act.t < 1.8f) {
    // thought bubble: what the Catbot wants to go and do
    static const uint8_t want[NACT] = { 0, ICON_BUG, ICON_APPLE, ICON_CAN, ICON_FISH };
    int sx = (int)toSx(A.x), sy = (int)toSy(A.y);
    int top = sy - (int)(CATBOT_ANCHOR_Y * BOT_SPR_SCALE) - 8;
    int bw = 44, bh = 36, bx = sx + 12, by = top - bh;
    canvas.fillRoundRect(bx, by, bw, bh, 14, C(255, 255, 255));
    canvas.fillCircle(bx + 6, by + bh + 5, 5, C(255, 255, 255));
    canvas.fillCircle(bx - 1, by + bh + 13, 3, C(255, 255, 255));
    drawIcon(want[act.kind], bx + bw / 2, by + bh / 2 + 1, 1.4f);
    return;
  }
  if (A.talkT <= 0) return;
  bool speaking = ((int)(A.talkT / 0.7f) + (idx < A.partner ? 0 : 1)) % 2 == 0;
  if (!speaking) return;
  int sx = (int)toSx(A.x), sy = (int)toSy(A.y);
  int top = sy - (int)(CATBOT_ANCHOR_Y * BOT_SPR_SCALE) - 8;
  int bw = 46, bh = 26, bx = sx + 10, by = top - bh;
  if (bx + bw < 0 || bx > SCR || by + bh + 10 < 0 || by > SCR) return;
  canvas.fillRoundRect(bx, by, bw, bh, 9, C(255, 255, 255));
  canvas.fillTriangle(bx + 6, by + bh - 1, bx + 18, by + bh - 1, bx + 2, by + bh + 9, C(255, 255, 255));
  int nd = 1 + (int)(t * 3.0f) % 3;
  for (int i = 0; i < nd; i++) canvas.fillCircle(bx + 12 + i * 11, by + bh / 2, 3, C(70, 70, 80));
}

static void drawFrame(float t) {
  const Palette &pal = night ? PAL_NIGHT : PAL_DAY;
  canvas.fillScreen(pal.grass);   // endless meadow
  // pond
  for (int i = 0; i < NPONDS; i++) {
    const Pond &q = PONDS[i];
    float px = toSx(q.x), py = toSy(q.y);
    if (px < -q.rx * VIEW_SCALE || px > SCR + q.rx * VIEW_SCALE || py < -q.ry * VIEW_SCALE || py > SCR + q.ry * VIEW_SCALE) continue;
    canvas.fillEllipse((int)px, (int)py, (int)(q.rx * VIEW_SCALE), (int)(q.ry * VIEW_SCALE * VIEW_TILT), pal.pond);
    canvas.fillEllipse((int)px - 8, (int)py - 3, (int)(q.rx * 0.68f * VIEW_SCALE), (int)(q.ry * 0.62f * VIEW_SCALE * VIEW_TILT), pal.pondLight);
  }
  drawFishWater(pal);
  // grass speckles + water sparkles (world-anchored)
  for (int i = 0; i < NSPECK; i++) {
    float sx = toSx(speckles[i][0]), sy = toSy(speckles[i][1]);
    if (sx < -4 || sx > SCR + 4 || sy < -4 || sy > SCR + 4) continue;
    canvas.fillCircle((int)sx, (int)sy, 3, pal.grassDark);
  }
  // build draw list (decorations + bot), painter-sorted by screen y
  int n = 0;
  for (int i = 0; i < itemCount; i++) {
    float sx = toSx(items[i].x), sy = toSy(items[i].y);
    if (sx < -120 * OBJ || sx > SCR + 120 * OBJ || sy < -30 || sy > SCR + 110 * OBJ) continue;
    if (n >= 240) break;
    drawList[n++] = { sx, sy, items[i].type, items[i].var, items[i].s, (int16_t)i };
  }
  for (int i = 0; i < NAGENT && n < 300; i++) {
    float sx = toSx(ag[i].x), sy = toSy(ag[i].y);
    if (sx < -90 || sx > SCR + 90 || sy < -30 || sy > SCR + 200) continue;
    drawList[n++] = { sx, sy, T_BOT, (uint8_t)i, 1.0f, -1 };
  }
  if (!night) {
    for (int i = 0; i < NBUG && n < 350; i++) {
      if (!bugs[i].alive) continue;
      float x, y, z; bugPos(i, t, x, y, z);
      float sx = toSx(x), sy = toSy(y);
      if (sx < -20 || sx > SCR + 20 || sy < -10 || sy > SCR + 80) continue;
      drawList[n++] = { sx, sy, T_BUG, 0, z, (int16_t)i };
    }
  }
  if (act.kind != ACT_NONE && act.phase == PH_DO) {
    if (act.kind != ACT_FRUIT) {   // held prop sorts at the ground spot in front of the Catbot
      float gy = ag[0].y + sinf(ag[0].heading) * 26.0f;
      drawList[n++] = { toSx(ag[0].x), toSy(gy), T_PROP, 0, 1.0f, -1 };
    } else if (act.sub == 1) {
      drawList[n++] = { toSx(act.lx), toSy(act.ly), T_PROP, 1, 1.0f, -1 };
    }
  }
  for (int i = 1; i < n; i++) {  // insertion sort by sy
    DrawEnt key = drawList[i];
    int j = i - 1;
    while (j >= 0 && drawList[j].sy > key.sy) { drawList[j + 1] = drawList[j]; j--; }
    drawList[j + 1] = key;
  }
  for (int i = 0; i < n; i++) drawItem(drawList[i], pal, t);
  for (int i = 0; i < NAGENT; i++) drawBubble(i, t);
  drawEventOverlay(t);
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
  buildObstacles();
  buildTints();
  buildBotSprites();
  spawnNpcs();
  initEvents();
}

void loop() {
  M5.update();
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  float dt = lastMs ? (now - lastMs) / 1000.0f : 0.016f;
  lastMs = now;
  if (dt > 0.05f) dt = 0.05f;
  float t = now / 1000.0f;
  gT = t;

  if (M5.Touch.getCount() > 0) {
    auto d = M5.Touch.getDetail(0);
    if (d.wasClicked() && ag[0].jumpT < 0) ag[0].jumpT = 0;
  }
  if (M5.BtnA.wasPressed() && ag[0].jumpT < 0) ag[0].jumpT = 0;
  if (M5.BtnB.wasPressed()) night = !night;

  updateWorld(dt);
  updateActivity(dt);
  updateTalks(dt);
  for (int i = 0; i < NAGENT; i++) updateAgent(i, dt);
  float k = 1.0f - expf(-5.0f * dt);
  camX = wrapPos(camX + wd(ag[0].x - camX) * k);
  camY = wrapPos(camY + wd(ag[0].y - camY) * k);
  drawFrame(t);
}
