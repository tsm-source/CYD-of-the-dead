#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

namespace {

TFT_eSPI display;
TFT_eSprite frameBuffer(&display);
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(33, 36);
Preferences preferences;

constexpr uint8_t MAX_ZOMBIES = 5;
constexpr uint8_t MAX_HIT_EFFECTS = 4;
constexpr uint8_t MAX_CIVILIANS = 3;
constexpr uint8_t KILLS_PER_WAVE = 10;
constexpr uint8_t EASY_STARTING_LIVES = 10;
constexpr uint8_t VERY_EASY_STARTING_LIVES = 15;
constexpr uint8_t HARD_STARTING_LIVES = 5;
constexpr uint8_t MAGAZINE_SIZE = 10;
constexpr uint8_t LEVEL_COUNT = 5;
constexpr uint8_t VOLUME_STEP = 10;
constexpr uint16_t FRAME_INTERVAL_MS = 33;
constexpr uint16_t GAME_OVER_FRAME_INTERVAL_MS = 100;
constexpr uint16_t MENU_FRAME_INTERVAL_MS = 100;
constexpr uint16_t MUSIC_NOTE_GAP_MS = 24;
constexpr uint16_t LEVEL_TRANSITION_MS = 700;
constexpr uint16_t TOUCH_SAMPLES = 8;
constexpr uint16_t ZOMBIE_EMERGE_MS = 1800;
constexpr uint16_t ZOMBIE_REACH_PLAYER_MS = 8500;
constexpr uint16_t BOSS_REACH_PLAYER_MS = 18000;
constexpr uint16_t BOSS_WEAKPOINT_CYCLE_MS = 2600;
constexpr uint16_t BOSS_WEAKPOINT_OPEN_MS = 1300;
constexpr uint16_t EASY_BOSS_WEAKPOINT_OPEN_MS = 1950;
constexpr uint8_t BOSS_HEAD_HITS = 3;
constexpr uint8_t BOSS_TORSO_HITS = 5;
constexpr uint16_t RUNNER_REACH_PLAYER_MS = 6200;
constexpr uint16_t BRUTE_REACH_PLAYER_MS = 12500;
constexpr uint16_t HIT_EFFECT_DURATION_MS = 420;
constexpr uint16_t ENEMY_SHOT_TRAVEL_MS = 460;
constexpr uint16_t ENEMY_SHOT_WARNING_MS = 360;
constexpr uint8_t BUZZER_PIN = 26;
constexpr uint8_t BUZZER_CHANNEL = 7;
constexpr int16_t POLICE_LANE_X[3] = {65, 160, 255};
constexpr uint8_t MAX_SCENERY_DAMAGE = 8;
constexpr int16_t LEVEL_HIT_THRESHOLD = 20;

struct TouchCalibration {
  uint16_t xLeft;
  uint16_t xRight;
  uint16_t yTop;
  uint16_t yBottom;
};

enum class ZombieKind : uint8_t {
  Police,
  Suit,
  Dress,
  Brute,
  Runner,
  Peeker,
  Boss
};
enum class ZombieAnimation : uint8_t { Shamble, Lurch, Reach };
enum class Difficulty : uint8_t { Easy = 0, Hard = 1, Medium = 2 };
enum class WaveObjective : uint8_t { Headshots, Combo, Eliminations };

struct Zombie {
  float x;
  float y;
  int16_t entryX;
  int16_t entryY;
  uint32_t spawnedAt;
  uint32_t nextShotAt;
  uint8_t lane;
  uint8_t torsoHits;
  uint8_t headHits;
  ZombieAnimation animation;
  ZombieKind kind;
  bool active;
  bool hasHat;
};

struct HitEffect {
  int16_t x;
  int16_t y;
  uint32_t startedAt;
  bool headshot;
  bool hatHit;
  bool active;
};

struct SceneryDamage {
  int16_t x;
  int16_t y;
  bool active;
};

struct EnemyShot {
  int16_t x;
  int16_t y;
  uint32_t startedAt;
  bool active;
};

enum class CivilianType : uint8_t { Dog, Cat, Man, Woman, Child };

struct Civilian {
  float x;
  float y;
  uint32_t spawnedAt;
  uint32_t lastMovedAt;
  uint8_t lane;
  int8_t direction;
  CivilianType type;
  bool active;
};

enum class PropType : uint8_t { Window, Monitor, Bottle, TrashBin, Chandelier };

struct SceneProp {
  uint8_t scene;
  uint8_t id;
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
  PropType type;
};

struct SceneDoor {
  uint8_t scene;
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

struct MusicNote {
  uint16_t frequency;
  uint16_t durationMs;
};

enum class Screen : uint8_t { Menu, Playing, GameOver, Victory };
enum class MusicTrack : uint8_t { None, Menu, GameOver, Victory };

TouchCalibration calibration{};
Zombie zombies[MAX_ZOMBIES]{};
HitEffect hitEffects[MAX_HIT_EFFECTS]{};
SceneryDamage sceneryDamage[MAX_SCENERY_DAMAGE]{};
EnemyShot enemyShots[MAX_ZOMBIES]{};
Civilian civilians[MAX_CIVILIANS]{};
Screen screen = Screen::Menu;
uint32_t score = 0;
uint32_t highScore = 0;
uint32_t lastFrameAt = 0;
uint32_t nextSpawnAt = 0;
uint32_t speakerStopAt = 0;
uint32_t playerHitUntil = 0;
uint32_t nextCivilianAt = 0;
uint32_t levelTransitionStartedAt = 0;
uint32_t musicNextNoteAt = 0;
uint32_t reloadFollowupAt = 0;
uint32_t totalKills = 0;
uint32_t shotsFired = 0;
uint32_t zombieShotsHit = 0;
uint32_t headshotCount = 0;
uint32_t currentCombo = 0;
uint32_t bestCombo = 0;
uint32_t waveCombo = 0;
uint32_t sceneKills = 0;
uint8_t lastSceneReached = 1;
uint8_t objectiveProgress = 0;
uint32_t menuCatLastMovedAt = 0;
uint32_t menuCatStateUntil = 0;
uint32_t menuCarLastMovedAt = 0;
uint32_t menuCarNextPassAt = 0;
float menuCatX = 26.0f;
float menuCatTargetX = 160.0f;
float menuCarX = -150.0f;
uint8_t lives = EASY_STARTING_LIVES;
uint8_t ammo = MAGAZINE_SIZE;
uint8_t wave = 1;
uint8_t waveKills = 0;
uint8_t nextHitEffect = 0;
uint8_t nextSceneryDamage = 0;
uint8_t volume = 70;
Difficulty difficulty = Difficulty::Easy;
uint8_t sceneryDamageLevel = 0;
uint16_t destroyedSceneProps = 0;
uint8_t destroyedPropsLevel = UINT8_MAX;
uint8_t displayedLevel = 0;
uint8_t transitionLevel = 0;
uint8_t musicNoteIndex = 0;
int8_t menuCatDirection = 1;
bool menuCatWalking = true;
bool menuCarPassing = false;
bool objectiveCompleted = false;
bool bossEncounterSpawned = false;
bool touchWasDown = false;
bool staticScreenDirty = true;
bool speakerReady = false;
bool levelTransitionActive = false;
MusicTrack musicTrack = MusicTrack::None;

const uint16_t backgroundColor = TFT_BLACK;
const uint16_t hudColor = TFT_DARKGREY;
const uint16_t zombieGreen = 0x4D8A;
const uint16_t zombieDark = 0x2B45;
const uint16_t woundRed = TFT_RED;
const uint16_t wallColor = 0x31A6;
const uint16_t wallShadow = 0x2124;
const uint16_t deskColor = 0x52AA;
const uint16_t deskEdge = 0x9CF3;
const uint16_t uniformBlue = 0x2118;
const uint16_t uniformLight = 0x4A7F;
const uint16_t badgeGold = 0xFEA0;
constexpr SceneProp SCENE_PROPS[] = {
    {0, 0, 24, 48, 37, 42, PropType::Window},
    {0, 1, 153, 47, 35, 24, PropType::Window},
    {0, 2, 282, 46, 26, 42, PropType::Window},
    {0, 3, 30, 160, 27, 36, PropType::TrashBin},
    {1, 0, 31, 43, 28, 27, PropType::Monitor},
    {1, 1, 90, 45, 28, 27, PropType::Monitor},
    {1, 2, 196, 45, 28, 27, PropType::Monitor},
    {1, 3, 264, 49, 29, 37, PropType::Window},
    {2, 0, 26, 51, 37, 47, PropType::Window},
    {2, 1, 160, 42, 42, 33, PropType::Chandelier},
    {2, 2, 257, 51, 37, 47, PropType::Window},
    {3, 0, 38, 48, 37, 41, PropType::Window},
    {3, 1, 273, 164, 29, 43, PropType::TrashBin},
    {4, 0, 177, 49, 14, 31, PropType::Bottle},
    {4, 1, 205, 49, 14, 31, PropType::Bottle},
    {4, 2, 233, 49, 14, 31, PropType::Bottle},
    {4, 3, 278, 43, 31, 39, PropType::Window},
};

constexpr SceneDoor SCENE_DOORS[] = {
    {0, 80, 63, 39, 51}, {0, 200, 55, 34, 59},
    {1, 75, 37, 28, 76}, {2, 143, 63, 34, 60},
    {3, 140, 103, 40, 46}, {4, 143, 47, 25, 62},
};

struct MenuWindowLight {
  int16_t x;
  int16_t y;
  uint32_t nextChangeAt;
  bool lit;
  bool permanent;
  uint16_t color;
};

MenuWindowLight menuWindowLights[] = {
    {6, 74, 0, true, true, 0xFFE0},   {18, 87, 0, true, true, 0xFEA0},
    {37, 49, 0, true, true, 0xFDC0},  {61, 75, 0, true, true, 0xFFE0},
    {262, 71, 0, true, true, 0xFEA0}, {286, 84, 0, true, true, 0xFDC0},
    {300, 54, 0, true, true, 0xFFE0}, {312, 93, 0, true, true, 0xFEA0},
    {30, 74, 0, false, false, 0xFFE0},  {49, 62, 0, false, false, 0xFEA0},
    {78, 67, 0, false, false, 0xFDC0},  {122, 40, 0, false, false, 0xFFE0},
    {170, 63, 0, false, false, 0xFEA0}, {215, 58, 0, false, false, 0xFDC0},
    {262, 97, 0, false, false, 0xFFE0}, {300, 67, 0, false, false, 0xFEA0},
};

constexpr MusicNote MENU_MUSIC[] = {
    {523, 120}, {659, 120}, {784, 180}, {659, 120}, {880, 180},
    {784, 120}, {659, 120}, {587, 180}, {523, 120}, {659, 120},
    {784, 180}, {988, 120}, {880, 120}, {784, 180}, {659, 180},
    {0, 100},   {523, 120}, {659, 120}, {784, 180}, {659, 120},
    {880, 180}, {784, 120}, {659, 120}, {587, 180}, {523, 120},
    {659, 120}, {784, 180}, {988, 120}, {880, 120}, {784, 180},
    {659, 180}, {0, 160},   {659, 120}, {784, 120}, {988, 180},
    {784, 120}, {1047, 180}, {988, 120}, {784, 120}, {698, 180},
    {659, 120}, {784, 120}, {988, 180}, {1175, 120}, {1047, 120},
    {988, 180}, {784, 180}, {0, 100},   {659, 120}, {784, 120},
    {988, 180}, {784, 120}, {1047, 180}, {988, 120}, {784, 120},
    {698, 180}, {659, 120}, {784, 120}, {988, 180}, {1175, 120},
    {1047, 120}, {988, 180}, {784, 180}, {0, 160}};
constexpr MusicNote GAME_OVER_MUSIC[] = {
    {392, 250}, {349, 250}, {311, 380}, {0, 120}, {311, 180},
    {349, 180}, {392, 380}, {0, 160}, {349, 250}, {311, 250},
    {294, 420}, {0, 180}, {262, 250}, {294, 250}, {311, 500},
    {0, 220},   {392, 250}, {349, 250}, {311, 380}, {0, 120},
    {311, 180}, {349, 180}, {392, 380}, {0, 160}, {349, 250},
    {311, 250}, {294, 420}, {0, 180}, {262, 250}, {294, 250},
    {311, 500}, {0, 220}};
constexpr MusicNote VICTORY_MUSIC[] = {
    {784, 110}, {988, 110}, {1175, 180}, {1568, 220}, {0, 70},
    {1175, 110}, {1568, 110}, {1760, 250}, {1568, 130}, {1175, 130},
    {1568, 300}, {0, 100}, {988, 110}, {1175, 110}, {1568, 180},
    {1976, 260}, {1760, 130}, {1568, 130}, {1175, 320}, {0, 180}};

void playTone(uint16_t frequency, uint16_t durationMs);
void startMusic(MusicTrack track, uint32_t now);
void endVictory();
void addSceneryDamage(int16_t x, int16_t y);

void drawSceneProp(const SceneProp &prop, bool destroyed) {
  if (destroyed) {
    frameBuffer.drawRect(prop.x, prop.y, prop.width, prop.height, wallShadow);
    frameBuffer.drawLine(prop.x + 3, prop.y + 3,
                         prop.x + prop.width - 4, prop.y + prop.height - 4,
                         deskEdge);
    frameBuffer.drawLine(prop.x + prop.width - 4, prop.y + 3,
                         prop.x + 3, prop.y + prop.height - 4, deskEdge);
    frameBuffer.fillCircle(prop.x + prop.width / 2,
                           prop.y + prop.height / 2, 3, TFT_YELLOW);
    if (prop.type == PropType::Bottle) {
      frameBuffer.fillTriangle(prop.x - 3, prop.y + prop.height,
                               prop.x + 7, prop.y + prop.height - 3,
                               prop.x + prop.width + 4,
                               prop.y + prop.height + 1, 0x07E0);
    } else if (prop.type == PropType::TrashBin) {
      frameBuffer.fillRect(prop.x + 2, prop.y + prop.height - 5,
                           prop.width + 5, 4, 0xBDF7);
      frameBuffer.fillCircle(prop.x + prop.width + 3,
                             prop.y + prop.height - 2, 2, 0xD69A);
    } else if (prop.type == PropType::Window) {
      frameBuffer.fillTriangle(prop.x + 2, prop.y + 3,
                               prop.x + prop.width - 4, prop.y + 9,
                               prop.x + prop.width / 2, prop.y + prop.height - 3,
                               0xBDF7);
    }
    return;
  }

  switch (prop.type) {
    case PropType::Window:
      frameBuffer.fillRect(prop.x, prop.y, prop.width, prop.height, 0x10E3);
      frameBuffer.fillRect(prop.x + 3, prop.y + 3,
                           prop.width / 2 - 4, prop.height / 2 - 4, 0x5E9F);
      frameBuffer.fillRect(prop.x + prop.width / 2 + 1, prop.y + 3,
                           prop.width / 2 - 4, prop.height / 2 - 4, 0x7DDF);
      frameBuffer.fillRect(prop.x + 3, prop.y + prop.height / 2 + 1,
                           prop.width - 6, prop.height / 2 - 4, 0x3186);
      frameBuffer.drawRect(prop.x, prop.y, prop.width, prop.height, deskEdge);
      frameBuffer.drawFastVLine(prop.x + prop.width / 2, prop.y,
                                prop.height, deskEdge);
      frameBuffer.drawFastHLine(prop.x, prop.y + prop.height / 2,
                                prop.width, deskEdge);
      frameBuffer.drawLine(prop.x + 4, prop.y + prop.height - 5,
                           prop.x + prop.width - 5, prop.y + 5, 0xBDF7);
      break;
    case PropType::Monitor:
      frameBuffer.fillRoundRect(prop.x, prop.y, prop.width, prop.height,
                                3, wallShadow);
      frameBuffer.fillRect(prop.x + 3, prop.y + 3,
                           prop.width - 6, prop.height - 8, 0x07E0);
      frameBuffer.drawFastHLine(prop.x + 5, prop.y + 6,
                                prop.width - 10, 0xBFEF);
      frameBuffer.drawFastHLine(prop.x + 5, prop.y + 11,
                                prop.width - 13, 0x5F6D);
      frameBuffer.fillRect(prop.x + prop.width / 2 - 2,
                           prop.y + prop.height - 5, 4, 4, deskEdge);
      break;
    case PropType::Bottle:
      frameBuffer.fillRoundRect(prop.x + 3, prop.y + 9,
                                prop.width - 6, prop.height - 10,
                                4, 0x07E0);
      frameBuffer.fillRect(prop.x + prop.width / 2 - 2, prop.y + 3,
                           4, 8, 0x07E0);
      frameBuffer.fillRect(prop.x + prop.width / 2 - 3, prop.y + 2,
                           6, 3, 0xD69A);
      frameBuffer.drawFastVLine(prop.x + 5, prop.y + 12,
                                prop.height - 17, 0x87F0);
      break;
    case PropType::TrashBin:
      frameBuffer.fillRoundRect(prop.x + 3, prop.y + 6,
                                prop.width - 6, prop.height - 6,
                                4, 0x5AEB);
      frameBuffer.drawRect(prop.x + 3, prop.y + 6,
                           prop.width - 6, prop.height - 6, deskEdge);
      frameBuffer.fillRect(prop.x, prop.y + 3,
                           prop.width, 5, wallShadow);
      frameBuffer.drawFastHLine(prop.x + 8, prop.y + 15,
                                prop.width - 16, 0xBDF7);
      frameBuffer.drawFastHLine(prop.x + 8, prop.y + 21,
                                prop.width - 16, 0xBDF7);
      break;
    case PropType::Chandelier:
      frameBuffer.drawFastVLine(prop.x + prop.width / 2, prop.y - 16,
                                17, deskEdge);
      frameBuffer.fillEllipse(prop.x + prop.width / 2,
                              prop.y + 11, prop.width / 2, 7, badgeGold);
      for (int8_t light = -1; light <= 1; ++light) {
        const int16_t x = prop.x + prop.width / 2 + light * 13;
        frameBuffer.drawLine(prop.x + prop.width / 2, prop.y + 10,
                             x, prop.y + 20, deskEdge);
        frameBuffer.fillCircle(x, prop.y + 23, 4, TFT_YELLOW);
      }
      break;
  }
}

void drawSceneProps(uint8_t level) {
  for (const SceneProp &prop : SCENE_PROPS) {
    if (prop.scene != level) {
      continue;
    }
    const bool destroyed = (destroyedSceneProps & (1U << prop.id)) != 0;
    drawSceneProp(prop, destroyed);
  }
}

void drawSceneDoors(uint8_t level) {
  for (const SceneDoor &door : SCENE_DOORS) {
    if (door.scene != level) {
      continue;
    }
    frameBuffer.fillRect(door.x - 3, door.y - 3, door.width + 6,
                         door.height + 3, wallShadow);
    frameBuffer.fillRect(door.x, door.y, door.width, door.height, 0x1082);
    frameBuffer.drawRect(door.x, door.y, door.width, door.height, 0xD69A);
    frameBuffer.drawFastVLine(door.x + door.width / 2, door.y + 2,
                              door.height - 3, 0x5AEB);
    frameBuffer.fillCircle(door.x + door.width / 2 - 3,
                           door.y + door.height / 2, 2, badgeGold);
  }
}

bool damageSceneProp(uint8_t level, int16_t x, int16_t y) {
  for (const SceneProp &prop : SCENE_PROPS) {
    if (prop.scene != level || x < prop.x - 3 || x > prop.x + prop.width + 3 ||
        y < prop.y - 3 || y > prop.y + prop.height + 3) {
      continue;
    }
    const uint16_t mask = static_cast<uint16_t>(1U << prop.id);
    if ((destroyedSceneProps & mask) == 0) {
      destroyedSceneProps |= mask;
      addSceneryDamage(x, y);
      playTone(320, 75);
    } else {
      addSceneryDamage(x, y);
      playTone(760, 25);
    }
    return true;
  }
  return false;
}

void playTone(uint16_t frequency, uint16_t durationMs) {
  reloadFollowupAt = 0;
  if (!speakerReady || volume == 0) {
    return;
  }
  if (ledcWriteTone(BUZZER_CHANNEL, frequency) == 0) {
    Serial.println("ERROR: Could not start buzzer tone.");
    return;
  }
  ledcWrite(BUZZER_CHANNEL, 512U * volume / 100U);
  speakerStopAt = millis() + durationMs;
}

void applySpeakerVolume() {
  if (!speakerReady || speakerStopAt == 0) {
    return;
  }
  if (volume == 0) {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    speakerStopAt = 0;
    return;
  }
  ledcWrite(BUZZER_CHANNEL, 512U * volume / 100U);
}

void playReloadSound() {
  playTone(560, 40);
  reloadFollowupAt = millis() + 62;
}

void startMusic(MusicTrack track, uint32_t now) {
  musicTrack = track;
  musicNoteIndex = 0;
  musicNextNoteAt = now;
  reloadFollowupAt = 0;
  if (track == MusicTrack::None && speakerReady) {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    speakerStopAt = 0;
  }
}

void updateMusic(uint32_t now) {
  if (reloadFollowupAt != 0) {
    if (speakerStopAt == 0 &&
        static_cast<int32_t>(now - reloadFollowupAt) >= 0) {
      playTone(880, 48);
    }
    return;
  }
  if (musicTrack == MusicTrack::None) {
    return;
  }
  if (speakerStopAt != 0 ||
      static_cast<int32_t>(now - musicNextNoteAt) < 0) {
    return;
  }

  const MusicNote *notes = nullptr;
  uint8_t noteCount = 0;
  switch (musicTrack) {
    case MusicTrack::Menu:
      notes = MENU_MUSIC;
      noteCount = sizeof(MENU_MUSIC) / sizeof(MENU_MUSIC[0]);
      break;
    case MusicTrack::GameOver:
      notes = GAME_OVER_MUSIC;
      noteCount = sizeof(GAME_OVER_MUSIC) / sizeof(GAME_OVER_MUSIC[0]);
      break;
    case MusicTrack::Victory:
      notes = VICTORY_MUSIC;
      noteCount = sizeof(VICTORY_MUSIC) / sizeof(VICTORY_MUSIC[0]);
      break;
    case MusicTrack::None:
      return;
  }

  if (musicNoteIndex >= noteCount) {
    musicTrack = MusicTrack::None;
    return;
  }
  const MusicNote note = notes[musicNoteIndex++];
  musicNextNoteAt = now + note.durationMs + MUSIC_NOTE_GAP_MS;
  if (note.frequency != 0) {
    playTone(note.frequency, note.durationMs);
  }
}

void updateSpeaker(uint32_t now) {
  if (speakerReady && speakerStopAt != 0 &&
      static_cast<int32_t>(now - speakerStopAt) >= 0) {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    speakerStopAt = 0;
  }
}

void addHitEffect(int16_t x, int16_t y, bool headshot, uint32_t now,
                  bool hatHit = false) {
  hitEffects[nextHitEffect] = {x, y, now, headshot, hatHit, true};
  nextHitEffect = (nextHitEffect + 1) % MAX_HIT_EFFECTS;
}

void drawHitEffects(uint32_t now) {
  constexpr int8_t rayX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  constexpr int8_t rayY[] = {-1, -1, -1, 0, 0, 1, 1, 1};

  for (const HitEffect &effect : hitEffects) {
    if (!effect.active) {
      continue;
    }
    const uint32_t age = now - effect.startedAt;
    if (age >= HIT_EFFECT_DURATION_MS) {
      continue;
    }

    const int16_t radius = 3 + age / 28;
    const uint16_t color = effect.hatHit
                               ? TFT_CYAN
                               : effect.headshot
                                     ? TFT_YELLOW
                                     : frameBuffer.color565(255, 105, 35);
    frameBuffer.drawCircle(effect.x, effect.y, radius, color);
    frameBuffer.drawCircle(effect.x, effect.y, max<int16_t>(1, radius / 2), TFT_RED);
    for (uint8_t ray = 0; ray < 8; ++ray) {
      const int16_t startRadius = radius + 2;
      const int16_t endRadius = radius + 5 + (ray % 3) * 2;
      frameBuffer.drawLine(effect.x + rayX[ray] * startRadius,
                           effect.y + rayY[ray] * startRadius,
                           effect.x + rayX[ray] * endRadius,
                           effect.y + rayY[ray] * endRadius, color);
    }
    if (effect.headshot) {
      frameBuffer.setTextColor(TFT_YELLOW);
      frameBuffer.setTextSize(1);
      frameBuffer.setCursor(effect.x - 17, effect.y - radius - 11);
      frameBuffer.print("HEADSHOT");
    } else if (effect.hatHit) {
      frameBuffer.setTextColor(TFT_CYAN);
      frameBuffer.setTextSize(1);
      frameBuffer.setCursor(effect.x - 13, effect.y - radius - 11);
      frameBuffer.print("HAT OFF");
    }
  }
}

void addSceneryDamage(int16_t x, int16_t y) {
  if (y < 27) {
    return;
  }
  sceneryDamage[nextSceneryDamage] = {x, y, true};
  nextSceneryDamage = (nextSceneryDamage + 1) % MAX_SCENERY_DAMAGE;
}

void drawSceneryDamage() {
  for (const SceneryDamage &damage : sceneryDamage) {
    if (!damage.active) {
      continue;
    }
    frameBuffer.fillCircle(damage.x, damage.y, 3, wallShadow);
    frameBuffer.drawLine(damage.x - 2, damage.y - 2, damage.x - 10,
                         damage.y - 8, deskEdge);
    frameBuffer.drawLine(damage.x + 2, damage.y - 2, damage.x + 9,
                         damage.y - 9, deskEdge);
    frameBuffer.drawLine(damage.x + 2, damage.y + 2, damage.x + 10,
                         damage.y + 8, deskEdge);
    frameBuffer.drawLine(damage.x - 2, damage.y + 2, damage.x - 8,
                         damage.y + 10, deskEdge);
  }
}

void drawDesk(int16_t x, int16_t y, int16_t width, int16_t height) {
  const int16_t halfWidth = width / 2;
  frameBuffer.fillEllipse(x, y + height + 10, halfWidth + 4, 7, wallShadow);
  frameBuffer.fillRect(x - halfWidth, y, width, 8, deskEdge);
  frameBuffer.drawFastHLine(x - halfWidth, y, width, TFT_WHITE);
  frameBuffer.fillRect(x - halfWidth + 3, y + 8, width - 6, height, deskColor);
  frameBuffer.drawFastHLine(x - halfWidth + 3, y + 8, width - 6, wallShadow);
  frameBuffer.fillRect(x - halfWidth + 8, y + height + 8, 5, 14, wallShadow);
  frameBuffer.fillRect(x + halfWidth - 13, y + height + 8, 5, 14, wallShadow);
  frameBuffer.fillRect(x - 13, y - 20, 26, 18, wallShadow);
  frameBuffer.drawRect(x - 11, y - 18, 22, 13, 0x5E9F);
  frameBuffer.fillRect(x - 7, y - 3, 14, 3, deskEdge);
  frameBuffer.fillRect(x + halfWidth - 21, y + 10, 13, 7, 0xD69A);
}

void drawBrickPattern(int16_t left, int16_t top, int16_t right,
                      int16_t bottom, int16_t brickWidth,
                      int16_t brickHeight, uint16_t mortarColor) {
  for (int16_t y = top; y < bottom; y += brickHeight) {
    frameBuffer.drawFastHLine(left, y, right - left, mortarColor);
    const int16_t offset = ((y - top) / brickHeight) % 2 == 0
                               ? 0
                               : brickWidth / 2;
    for (int16_t x = left + offset; x < right; x += brickWidth) {
      frameBuffer.drawFastVLine(x, y, brickHeight, mortarColor);
    }
  }
}

void drawPerspectiveFloor(int16_t top, int16_t bottom, int16_t centerX,
                          uint16_t baseColor, uint16_t groutColor,
                          uint8_t lineSpacing) {
  frameBuffer.fillRect(0, top, frameBuffer.width(), bottom - top, baseColor);
  for (int16_t x = -80; x <= frameBuffer.width() + 80; x += 40) {
    frameBuffer.drawLine(centerX, top, x, bottom, groutColor);
  }
  for (int16_t y = top + lineSpacing; y < bottom; y += lineSpacing) {
    const int16_t offset = y - top;
    const int16_t left = max<int16_t>(0, centerX - offset * 2);
    const int16_t right = min<int16_t>(frameBuffer.width(),
                                       centerX + offset * 2);
    frameBuffer.drawFastHLine(left, y, right - left, groutColor);
  }
}

void drawWoodFloor(int16_t top, uint16_t baseColor, uint16_t seamColor,
                   uint8_t plankSpacing) {
  const int16_t bottom = frameBuffer.height();
  frameBuffer.fillRect(0, top, frameBuffer.width(), bottom - top, baseColor);
  for (int16_t x = -80; x <= frameBuffer.width() + 80; x += 48) {
    frameBuffer.drawLine(frameBuffer.width() / 2, top, x, bottom, seamColor);
  }
  for (int16_t y = top + plankSpacing; y < bottom; y += plankSpacing) {
    frameBuffer.drawFastHLine(0, y, frameBuffer.width(), seamColor);
  }
}

const char *levelName(uint8_t level) {
  constexpr const char *names[LEVEL_COUNT] = {
      "ALLEY", "POLICE STATION", "BALLROOM", "BACKYARD", "TAVERN"};
  return names[level % LEVEL_COUNT];
}

uint8_t currentLevel() {
  return static_cast<uint8_t>((totalKills / LEVEL_HIT_THRESHOLD) % LEVEL_COUNT);
}

uint8_t startingLives() {
  switch (difficulty) {
    case Difficulty::Easy:
      return VERY_EASY_STARTING_LIVES;
    case Difficulty::Hard:
      return HARD_STARTING_LIVES;
    case Difficulty::Medium:
      return EASY_STARTING_LIVES;
  }
  return VERY_EASY_STARTING_LIVES;
}

const char *difficultyName() {
  switch (difficulty) {
    case Difficulty::Easy:
      return "EASY";
    case Difficulty::Medium:
      return "MEDIUM";
    case Difficulty::Hard:
      return "HARD";
  }
  return "EASY";
}

bool bossWeakpointIsOpen(uint32_t age) {
  const uint16_t openDuration =
      difficulty == Difficulty::Easy ? EASY_BOSS_WEAKPOINT_OPEN_MS
                                     : BOSS_WEAKPOINT_OPEN_MS;
  return (age % BOSS_WEAKPOINT_CYCLE_MS) < openDuration;
}

uint8_t zombieTorsoHealth(ZombieKind kind) {
  switch (kind) {
    case ZombieKind::Brute:
      return 5;
    case ZombieKind::Boss:
      return BOSS_TORSO_HITS;
    default:
      return 3;
  }
}

uint16_t zombieReachTime(ZombieKind kind) {
  uint16_t reachTime;
  switch (kind) {
    case ZombieKind::Runner:
      reachTime = RUNNER_REACH_PLAYER_MS;
      break;
    case ZombieKind::Brute:
      reachTime = BRUTE_REACH_PLAYER_MS;
      break;
    case ZombieKind::Boss:
      reachTime = BOSS_REACH_PLAYER_MS;
      break;
    default:
      reachTime = ZOMBIE_REACH_PLAYER_MS;
      break;
  }
  return difficulty == Difficulty::Easy ? reachTime * 3 / 2 : reachTime;
}

WaveObjective currentObjective() {
  return static_cast<WaveObjective>((wave - 1) % 3);
}

uint8_t objectiveTarget() {
  switch (currentObjective()) {
    case WaveObjective::Headshots:
      return 3;
    case WaveObjective::Combo:
      return 4;
    case WaveObjective::Eliminations:
      return 8;
  }
  return 0;
}

const char *objectiveLabel() {
  switch (currentObjective()) {
    case WaveObjective::Headshots:
      return "HEADSHOTS";
    case WaveObjective::Combo:
      return "COMBO";
    case WaveObjective::Eliminations:
      return "ELIMINATIONS";
  }
  return "";
}

void awardPoints(uint32_t points) {
  score += points;
  if (score > highScore) {
    highScore = score;
    preferences.putULong("highScore", highScore);
  }
}

void updateWaveObjective(bool headshot) {
  if (objectiveCompleted) {
    return;
  }
  if (currentObjective() == WaveObjective::Headshots) {
    if (headshot) {
      ++objectiveProgress;
    }
  } else if (currentObjective() == WaveObjective::Combo) {
    objectiveProgress = static_cast<uint8_t>(
        min<uint32_t>(waveCombo, objectiveTarget()));
  } else {
    ++objectiveProgress;
  }
  if (objectiveProgress >= objectiveTarget()) {
    objectiveCompleted = true;
    awardPoints(100);
    playTone(1480, 180);
  }
}

void resetCombo() {
  currentCombo = 0;
  waveCombo = 0;
  if (!objectiveCompleted && currentObjective() == WaveObjective::Combo) {
    objectiveProgress = 0;
  }
}

void drawPoliceStation(uint32_t now) {
  const int16_t floorY = 113;
  frameBuffer.fillScreen(0x1082);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 94, 0x31A6);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 12, 0x2945);
  frameBuffer.fillRect(0, 37, frameBuffer.width(), 3, 0x5AEB);
  drawBrickPattern(0, 42, frameBuffer.width(), 111, 29, 16, 0x39C7);
  frameBuffer.fillRect(0, 109, frameBuffer.width(), 10, 0x2124);
  frameBuffer.fillRect(0, 111, frameBuffer.width(), 3, 0x7BEF);
  frameBuffer.fillRect(0, 115, frameBuffer.width(), 8, 0x4208);
  for (int16_t x = 18; x < frameBuffer.width(); x += 68) {
    frameBuffer.fillRect(x, 26, 42, 6, 0x5AEB);
    frameBuffer.drawFastHLine(x + 4, 28, 34, 0xBDF7);
    frameBuffer.fillRect(x + 6, 30, 4, 2, TFT_YELLOW);
  }
  frameBuffer.fillRect(73, 40, 3, 68, 0x2945);
  frameBuffer.fillRect(243, 40, 3, 68, 0x2945);
  frameBuffer.fillRect(70, 40, 2, 67, 0x5AEB);
  frameBuffer.fillRect(244, 40, 2, 67, 0x5AEB);
  frameBuffer.fillRect(16, 39, 56, 43, 0x10E3);
  frameBuffer.drawRect(16, 39, 56, 43, deskEdge);
  frameBuffer.drawFastVLine(44, 40, 41, deskEdge);
  frameBuffer.drawFastHLine(17, 60, 54, deskEdge);
  frameBuffer.fillRect(19, 43, 21, 15, 0x39E7);
  frameBuffer.fillRect(47, 43, 18, 15, 0x2128);
  frameBuffer.fillRect(21, 46, 6, 6, 0xBDF7);
  frameBuffer.fillRect(29, 46, 8, 6, 0x5E9F);
  frameBuffer.fillRect(49, 46, 14, 3, 0x07E0);
  frameBuffer.fillRect(49, 51, 10, 3, 0xF800);
  frameBuffer.fillRect(18, 64, 50, 14, 0x1082);
  for (int16_t x = 22; x < 66; x += 9) {
    frameBuffer.fillRect(x, 68 + (x % 3), 5, 7, 0x7BEF);
  }
  frameBuffer.fillRect(79, 39, 162, 43, 0x2128);
  frameBuffer.drawRect(79, 39, 162, 43, deskEdge);
  frameBuffer.fillRect(83, 43, 154, 34, 0x0841);
  frameBuffer.drawFastHLine(88, 47, 143, 0x39E7);
  frameBuffer.drawFastHLine(88, 73, 143, 0x39E7);
  for (int16_t x = 91; x < 232; x += 18) {
    frameBuffer.drawFastVLine(x, 49, 22, 0x2124);
    frameBuffer.fillRect(x + 4, 53, 7, 5, (x % 3 == 0) ? 0x07E0 : 0xFFE0);
    frameBuffer.fillRect(x + 4, 62, 11, 3, (x % 2 == 0) ? 0xF800 : 0x001F);
    frameBuffer.drawFastHLine(x + 3, 69, 12, 0x7BEF);
  }
  frameBuffer.setTextColor(TFT_WHITE);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(114, 48);
  frameBuffer.print("POLICE");
  frameBuffer.setCursor(101, 65);
  frameBuffer.print("CENTRAL DESK");

  frameBuffer.fillRect(251, 38, 53, 57, wallShadow);
  frameBuffer.fillRect(257, 45, 41, 49, 0x3186);
  frameBuffer.fillRect(261, 48, 33, 5, 0xBDF7);
  frameBuffer.fillRect(262, 55, 31, 34, 0x1082);
  frameBuffer.fillRect(266, 58, 11, 12, 0xFEA0);
  frameBuffer.fillRect(280, 58, 9, 12, 0xFEA0);
  frameBuffer.fillRect(266, 74, 23, 3, 0x7BEF);
  frameBuffer.fillRect(266, 80, 18, 3, 0x7BEF);
  frameBuffer.drawRect(257, 45, 41, 49, deskEdge);
  frameBuffer.fillRect(278, 89, 3, 4, badgeGold);
  frameBuffer.fillRect(10, 87, 55, 15, wallShadow);
  frameBuffer.fillRect(14, 90, 47, 4, 0xA514);
  frameBuffer.fillRect(14, 96, 34, 3, 0xD69A);
  frameBuffer.fillRect(6, 107, 51, 5, 0x39E7);
  frameBuffer.fillRect(6, 112, 51, 2, 0x7BEF);

  drawPerspectiveFloor(floorY, frameBuffer.height(), frameBuffer.width() / 2,
                      0x5AEB, 0x39C7, 17);
  frameBuffer.fillTriangle(0, floorY, frameBuffer.width() / 2, floorY + 14,
                           frameBuffer.width(), floorY, 0x4208);
  for (int16_t x = 10; x < frameBuffer.width(); x += 61) {
    frameBuffer.fillRect(x, 125, 28, 3, 0xBDF7);
    frameBuffer.drawFastHLine(x + 4, 129, 20, 0x39C7);
  }
  frameBuffer.fillRect(4, 194, 29, 7, 0x1082);
  frameBuffer.drawRect(4, 194, 29, 7, 0x7BEF);
  for (int16_t x = 8; x < 31; x += 6) {
    frameBuffer.drawFastVLine(x, 196, 3, 0x5AEB);
  }

  drawDesk(POLICE_LANE_X[0], 125, 61, 27);
  drawDesk(POLICE_LANE_X[1], 116, 75, 34);
  drawDesk(POLICE_LANE_X[2], 125, 61, 27);

  const bool redBeacon = (now / 260) % 2 == 0;
  frameBuffer.fillRect(129, 28, 28, 4, redBeacon ? TFT_RED : TFT_BLUE);
  frameBuffer.fillRect(163, 28, 28, 4, redBeacon ? TFT_BLUE : TFT_RED);
}

void drawAlley() {
  frameBuffer.fillScreen(0x0843);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 92, 0x0210);
  for (int16_t x = 15; x < frameBuffer.width(); x += 43) {
    frameBuffer.fillCircle(x, 43 + (x * 3 % 29), 1, 0x7BEF);
  }
  frameBuffer.fillCircle(187, 46, 10, 0xD69A);
  frameBuffer.fillCircle(191, 42, 9, 0x0210);
  frameBuffer.fillRect(0, 25, 71, 118, 0x39A6);
  frameBuffer.fillRect(249, 25, 71, 118, 0x2945);
  frameBuffer.fillRect(0, 25, 71, 10, 0x2124);
  frameBuffer.fillRect(249, 25, 71, 10, 0x2124);
  frameBuffer.drawFastHLine(0, 36, 71, 0x7BEF);
  frameBuffer.drawFastHLine(249, 36, 71, 0x7BEF);
  drawBrickPattern(0, 39, 72, 137, 27, 14, 0x5AEB);
  drawBrickPattern(248, 39, 320, 137, 31, 15, 0x4A49);
  frameBuffer.fillRect(72, 39, 176, 78, 0x2945);
  drawBrickPattern(72, 39, 248, 117, 25, 13, 0x39C7);
  frameBuffer.drawFastHLine(72, 114, 176, 0x7BEF);
  for (int16_t x = 9; x < 70; x += 28) {
    frameBuffer.fillRect(x, 39, 3, 97, 0x2124);
    frameBuffer.drawFastVLine(x + 3, 39, 97, 0x7BEF);
  }
  frameBuffer.drawFastHLine(0, 119, 72, 0xBDF7);
  frameBuffer.drawFastHLine(248, 119, 72, 0x9CF3);
  frameBuffer.fillRect(7, 125, 5, 16, 0x5AEB);
  frameBuffer.fillRect(12, 125, 3, 16, 0x7BEF);
  frameBuffer.fillRect(302, 120, 4, 23, 0x7BEF);
  frameBuffer.fillRect(306, 120, 3, 23, 0x4208);
  drawPerspectiveFloor(116, frameBuffer.height(), frameBuffer.width() / 2,
                      0x2104, 0x3186, 20);
  frameBuffer.drawLine(35, 178, 49, 170, 0x4208);
  frameBuffer.drawLine(49, 170, 56, 174, 0x4208);
  frameBuffer.drawLine(232, 204, 247, 197, 0x4208);
  frameBuffer.drawLine(247, 197, 257, 201, 0x4208);
  frameBuffer.fillTriangle(0, 116, frameBuffer.width() / 2, 105,
                           frameBuffer.width(), 116, 0x3186);
  frameBuffer.fillEllipse(151, 191, 31, 8, 0x0841);
  frameBuffer.drawEllipse(151, 191, 31, 8, 0x7BEF);
  frameBuffer.drawFastVLine(151, 184, 14, 0x9CF3);
  frameBuffer.drawFastHLine(125, 191, 53, 0x5AEB);
  frameBuffer.fillRect(17, 167, 27, 28, 0x3186);
  frameBuffer.drawRect(17, 167, 27, 28, 0x7BEF);
  frameBuffer.fillRect(20, 160, 21, 8, 0x4A49);
  frameBuffer.drawFastHLine(15, 165, 31, 0xBDF7);
  frameBuffer.fillRect(267, 153, 38, 43, 0x4208);
  frameBuffer.fillRect(264, 148, 44, 7, 0x5AEB);
  frameBuffer.drawRect(267, 153, 38, 43, 0x7BEF);
  frameBuffer.fillRect(271, 159, 30, 5, 0x2945);
  frameBuffer.fillRect(275, 169, 4, 21, 0x7BEF);
  frameBuffer.fillRect(289, 169, 4, 21, 0x7BEF);
  frameBuffer.fillCircle(52, 204, 10, 0x2104);
  frameBuffer.fillCircle(66, 208, 7, 0x3186);
  frameBuffer.fillCircle(267, 211, 12, 0x2104);
  frameBuffer.fillCircle(281, 206, 8, 0x3186);

}

void drawBallroom() {
  frameBuffer.fillScreen(0x2128);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 95, 0x8410);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 11, 0xD69A);
  frameBuffer.fillRect(0, 36, frameBuffer.width(), 4, 0xA514);
  frameBuffer.fillRect(0, 40, frameBuffer.width(), 5, 0xFDC0);
  frameBuffer.fillRect(0, 45, frameBuffer.width(), 68, 0x7BEF);
  for (int16_t x = 0; x < frameBuffer.width(); x += 32) {
    frameBuffer.fillRect(x, 44, 2, 69, 0x6B4D);
    frameBuffer.fillRect(x + 3, 45, 1, 67, 0xBDF7);
    frameBuffer.fillRect(x + 7, 46, 1, 65, 0x9CF3);
  }
  for (int16_t x : {18, 82, 238, 302}) {
    frameBuffer.fillRect(x - 5, 47, 27, 73, 0xD69A);
    frameBuffer.fillRect(x - 1, 50, 19, 67, 0xFEA0);
    frameBuffer.fillRect(x + 2, 51, 4, 63, 0xFFE0);
    frameBuffer.fillTriangle(x - 8, 50, x + 8, 29, x + 25, 50, 0xFEA0);
    frameBuffer.fillTriangle(x - 4, 46, x + 8, 33, x + 20, 46, 0xFFE0);
    frameBuffer.fillRect(x - 8, 117, 33, 7, 0xD69A);
    frameBuffer.fillRect(x - 4, 119, 25, 3, 0xFEA0);
    frameBuffer.drawFastVLine(x - 2, 53, 59, 0xBDF7);
  }
  frameBuffer.fillRect(0, 111, frameBuffer.width(), 9, 0xA514);
  frameBuffer.drawFastHLine(0, 111, frameBuffer.width(), 0xFFE0);
  frameBuffer.drawFastHLine(0, 117, frameBuffer.width(), 0xFDC0);
  frameBuffer.fillTriangle(0, 124, frameBuffer.width() / 2, 88,
                           frameBuffer.width(), 124, 0x39E7);
  frameBuffer.fillRect(0, 123, frameBuffer.width(), 6, 0xD69A);
  drawWoodFloor(128, 0x8A43, 0x5A20, 15);
  frameBuffer.fillTriangle(0, 129, frameBuffer.width() / 2, 120,
                           frameBuffer.width(), 129, 0xFDC0);
  frameBuffer.fillTriangle(95, 130, frameBuffer.width() / 2, 125,
                           225, frameBuffer.height(), 0xA514);
  frameBuffer.drawLine(96, 130, 150, frameBuffer.height(), 0xD69A);
  frameBuffer.drawLine(224, 130, 170, frameBuffer.height(), 0xD69A);
  for (int16_t x = 22; x < 304; x += 39) {
    frameBuffer.fillRect(x, 143 + x % 9, 17, 2, 0xD69A);
    frameBuffer.fillRect(x + 4, 158 + x % 7, 10, 2, 0x9CF3);
    frameBuffer.fillRect(x - 3, 185 + x % 11, 22, 2, 0xD69A);
  }
  frameBuffer.fillRoundRect(118, 91, 84, 25, 3, 0x4208);
  frameBuffer.drawRoundRect(118, 91, 84, 25, 3, badgeGold);
  frameBuffer.fillRect(126, 96, 68, 14, 0x8000);
  frameBuffer.fillTriangle(132, 104, 142, 97, 152, 104, 0xFEA0);
  frameBuffer.fillRect(158, 98, 26, 3, 0xFFE0);
  frameBuffer.fillRect(158, 104, 21, 2, 0xFDC0);
  frameBuffer.fillRect(126, 111, 68, 3, 0xD69A);
  frameBuffer.drawLine(147, 25, 147, 50, deskEdge);
  frameBuffer.drawLine(173, 25, 173, 50, deskEdge);
  frameBuffer.fillCircle(160, 62, 13, 0xD69A);
  frameBuffer.fillCircle(160, 62, 9, 0xFEA0);
  frameBuffer.fillCircle(145, 55, 5, TFT_YELLOW);
  frameBuffer.fillCircle(175, 55, 5, TFT_YELLOW);
  frameBuffer.fillCircle(160, 45, 5, TFT_YELLOW);
  frameBuffer.fillTriangle(147, 64, 160, 85, 173, 64, 0xFEA0);
  for (int8_t ray = -2; ray <= 2; ++ray) {
    frameBuffer.drawLine(160, 72, 160 + ray * 13, 110,
                         (ray % 2 == 0) ? 0xFDC0 : 0xFFE0);
  }
}

void drawBackyard() {
  frameBuffer.fillScreen(0x0010);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 110, 0x0843);
  for (int16_t x = 12; x < frameBuffer.width(); x += 37) {
    frameBuffer.fillCircle(x, 42 + (x * 3 % 45), 1, 0xBDF7);
    frameBuffer.fillCircle(x + 8, 56 + (x * 5 % 39), 1, 0x7BEF);
  }
  frameBuffer.fillCircle(274, 57, 21, 0xFFDF);
  frameBuffer.fillCircle(281, 51, 20, 0x0843);
  frameBuffer.drawFastHLine(260, 58, 5, 0xFEA0);
  frameBuffer.fillRect(0, 90, frameBuffer.width(), 47, 0x4208);
  frameBuffer.fillRect(0, 88, frameBuffer.width(), 5, 0x5AEB);
  frameBuffer.fillTriangle(0, 75, 61, 40, 123, 75, 0x2124);
  frameBuffer.fillRect(11, 74, 100, 48, 0x4208);
  frameBuffer.fillTriangle(192, 81, 247, 48, 310, 81, 0x3186);
  frameBuffer.fillRect(203, 78, 99, 48, 0x2124);
  frameBuffer.fillRect(224, 93, 17, 25, 0x0841);
  frameBuffer.fillRect(246, 92, 20, 20, 0x10A3);
  frameBuffer.fillRect(249, 95, 14, 14, 0xFEA0);
  frameBuffer.fillRect(39, 88, 29, 25, 0x0010);
  frameBuffer.fillRect(43, 91, 21, 19, 0xFEA0);
  frameBuffer.drawRect(39, 88, 29, 25, 0x7BEF);
  frameBuffer.fillRect(48, 92, 3, 15, 0x4208);
  frameBuffer.fillRect(0, 125, frameBuffer.width(), 26, 0x5AEB);
  frameBuffer.fillRect(0, 132, frameBuffer.width(), 14, 0x39E7);
  for (int16_t x = 0; x < frameBuffer.width(); x += 24) {
    frameBuffer.fillRect(x, 99, 5, 51, 0x7BEF);
    frameBuffer.fillRect(x + 6, 101, 2, 49, 0xBDF7);
    frameBuffer.fillTriangle(x, 101, x + 8, 101, x + 4, 95, 0x9CF3);
  }
  frameBuffer.fillRect(0, 119, frameBuffer.width(), 5, 0xD69A);
  frameBuffer.fillRect(0, 142, frameBuffer.width(), 5, 0xD69A);
  frameBuffer.fillRect(0, 147, frameBuffer.width(),
                       frameBuffer.height() - 147, 0x1A64);
  frameBuffer.fillRect(0, 150, frameBuffer.width(), 3, 0x2E85);
  for (int16_t x = 0; x < frameBuffer.width(); x += 31) {
    frameBuffer.drawFastHLine(x, 166 + (x % 3) * 13, 14, 0x2E85);
    frameBuffer.fillTriangle(x, 148, x + 14, 148, x + 7, 158, 0x07E0);
    frameBuffer.fillTriangle(x + 2, 148, x + 6, 139, x + 10, 148, 0x2D68);
  }
  for (int16_t x = 8; x < frameBuffer.width(); x += 27) {
    frameBuffer.drawFastVLine(x, 174 + x % 27, 4 + x % 4, 0x2D68);
    frameBuffer.drawFastVLine(x + 3, 186 + x % 17, 3 + x % 3, 0x07E0);
  }
  frameBuffer.fillTriangle(103, 148, 159, 133, 217, 148, 0x7BEF);
  frameBuffer.fillRect(116, 146, 88, 5, 0x9CF3);
  for (int16_t x = 122; x < 200; x += 15) {
    frameBuffer.fillRect(x, 137 + x % 6, 4, 10, 0x4208);
  }
  frameBuffer.fillRect(21, 177, 42, 32, 0xA514);
  frameBuffer.drawRect(21, 177, 42, 32, 0xFEA0);
  frameBuffer.fillRect(25, 181, 12, 11, 0x1082);
  frameBuffer.fillRect(40, 181, 18, 4, 0xD69A);
  frameBuffer.fillRect(40, 188, 15, 3, 0x7BEF);
  frameBuffer.fillRect(263, 168, 35, 42, 0x8A43);
  frameBuffer.drawRect(263, 168, 35, 42, 0xD69A);
  frameBuffer.fillRect(268, 173, 25, 8, 0xD69A);
  frameBuffer.fillRect(268, 185, 25, 19, 0x5AEB);
  frameBuffer.fillRect(273, 189, 15, 11, 0x1082);
  frameBuffer.fillRect(0, 213, 320, 3, 0x2E85);
  for (int16_t x = 6; x < frameBuffer.width(); x += 19) {
    frameBuffer.drawFastVLine(x, 208 - x % 7, 7 + x % 7, 0x07E0);
  }
}

void drawTavern() {
  frameBuffer.fillScreen(0x1082);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 101, 0x39A6);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 9, 0x2124);
  frameBuffer.fillRect(0, 34, frameBuffer.width(), 4, 0xA514);
  drawBrickPattern(0, 39, frameBuffer.width(), 107, 31, 18, 0x5AEB);
  for (int16_t x = 13; x < frameBuffer.width(); x += 54) {
    frameBuffer.fillRect(x, 42, 4, 62, 0x2124);
    frameBuffer.drawFastVLine(x + 4, 42, 62, 0x7BEF);
  }
  frameBuffer.fillRect(19, 42, 124, 40, 0x4208);
  frameBuffer.fillRect(23, 46, 116, 32, 0x1082);
  frameBuffer.drawRect(19, 42, 124, 40, 0xFEA0);
  frameBuffer.drawRect(22, 45, 118, 34, 0xD69A);
  frameBuffer.fillRect(27, 50, 108, 3, 0xFEA0);
  frameBuffer.fillRect(27, 72, 108, 3, 0xFEA0);
  frameBuffer.fillRect(30, 56, 17, 11, 0x0010);
  frameBuffer.fillRect(51, 56, 17, 11, 0x0010);
  frameBuffer.fillRect(72, 56, 17, 11, 0x0010);
  frameBuffer.fillRect(93, 56, 17, 11, 0x0010);
  frameBuffer.fillRect(114, 56, 17, 11, 0x0010);
  frameBuffer.fillRect(31, 57, 14, 8, 0xFEA0);
  frameBuffer.fillRect(52, 57, 14, 8, 0xF800);
  frameBuffer.fillRect(73, 57, 14, 8, 0xFFE0);
  frameBuffer.fillRect(94, 57, 14, 8, 0x001F);
  frameBuffer.fillRect(115, 57, 14, 8, 0x07E0);
  for (int16_t x = 158; x < 307; x += 38) {
    frameBuffer.fillRect(x, 43, 31, 4, 0xD69A);
    frameBuffer.fillRect(x + 3, 47, 25, 2, 0xA514);
    frameBuffer.fillRect(x + 5, 49, 21, 5, 0x0841);
    frameBuffer.fillRect(x + 7, 50, 17, 3, 0xFEA0);
    frameBuffer.drawFastVLine(x + 2, 42, 13, 0x7BEF);
  }
  frameBuffer.fillRect(11, 87, 298, 3, 0xD69A);
  frameBuffer.fillRect(15, 91, 290, 3, 0xA514);
  for (int16_t x = 19; x < 304; x += 28) {
    frameBuffer.fillRect(x, 95, 6, 10, 0x1082);
    frameBuffer.fillRect(x + 2, 97, 2, 6, 0x5AEB);
  }
  frameBuffer.fillRect(0, 105, frameBuffer.width(), 22, 0x7A20);
  frameBuffer.drawFastHLine(0, 105, frameBuffer.width(), 0xFEA0);
  frameBuffer.drawFastHLine(0, 111, frameBuffer.width(), 0xD69A);
  frameBuffer.drawFastHLine(0, 121, frameBuffer.width(), 0xA514);
  frameBuffer.fillRect(0, 126, frameBuffer.width(),
                       frameBuffer.height() - 126, 0x5A20);
  drawWoodFloor(128, 0x5A20, 0x4208, 13);
  for (int16_t x = 17; x < frameBuffer.width(); x += 37) {
    frameBuffer.fillRect(x, 137 + x % 9, 19, 2, 0xD69A);
    frameBuffer.fillRect(x + 8, 163 + x % 11, 11, 2, 0x7A20);
    frameBuffer.fillRect(x - 4, 194 + x % 13, 26, 2, 0xA514);
  }
  frameBuffer.fillRect(0, 25, 10, 101, 0x2124);
  frameBuffer.fillRect(310, 25, 10, 101, 0x2124);
  frameBuffer.fillRect(15, 108, 292, 3, 0xD69A);
  frameBuffer.fillRect(17, 113, 286, 3, 0x7A20);
  frameBuffer.fillRect(0, 126, frameBuffer.width(), 4, 0xA514);
  for (int16_t x : {47, 160, 273}) {
    frameBuffer.fillRect(x - 20, 146, 40, 8, 0xA514);
    frameBuffer.fillRect(x - 15, 154, 30, 30, 0x7A20);
    frameBuffer.fillRect(x - 12, 184, 5, 26, wallShadow);
    frameBuffer.fillRect(x + 7, 184, 5, 26, wallShadow);
  }
  frameBuffer.fillRect(34, 31, 4, 13, 0x2124);
  frameBuffer.fillRect(160, 25, 3, 24, 0x5AEB);
  frameBuffer.fillEllipse(161, 52, 18, 6, 0xA514);
  frameBuffer.fillEllipse(161, 50, 14, 4, 0xFEA0);
  frameBuffer.fillCircle(151, 57, 3, TFT_YELLOW);
  frameBuffer.fillCircle(161, 59, 3, TFT_YELLOW);
  frameBuffer.fillCircle(171, 57, 3, TFT_YELLOW);
  frameBuffer.fillTriangle(148, 54, 174, 54, 161, 75, 0xFDC0);
  frameBuffer.drawFastVLine(161, 65, 19, 0xFEA0);
  frameBuffer.fillCircle(161, 84, 2, TFT_YELLOW);

  frameBuffer.fillRoundRect(26, 42, 110, 33, 3, 0x1082);
  frameBuffer.drawRoundRect(26, 42, 110, 33, 3, 0xFEA0);
  frameBuffer.setTextColor(TFT_ORANGE);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(44, 52);
  frameBuffer.print("TAVERN");
}

void drawCurrentScene(uint32_t now) {
  const uint8_t level = currentLevel();
  if (level != sceneryDamageLevel) {
    for (SceneryDamage &damage : sceneryDamage) {
      damage.active = false;
    }
    sceneryDamageLevel = level;
  }
  if (level != destroyedPropsLevel) {
    destroyedSceneProps = 0;
    destroyedPropsLevel = level;
  }

  switch (level) {
    case 0:
      drawAlley();
      break;
    case 1:
      drawPoliceStation(now);
      break;
    case 2:
      drawBallroom();
      break;
    case 3:
      drawBackyard();
      break;
    default:
      drawTavern();
      break;
  }
  drawSceneDoors(level);
  drawSceneProps(level);
  drawSceneryDamage();
}

void drawCrosshair(int16_t x, int16_t y) {
  display.drawCircle(x, y, 12, TFT_CYAN);
  display.drawFastHLine(x - 18, y, 37, TFT_CYAN);
  display.drawFastVLine(x, y - 18, 37, TFT_CYAN);
}

bool readTouchPoint(uint16_t &rawX, uint16_t &rawY) {
  if (!touch.touched()) {
    return false;
  }

  uint32_t sumX = 0;
  uint32_t sumY = 0;
  uint8_t samples = 0;
  for (uint8_t i = 0; i < TOUCH_SAMPLES; ++i) {
    if (!touch.touched()) {
      continue;
    }
    const TS_Point point = touch.getPoint();
    sumX += point.x;
    sumY += point.y;
    ++samples;
    delay(5);
  }

  if (samples == 0) {
    return false;
  }
  rawX = sumX / samples;
  rawY = sumY / samples;
  return true;
}

void waitForTouchRelease() {
  while (touch.touched()) {
    delay(10);
  }
  delay(120);
}

void calibrateTouch() {
  const int16_t targetX[] = {20, static_cast<int16_t>(display.width() - 21),
                             20, static_cast<int16_t>(display.width() - 21)};
  const int16_t targetY[] = {20, 20, static_cast<int16_t>(display.height() - 21),
                             static_cast<int16_t>(display.height() - 21)};
  uint16_t measuredX[4]{};
  uint16_t measuredY[4]{};

  for (;;) {
    for (uint8_t i = 0; i < 4; ++i) {
      display.fillScreen(TFT_BLACK);
      display.setTextColor(TFT_WHITE, TFT_BLACK);
      display.setTextSize(2);
      display.setCursor(10, 10);
      display.printf("Touch target %u/4", i + 1);
      drawCrosshair(targetX[i], targetY[i]);
      waitForTouchRelease();
      while (!readTouchPoint(measuredX[i], measuredY[i])) {
        delay(10);
      }
      waitForTouchRelease();
    }

    calibration.xLeft = (measuredX[0] + measuredX[2]) / 2;
    calibration.xRight = (measuredX[1] + measuredX[3]) / 2;
    calibration.yTop = (measuredY[0] + measuredY[1]) / 2;
    calibration.yBottom = (measuredY[2] + measuredY[3]) / 2;

    if (abs(static_cast<int>(calibration.xRight) - calibration.xLeft) >= 500 &&
        abs(static_cast<int>(calibration.yBottom) - calibration.yTop) >= 500) {
      break;
    }

    display.fillScreen(TFT_RED);
    display.setTextColor(TFT_WHITE, TFT_RED);
    display.setCursor(12, 30);
    display.println("Calibration failed");
    Serial.println("Touch calibration failed: invalid measurement range. Try again.");
    delay(2000);
  }

  preferences.putBytes("touch", &calibration, sizeof(calibration));
}

int16_t mapTouchCoordinate(uint16_t raw, uint16_t start, uint16_t end,
                           int16_t screenSize) {
  if (start == end) {
    return 0;
  }
  const int32_t mapped =
      (static_cast<int32_t>(raw) - start) * (screenSize - 1) /
      (static_cast<int32_t>(end) - start);
  return constrain(mapped, 0, screenSize - 1);
}

void drawMenuOfficer(uint32_t now) {
  const int16_t x = 48;
  const int16_t bob = (now / 240) % 2 == 0 ? 0 : 1;
  const int16_t headY = 105 + bob;
  const int16_t torsoY = 127 + bob;

  frameBuffer.fillEllipse(x, 205, 25, 5, wallShadow);
  frameBuffer.drawLine(x - 9, torsoY + 43, x - 13, 201, uniformBlue);
  frameBuffer.drawLine(x + 9, torsoY + 43, x + 15, 201, uniformBlue);
  frameBuffer.fillRect(x - 21, 200, 17, 5, 0x0841);
  frameBuffer.fillRect(x + 10, 200, 17, 5, 0x0841);
  frameBuffer.fillRoundRect(x - 17, torsoY, 34, 46, 7, uniformBlue);
  frameBuffer.fillRect(x - 13, torsoY + 5, 6, 36, uniformLight);
  frameBuffer.fillTriangle(x - 5, torsoY + 2, x, torsoY + 18,
                           x + 6, torsoY + 2, TFT_WHITE);
  frameBuffer.fillRect(x + 7, torsoY + 10, 6, 7, badgeGold);
  frameBuffer.fillCircle(x, headY, 15, 0xD69A);
  frameBuffer.fillRect(x - 15, headY - 3, 30, 9, 0x8410);
  frameBuffer.fillRoundRect(x - 17, headY - 17, 34, 9, 3, uniformBlue);
  frameBuffer.fillRect(x - 21, headY - 10, 42, 4, uniformBlue);
  frameBuffer.fillRect(x - 3, headY - 15, 6, 3, badgeGold);
  frameBuffer.drawFastHLine(x - 7, headY + 1, 4, 0x2104);
  frameBuffer.drawFastHLine(x + 3, headY + 1, 4, 0x2104);
  frameBuffer.drawFastHLine(x - 4, headY + 8, 8, 0x630C);

  frameBuffer.drawLine(x - 13, torsoY + 12, x - 25, torsoY + 4, uniformBlue);
  frameBuffer.drawLine(x - 25, torsoY + 4, x - 31, torsoY + 7, 0xD69A);
  frameBuffer.fillRect(x - 48, torsoY + 1, 19, 6, 0x2104);
  frameBuffer.fillRect(x - 34, torsoY + 5, 7, 10, 0x2104);
  frameBuffer.fillRect(x - 30, torsoY - 2, 4, 4, 0xD69A);
  frameBuffer.drawLine(x + 13, torsoY + 12, x + 22, torsoY + 32, uniformBlue);
  frameBuffer.drawLine(x + 22, torsoY + 32, x + 17, torsoY + 42, 0xD69A);
}

void updateMenuCar(uint32_t now) {
  if (menuCarLastMovedAt == 0) {
    menuCarLastMovedAt = now;
  }
  const uint32_t elapsed = min<uint32_t>(now - menuCarLastMovedAt, 200);
  menuCarLastMovedAt = now;

  if (menuCarPassing) {
    menuCarX += elapsed * 0.12f;
    if (menuCarX > frameBuffer.width() + 185) {
      menuCarPassing = false;
      menuCarNextPassAt = now + random(8000, 20001);
    }
  } else if (static_cast<int32_t>(now - menuCarNextPassAt) >= 0) {
    menuCarPassing = true;
    menuCarX = -185.0f;
  }
}

void drawMenuPoliceCar(int16_t carX, int16_t carY) {
  const auto scale = [](int16_t value) {
    return static_cast<int16_t>(value * 1.3f);
  };
  const uint16_t carWhite = 0xFFFF;
  const uint16_t carBlue = 0x001F;
  frameBuffer.fillEllipse(carX + scale(68), carY + scale(42), scale(75),
                          scale(6), 0x0000);
  frameBuffer.fillRoundRect(carX + scale(7), carY + scale(18), scale(128),
                             scale(27), scale(8), carWhite);
  frameBuffer.fillRoundRect(carX + scale(37), carY + scale(2), scale(69),
                             scale(22), scale(7), carWhite);
  frameBuffer.fillRect(carX + scale(45), carY + scale(5), scale(25),
                       scale(14), 0x0210);
  frameBuffer.fillRect(carX + scale(74), carY + scale(5), scale(27),
                       scale(14), 0x0210);
  frameBuffer.drawFastVLine(carX + scale(72), carY + scale(4), scale(16),
                            0x7BEF);
  frameBuffer.fillRect(carX + scale(59), carY - scale(3), scale(14),
                       scale(4), TFT_RED);
  frameBuffer.fillRect(carX + scale(73), carY - scale(3), scale(14),
                       scale(4), TFT_BLUE);
  frameBuffer.fillRect(carX + scale(20), carY + scale(23), scale(76),
                       scale(9), carBlue);
  frameBuffer.fillRect(carX + scale(9), carY + scale(24), scale(11),
                       scale(6), 0xFEA0);
  frameBuffer.fillRect(carX + scale(121), carY + scale(23), scale(13),
                       scale(7), TFT_RED);
  frameBuffer.fillRect(carX + scale(133), carY + scale(19), scale(8),
                       scale(15), carWhite);
  frameBuffer.fillRect(carX + scale(138), carY + scale(22), scale(4),
                       scale(8), 0xFEA0);
  frameBuffer.fillRect(carX + scale(110), carY + scale(15), scale(21),
                       scale(4), 0x7BEF);
  frameBuffer.fillCircle(carX + scale(37), carY + scale(43), scale(10),
                         0x0841);
  frameBuffer.fillCircle(carX + scale(37), carY + scale(43), scale(5),
                         0xBDF7);
  frameBuffer.fillCircle(carX + scale(111), carY + scale(43), scale(10),
                         0x0841);
  frameBuffer.fillCircle(carX + scale(111), carY + scale(43), scale(5),
                         0xBDF7);
  frameBuffer.drawFastHLine(carX + scale(17), carY + scale(36), scale(113),
                            0x7BEF);
}

void updateMenuWindowLights(uint32_t now) {
  for (MenuWindowLight &light : menuWindowLights) {
    if (light.permanent) {
      continue;
    }
    if (light.nextChangeAt == 0) {
      light.nextChangeAt = now + random(4000, 18001);
    } else if (static_cast<int32_t>(now - light.nextChangeAt) >= 0) {
      light.lit = !light.lit;
      light.nextChangeAt = now + random(5000, 20001);
    }
  }
}

void drawMenuBackground(uint32_t now) {
  const int16_t width = frameBuffer.width();
  updateMenuCar(now);
  updateMenuWindowLights(now);
  frameBuffer.fillSprite(0x0010);
  for (int16_t y = 0; y < 116; y += 12) {
    frameBuffer.fillRect(0, y, width, 12,
                         y < 48 ? 0x0192 : y < 84 ? 0x0210 : 0x11D4);
  }

  constexpr int16_t BUILDING_X[] = {0, 31, 72, 116, 164, 209, 256, 294};
  constexpr int16_t BUILDING_W[] = {35, 46, 49, 53, 50, 52, 46, 38};
  constexpr int16_t BUILDING_H[] = {54, 79, 61, 88, 65, 83, 57, 74};
  for (uint8_t i = 0; i < sizeof(BUILDING_X) / sizeof(BUILDING_X[0]); ++i) {
    const int16_t x = BUILDING_X[i];
    const int16_t top = 120 - BUILDING_H[i];
    const uint16_t building = i % 2 == 0 ? 0x2108 : 0x318C;
    frameBuffer.fillRect(x, top, BUILDING_W[i], BUILDING_H[i], building);
    frameBuffer.drawFastVLine(x + 2, top, BUILDING_H[i], 0x420F);
    for (int16_t wy = top + 8; wy < 114; wy += 13) {
      for (int16_t wx = x + 6; wx < x + BUILDING_W[i] - 5; wx += 12) {
        uint16_t lightColor = 0x318C;
        for (const MenuWindowLight &light : menuWindowLights) {
          if (light.x == wx && light.y == wy) {
            if (light.lit) {
              lightColor = light.color;
            }
            break;
          }
        }
        frameBuffer.fillRect(wx, wy, 5, 6, lightColor);
      }
    }
  }
  frameBuffer.fillRect(0, 120, width, 44, 0x7BEF);
  frameBuffer.drawFastHLine(0, 120, width, 0xD69A);
  frameBuffer.fillRect(0, 164, width, 48, 0x2104);
  frameBuffer.drawFastHLine(0, 164, width, 0xBDF7);
  frameBuffer.drawFastHLine(0, 207, width, 0x7BEF);
  for (int16_t x = 4; x < width; x += 43) {
    frameBuffer.fillRect(x, 184, 20, 2, 0x7BEF);
  }

  frameBuffer.fillRect(0, 212, width, frameBuffer.height() - 212, 0x0320);
  frameBuffer.fillRect(0, 212, width, 3, 0x5B22);
  for (int16_t x = 6; x < width; x += 17) {
    const int16_t grassHeight = 4 + (x * 7 % 9);
    frameBuffer.drawFastVLine(x, 212 - grassHeight, grassHeight, 0x04A0);
    frameBuffer.drawFastVLine(x + 3, 214 - grassHeight / 2,
                              grassHeight / 2, 0x3A01);
  }

  if (menuCarPassing) {
    drawMenuPoliceCar(static_cast<int16_t>(menuCarX), 143);
  }
}

void chooseMenuCatDestination(uint32_t now) {
  const int16_t targets[] = {
      18, static_cast<int16_t>(frameBuffer.width() / 2),
                              static_cast<int16_t>(frameBuffer.width() - 18)};
  int8_t available[3];
  uint8_t count = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    if (abs(targets[i] - menuCatX) > 45) {
      available[count++] = i;
    }
  }
  const uint8_t targetIndex =
      count > 0 ? available[random(0, count)]
                : (menuCatX < frameBuffer.width() / 2 ? 2 : 0);
  menuCatTargetX = targets[targetIndex];
  menuCatDirection = menuCatTargetX > menuCatX ? 1 : -1;
  menuCatWalking = true;
  menuCatStateUntil = now + random(3500, 7001);
}

void updateMenuCat(uint32_t now) {
  const uint32_t elapsed = min<uint32_t>(now - menuCatLastMovedAt, 200);
  menuCatLastMovedAt = now;
  if (menuCatWalking) {
    const float distance = abs(menuCatTargetX - menuCatX);
    const float step = elapsed * 0.055f;
    if (distance <= step ||
        static_cast<int32_t>(now - menuCatStateUntil) >= 0) {
      if (distance <= step) {
        menuCatX = menuCatTargetX;
      } else {
        menuCatX = constrain(menuCatX, 18.0f,
                             static_cast<float>(frameBuffer.width() - 18));
      }
      menuCatWalking = false;
      menuCatStateUntil = now + random(1800, 7001);
    } else {
      menuCatX += menuCatDirection * step;
    }
  } else if (static_cast<int32_t>(now - menuCatStateUntil) >= 0) {
    chooseMenuCatDestination(now);
  }
}

void drawMenuCat(uint32_t now) {
  const int16_t x = static_cast<int16_t>(menuCatX);
  const int16_t y = 225;
  const int8_t stride = menuCatWalking && (now / 130) % 2 == 0 ? -2 : 2;
  const int16_t facing = menuCatDirection;
  const uint16_t fur = 0xBDF7;
  if (menuCatWalking) {
    frameBuffer.drawLine(x - 5, y + 4, x - 11 * facing, y, 0x7BEF);
    frameBuffer.drawLine(x - 3, y + 4, x - 5 + stride, y + 10, fur);
    frameBuffer.drawLine(x + 5, y + 4, x + 7 - stride, y + 10, fur);
    frameBuffer.fillEllipse(x, y, 10, 5, fur);
    frameBuffer.fillCircle(x + 9 * facing, y - 5, 5, fur);
    frameBuffer.fillTriangle(x + 6 * facing, y - 8,
                             x + 7 * facing, y - 14,
                             x + 11 * facing, y - 8, fur);
    frameBuffer.fillTriangle(x + 10 * facing, y - 8,
                             x + 14 * facing, y - 13,
                             x + 15 * facing, y - 5, fur);
    frameBuffer.fillCircle(x + 11 * facing, y - 6, 1, TFT_GREEN);
    frameBuffer.drawLine(x + 13 * facing, y - 2, x + 17 * facing, y - 2,
                         0x7BEF);
  } else {
    frameBuffer.fillEllipse(x, y + 1, 8, 7, fur);
    frameBuffer.fillCircle(x + 6 * facing, y - 6, 5, fur);
    frameBuffer.fillTriangle(x + 3 * facing, y - 9,
                             x + 3 * facing, y - 15,
                             x + 8 * facing, y - 10, fur);
    frameBuffer.fillTriangle(x + 8 * facing, y - 9,
                             x + 13 * facing, y - 13,
                             x + 13 * facing, y - 6, fur);
    frameBuffer.fillCircle(x + 8 * facing, y - 7, 1, TFT_GREEN);
    frameBuffer.drawLine(x + 8 * facing, y, x + 13 * facing, y - 9,
                         0x7BEF);
    frameBuffer.drawLine(x - 2, y + 5, x - 4, y + 8, fur);
    frameBuffer.drawLine(x + 3, y + 5, x + 5, y + 8, fur);
  }
}

void drawMenuZombie(uint32_t now) {
  const int16_t x = 273;
  const int16_t bob = (now / 310) % 2 == 0 ? 0 : 2;
  const int16_t gaze = (now / 650) % 2 == 0 ? -2 : 2;
  const int16_t headY = 105 + bob;
  const int16_t torsoY = 130 + bob;

  frameBuffer.fillEllipse(x, 205, 24, 5, wallShadow);
  frameBuffer.fillRoundRect(x - 17, torsoY, 34, 47, 7, uniformBlue);
  frameBuffer.fillRect(x - 13, torsoY + 5, 6, 36, uniformLight);
  frameBuffer.fillTriangle(x - 5, torsoY + 2, x, torsoY + 18,
                           x + 6, torsoY + 2, TFT_WHITE);
  frameBuffer.fillRect(x + 7, torsoY + 10, 6, 7, badgeGold);
  frameBuffer.drawLine(x - 9, torsoY + 43, x - 13, 201, uniformBlue);
  frameBuffer.drawLine(x + 9, torsoY + 43, x + 14, 201, uniformBlue);
  frameBuffer.fillRect(x - 27, 200, 17, 5, 0x0841);
  frameBuffer.fillRect(x + 9, 200, 17, 5, 0x0841);
  frameBuffer.drawLine(x - 13, torsoY + 10, x - 29, torsoY + 24 + bob,
                       zombieGreen);
  frameBuffer.drawLine(x - 29, torsoY + 24 + bob, x - 38, torsoY + 18,
                       zombieGreen);
  frameBuffer.fillCircle(x - 39, torsoY + 17, 4, zombieGreen);
  frameBuffer.drawLine(x + 13, torsoY + 10, x + 28, torsoY + 22 - bob,
                       zombieGreen);
  frameBuffer.drawLine(x + 28, torsoY + 22 - bob, x + 38, torsoY + 16,
                       zombieGreen);
  frameBuffer.fillCircle(x + 39, torsoY + 15, 4, zombieGreen);

  frameBuffer.fillCircle(x, headY, 17, zombieGreen);
  frameBuffer.fillRoundRect(x - 19, headY - 16, 38, 9, 3, uniformBlue);
  frameBuffer.fillRect(x - 23, headY - 9, 46, 5, uniformBlue);
  frameBuffer.fillRect(x - 3, headY - 14, 6, 3, badgeGold);
  frameBuffer.fillRect(x - 10, headY - 3, 8, 7, zombieDark);
  frameBuffer.fillRect(x + 2, headY - 3, 8, 7, zombieDark);
  frameBuffer.fillCircle(x - 6 + gaze, headY, 2, TFT_RED);
  frameBuffer.fillCircle(x + 6 + gaze, headY, 2, TFT_RED);
  frameBuffer.drawFastHLine(x - 5, headY + 9, 10, zombieDark);
}

void drawMenu(uint32_t now) {
  drawMenuBackground(now);
  updateMenuCat(now);
  drawMenuOfficer(now);
  drawMenuZombie(now);
  frameBuffer.drawRoundRect(77, 25, 166, 211, 10, deskEdge);
  frameBuffer.fillRoundRect(86, 29, 148, 61, 7, 0x0000);
  frameBuffer.setTextColor(TFT_ORANGE);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(100, 34);
  frameBuffer.print("CYD OF THE");
  frameBuffer.setCursor(132, 59);
  frameBuffer.print("DEAD");

  frameBuffer.setTextColor(TFT_WHITE);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(105, 101);
  frameBuffer.printf("BEST: %lu", static_cast<unsigned long>(highScore));
  frameBuffer.setCursor(142, 119);
  frameBuffer.printf("%u%%", volume);
  frameBuffer.fillRoundRect(101, 133, 27, 27, 5, 0x3186);
  frameBuffer.drawRoundRect(101, 133, 27, 27, 5, TFT_WHITE);
  frameBuffer.setTextSize(2);
  frameBuffer.setTextColor(TFT_WHITE, 0x3186);
  frameBuffer.setCursor(110, 137);
  frameBuffer.print("-");
  frameBuffer.fillRect(132, 143, 56, 9, wallShadow);
  frameBuffer.fillRect(132, 143, 56 * volume / 100, 9, TFT_GREEN);
  frameBuffer.fillRoundRect(192, 133, 27, 27, 5, 0x3186);
  frameBuffer.drawRoundRect(192, 133, 27, 27, 5, TFT_WHITE);
  frameBuffer.setCursor(200, 137);
  frameBuffer.print("+");

  frameBuffer.fillRoundRect(99, 161, 122, 27, 6, 0x3186);
  frameBuffer.drawRoundRect(99, 161, 122, 27, 6, TFT_WHITE);
  frameBuffer.setTextColor(TFT_CYAN, 0x3186);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(121, 164);
  frameBuffer.print("TAP TO CHANGE");
  frameBuffer.setCursor(109, 175);
  frameBuffer.printf("%s - %u LIVES", difficultyName(), startingLives());

  frameBuffer.drawRoundRect(99, 195, 122, 31, 8, TFT_WHITE);
  frameBuffer.setTextColor(TFT_ORANGE);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(133, 202);
  frameBuffer.print("START");

  drawMenuCat(now);
  frameBuffer.pushSprite(0, 0);
}

void drawZombie(const Zombie &zombie, uint32_t now) {
  const uint32_t age = now - zombie.spawnedAt;
  const int16_t x = static_cast<int16_t>(zombie.x);
  const int16_t y = static_cast<int16_t>(zombie.y);
  if (zombie.kind == ZombieKind::Peeker && age < 850) {
    const int16_t peekHeadY = zombie.entryY - 8;
    frameBuffer.fillCircle(zombie.entryX, peekHeadY, 9, zombieGreen);
    frameBuffer.fillCircle(zombie.entryX - 3, peekHeadY, 2, TFT_RED);
    frameBuffer.fillCircle(zombie.entryX + 3, peekHeadY, 2, TFT_RED);
    frameBuffer.drawFastHLine(zombie.entryX - 8, zombie.entryY + 5, 16,
                              deskEdge);
    return;
  }
  const int16_t radius = zombie.kind == ZombieKind::Boss
                             ? min<int16_t>(32, 18 + age / 400)
                         : zombie.kind == ZombieKind::Brute
                             ? min<int16_t>(29, 13 + age / 380)
                         : zombie.kind == ZombieKind::Runner
                             ? min<int16_t>(21, 8 + age / 450)
                             : min<int16_t>(24, 9 + age / 420);
  const uint16_t skin = zombie.kind == ZombieKind::Boss ? 0xFBA0 :
                        zombie.kind == ZombieKind::Brute ? 0x8A43 :
                        zombie.kind == ZombieKind::Runner ? 0x7FE0 :
                        zombie.lane == 1 ? 0x8D6A : zombieGreen;
  const uint32_t animationPhase =
      now / (zombie.animation == ZombieAnimation::Lurch ? 310 : 190) +
      zombie.lane;
  const bool animationStep = animationPhase % 2 != 0;
  const int16_t sway = animationStep ? -4 : 4;
  const int16_t bob = zombie.animation == ZombieAnimation::Lurch
                          ? (animationStep ? 3 : -1)
                          : zombie.animation == ZombieAnimation::Reach
                                ? (animationStep ? 2 : 0)
                                : (animationStep ? 0 : 2);
  const bool emerging = age < ZOMBIE_EMERGE_MS;
  const int16_t headLean =
      zombie.animation == ZombieAnimation::Lurch && animationStep ? -2 : 0;
  const int16_t torsoTop = y - radius / 2 + bob;
  const int16_t torsoWidth =
      max<int16_t>(12, radius + (zombie.kind == ZombieKind::Boss ? 12 : 0));
  const int16_t headRadius = radius / 2 + 2;
  const bool isPolice = zombie.kind == ZombieKind::Police;
  const uint16_t suitColor = zombie.kind == ZombieKind::Boss ? 0x8000 :
                             zombie.kind == ZombieKind::Brute ? 0x5A20 :
                             zombie.lane == 1 ? 0x4208 : 0x3186;
  const uint16_t dressColor = zombie.lane == 1 ? 0xF81F : 0x8010;
  const uint16_t outfitColor = zombie.kind == ZombieKind::Boss ? 0x8000 :
                               isPolice ? uniformBlue :
                               zombie.kind == ZombieKind::Suit ? suitColor :
                               zombie.kind == ZombieKind::Dress ? dressColor :
                               suitColor;
  const uint16_t pantsColor = isPolice ? uniformBlue : suitColor;
  const int16_t stride = animationStep ? -7 : 7;
  const int16_t armSwing = zombie.animation == ZombieAnimation::Lurch
                               ? (animationStep ? 10 : -7)
                               : zombie.animation == ZombieAnimation::Reach
                                     ? (animationStep ? -12 : -5)
                                     : sway;

  frameBuffer.fillEllipse(x, y + radius / 2 + bob, radius / 2 + 3,
                          radius / 3 + 2, zombieDark);
  frameBuffer.fillRoundRect(x - torsoWidth / 2, torsoTop, torsoWidth,
                            radius + 9, 5, outfitColor);
  if (zombie.kind == ZombieKind::Boss) {
    frameBuffer.drawRoundRect(x - torsoWidth / 2, torsoTop, torsoWidth,
                              radius + 9, 5, badgeGold);
    const bool weakPointOpen = bossWeakpointIsOpen(age);
    const uint16_t weakPointColor =
        weakPointOpen ? TFT_YELLOW : 0x8000;
    frameBuffer.fillCircle(x, y + 1, 9, weakPointColor);
    frameBuffer.drawCircle(x, y + 1, 11, TFT_WHITE);
    frameBuffer.fillCircle(x, y + 1, 4, weakPointOpen ? TFT_ORANGE : 0x4208);
    frameBuffer.fillTriangle(x - 15, torsoTop + 3, x, torsoTop - 13,
                             x + 15, torsoTop + 3, badgeGold);
  } else if (zombie.kind == ZombieKind::Brute) {
    frameBuffer.fillRect(x - 2, torsoTop + 5, 4, radius + 2, 0xD69A);
    frameBuffer.fillCircle(x - torsoWidth / 2 + 3, torsoTop + 4, 3, 0x7BEF);
    frameBuffer.fillCircle(x + torsoWidth / 2 - 3, torsoTop + 4, 3, 0x7BEF);
  } else if (zombie.kind == ZombieKind::Runner) {
    frameBuffer.drawFastVLine(x, torsoTop + 2, radius + 5, 0xFFE0);
  }
  if (isPolice) {
    frameBuffer.fillRect(x - torsoWidth / 2 + 2, torsoTop + 2,
                         max<int16_t>(3, torsoWidth / 5), radius + 3,
                         uniformLight);
    frameBuffer.drawFastVLine(x, torsoTop + 3, radius + 4, 0x10D3);
    frameBuffer.fillRect(x + torsoWidth / 5, torsoTop + 5, 5, 6, badgeGold);
    frameBuffer.drawFastHLine(x - torsoWidth / 3, torsoTop + 8,
                              max<int16_t>(3, torsoWidth / 4), 0xFFFF);
  } else if (zombie.kind == ZombieKind::Suit) {
    frameBuffer.fillTriangle(x - torsoWidth / 3, torsoTop + 3,
                             x - 2, torsoTop + 14, x + 1,
                             torsoTop + 3, 0xFFFF);
    frameBuffer.fillTriangle(x + torsoWidth / 3, torsoTop + 3,
                             x - 2, torsoTop + 14, x + 1,
                             torsoTop + 3, 0xFFFF);
    frameBuffer.fillTriangle(x - 2, torsoTop + 11, x + 2, torsoTop + 11,
                             x, torsoTop + 20, TFT_RED);
  } else if (zombie.kind == ZombieKind::Dress) {
    frameBuffer.fillTriangle(x - torsoWidth / 2, torsoTop + radius / 3,
                             x + torsoWidth / 2, torsoTop + radius / 3,
                             x + torsoWidth / 2 + 2, torsoTop + radius + 9,
                             dressColor);
    frameBuffer.fillTriangle(x - torsoWidth / 2, torsoTop + radius / 3,
                             x + torsoWidth / 2 + 2, torsoTop + radius + 9,
                             x - torsoWidth / 2 - 2, torsoTop + radius + 9,
                             dressColor);
    frameBuffer.drawFastHLine(x - torsoWidth / 2 - 1,
                              torsoTop + radius + 7, torsoWidth + 2,
                              0xF9B6);
  }
  frameBuffer.fillCircle(x + headLean, y - radius + bob, headRadius, skin);

  const int16_t headX = x + headLean;
  const int16_t capY = y - radius - headRadius + bob;
  if (isPolice && zombie.hasHat) {
    frameBuffer.fillRect(x - headRadius - 2, capY + 2, headRadius * 2 + 4, 5,
                         uniformBlue);
    frameBuffer.fillRoundRect(x - headRadius, capY - 3, headRadius * 2, 7,
                              3, uniformBlue);
    frameBuffer.fillRect(x - 3, capY - 2, 6, 3, badgeGold);
  }

  const int16_t eyeOffset = max<int16_t>(3, headRadius / 3);
  frameBuffer.fillCircle(headX - eyeOffset, y - radius + bob, 2, TFT_RED);
  frameBuffer.fillCircle(headX + eyeOffset, y - radius + bob, 2, TFT_RED);
  frameBuffer.drawFastHLine(headX - 3, y - radius + headRadius / 2 + bob, 7,
                            zombieDark);
  if (!isPolice) {
    if (zombie.kind == ZombieKind::Suit) {
      frameBuffer.fillRect(headX - headRadius, y - radius + bob - headRadius / 3,
                           headRadius * 2, max<int16_t>(2, headRadius / 3),
                           0x2104);
    } else {
      frameBuffer.fillCircle(headX - headRadius + 2,
                             y - radius + bob + 1, headRadius / 2, 0x2104);
      frameBuffer.fillCircle(headX + headRadius - 2,
                             y - radius + bob + 1, headRadius / 2, 0x2104);
    }
  }
  for (uint8_t hit = 0; hit < zombie.torsoHits; ++hit) {
    const int16_t woundX = x - torsoWidth / 3 + hit * 5;
    frameBuffer.drawLine(woundX, torsoTop + 13, woundX + 4,
                         torsoTop + 18, woundRed);
  }
  if (isPolice || zombie.kind == ZombieKind::Boss) {
    frameBuffer.drawLine(x + torsoWidth / 2, torsoTop + 7,
                         x + torsoWidth / 2 + 8, torsoTop + 12, skin);
    frameBuffer.fillRect(x + torsoWidth / 2 + 6, torsoTop + 9, 9, 4,
                         wallShadow);
    frameBuffer.fillRect(x + torsoWidth / 2 + 12, torsoTop + 7, 4, 3,
                         deskEdge);
  }

  const bool usesGun = isPolice || zombie.kind == ZombieKind::Boss;
  const int32_t shotWarning = static_cast<int32_t>(zombie.nextShotAt - now);
  if (usesGun && shotWarning > 0 &&
      shotWarning <= ENEMY_SHOT_WARNING_MS) {
    frameBuffer.setTextColor(TFT_RED);
    frameBuffer.setTextSize(2);
    frameBuffer.setCursor(x - 3, capY - 24);
    frameBuffer.print("!");
  }

  if (emerging) {
    const int16_t reach = 9 + age / 210;
    const int16_t leftReach =
        zombie.animation == ZombieAnimation::Lurch && animationStep
            ? reach / 2
            : zombie.animation == ZombieAnimation::Reach ? reach + 9 : reach;
    const int16_t rightReach =
        zombie.animation == ZombieAnimation::Lurch && !animationStep
            ? reach / 2
            : zombie.animation == ZombieAnimation::Reach ? reach + 9 : reach;
    frameBuffer.drawLine(x - torsoWidth / 2, torsoTop + 6,
                         x - torsoWidth - 5,
                         torsoTop - leftReach + armSwing, skin);
    frameBuffer.drawLine(x + torsoWidth / 2, torsoTop + 6,
                         x + torsoWidth + 5,
                         torsoTop - rightReach - armSwing, skin);
    frameBuffer.fillCircle(x - torsoWidth - 5,
                           torsoTop - leftReach + armSwing, 3, skin);
    frameBuffer.fillCircle(x + torsoWidth + 5,
                           torsoTop - rightReach - armSwing, 3, skin);
  } else {
    const int16_t leftArmY = zombie.animation == ZombieAnimation::Reach
                                 ? torsoTop - 5
                                 : torsoTop + radius / 2 + armSwing;
    const int16_t rightArmY = zombie.animation == ZombieAnimation::Reach
                                  ? torsoTop - 5
                                  : torsoTop + radius / 2 - armSwing;
    const int16_t leftArmX =
        zombie.animation == ZombieAnimation::Reach ? x - torsoWidth - 10
                                                    : x - torsoWidth - 4;
    const int16_t rightArmX =
        zombie.animation == ZombieAnimation::Reach ? x + torsoWidth + 10
                                                    : x + torsoWidth + 4;
    frameBuffer.drawLine(x - torsoWidth / 2, torsoTop + 7,
                         leftArmX, leftArmY, skin);
    frameBuffer.drawLine(x + torsoWidth / 2, torsoTop + 7,
                         rightArmX, rightArmY, skin);
    frameBuffer.fillCircle(leftArmX, leftArmY, 3, skin);
    frameBuffer.fillCircle(rightArmX, rightArmY, 3, skin);
    frameBuffer.drawLine(x - torsoWidth / 4, y + radius / 2,
                         x - torsoWidth / 3 + stride, y + radius + 8,
                         pantsColor);
    frameBuffer.drawLine(x + torsoWidth / 4, y + radius / 2,
                         x + torsoWidth / 3 - stride, y + radius + 8,
                         pantsColor);
  }
  frameBuffer.drawFastHLine(x - radius / 3, y - radius / 2,
                            max<int16_t>(3, radius * 2 / 3), woundRed);
  if (zombie.kind == ZombieKind::Boss) {
    const int16_t barWidth = 42;
    const int16_t barX = x - barWidth / 2;
    const int16_t barY = y + radius + 11;
    frameBuffer.setTextColor(TFT_WHITE);
    frameBuffer.setTextSize(1);
    frameBuffer.setCursor(barX - 15, barY);
    frameBuffer.print("H");
    frameBuffer.fillRect(barX, barY, barWidth, 4, wallShadow);
    frameBuffer.fillRect(barX, barY,
                         barWidth * (BOSS_HEAD_HITS - zombie.headHits) /
                             BOSS_HEAD_HITS,
                         4, TFT_CYAN);
    frameBuffer.drawRect(barX, barY, barWidth, 4, TFT_WHITE);
    frameBuffer.setCursor(barX - 15, barY + 7);
    frameBuffer.print("T");
    frameBuffer.fillRect(barX, barY + 7, barWidth, 4, wallShadow);
    frameBuffer.fillRect(barX, barY + 7,
                         barWidth * (zombieTorsoHealth(zombie.kind) -
                                     zombie.torsoHits) /
                             zombieTorsoHealth(zombie.kind),
                         4, TFT_RED);
    frameBuffer.drawRect(barX, barY + 7, barWidth, 4, TFT_WHITE);
    frameBuffer.setTextColor(TFT_YELLOW);
    frameBuffer.setCursor(x - 12, barY - 10);
    frameBuffer.print("BOSS");
  }
}

void updateCivilians(uint32_t now) {
  if (static_cast<int32_t>(now - nextCivilianAt) >= 0) {
    for (Civilian &civilian : civilians) {
      if (civilian.active) {
        continue;
      }
      civilian.lane = random(0, 3);
      civilian.x = random(24, frameBuffer.width() - 24);
      civilian.y = 139 + civilian.lane * 15;
      civilian.spawnedAt = now;
      civilian.lastMovedAt = now;
      civilian.direction = random(0, 2) == 0 ? -1 : 1;
      civilian.type = static_cast<CivilianType>(random(0, 5));
      civilian.active = true;
      nextCivilianAt = now + random(1300, 3001);
      break;
    }
  }

  for (Civilian &civilian : civilians) {
    if (!civilian.active) {
      continue;
    }
    if (now - civilian.spawnedAt >= 9000) {
      civilian.active = false;
      continue;
    }

    const uint32_t elapsed = now - civilian.lastMovedAt;
    civilian.lastMovedAt = now;
    civilian.x += civilian.direction * elapsed * 0.018f;
    if (civilian.x < 20 || civilian.x > frameBuffer.width() - 20) {
      civilian.direction = -civilian.direction;
      civilian.x = constrain(civilian.x, 20.0f,
                             static_cast<float>(frameBuffer.width() - 20));
    }
  }
}

void drawCivilian(const Civilian &civilian, uint32_t now) {
  const int16_t x = static_cast<int16_t>(civilian.x);
  const int16_t y = static_cast<int16_t>(civilian.y);
  const int8_t stride = (now / 150 + civilian.lane) % 2 == 0 ? -4 : 4;
  if (civilian.type == CivilianType::Dog ||
      civilian.type == CivilianType::Cat) {
    const bool dog = civilian.type == CivilianType::Dog;
    const uint16_t fur = dog ? 0xD69A : 0xBDF7;
    frameBuffer.fillEllipse(x, y, dog ? 12 : 10, dog ? 6 : 7, fur);
    frameBuffer.fillCircle(x + 9, y - 4, 5, fur);
    if (dog) {
      frameBuffer.fillEllipse(x - 10, y - 2, 4, 6, fur);
    } else {
      frameBuffer.fillTriangle(x + 5, y - 7, x + 7, y - 15,
                               x + 11, y - 7, fur);
      frameBuffer.fillTriangle(x + 11, y - 7, x + 16, y - 14,
                               x + 15, y - 4, fur);
    }
    frameBuffer.fillCircle(x + 11, y - 5, 1, TFT_BLACK);
    frameBuffer.drawLine(x - 6, y + 3, x - 8 + stride, y + 9, fur);
    frameBuffer.drawLine(x + 5, y + 3, x + 6 - stride, y + 9, fur);
    if (dog) {
      frameBuffer.drawLine(x - 10, y - 1, x - 15, y - 5, fur);
      frameBuffer.fillCircle(x + 14, y - 2, 2, 0xF800);
    } else {
      frameBuffer.fillCircle(x + 14, y - 1, 2, 0xF800);
    }
    return;
  }

  const bool child = civilian.type == CivilianType::Child;
  const int16_t scale = child ? 1 : 2;
  const uint16_t shirt = civilian.type == CivilianType::Man
                             ? 0x07FF
                             : civilian.type == CivilianType::Woman
                                   ? 0xF81F
                                   : 0xFFE0;
  const uint16_t hair = civilian.type == CivilianType::Woman ? 0x4208 : 0x2104;
  const int16_t headY = y - 12 * scale;
  frameBuffer.fillCircle(x, headY, 5 * scale, 0xD69A);
  if (civilian.type == CivilianType::Woman) {
    frameBuffer.fillCircle(x - 4 * scale, headY + 2, 3 * scale, hair);
    frameBuffer.fillCircle(x + 4 * scale, headY + 2, 3 * scale, hair);
  } else {
    frameBuffer.fillRoundRect(x - 5 * scale, headY - 5 * scale,
                              10 * scale, 4 * scale, 2, hair);
  }
  frameBuffer.fillRoundRect(x - 7 * scale, y - 6 * scale,
                            14 * scale, 13 * scale, 3, shirt);
  frameBuffer.drawLine(x - 4 * scale, y + 6 * scale,
                       x - 6 * scale + stride, y + 15 * scale, 0x2104);
  frameBuffer.drawLine(x + 4 * scale, y + 6 * scale,
                       x + 6 * scale - stride, y + 15 * scale, 0x2104);
}

void drawEnemyShots(uint32_t now) {
  for (const EnemyShot &shot : enemyShots) {
    if (!shot.active) {
      continue;
    }
    const uint32_t age = now - shot.startedAt;
    const float progress =
        min<uint32_t>(age, ENEMY_SHOT_TRAVEL_MS) /
        static_cast<float>(ENEMY_SHOT_TRAVEL_MS);
    const int16_t startX = shot.x;
    const int16_t startY = shot.y;
    const int16_t targetX = frameBuffer.width() / 2;
    const int16_t targetY = frameBuffer.height() - 31;
    const int16_t bulletX = startX + (targetX - startX) * progress;
    const int16_t bulletY = startY + (targetY - startY) * progress;

    frameBuffer.drawLine(startX, startY, bulletX, bulletY, TFT_RED);
    frameBuffer.drawLine(startX + 1, startY, bulletX + 1, bulletY, TFT_YELLOW);
    frameBuffer.fillCircle(bulletX, bulletY, 3, TFT_YELLOW);
    if (age < 90) {
      frameBuffer.fillCircle(startX, startY, 6, TFT_ORANGE);
      frameBuffer.fillCircle(startX, startY, 3, TFT_YELLOW);
    }
  }
}

void drawHud() {
  frameBuffer.fillRect(0, 0, frameBuffer.width(), 25, hudColor);
  frameBuffer.setTextColor(TFT_WHITE, hudColor);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(8, 9);
  frameBuffer.printf("SCORE %lu", static_cast<unsigned long>(score));
  frameBuffer.setCursor(140, 9);
  frameBuffer.printf("WAVE %u", wave);
  frameBuffer.setCursor(215, 9);
  frameBuffer.printf("C%lu", static_cast<unsigned long>(
                                 min<uint32_t>(currentCombo, 999)));
  frameBuffer.setCursor(frameBuffer.width() - 31, 9);
  frameBuffer.printf("L%u", lives);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 14, 0x2104);
  frameBuffer.setTextColor(objectiveCompleted ? TFT_GREEN : TFT_CYAN, 0x2104);
  frameBuffer.setCursor(8, 29);
  if (objectiveCompleted) {
    frameBuffer.printf("OBJECTIVE COMPLETE  +100");
  } else {
    frameBuffer.printf("OBJECTIVE %s %u/%u", objectiveLabel(),
                       objectiveProgress, objectiveTarget());
  }
}

void drawPlayerDamage(uint32_t now) {
  if (static_cast<int32_t>(playerHitUntil - now) <= 0) {
    return;
  }
  const int16_t edge = 7;
  frameBuffer.fillRect(0, 25, frameBuffer.width(), edge, TFT_RED);
  frameBuffer.fillRect(0, frameBuffer.height() - edge, frameBuffer.width(),
                       edge, TFT_RED);
  frameBuffer.fillRect(0, 25, edge, frameBuffer.height() - 25, TFT_RED);
  frameBuffer.fillRect(frameBuffer.width() - edge, 25, edge,
                       frameBuffer.height() - 25, TFT_RED);
  frameBuffer.fillRoundRect(frameBuffer.width() / 2 - 30, 34, 60, 24,
                            4, TFT_RED);
  frameBuffer.setTextColor(TFT_WHITE, TFT_RED);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(frameBuffer.width() / 2 - 19, 38);
  frameBuffer.print("HIT!");
}

bool isReloadButtonHit(int16_t x, int16_t y) {
  const int16_t buttonX = frameBuffer.width() - 112;
  const int16_t buttonY = frameBuffer.height() - 48;
  return x >= buttonX - 12 && x <= buttonX + 116 &&
         y >= buttonY - 10 && y <= buttonY + 36;
}

void drawAmmo() {
  const int16_t baseX = 8;
  const int16_t topY = frameBuffer.height() - 24;
  for (uint8_t i = 0; i < MAGAZINE_SIZE; ++i) {
    const int16_t x = baseX + i * 9;
    const uint16_t color = i < ammo ? badgeGold : 0x630C;
    frameBuffer.fillRect(x + 1, topY + 4, 5, 9, color);
    frameBuffer.fillTriangle(x + 1, topY + 4, x + 6, topY + 4,
                             x + 3, topY, color);
    frameBuffer.drawFastHLine(x + 1, topY + 13, 5, 0xD69A);
  }

  const bool needsReload = ammo == 0;
  const uint16_t buttonColor = needsReload ? TFT_RED : 0x4208;
  const int16_t buttonX = frameBuffer.width() - 112;
  const int16_t buttonY = frameBuffer.height() - 48;
  frameBuffer.fillRoundRect(buttonX, buttonY, 104, 25, 5,
                            buttonColor);
  frameBuffer.drawRoundRect(buttonX, buttonY, 104, 25, 5,
                            needsReload ? TFT_YELLOW : 0x7BEF);
  frameBuffer.setTextColor(TFT_WHITE, buttonColor);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(buttonX + 24, buttonY + 8);
  frameBuffer.print("RELOAD");
}

void drawLevelTransition(uint32_t now) {
  if (!levelTransitionActive) {
    return;
  }

  const uint32_t elapsed = now - levelTransitionStartedAt;
  if (elapsed >= LEVEL_TRANSITION_MS) {
    levelTransitionActive = false;
    return;
  }

  const int16_t wipeX =
      static_cast<int32_t>(elapsed) * frameBuffer.width() / LEVEL_TRANSITION_MS;
  const int16_t wipeWidth = frameBuffer.width() - wipeX;
  frameBuffer.fillRect(wipeX, 25, wipeWidth, frameBuffer.height() - 43,
                       backgroundColor);
  if (wipeWidth > 105) {
    frameBuffer.drawFastVLine(wipeX, 25, frameBuffer.height() - 43, TFT_ORANGE);
    frameBuffer.setTextColor(TFT_ORANGE);
    frameBuffer.setTextSize(2);
    frameBuffer.setCursor(wipeX + 12, frameBuffer.height() / 2 - 16);
    frameBuffer.printf("LEVEL %u", transitionLevel + 1);
    frameBuffer.setTextColor(TFT_WHITE);
    frameBuffer.setTextSize(1);
    frameBuffer.setCursor(wipeX + 12, frameBuffer.height() / 2 + 8);
    frameBuffer.print(levelName(transitionLevel));
  }
}

void drawGame(uint32_t now) {
  drawCurrentScene(now);
  for (const Zombie &zombie : zombies) {
    if (zombie.active) {
      drawZombie(zombie, now);
    }
  }
  for (const Civilian &civilian : civilians) {
    if (civilian.active) {
      drawCivilian(civilian, now);
    }
  }
  drawEnemyShots(now);
  drawHitEffects(now);
  drawLevelTransition(now);
  drawHud();
  frameBuffer.setTextColor(TFT_LIGHTGREY);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(117, frameBuffer.height() - 14);
  frameBuffer.print(levelName(currentLevel()));
  drawPlayerDamage(now);
  drawAmmo();
  frameBuffer.pushSprite(0, 0);
}

void drawCelebratingZombie(uint32_t now) {
  const int16_t panelX = frameBuffer.width() * 7 / 10;
  const int16_t panelWidth = frameBuffer.width() - panelX;
  const int16_t centerX = panelX + panelWidth / 2;
  const int16_t bob = (now / 180) % 2 == 0 ? 0 : -5;
  const bool armsWide = (now / 260) % 2 == 0;
  const int16_t headY = 91 + bob;
  const int16_t torsoY = 119 + bob;

  frameBuffer.fillRect(panelX, 28, panelWidth, frameBuffer.height() - 28,
                       0x10A3);
  frameBuffer.fillTriangle(panelX, frameBuffer.height() - 12,
                           centerX, 122, frameBuffer.width(),
                           frameBuffer.height() - 12, 0x2128);
  frameBuffer.drawFastVLine(panelX, 28, frameBuffer.height() - 28, 0x5AEB);
  frameBuffer.fillCircle(centerX - 30, 57 + (now / 140) % 18, 2, badgeGold);
  frameBuffer.fillCircle(centerX + 27, 48 + (now / 170) % 26, 2, TFT_CYAN);
  frameBuffer.fillCircle(centerX + 36, 111 + (now / 200) % 21, 2, TFT_YELLOW);
  frameBuffer.fillCircle(centerX - 31, 134 + (now / 160) % 17, 2, TFT_ORANGE);

  const int16_t armSpread = armsWide ? 28 : 20;
  const int16_t armLift = armsWide ? 23 : 35;
  frameBuffer.fillEllipse(centerX, torsoY + 40, 26, 7, 0x0841);
  frameBuffer.fillRoundRect(centerX - 22, torsoY, 44, 55, 9, uniformBlue);
  frameBuffer.fillRect(centerX - 18, torsoY + 5, 7, 43, uniformLight);
  frameBuffer.drawFastVLine(centerX, torsoY + 5, 47, 0x10D3);
  frameBuffer.fillRect(centerX + 9, torsoY + 12, 8, 9, badgeGold);
  frameBuffer.fillTriangle(centerX - 8, torsoY + 2, centerX + 1,
                           torsoY + 21, centerX + 8, torsoY + 2,
                           TFT_WHITE);
  frameBuffer.fillCircle(centerX, headY, 19, zombieGreen);
  frameBuffer.fillRoundRect(centerX - 21, headY - 19, 42, 10, 4, uniformBlue);
  frameBuffer.fillRect(centerX - 25, headY - 11, 50, 6, uniformBlue);
  frameBuffer.fillRect(centerX - 4, headY - 17, 8, 4, badgeGold);
  frameBuffer.fillCircle(centerX - 7, headY + 1, 2, TFT_RED);
  frameBuffer.fillCircle(centerX + 7, headY + 1, 2, TFT_RED);
  frameBuffer.drawFastHLine(centerX - 5, headY + 10, 10, zombieDark);

  frameBuffer.drawLine(centerX - 18, torsoY + 11,
                       centerX - armSpread, torsoY - armLift, zombieGreen);
  frameBuffer.drawLine(centerX - armSpread, torsoY - armLift,
                       centerX - armSpread - 8, torsoY - armLift - 12,
                       zombieGreen);
  frameBuffer.fillCircle(centerX - armSpread - 8,
                         torsoY - armLift - 12, 5, zombieGreen);
  frameBuffer.drawLine(centerX + 18, torsoY + 11,
                       centerX + armSpread, torsoY - armLift, zombieGreen);
  frameBuffer.drawLine(centerX + armSpread, torsoY - armLift,
                       centerX + armSpread + 8, torsoY - armLift - 12,
                       zombieGreen);
  frameBuffer.fillCircle(centerX + armSpread + 8,
                         torsoY - armLift - 12, 5, zombieGreen);

  frameBuffer.drawLine(centerX - 10, torsoY + 49,
                       centerX - 17, frameBuffer.height() - 17 + bob,
                       uniformBlue);
  frameBuffer.drawLine(centerX + 10, torsoY + 49,
                       centerX + 17, frameBuffer.height() - 17 + bob,
                       uniformBlue);
  frameBuffer.drawFastHLine(centerX - 24, frameBuffer.height() - 15 + bob,
                            12, 0x0841);
  frameBuffer.drawFastHLine(centerX + 12, frameBuffer.height() - 15 + bob,
                            12, 0x0841);
  frameBuffer.fillCircle(centerX - 29, headY - 27, 3, TFT_YELLOW);
  frameBuffer.fillCircle(centerX + 30, headY - 17, 3, TFT_ORANGE);
  frameBuffer.fillCircle(centerX + 1, headY - 35, 2, TFT_CYAN);
}

void drawGameOver(uint32_t now) {
  const int16_t width = display.width();
  const int16_t height = display.height();
  const uint32_t accuracy =
      shotsFired == 0 ? 0 : min<uint32_t>(100, zombieShotsHit * 100 / shotsFired);
  frameBuffer.fillSprite(0x0841);
  for (int16_t y = 0; y < height; y += 12) {
    frameBuffer.drawFastHLine(0, y, width, (y / 12) % 2 == 0 ? 0x1082 : 0x0861);
  }
  frameBuffer.fillRect(0, 0, width, 27, 0x8000);
  frameBuffer.drawFastHLine(0, 27, width, TFT_RED);
  frameBuffer.drawRect(8, 36, width - 16, height - 45, 0xA145);
  frameBuffer.drawRect(11, 39, width - 22, height - 51, 0x4208);
  drawCelebratingZombie(now);
  frameBuffer.setTextColor(TFT_RED);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(19, 48);
  frameBuffer.print("GAME OVER");
  frameBuffer.setTextColor(TFT_WHITE);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(20, 78);
  frameBuffer.printf("SCORE %lu", static_cast<unsigned long>(score));
  frameBuffer.setCursor(20, 95);
  frameBuffer.printf("KILLS %lu", static_cast<unsigned long>(totalKills));
  frameBuffer.setCursor(20, 112);
  frameBuffer.printf("HEADSHOTS %lu", static_cast<unsigned long>(headshotCount));
  frameBuffer.setCursor(20, 129);
  frameBuffer.printf("BEST COMBO %lu", static_cast<unsigned long>(bestCombo));
  frameBuffer.setCursor(20, 146);
  frameBuffer.printf("ACCURACY %lu%%", static_cast<unsigned long>(accuracy));
  frameBuffer.setCursor(20, 163);
  frameBuffer.printf("SCENE %u: %s", lastSceneReached,
                     levelName(lastSceneReached - 1));
  frameBuffer.setCursor(20, 180);
  frameBuffer.printf("BEST %lu", static_cast<unsigned long>(highScore));
  frameBuffer.fillRoundRect(16, 198, 137, 29, 8, TFT_DARKGREEN);
  frameBuffer.drawRoundRect(16, 198, 137, 29, 8, TFT_WHITE);
  frameBuffer.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  frameBuffer.setCursor(56, 209);
  frameBuffer.print("PLAY AGAIN");
  frameBuffer.fillRoundRect(163, 198, 141, 29, 8, 0x4208);
  frameBuffer.drawRoundRect(163, 198, 141, 29, 8, TFT_WHITE);
  frameBuffer.setTextColor(TFT_WHITE, 0x4208);
  frameBuffer.setCursor(207, 209);
  frameBuffer.print("MAIN MENU");
  frameBuffer.pushSprite(0, 0);
}

void drawVictory(uint32_t now) {
  const int16_t width = frameBuffer.width();
  const int16_t height = frameBuffer.height();
  frameBuffer.fillSprite(0x0010);
  frameBuffer.fillRect(0, 0, width, 145, 0x2118);
  frameBuffer.fillRect(0, 116, width, 34, 0xFD20);
  frameBuffer.fillRect(0, 136, width, 25, 0xF9A0);

  const uint16_t fireworkColors[] = {TFT_YELLOW, TFT_CYAN, TFT_MAGENTA,
                                     TFT_ORANGE};
  const int16_t fireworkX[] = {39, 160, 279};
  const int16_t fireworkY[] = {61, 43, 57};
  constexpr int8_t rayX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  constexpr int8_t rayY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
  const uint8_t phase = (now / 95) % 18;
  const int16_t radius = 5 + (phase < 9 ? phase : 18 - phase);
  for (uint8_t burst = 0; burst < 3; ++burst) {
    const int16_t x = fireworkX[burst];
    const int16_t y = fireworkY[burst];
    for (int8_t ray = 0; ray < 8; ++ray) {
      frameBuffer.drawLine(x + rayX[ray] * 3, y + rayY[ray] * 3,
                           x + rayX[ray] * radius,
                           y + rayY[ray] * radius,
                           fireworkColors[(burst + phase / 4) % 4]);
    }
    frameBuffer.fillCircle(x, y, 2, TFT_WHITE);
  }

  frameBuffer.fillRect(79, 84, 162, 57, 0xBDF7);
  frameBuffer.fillTriangle(66, 85, 160, 48, 254, 85, 0xA145);
  frameBuffer.fillRect(63, 82, 194, 7, 0xFEA0);
  frameBuffer.fillRect(92, 54, 12, 30, 0xD69A);
  frameBuffer.fillRect(216, 54, 12, 30, 0xD69A);
  frameBuffer.fillTriangle(144, 70, 160, 55, 176, 70, 0xD69A);
  frameBuffer.fillCircle(160, 73, 8, 0x0010);
  frameBuffer.fillCircle(160, 73, 6, TFT_WHITE);
  for (int16_t x = 88; x <= 232; x += 24) {
    frameBuffer.fillRect(x, 96, 13, 33, 0x7BEF);
    frameBuffer.fillRect(x + 3, 99, 7, 27, 0x0010);
  }
  frameBuffer.fillRect(149, 111, 22, 30, 0x7A20);
  frameBuffer.fillRect(153, 115, 14, 26, 0x4208);
  frameBuffer.fillTriangle(0, 159, width / 2, 145, width, 159, 0x39E7);

  frameBuffer.fillRect(0, 171, width, height - 171, 0x0841);
  constexpr int16_t CROWD_X[] = {9, 26, 43, 62, 80, 99, 119, 139,
                                  159, 179, 199, 219, 239, 258, 277, 296, 312};
  for (uint8_t person = 0; person < sizeof(CROWD_X) / sizeof(CROWD_X[0]);
       ++person) {
    const int16_t x = CROWD_X[person];
    const int16_t headY = 178 + (person % 3) * 4;
    const int16_t bodyTop = headY + 7;
    const uint16_t shirt = fireworkColors[person % 4];
    const int8_t armPose = ((now / 260 + person) % 3 == 0) ? -1 : 1;
    frameBuffer.drawLine(x - 4, bodyTop + 7, x - 9,
                         bodyTop + (armPose < 0 ? -4 : 11), shirt);
    frameBuffer.drawLine(x + 4, bodyTop + 7, x + 9,
                         bodyTop + (armPose > 0 ? -4 : 11), shirt);
    frameBuffer.fillCircle(x, headY, 5, 0xD69A);
    frameBuffer.fillRoundRect(x - 6, bodyTop, 12, 24, 3, shirt);
  }

  frameBuffer.fillRoundRect(18, 17, width - 36, 31, 6, 0x0000);
  frameBuffer.drawRoundRect(18, 17, width - 36, 31, 6, badgeGold);
  frameBuffer.setTextColor(TFT_YELLOW);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(62, 25);
  frameBuffer.print("CITY SAVED!");
  frameBuffer.fillRoundRect(80, height - 38, width - 160, 29, 7, 0x4208);
  frameBuffer.drawRoundRect(80, height - 38, width - 160, 29, 7, TFT_WHITE);
  frameBuffer.setTextColor(TFT_WHITE, 0x4208);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(116, height - 28);
  frameBuffer.print("MAIN MENU");
  frameBuffer.pushSprite(0, 0);
}

void startGame() {
  startMusic(MusicTrack::None, millis());
  score = 0;
  lives = startingLives();
  ammo = MAGAZINE_SIZE;
  wave = 1;
  waveKills = 0;
  totalKills = 0;
  sceneKills = 0;
  shotsFired = 0;
  zombieShotsHit = 0;
  headshotCount = 0;
  resetCombo();
  bestCombo = 0;
  lastSceneReached = 1;
  objectiveProgress = 0;
  objectiveCompleted = false;
  bossEncounterSpawned = false;
  const uint32_t now = millis();
  for (Zombie &zombie : zombies) {
    zombie.active = false;
  }
  for (HitEffect &effect : hitEffects) {
    effect.active = false;
  }
  for (SceneryDamage &damage : sceneryDamage) {
    damage.active = false;
  }
  for (EnemyShot &shot : enemyShots) {
    shot.active = false;
  }
  for (Civilian &civilian : civilians) {
    civilian.active = false;
  }
  nextSceneryDamage = 0;
  sceneryDamageLevel = 0;
  destroyedPropsLevel = UINT8_MAX;
  displayedLevel = 0;
  transitionLevel = 0;
  levelTransitionActive = false;
  playerHitUntil = 0;
  nextSpawnAt = now + 700;
  nextCivilianAt = now + 1200;
  screen = Screen::Playing;
}

void chooseZombieEntry(uint8_t level, int16_t &x, int16_t &y,
                       bool windowOnly = false) {
  uint8_t entryCount = 0;
  for (const SceneProp &prop : SCENE_PROPS) {
    if (prop.scene == level && prop.type == PropType::Window) {
      ++entryCount;
    }
  }
  if (!windowOnly) {
    for (const SceneDoor &door : SCENE_DOORS) {
      if (door.scene == level) {
        ++entryCount;
      }
    }
  }

  uint8_t selected = random(0, entryCount);
  for (const SceneProp &prop : SCENE_PROPS) {
    if (prop.scene != level || prop.type != PropType::Window) {
      continue;
    }
    if (selected-- == 0) {
      x = prop.x + prop.width / 2;
      y = prop.y + prop.height;
      return;
    }
  }
  if (!windowOnly) {
    for (const SceneDoor &door : SCENE_DOORS) {
      if (door.scene != level) {
        continue;
      }
      if (selected-- == 0) {
        x = door.x + door.width / 2;
        y = door.y + door.height;
        return;
      }
    }
  }
  x = frameBuffer.width() / 2;
  y = 154;
}

void spawnZombie(uint32_t now) {
  for (Zombie &zombie : zombies) {
    if (zombie.active) {
      continue;
    }
    zombie.lane = random(0, 3);
    const uint8_t roll = random(0, 12);
    if (difficulty == Difficulty::Hard) {
      zombie.kind = roll < 8 ? ZombieKind::Police :
                    roll == 8 ? ZombieKind::Suit :
                    roll == 9 ? ZombieKind::Dress :
                    roll == 10 ? ZombieKind::Brute : ZombieKind::Runner;
    } else {
      zombie.kind = roll < 4 ? ZombieKind::Police :
                    roll < 6 ? ZombieKind::Suit :
                    roll < 8 ? ZombieKind::Dress :
                    roll == 8 ? ZombieKind::Brute :
                    roll == 9 ? ZombieKind::Runner : ZombieKind::Peeker;
    }
    chooseZombieEntry(currentLevel(), zombie.entryX, zombie.entryY,
                      zombie.kind == ZombieKind::Peeker);
    zombie.x = zombie.entryX;
    zombie.y = zombie.entryY;
    zombie.spawnedAt = now;
    zombie.animation = static_cast<ZombieAnimation>(random(0, 3));
    zombie.nextShotAt =
        zombie.kind == ZombieKind::Police
            ? now + (difficulty == Difficulty::Hard
                         ? 1000
                         : ZOMBIE_EMERGE_MS + random(650, 1801))
            : UINT32_MAX;
    zombie.torsoHits = 0;
    zombie.headHits = 0;
    zombie.hasHat = zombie.kind == ZombieKind::Police;
    zombie.active = true;
    return;
  }
}

void spawnBoss(uint32_t now) {
  for (Zombie &zombie : zombies) {
    if (zombie.active) {
      continue;
    }
    zombie.lane = 1;
    chooseZombieEntry(currentLevel(), zombie.entryX, zombie.entryY);
    zombie.x = zombie.entryX;
    zombie.y = zombie.entryY;
    zombie.spawnedAt = now;
    zombie.nextShotAt = now + 2200;
    zombie.torsoHits = 0;
    zombie.headHits = 0;
    zombie.animation = ZombieAnimation::Reach;
    zombie.kind = ZombieKind::Boss;
    zombie.hasHat = false;
    zombie.active = true;
    bossEncounterSpawned = true;
    playTone(220, 280);
    return;
  }
}

void endGame() {
  screen = Screen::GameOver;
  staticScreenDirty = true;
  startMusic(MusicTrack::GameOver, millis());
  if (score > highScore) {
    highScore = score;
    preferences.putULong("highScore", highScore);
  }
}

void endVictory() {
  screen = Screen::Victory;
  staticScreenDirty = true;
  startMusic(MusicTrack::Victory, millis());
}

void returnToMainMenu() {
  screen = Screen::Menu;
  startMusic(MusicTrack::Menu, millis());
  staticScreenDirty = true;
}

void updateZombies(uint32_t now) {
  for (EnemyShot &shot : enemyShots) {
    if (!shot.active || now - shot.startedAt < ENEMY_SHOT_TRAVEL_MS) {
      continue;
    }
    shot.active = false;
    if (lives > 0) {
      --lives;
    }
    resetCombo();
    playerHitUntil = now + 320;
    playTone(240, 180);
    if (lives == 0) {
      endGame();
      return;
    }
  }

  const uint32_t baseSpawnInterval =
      max<uint32_t>(850, 1900U - min<uint32_t>(wave * 70U, 850U));
  const uint32_t spawnInterval =
      difficulty == Difficulty::Easy ? baseSpawnInterval * 3 / 2
                                     : baseSpawnInterval;
  if (sceneKills == LEVEL_HIT_THRESHOLD - 1) {
    if (!bossEncounterSpawned) {
      spawnBoss(now);
    }
  } else if (now >= nextSpawnAt) {
    spawnZombie(now);
    nextSpawnAt = now + spawnInterval;
  }

  for (Zombie &zombie : zombies) {
    if (!zombie.active) {
      continue;
    }
    const uint32_t age = now - zombie.spawnedAt;
    const uint16_t reachTime = zombieReachTime(zombie.kind);
    const uint32_t approachAge =
        age > ZOMBIE_EMERGE_MS ? age - ZOMBIE_EMERGE_MS : 0;
    const float emergeProgress =
        min<uint32_t>(age, ZOMBIE_EMERGE_MS) /
        static_cast<float>(ZOMBIE_EMERGE_MS);
    const float approachProgress =
        min<uint32_t>(approachAge, reachTime - ZOMBIE_EMERGE_MS) /
        static_cast<float>(reachTime - ZOMBIE_EMERGE_MS);
    if (zombie.kind == ZombieKind::Peeker && age < 850) {
      zombie.x = zombie.entryX;
      zombie.y = zombie.entryY;
    } else if (age < ZOMBIE_EMERGE_MS) {
      zombie.x = zombie.entryX +
                 (POLICE_LANE_X[zombie.lane] - zombie.entryX) *
                     emergeProgress;
      zombie.y = zombie.entryY + (154 - zombie.entryY) * emergeProgress;
    } else {
      zombie.y = 154 + approachProgress * 72;
      const float laneConvergence = approachProgress * 0.38f;
      zombie.x = 160 + (POLICE_LANE_X[zombie.lane] - 160) *
                           (1.0f - laneConvergence);
    }

    if ((zombie.kind == ZombieKind::Police ||
         zombie.kind == ZombieKind::Boss) &&
        static_cast<int32_t>(now - zombie.nextShotAt) >= 0) {
      bool shotQueued = false;
      for (EnemyShot &shot : enemyShots) {
        if (shot.active) {
          continue;
        }
        shot = {static_cast<int16_t>(zombie.x + 12),
                static_cast<int16_t>(zombie.y - 5), now, true};
        shotQueued = true;
        break;
      }
      if (!shotQueued) {
        Serial.println("ERROR: No slot available for enemy shot.");
        continue;
      }
      zombie.nextShotAt =
          now + (zombie.kind == ZombieKind::Boss ? random(1800, 3201)
                                                  : random(1000, 3001));
      playTone(680, 65);
    }

    if (age >= reachTime) {
      zombie.active = false;
      if (zombie.kind == ZombieKind::Boss) {
        bossEncounterSpawned = false;
      }
      if (lives > 0) {
        --lives;
        playTone(270, 120);
      }
      resetCombo();
      if (lives == 0) {
        endGame();
        return;
      }
    }
  }
}

void shootAt(int16_t x, int16_t y) {
  if (ammo == 0) {
    return;
  }
  --ammo;
  ++shotsFired;
  playTone(1120, 35);
  for (Civilian &civilian : civilians) {
    if (!civilian.active) {
      continue;
    }
    const bool animal = civilian.type == CivilianType::Dog ||
                        civilian.type == CivilianType::Cat;
    const bool child = civilian.type == CivilianType::Child;
    const int16_t radiusX = animal ? 17 : child ? 11 : 15;
    const int16_t topOffset = animal ? 11 : child ? 27 : 36;
    const int16_t bottomOffset = animal ? 11 : 16;
    if (abs(x - static_cast<int16_t>(civilian.x)) > radiusX ||
        y < civilian.y - topOffset || y > civilian.y + bottomOffset) {
      continue;
    }

    civilian.active = false;
    if (lives > 0) {
      --lives;
    }
    resetCombo();
    playerHitUntil = millis() + 420;
    addHitEffect(static_cast<int16_t>(civilian.x),
                 static_cast<int16_t>(civilian.y), false, millis());
    playTone(220, 180);
    if (lives == 0) {
      endGame();
    }
    return;
  }

  for (Zombie &zombie : zombies) {
    if (!zombie.active) {
      continue;
    }
    const uint32_t age = millis() - zombie.spawnedAt;
    const int16_t radius = zombie.kind == ZombieKind::Boss
                               ? min<int16_t>(32, 18 + age / 400)
                           : zombie.kind == ZombieKind::Brute
                               ? min<int16_t>(29, 13 + age / 380)
                           : zombie.kind == ZombieKind::Runner
                               ? min<int16_t>(21, 8 + age / 450)
                               : min<int16_t>(24, 9 + age / 420);
    const int16_t dx = x - static_cast<int16_t>(zombie.x);
    const int16_t dy = y - static_cast<int16_t>(zombie.y);
    const int16_t headY = static_cast<int16_t>(zombie.y) - radius;
    const int16_t bob = (millis() / 210 + zombie.lane) % 2 == 0 ? 0 : 2;
    const int16_t headRadius = radius / 2 + 2;
    const int16_t capY = headY - headRadius + bob;
    if (zombie.hasHat && abs(x - static_cast<int16_t>(zombie.x)) <= headRadius + 3 &&
        y >= capY - 4 && y <= capY + 8) {
      zombie.hasHat = false;
      addHitEffect(static_cast<int16_t>(zombie.x), capY, false, millis(), true);
      playTone(740, 75);
      return;
    }

    const int16_t hitRadius = radius + 9;
    if (dx * dx + dy * dy > hitRadius * hitRadius) {
      continue;
    }

    const int16_t headDx = x - static_cast<int16_t>(zombie.x);
    const int16_t headDy = y - headY;
    const int16_t headshotRadius = radius / 2 + 7;
    const bool headshot =
        headDx * headDx + headDy * headDy <= headshotRadius * headshotRadius;
    const int16_t impactX = static_cast<int16_t>(zombie.x);
    const int16_t impactY = headshot ? headY : static_cast<int16_t>(zombie.y);

    if (zombie.kind == ZombieKind::Boss && headshot) {
      ++zombie.headHits;
      ++zombieShotsHit;
      ++headshotCount;
      if (zombie.headHits < BOSS_HEAD_HITS) {
        addHitEffect(impactX, impactY, true, millis());
        playTone(1680, 115);
        return;
      }
    } else if (zombie.kind == ZombieKind::Boss) {
      const bool weakPointOpen = bossWeakpointIsOpen(age);
      const int16_t weakDx = x - static_cast<int16_t>(zombie.x);
      const int16_t weakDy = y - (static_cast<int16_t>(zombie.y) + 1);
      if (!weakPointOpen || weakDx * weakDx + weakDy * weakDy > 11 * 11) {
        resetCombo();
        playTone(260, 70);
        return;
      }
      ++zombie.torsoHits;
      ++zombieShotsHit;
      if (zombie.torsoHits < zombieTorsoHealth(zombie.kind)) {
        addHitEffect(static_cast<int16_t>(zombie.x),
                     static_cast<int16_t>(zombie.y), false, millis());
        playTone(840, 75);
        return;
      }
    }

    if (zombie.kind != ZombieKind::Boss && !headshot) {
      ++zombie.torsoHits;
      ++zombieShotsHit;
      addHitEffect(impactX, impactY, false, millis());
      if (zombie.torsoHits < zombieTorsoHealth(zombie.kind)) {
        playTone(470, 85);
        return;
      }
    } else if (zombie.kind != ZombieKind::Boss) {
      ++zombieShotsHit;
    }

    if (headshot && zombie.kind != ZombieKind::Boss) {
      ++headshotCount;
    }

    addHitEffect(impactX, impactY,
                 headshot, millis());
    playTone(headshot ? 1680 : 470, headshot ? 115 : 85);
    zombie.active = false;
    ++totalKills;
    ++sceneKills;
    ++waveKills;
    lastSceneReached = min<uint32_t>(
        LEVEL_COUNT, totalKills / LEVEL_HIT_THRESHOLD + 1);
    ++currentCombo;
    ++waveCombo;
    bestCombo = max<uint32_t>(bestCombo, currentCombo);
    awardPoints(100 + (headshot && zombie.kind != ZombieKind::Boss ? 50 : 0) +
                min<uint32_t>(currentCombo > 1 ? (currentCombo - 1) * 10 : 0,
                              100));
    updateWaveObjective(headshot && zombie.kind != ZombieKind::Boss);
    if (waveKills >= KILLS_PER_WAVE) {
      if (wave < UINT8_MAX) {
        ++wave;
      }
      waveKills = 0;
      waveCombo = 0;
      objectiveProgress = 0;
      objectiveCompleted = false;
    }
    if (totalKills >= LEVEL_COUNT * LEVEL_HIT_THRESHOLD) {
      endVictory();
      return;
    }
    if (sceneKills >= LEVEL_HIT_THRESHOLD) {
      sceneKills = 0;
      bossEncounterSpawned = false;
      lastSceneReached =
          max<uint8_t>(lastSceneReached, currentLevel() + 1);
      for (Zombie &remaining : zombies) {
        remaining.active = false;
      }
      for (EnemyShot &shot : enemyShots) {
        shot.active = false;
      }
      nextSpawnAt = millis() + 700;
    }
    const uint8_t newLevel = currentLevel();
    if (newLevel != displayedLevel) {
      displayedLevel = newLevel;
      transitionLevel = newLevel;
      levelTransitionStartedAt = millis();
      levelTransitionActive = true;
    }
    return;
  }

  resetCombo();
  if (!damageSceneProp(currentLevel(), x, y)) {
    addSceneryDamage(x, y);
    playTone(760, 25);
  }
}

void handleTouch() {
  const bool isTouching = touch.touched();
  if (isTouching && !touchWasDown) {
    const TS_Point point = touch.getPoint();
    const int16_t x = mapTouchCoordinate(point.x, calibration.xLeft,
                                         calibration.xRight, display.width());
    const int16_t y = mapTouchCoordinate(point.y, calibration.yTop,
                                         calibration.yBottom, display.height());
    if (screen == Screen::Menu) {
      if (x >= 101 && x <= 128 && y >= 133 && y <= 160) {
        volume = volume >= VOLUME_STEP ? volume - VOLUME_STEP : 0;
        preferences.putUChar("volume", volume);
        applySpeakerVolume();
        playTone(698, 55);
        staticScreenDirty = true;
      } else if (x >= 192 && x <= 219 && y >= 133 && y <= 160) {
        volume = min<uint8_t>(100, volume + VOLUME_STEP);
        preferences.putUChar("volume", volume);
        applySpeakerVolume();
        playTone(988, 55);
        staticScreenDirty = true;
      } else if (x >= 99 && x <= 221 && y >= 161 && y <= 188) {
        switch (difficulty) {
          case Difficulty::Easy:
            difficulty = Difficulty::Medium;
            break;
          case Difficulty::Medium:
            difficulty = Difficulty::Hard;
            break;
          case Difficulty::Hard:
            difficulty = Difficulty::Easy;
            break;
        }
        preferences.putUChar("difficulty",
                             static_cast<uint8_t>(difficulty));
        preferences.putUChar("diffVer", 1);
        staticScreenDirty = true;
      } else if (x >= 99 && x <= 221 && y >= 195 && y <= 226) {
        startGame();
      }
    } else if (screen == Screen::Playing) {
      if (isReloadButtonHit(x, y)) {
        ammo = MAGAZINE_SIZE;
        playReloadSound();
      } else {
        shootAt(x, y);
      }
    } else if (screen == Screen::GameOver && y >= 198 && y <= 227) {
      if (x >= 16 && x <= 153) {
        startGame();
      } else if (x >= 163 && x <= 304) {
        returnToMainMenu();
      }
    } else if (screen == Screen::Victory &&
               x >= 80 && x <= 240 &&
               y >= display.height() - 38 && y <= display.height() - 9) {
      returnToMainMenu();
    }
  }
  touchWasDown = isTouching;
}

} // namespace

void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(1);
  display.fillScreen(TFT_BLACK);
  frameBuffer.setColorDepth(8);
  if (!frameBuffer.createSprite(display.width(), display.height())) {
    Serial.printf("ERROR: Could not allocate %u-byte display frame buffer; "
                  "%u bytes of heap remain.\n",
                  static_cast<unsigned>(display.width() * display.height()),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    display.fillScreen(TFT_RED);
    display.setTextColor(TFT_WHITE, TFT_RED);
    display.setTextSize(1);
    display.setCursor(8, 8);
    display.println("Display buffer allocation failed");
    while (true) {
      delay(1000);
    }
  }

  touchSPI.begin(25, 39, 32, 33);
  if (!touch.begin(touchSPI)) {
    Serial.println("ERROR: XPT2046 touch controller initialization failed.");
  }
  touch.setRotation(1);

  preferences.begin("cyd-death", false);
  highScore = preferences.getULong("highScore", 0);
  volume = min<uint8_t>(100, preferences.getUChar("volume", 70));
  if (preferences.getUChar("diffVer", 0) == 0) {
    const uint8_t previousDifficulty = preferences.getUChar("difficulty", 0);
    difficulty = !preferences.isKey("difficulty")
                     ? Difficulty::Easy
                 : previousDifficulty ==
                           static_cast<uint8_t>(Difficulty::Hard)
                     ? Difficulty::Hard
                     : Difficulty::Medium;
    preferences.putUChar("difficulty",
                         static_cast<uint8_t>(difficulty));
    preferences.putUChar("diffVer", 1);
  } else {
    const uint8_t savedDifficulty = preferences.getUChar("difficulty", 0);
    switch (savedDifficulty) {
      case static_cast<uint8_t>(Difficulty::Medium):
        difficulty = Difficulty::Medium;
        break;
      case static_cast<uint8_t>(Difficulty::Hard):
        difficulty = Difficulty::Hard;
        break;
      default:
        difficulty = Difficulty::Easy;
        break;
    }
  }
  if (ledcSetup(BUZZER_CHANNEL, 2000, 10) == 0) {
    Serial.println("ERROR: Buzzer PWM initialization failed.");
  } else {
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    speakerReady = true;
    ledcWriteTone(BUZZER_CHANNEL, 0);
  }

  if (preferences.getBytesLength("touch") == sizeof(calibration)) {
    preferences.getBytes("touch", &calibration, sizeof(calibration));
  } else {
    calibrateTouch();
  }

  randomSeed(esp_random());
  menuCarLastMovedAt = millis();
  menuCarNextPassAt = menuCarLastMovedAt + random(8000, 20001);
  menuCatX = random(24, display.width() - 24);
  menuCatLastMovedAt = millis();
  chooseMenuCatDestination(menuCatLastMovedAt);
  screen = Screen::Menu;
  startMusic(MusicTrack::Menu, millis());
}

void loop() {
  const uint32_t now = millis();
  updateSpeaker(now);
  updateMusic(now);
  handleTouch();

  if (screen == Screen::Playing) {
    updateCivilians(now);
    updateZombies(now);
  }

  if (screen == Screen::Playing && now - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = now;
    drawGame(now);
  } else if (screen == Screen::GameOver &&
             (staticScreenDirty ||
              now - lastFrameAt >= GAME_OVER_FRAME_INTERVAL_MS)) {
    lastFrameAt = now;
    drawGameOver(now);
    staticScreenDirty = false;
  } else if (screen == Screen::Menu &&
             (staticScreenDirty ||
              now - lastFrameAt >= MENU_FRAME_INTERVAL_MS)) {
    lastFrameAt = now;
    drawMenu(now);
    staticScreenDirty = false;
  } else if (screen == Screen::Victory &&
             (staticScreenDirty ||
              now - lastFrameAt >= GAME_OVER_FRAME_INTERVAL_MS)) {
    lastFrameAt = now;
    drawVictory(now);
    staticScreenDirty = false;
  }
  delay(2);
}
