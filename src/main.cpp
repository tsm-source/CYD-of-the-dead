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
constexpr uint8_t STARTING_LIVES = 3;
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

enum class ZombieKind : uint8_t { Police, Suit, Dress };
enum class ZombieAnimation : uint8_t { Shamble, Lurch, Reach };

struct Zombie {
  float x;
  float y;
  int16_t entryX;
  int16_t entryY;
  uint32_t spawnedAt;
  uint32_t nextShotAt;
  uint8_t lane;
  uint8_t torsoHits;
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
uint32_t menuCatLastMovedAt = 0;
uint32_t menuCatStateUntil = 0;
uint32_t menuCarLastMovedAt = 0;
uint32_t menuCarNextPassAt = 0;
float menuCatX = 26.0f;
float menuCatTargetX = 160.0f;
float menuCarX = -150.0f;
uint8_t lives = STARTING_LIVES;
uint8_t ammo = MAGAZINE_SIZE;
uint8_t wave = 1;
uint8_t waveKills = 0;
uint8_t nextHitEffect = 0;
uint8_t nextSceneryDamage = 0;
uint8_t volume = 70;
uint8_t sceneryDamageLevel = 0;
uint16_t destroyedSceneProps = 0;
uint8_t destroyedPropsLevel = UINT8_MAX;
uint8_t displayedLevel = 0;
uint8_t transitionLevel = 0;
uint8_t musicNoteIndex = 0;
int8_t menuCatDirection = 1;
bool menuCatWalking = true;
bool menuCarPassing = false;
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
    if (door.scene != level || level == 0) {
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
    if (musicTrack == MusicTrack::Victory) {
      musicNoteIndex = 0;
    } else {
      musicTrack = MusicTrack::None;
      return;
    }
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

const char *levelName(uint8_t level) {
  constexpr const char *names[LEVEL_COUNT] = {
      "ALLEY", "POLICE STATION", "BALLROOM", "BACKYARD", "TAVERN"};
  return names[level % LEVEL_COUNT];
}

uint8_t currentLevel() {
  return static_cast<uint8_t>((score / LEVEL_HIT_THRESHOLD) % LEVEL_COUNT);
}

void drawPoliceStation(uint32_t now) {
  const int16_t floorY = 113;
  frameBuffer.fillScreen(0x18E3);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 94, wallColor);
  frameBuffer.fillRect(0, 32, frameBuffer.width(), 2, 0x5AEB);
  frameBuffer.fillRect(73, 35, 3, 66, 0x2945);
  frameBuffer.fillRect(243, 35, 3, 66, 0x2945);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 7, wallShadow);
  frameBuffer.fillRect(0, 103, frameBuffer.width(), 10, wallShadow);
  frameBuffer.fillRect(16, 39, 56, 43, 0x10E3);
  frameBuffer.drawRect(16, 39, 56, 43, deskEdge);
  frameBuffer.drawFastVLine(44, 40, 41, deskEdge);
  frameBuffer.drawFastHLine(17, 60, 54, deskEdge);
  frameBuffer.fillRect(79, 39, 162, 43, 0x2128);
  frameBuffer.drawRect(79, 39, 162, 43, deskEdge);
  frameBuffer.setTextColor(TFT_WHITE);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(114, 48);
  frameBuffer.print("POLICE");
  frameBuffer.setCursor(101, 65);
  frameBuffer.print("CENTRAL DESK");

  frameBuffer.fillRect(251, 38, 53, 57, wallShadow);
  frameBuffer.fillRect(257, 45, 41, 49, 0x3186);
  frameBuffer.fillRect(266, 53, 25, 25, 0x52AA);
  frameBuffer.drawRect(266, 53, 25, 25, deskEdge);
  frameBuffer.fillRect(278, 79, 3, 4, badgeGold);
  frameBuffer.fillRect(10, 87, 55, 15, wallShadow);
  frameBuffer.fillRect(14, 90, 47, 4, 0xA514);
  frameBuffer.fillRect(14, 96, 34, 3, 0xD69A);
  frameBuffer.fillRect(6, 107, 51, 5, 0x39E7);
  frameBuffer.drawFastHLine(0, 122, frameBuffer.width(), 0x4208);

  frameBuffer.fillRect(0, floorY, frameBuffer.width(),
                       frameBuffer.height() - floorY, 0x2104);
  frameBuffer.fillTriangle(0, floorY, frameBuffer.width() / 2, 130,
                           frameBuffer.width(), floorY, 0x39E7);
  for (int16_t x = 0; x < frameBuffer.width(); x += 40) {
    frameBuffer.drawLine(x, frameBuffer.height() - 1,
                         frameBuffer.width() / 2, floorY, 0x3186);
  }
  for (int16_t y = 151; y < frameBuffer.height(); y += 27) {
    frameBuffer.drawFastHLine(0, y, frameBuffer.width(), 0x3186);
    frameBuffer.drawFastHLine(5, y + 1, frameBuffer.width() - 10, 0x2945);
  }

  drawDesk(POLICE_LANE_X[0], 125, 61, 27);
  drawDesk(POLICE_LANE_X[1], 116, 75, 34);
  drawDesk(POLICE_LANE_X[2], 125, 61, 27);

  const bool redBeacon = (now / 260) % 2 == 0;
  frameBuffer.fillRect(129, 28, 28, 4, redBeacon ? TFT_RED : TFT_BLUE);
  frameBuffer.fillRect(163, 28, 28, 4, redBeacon ? TFT_BLUE : TFT_RED);
}

void drawAlley() {
  frameBuffer.fillScreen(0x10A3);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 92, 0x2128);
  frameBuffer.fillRect(0, 25, 72, 112, 0x4208);
  frameBuffer.fillRect(247, 25, 73, 112, 0x3186);
  for (int16_t y = 37; y < 139; y += 14) {
    frameBuffer.drawFastHLine(0, y, 71, 0x5AEB);
    frameBuffer.drawFastHLine(248, y, 72, 0x4A49);
    const int16_t offset = (y / 14) % 2 == 0 ? 0 : 17;
    for (int16_t x = offset; x < 72; x += 34) {
      frameBuffer.drawFastVLine(x, y, 14, 0x5AEB);
    }
    for (int16_t x = 247 + offset; x < 320; x += 34) {
      frameBuffer.drawFastVLine(x, y, 14, 0x4A49);
    }
  }
  for (int16_t y = 42; y < 105; y += 27) {
    frameBuffer.fillRect(13, y, 21, 12, 0xFEA0);
    frameBuffer.fillRect(271, y, 28, 12, 0xFEA0);
  }
  frameBuffer.fillRect(80, 63, 39, 51, 0x18E3);
  frameBuffer.drawRect(80, 63, 39, 51, 0x8410);
  frameBuffer.fillRect(200, 55, 34, 59, 0x1082);
  frameBuffer.drawRect(200, 55, 34, 59, 0x5AEB);
  frameBuffer.fillRect(0, 115, frameBuffer.width(),
                       frameBuffer.height() - 115, 0x2945);
  frameBuffer.fillEllipse(151, 191, 27, 6, 0x1082);
  frameBuffer.drawEllipse(151, 191, 27, 6, 0x5AEB);
  frameBuffer.drawFastVLine(151, 186, 10, 0x5AEB);
  for (int16_t x = 0; x <= frameBuffer.width(); x += 40) {
    frameBuffer.drawLine(x, frameBuffer.height() - 1,
                         frameBuffer.width() / 2, 116, 0x4A49);
  }
  frameBuffer.fillRect(15, 166, 31, 28, 0x3186);
  frameBuffer.fillRect(19, 158, 23, 10, 0x4A49);
  frameBuffer.drawRect(15, 166, 31, 28, 0x7BEF);
  frameBuffer.fillRect(270, 153, 32, 38, 0x4208);
  frameBuffer.fillRect(267, 147, 38, 8, 0x5AEB);
}

void drawBallroom() {
  frameBuffer.fillScreen(0x5AEB);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 98, 0x7BEF);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 10, 0xD69A);
  frameBuffer.fillRect(0, 35, frameBuffer.width(), 5, 0xA514);
  frameBuffer.fillRect(0, 115, frameBuffer.width(), 8, 0xD69A);
  for (int16_t x = 0; x < frameBuffer.width(); x += 32) {
    frameBuffer.drawFastVLine(x, 42, 65, 0x6B4D);
    frameBuffer.drawFastVLine(x + 2, 43, 63, 0x9CF3);
  }
  frameBuffer.fillTriangle(0, 123, frameBuffer.width() / 2, 88,
                           frameBuffer.width(), 123, 0x39E7);
  frameBuffer.fillRect(0, 123, frameBuffer.width(),
                       frameBuffer.height() - 123, 0x6B4D);
  frameBuffer.fillTriangle(98, 123, frameBuffer.width() / 2, 123,
                           233, frameBuffer.height(), 0xA514);
  for (int16_t x : {18, 82, 238, 302}) {
    frameBuffer.fillRect(x, 43, 18, 104, 0xD69A);
    frameBuffer.fillTriangle(x - 5, 48, x + 9, 29, x + 23, 48, 0xFEA0);
    frameBuffer.fillRect(x - 3, 140, 24, 8, 0xFEA0);
  }
  frameBuffer.drawLine(147, 25, 147, 51, deskEdge);
  frameBuffer.drawLine(173, 25, 173, 51, deskEdge);
  frameBuffer.fillCircle(160, 62, 12, 0xFEA0);
  frameBuffer.fillCircle(145, 55, 5, TFT_YELLOW);
  frameBuffer.fillCircle(175, 55, 5, TFT_YELLOW);
  frameBuffer.fillCircle(160, 45, 5, TFT_YELLOW);
}

void drawBackyard() {
  frameBuffer.fillScreen(0x0843);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 105, 0x10A3);
  frameBuffer.fillCircle(274, 57, 19, 0xFFDF);
  frameBuffer.fillCircle(281, 51, 19, 0x10A3);
  frameBuffer.fillRect(0, 100, frameBuffer.width(), 49, 0x31A6);
  for (int16_t x = 0; x < frameBuffer.width(); x += 24) {
    frameBuffer.fillRect(x, 104, 6, 46, 0x7BEF);
  }
  frameBuffer.fillRect(0, 119, frameBuffer.width(), 6, 0x9CF3);
  frameBuffer.fillRect(0, 142, frameBuffer.width(), 6, 0x9CF3);
  frameBuffer.fillRect(0, 149, frameBuffer.width(),
                       frameBuffer.height() - 149, 0x1A64);
  frameBuffer.fillRect(0, 150, frameBuffer.width(), 3, 0x2E85);
  for (int16_t x = 0; x < frameBuffer.width(); x += 31) {
    frameBuffer.drawFastHLine(x, 169 + (x % 3) * 13, 14, 0x2E85);
    frameBuffer.fillTriangle(x, 148, x + 14, 148, x + 7, 158, 0x07E0);
  }
  frameBuffer.fillRect(21, 177, 42, 32, 0xA514);
  frameBuffer.drawRect(21, 177, 42, 32, 0xFEA0);
  frameBuffer.fillRect(263, 168, 35, 42, 0x8A43);
  frameBuffer.drawRect(263, 168, 35, 42, 0xD69A);
}

void drawTavern() {
  frameBuffer.fillScreen(0x2945);
  frameBuffer.fillRect(0, 25, frameBuffer.width(), 99, 0x4208);
  for (int16_t y = 35; y < 111; y += 19) {
    frameBuffer.drawFastHLine(0, y, frameBuffer.width(), 0x5AEB);
    for (int16_t x = (y % 2) * 22; x < frameBuffer.width(); x += 44) {
      frameBuffer.drawFastVLine(x, y, 19, 0x5AEB);
    }
  }
  frameBuffer.fillRect(26, 44, 110, 33, 0x1082);
  frameBuffer.drawRect(26, 44, 110, 33, 0xFEA0);
  frameBuffer.setTextColor(TFT_ORANGE);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(44, 52);
  frameBuffer.print("TAVERN");
  for (int16_t x : {159, 197, 235}) {
    frameBuffer.fillRect(x, 42, 31, 4, deskEdge);
    frameBuffer.fillRect(x + 3, 46, 25, 2, 0xA514);
  }
  frameBuffer.fillRect(0, 108, frameBuffer.width(), 18, 0x7A20);
  frameBuffer.drawFastHLine(0, 108, frameBuffer.width(), 0xFEA0);
  frameBuffer.drawFastHLine(0, 114, frameBuffer.width(), 0xA514);
  frameBuffer.fillRect(14, 81, 292, 3, 0xD69A);
  frameBuffer.fillRect(17, 86, 286, 4, 0x7A20);
  frameBuffer.fillRect(0, 126, frameBuffer.width(),
                       frameBuffer.height() - 126, 0x2104);
  for (int16_t x : {47, 160, 273}) {
    frameBuffer.fillRect(x - 20, 146, 40, 8, 0xA514);
    frameBuffer.fillRect(x - 15, 154, 30, 30, 0x7A20);
    frameBuffer.fillRect(x - 12, 184, 5, 26, wallShadow);
    frameBuffer.fillRect(x + 7, 184, 5, 26, wallShadow);
  }
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

  frameBuffer.drawRoundRect(99, 173, 122, 39, 8, TFT_WHITE);
  frameBuffer.setTextColor(TFT_ORANGE);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(133, 183);
  frameBuffer.print("START");

  drawMenuCat(now);
  frameBuffer.pushSprite(0, 0);
}

void drawZombie(const Zombie &zombie, uint32_t now) {
  const uint32_t age = now - zombie.spawnedAt;
  const int16_t radius = min<int16_t>(24, 9 + age / 420);
  const int16_t x = static_cast<int16_t>(zombie.x);
  const int16_t y = static_cast<int16_t>(zombie.y);
  const uint16_t skin = zombie.lane == 1 ? 0x8D6A : zombieGreen;
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
  const int16_t torsoWidth = max<int16_t>(12, radius);
  const int16_t headRadius = radius / 2 + 2;
  const bool isPolice = zombie.kind == ZombieKind::Police;
  const uint16_t suitColor = zombie.lane == 1 ? 0x4208 : 0x3186;
  const uint16_t dressColor = zombie.lane == 1 ? 0xF81F : 0x8010;
  const uint16_t outfitColor = isPolice ? uniformBlue :
                               zombie.kind == ZombieKind::Suit ? suitColor :
                               dressColor;
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
  } else {
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
  if (isPolice) {
    frameBuffer.drawLine(x + torsoWidth / 2, torsoTop + 7,
                         x + torsoWidth / 2 + 8, torsoTop + 12, skin);
    frameBuffer.fillRect(x + torsoWidth / 2 + 6, torsoTop + 9, 9, 4,
                         wallShadow);
    frameBuffer.fillRect(x + torsoWidth / 2 + 12, torsoTop + 7, 4, 3,
                         deskEdge);
  }

  const int32_t shotWarning = static_cast<int32_t>(zombie.nextShotAt - now);
  if (isPolice && shotWarning > 0 &&
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
  frameBuffer.setCursor(130, 9);
  frameBuffer.printf("WAVE %u", wave);
  frameBuffer.setCursor(frameBuffer.width() - 72, 9);
  frameBuffer.printf("LIVES %u", lives);
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
  return x >= frameBuffer.width() - 120 && x < frameBuffer.width() &&
         y >= frameBuffer.height() - 44 && y <= frameBuffer.height() - 6;
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
  frameBuffer.fillRoundRect(frameBuffer.width() - 112,
                            frameBuffer.height() - 30, 104, 25, 5,
                            buttonColor);
  frameBuffer.drawRoundRect(frameBuffer.width() - 112,
                            frameBuffer.height() - 30, 104, 25, 5,
                            needsReload ? TFT_YELLOW : 0x7BEF);
  frameBuffer.setTextColor(TFT_WHITE, buttonColor);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(frameBuffer.width() - 88,
                        frameBuffer.height() - 22);
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
  frameBuffer.setCursor(19, 54);
  frameBuffer.print("GAME OVER");
  frameBuffer.setTextColor(TFT_WHITE);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(20, 101);
  frameBuffer.printf("SCORE %lu", static_cast<unsigned long>(score));
  frameBuffer.setCursor(20, 126);
  frameBuffer.printf("BEST %lu", static_cast<unsigned long>(highScore));
  frameBuffer.fillRoundRect(17, 180, 159, 42, 8, TFT_DARKGREEN);
  frameBuffer.drawRoundRect(17, 180, 159, 42, 8, TFT_WHITE);
  frameBuffer.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  frameBuffer.setCursor(27, 192);
  frameBuffer.print("PLAY AGAIN");
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
  frameBuffer.setTextColor(TFT_WHITE);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(91, height - 17);
  frameBuffer.print("TAP TO RETURN TO MENU");
  frameBuffer.pushSprite(0, 0);
}

void startGame() {
  startMusic(MusicTrack::None, millis());
  score = 0;
  lives = STARTING_LIVES;
  ammo = MAGAZINE_SIZE;
  wave = 1;
  waveKills = 0;
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

void chooseZombieEntry(uint8_t level, int16_t &x, int16_t &y) {
  uint8_t entryCount = 0;
  for (const SceneProp &prop : SCENE_PROPS) {
    if (prop.scene == level && prop.type == PropType::Window) {
      ++entryCount;
    }
  }
  for (const SceneDoor &door : SCENE_DOORS) {
    if (door.scene == level) {
      ++entryCount;
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
  x = frameBuffer.width() / 2;
  y = 154;
}

void spawnZombie(uint32_t now) {
  for (Zombie &zombie : zombies) {
    if (zombie.active) {
      continue;
    }
    zombie.lane = random(0, 3);
    chooseZombieEntry(currentLevel(), zombie.entryX, zombie.entryY);
    zombie.x = zombie.entryX;
    zombie.y = zombie.entryY;
    zombie.spawnedAt = now;
    zombie.kind = static_cast<ZombieKind>(random(0, 3));
    zombie.animation = static_cast<ZombieAnimation>(random(0, 3));
    zombie.nextShotAt = zombie.kind == ZombieKind::Police
                            ? now + ZOMBIE_EMERGE_MS + random(650, 1801)
                            : UINT32_MAX;
    zombie.torsoHits = 0;
    zombie.hasHat = zombie.kind == ZombieKind::Police;
    zombie.active = true;
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

void updateZombies(uint32_t now) {
  for (EnemyShot &shot : enemyShots) {
    if (!shot.active || now - shot.startedAt < ENEMY_SHOT_TRAVEL_MS) {
      continue;
    }
    shot.active = false;
    if (lives > 0) {
      --lives;
    }
    playerHitUntil = now + 320;
    playTone(240, 180);
    if (lives == 0) {
      endGame();
      return;
    }
  }

  const uint32_t spawnInterval =
      max<uint32_t>(850, 1900U - min<uint32_t>(wave * 70U, 850U));
  if (now >= nextSpawnAt) {
    spawnZombie(now);
    nextSpawnAt = now + spawnInterval;
  }

  for (Zombie &zombie : zombies) {
    if (!zombie.active) {
      continue;
    }
    const uint32_t age = now - zombie.spawnedAt;
    const uint32_t approachAge =
        age > ZOMBIE_EMERGE_MS ? age - ZOMBIE_EMERGE_MS : 0;
    const float emergeProgress =
        min<uint32_t>(age, ZOMBIE_EMERGE_MS) /
        static_cast<float>(ZOMBIE_EMERGE_MS);
    const float approachProgress =
        min<uint32_t>(approachAge, ZOMBIE_REACH_PLAYER_MS - ZOMBIE_EMERGE_MS) /
        static_cast<float>(ZOMBIE_REACH_PLAYER_MS - ZOMBIE_EMERGE_MS);
    if (age < ZOMBIE_EMERGE_MS) {
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

    if (zombie.kind == ZombieKind::Police &&
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
      zombie.nextShotAt = now + random(1000, 3001);
      playTone(680, 65);
    }

    if (age >= ZOMBIE_REACH_PLAYER_MS) {
      zombie.active = false;
      if (lives > 0) {
        --lives;
        playTone(270, 120);
      }
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
    const int16_t radius = min<int16_t>(24, 9 + age / 420);
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
    if (!headshot) {
      ++zombie.torsoHits;
      addHitEffect(impactX, impactY, false, millis());
      if (zombie.torsoHits < 3) {
        playTone(470, 85);
        return;
      }
    }

    addHitEffect(impactX, impactY, headshot, millis());
    playTone(headshot ? 1680 : 470, headshot ? 115 : 85);
    zombie.active = false;
    ++score;
    ++waveKills;
    if (waveKills >= KILLS_PER_WAVE) {
      if (wave < UINT8_MAX) {
        ++wave;
      }
      waveKills = 0;
    }
    if (score > highScore) {
      highScore = score;
      preferences.putULong("highScore", highScore);
    }
    if (score >= LEVEL_COUNT * LEVEL_HIT_THRESHOLD) {
      endVictory();
      return;
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
      } else if (x >= 99 && x <= 221 && y >= 173 && y <= 212) {
        startGame();
      }
    } else if (screen == Screen::Playing) {
      if (isReloadButtonHit(x, y)) {
        ammo = MAGAZINE_SIZE;
        playReloadSound();
      } else {
        shootAt(x, y);
      }
    } else if (screen == Screen::GameOver &&
               x >= 17 && x <= 176 && y >= 180 && y <= 222) {
      screen = Screen::Menu;
      startMusic(MusicTrack::Menu, millis());
      staticScreenDirty = true;
    } else if (screen == Screen::Victory) {
      screen = Screen::Menu;
      startMusic(MusicTrack::Menu, millis());
      staticScreenDirty = true;
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
