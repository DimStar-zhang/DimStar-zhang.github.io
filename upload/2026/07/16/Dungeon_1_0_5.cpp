#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <climits>
#include <fstream>
#include <conio.h>
#include <windows.h>

using namespace std;

// ---------- 基础结构 ----------
struct Point
{
    int x, y;
    bool operator==(const Point &o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point &o) const { return !(*this == o); }
};

const int MAP_WIDTH = 80;
const int MAP_HEIGHT = 30;
const int BASE_ENEMIES = 4;
const int MAX_ENEMIES_HARD = 15;

enum Tile
{
    WALL = '#',
    FLOOR = '.',
    STAIRS_DOWN = '>',
    TRAP = '^'
};
enum ItemType
{
    POTION,
    ATK_BOOST,
    DEF_BOOST,
    GOLD
};
enum EnemyType
{
    MELEE,
    ARCHER
};

struct MapItem
{
    Point pos;
    ItemType type;
    int amount;
};

struct Entity
{
    Point pos;
    int hp, maxHp;
    int atk, def;
    int exp;
    int level;
    char symbol;
    bool isElite, isBoss;
    EnemyType enemyType;
    int gold;
    float rangedBonus;
    int rangedPurchaseCount;
    float meleeBonus; // 近战倍率加成
    int meleePurchaseCount;
    int mp, maxMp;
    int originalDef; // 原始防御（用于破甲恢复）
    int hitStreak;   // 连续命中次数
    int lastHitTurn; // 上次被攻击的回合数
};

struct Stats
{
    int deepestFloor;
    int maxLevel;
    int killsMelee;
    int killsEliteMelee;
    int killsArcher;
    int killsEliteArcher;
    int killsBoss;
};

// ---------- 技能系统 ----------
enum SkillType
{
    PASSIVE,
    ACTIVE
};
struct Skill
{
    string name;
    string desc;
    SkillType type;
    int level;    // 当前等级
    int maxLevel; // 最大等级
    int cooldown; // 基础冷却（回合）
    int mpCost;   // 基础蓝耗
    float value;  // 基础效果倍率（根据等级实际计算）
};

// 所有可学技能模板
vector<Skill> ALL_SKILLS = {
    {"自动锁定", "远程攻击自动瞄准最近敌人", PASSIVE, 0, 3, 0, 0, 0},
    {"穿透射击", "远程攻击可穿透敌人/墙壁", PASSIVE, 0, 3, 0, 0, 0},
    {"经验加成", "获得经验增加", PASSIVE, 0, 3, 0, 0, 0.20f},
    {"金币加成", "获得金币增加", PASSIVE, 0, 3, 0, 0, 0.20f},
    {"魔力再生", "每回合额外回复MP", PASSIVE, 0, 3, 0, 0, 1.0f},
    {"吸血", "近战攻击恢复造成伤害的百分比生命", PASSIVE, 0, 3, 0, 0, 0.10f},
    {"远程专精", "远程攻击生命消耗降低", PASSIVE, 0, 3, 0, 0, 0.50f},
    {"冲刺", "向移动方向快速移动4/5/6格", ACTIVE, 0, 3, 3, 15, 3},           // 蓝耗 5→15
    {"烈焰风暴", "对周围敌人造成伤害", ACTIVE, 0, 3, 4, 25, 0.5f},           // 蓝耗 8→25
    {"治疗术", "回复最大生命百分比", ACTIVE, 0, 3, 15, 70, 0.15f},           // 蓝耗 9→70
    {"护盾", "获得可吸收伤害的护盾，持续6回合", ACTIVE, 0, 3, 11, 50, 0.2f}, // 冷却 10→11
    {"狂暴", "临时增加攻击力，持续6回合", ACTIVE, 0, 3, 8, 25, 0.3f},        // 冷却 7→8
    {"铁壁", "临时增加防御力，持续6回合", ACTIVE, 0, 3, 8, 25, 0.3f},        // 冷却 7→8
    {"冻结", "冻结视野内所有敌人", ACTIVE, 0, 3, 11, 50, 2},                 // 冷却 10→11
    {"闪现", "向移动方向瞬间移动，可穿墙", ACTIVE, 0, 3, 6, 30, 8},          // 蓝耗 12→30
};

// ---------- 主游戏类 ----------
class DungeonGame
{
private:
    vector<string> map;
    Entity player;
    vector<Entity> enemies;
    vector<MapItem> items;
    Point stairs;
    bool gameOver, gameWon;
    int floorNumber;
    bool shopAvailable;
    Point shopPos;
    string message;
    Stats stats;
    int lastDx, lastDy;
    int gameTurn; // 当前回合数

    // 技能相关
    vector<Skill> playerSkills; // 已获得技能（等级>0）
    int activeCooldown[5];      // 5个主动技能槽冷却
    int shieldHp;
    int shieldTurns;
    int buffAttack, buffDefense;
    int buffAttackTurns;
    int buffDefenseTurns;
    int frozenCounter; // 全局冻结剩余回合

    // 出生房间
    Point spawnRoomCenter;
    int spawnRoomW, spawnRoomH;

    const string SAVE_FILE = "savegame.txt";
    const string RECORD_FILE = "records.txt";

    // 防御常数
    static const int PLAYER_DEF_BONUS = 8;
    static const int PLAYER_DEF_CONST = 30;
    static const int ENEMY_DEF_CONST = 80;

    // 伤害计算
    int calcPlayerDamage(int attack)
    {
        int effectiveDef = player.def + buffDefense + PLAYER_DEF_BONUS; // 加上临时防御
        float reduction = effectiveDef / (effectiveDef + (float)PLAYER_DEF_CONST);
        return max(1, (int)(attack * (1.0f - reduction)));
    }
    void applyArmorBreak(Entity *enemy)
    {
        if (enemy->lastHitTurn == gameTurn - 1 || enemy->lastHitTurn == gameTurn)
        {
            enemy->hitStreak++;
        }
        else
        {
            enemy->hitStreak = 1;
        }
        enemy->lastHitTurn = gameTurn;

        float reduction = min(0.6f, enemy->hitStreak * 0.15f);
        enemy->def = max(1, (int)(enemy->originalDef * (1.0f - reduction)));
    }
    // 原声明： int calcEnemyDamage(int attack, int enemyDef);
    int calcEnemyDamage(int attack, Entity &enemy)
    {
        // 自动应用破甲效果
        applyArmorBreak(&enemy);
        // 伤害计算（使用敌人当前防御，已经过破甲调整）
        float reduction = enemy.def / (enemy.def + 80.0f);
        int rawDmg = max(1, (int)(attack * (1.0f - reduction)));
        int minDmg = max(1, (int)(attack * 0.15f)); // 保底15%
        return max(rawDmg, minDmg);
    }
    bool canSee(const Point &from, const Point &target, const int &dist)
    {
        int dx = target.x - from.x, dy = target.y - from.y;
        if (abs(dx) + abs(dy) > dist)
            return false;
        int steps = max(abs(dx), abs(dy));
        if (steps == 0)
            return true;
        float xInc = dx / (float)steps, yInc = dy / (float)steps;
        float x = (float)from.x, y = (float)from.y;
        for (int i = 0; i < steps; ++i)
        {
            x += xInc;
            y += yInc;
            int cx = (int)(x + 0.5f), cy = (int)(y + 0.5f);
            if (cx < 0 || cy < 0 || cx >= MAP_WIDTH || cy >= MAP_HEIGHT)
                return false;
            if (map[cy][cx] == WALL)
                return false;
        }
        return true;
    }

    // ---------- 地图生成 ----------
    void generateMap()
    {
        map = vector<string>(MAP_HEIGHT, string(MAP_WIDTH, (char)WALL));
        int rooms = 8 + rand() % 6;
        vector<Point> roomCenters;
        for (int r = 0; r < rooms; ++r)
        {
            int w = 5 + rand() % 7, h = 4 + rand() % 6;
            int rx = 1 + rand() % (MAP_WIDTH - w - 2), ry = 1 + rand() % (MAP_HEIGHT - h - 2);
            for (int y = ry; y < ry + h; ++y)
                for (int x = rx; x < rx + w; ++x)
                    map[y][x] = FLOOR;
            roomCenters.push_back({rx + w / 2, ry + h / 2});
        }
        vector<bool> connected(roomCenters.size(), false);
        connected[0] = true;
        for (size_t i = 1; i < roomCenters.size(); ++i)
        {
            int bestDist = INT_MAX, bestIdx = -1;
            for (size_t j = 0; j < roomCenters.size(); ++j)
            {
                if (!connected[j])
                    continue;
                for (size_t k = 0; k < roomCenters.size(); ++k)
                {
                    if (connected[k])
                        continue;
                    int d = abs(roomCenters[j].x - roomCenters[k].x) + abs(roomCenters[j].y - roomCenters[k].y);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestIdx = k;
                    }
                }
            }
            int connectTo = -1, minD = INT_MAX;
            for (size_t j = 0; j < roomCenters.size(); ++j)
            {
                if (!connected[j])
                    continue;
                int d = abs(roomCenters[j].x - roomCenters[bestIdx].x) + abs(roomCenters[j].y - roomCenters[bestIdx].y);
                if (d < minD)
                {
                    minD = d;
                    connectTo = j;
                }
            }
            Point a = roomCenters[connectTo], b = roomCenters[bestIdx];
            if (rand() % 2)
            {
                for (int x = min(a.x, b.x); x <= max(a.x, b.x); ++x)
                    map[a.y][x] = FLOOR;
                for (int y = min(a.y, b.y); y <= max(a.y, b.y); ++y)
                    map[y][b.x] = FLOOR;
            }
            else
            {
                for (int y = min(a.y, b.y); y <= max(a.y, b.y); ++y)
                    map[y][a.x] = FLOOR;
                for (int x = min(a.x, b.x); x <= max(a.x, b.x); ++x)
                    map[b.y][x] = FLOOR;
            }
            connected[bestIdx] = true;
        }
    }

    bool inRoom(const Point &center, int w, int h, const Point &p)
    {
        return p.x >= center.x - w / 2 && p.x <= center.x + w / 2 &&
               p.y >= center.y - h / 2 && p.y <= center.y + h / 2;
    }

    void placePlayer()
    {
        do
        {
            player.pos.x = rand() % MAP_WIDTH;
            player.pos.y = rand() % MAP_HEIGHT;
        } while (map[player.pos.y][player.pos.x] != FLOOR);
        spawnRoomCenter = player.pos;
        spawnRoomW = 9;
        spawnRoomH = 7; // 比实际房间略大，确保不会刷怪
    }

    void placeStairs()
    {
        do
        {
            stairs.x = rand() % MAP_WIDTH;
            stairs.y = rand() % MAP_HEIGHT;
        } while (map[stairs.y][stairs.x] != FLOOR || stairs == player.pos ||
                 inRoom(spawnRoomCenter, spawnRoomW, spawnRoomH, stairs));
        map[stairs.y][stairs.x] = STAIRS_DOWN;
    }

    void placeEnemies()
    {
        enemies.clear();
        int enemyCount = min(BASE_ENEMIES + floorNumber / 2, MAX_ENEMIES_HARD);
        for (int i = 0; i < enemyCount; ++i)
        {
            Entity e;
            e.isBoss = false;
            e.isElite = (rand() % 100) < 20;
            e.enemyType = (rand() % 100 < 70) ? MELEE : ARCHER;

            int baseHp = (int)((6 + rand() % 2 + floorNumber * 6) * 1.5f); // 生命增速 5→6
            int baseAtk = (int)((2 + rand() % 2 + floorNumber * 3) * 1.5f);
            int baseDef = (int)((1 + rand() % 2 + floorNumber * 1.5) * 1.5f); // 防御增速 1.2→1.5

            if (e.isElite)
            {
                float hpMult = 2.0f + floorNumber * 0.25f;
                float atkMult = 1.3f + floorNumber * 0.05f;
                float defMult = 1.1f + floorNumber * 0.02f; // 倍率增速降低
                e.maxHp = (int)(baseHp * hpMult);
                e.atk = (int)(baseAtk * atkMult) + floorNumber * 2;
                e.def = (int)(baseDef * defMult) + (int)(floorNumber * 0.3f); // 额外防御增速降低

                e.originalDef = e.def;
                e.hitStreak = 0;
                e.lastHitTurn = -1;

                e.symbol = (e.enemyType == ARCHER) ? 'R' : 'M';
            }
            else
            {
                e.maxHp = baseHp;
                e.atk = baseAtk;
                e.def = baseDef;

                e.originalDef = e.def;
                e.hitStreak = 0;
                e.lastHitTurn = -1;

                e.symbol = (e.enemyType == ARCHER) ? 'r' : 'E';
            }
            if (e.enemyType == ARCHER)
            {
                e.maxHp = (int)(e.maxHp * 0.8f);
                e.atk = (int)(e.atk * 1.2f);
            }
            e.hp = e.maxHp;
            e.rangedBonus = 0;         // 怪物不适用
            e.rangedPurchaseCount = 0; // 怪物不适用
            do
            {
                e.pos.x = rand() % MAP_WIDTH;
                e.pos.y = rand() % MAP_HEIGHT;
            } while (map[e.pos.y][e.pos.x] != FLOOR || e.pos == player.pos ||
                     inRoom(spawnRoomCenter, spawnRoomW, spawnRoomH, e.pos));
            enemies.push_back(e);
        }
    }

    void placeBoss()
    {
        Entity boss;
        boss.isBoss = true;
        boss.isElite = true;
        boss.enemyType = MELEE;
        boss.symbol = 'B';
        int baseHp = (int)(12 + rand() % 3 + floorNumber * 8);
        int baseAtk = (int)(6 + rand() % 2 + floorNumber * 5);
        int baseDef = (int)(3 + rand() % 2 + floorNumber * 2);
        boss.maxHp = (int)(baseHp * (3.5f + floorNumber * 0.3f));
        boss.atk = (int)(baseAtk * (2.2f + floorNumber * 0.1f));
        boss.def = (int)(baseDef * (1.4f + floorNumber * 0.04f));

        boss.originalDef = boss.def;
        boss.hitStreak = 0;
        boss.lastHitTurn = -1;

        boss.hp = boss.maxHp;
        do
        {
            boss.pos.x = rand() % MAP_WIDTH;
            boss.pos.y = rand() % MAP_HEIGHT;
        } while (map[boss.pos.y][boss.pos.x] != FLOOR || boss.pos == player.pos ||
                 inRoom(spawnRoomCenter, spawnRoomW, spawnRoomH, boss.pos) ||
                 (abs(boss.pos.x - player.pos.x) + abs(boss.pos.y - player.pos.y) < 15));
        enemies.push_back(boss);
    }

    void placeItems()
    {
        items.clear();
        int itemCount = 5 + floorNumber;
        for (int i = 0; i < itemCount; ++i)
        {
            MapItem mi;
            int r = rand() % 100;
            if (r < 70)
                mi.type = POTION;
            else if (r < 80)
                mi.type = ATK_BOOST;
            else if (r < 90)
                mi.type = DEF_BOOST;
            else
                mi.type = GOLD;
            mi.amount = (mi.type == GOLD) ? 10 + rand() % 20 : 1;
            do
            {
                mi.pos.x = rand() % MAP_WIDTH;
                mi.pos.y = rand() % MAP_HEIGHT;
            } while (map[mi.pos.y][mi.pos.x] != FLOOR || mi.pos == player.pos ||
                     find_if(items.begin(), items.end(), [&](const MapItem &it)
                             { return it.pos == mi.pos; }) != items.end());
            items.push_back(mi);
        }
    }

    void initFloor()
    {
        generateMap();
        placePlayer();
        lastDx = 0;
        lastDy = 0;
        if (floorNumber % 10 == 0)
        {
            enemies.clear();
            placeBoss();
            stairs = {-1, -1};
        }
        else
        {
            placeStairs();
            placeEnemies();
        }
        // 陷阱避开出生房间
        int trapCount = 8 + rand() % 10;
        for (int i = 0; i < trapCount; ++i)
        {
            int x = rand() % MAP_WIDTH, y = rand() % MAP_HEIGHT;
            if (map[y][x] == FLOOR && !inRoom(spawnRoomCenter, spawnRoomW, spawnRoomH, {x, y}))
                map[y][x] = TRAP;
        }
        placeItems();
        // 商店必定在出生房间
        shopAvailable = true;
        do
        {
            shopPos.x = spawnRoomCenter.x + rand() % (spawnRoomW)-spawnRoomW / 2;
            shopPos.y = spawnRoomCenter.y + rand() % (spawnRoomH)-spawnRoomH / 2;
        } while (shopPos.x < 0 || shopPos.x >= MAP_WIDTH || shopPos.y < 0 || shopPos.y >= MAP_HEIGHT ||
                 map[shopPos.y][shopPos.x] != FLOOR || shopPos == player.pos);
    }

    bool isWalkable(int x, int y)
    {
        if (x < 0 || y < 0 || x >= MAP_WIDTH || y >= MAP_HEIGHT)
            return false;
        return map[y][x] != WALL;
    }

    // ---------- 被动技能辅助函数 ----------
    int getPassiveLevel(const string &name)
    {
        for (auto &s : playerSkills)
            if (s.name == name && s.type == PASSIVE)
                return s.level;
        return 0;
    }

    // ---------- 主动技能实现 ----------
    void applySkill(int slot)
    {
        if (slot < 0 || slot >= 5)
            return;
        // 查找第slot个主动技能
        int idx = 0;
        int target = -1;
        for (int i = 0; i < (int)playerSkills.size(); ++i)
        {
            if (playerSkills[i].type == ACTIVE)
            {
                if (idx == slot)
                {
                    target = i;
                    break;
                }
                idx++;
            }
        }
        if (target == -1)
            return;
        Skill &sk = playerSkills[target];
        if (sk.level <= 0 || activeCooldown[slot] > 0)
            return;
        if (player.mp < sk.mpCost)
        {
            message = "Not enough MP!";
            return;
        }
        player.mp -= sk.mpCost;
        activeCooldown[slot] = sk.cooldown;

        // 根据名称执行效果，数值按百分比和等级计算
        float lvMult = sk.level;
        if (sk.name == "冲刺")
        {
            if (lastDx == 0 && lastDy == 0)
            {
                message = "No direction to dash!";
                // 退还 MP 并取消冷却（因为已扣除，需要回退）
                player.mp += sk.mpCost;
                activeCooldown[slot] = 0;
                return;
            }
            int dist = (int)(sk.value + lvMult);
            int x = player.pos.x, y = player.pos.y;
            for (int step = 0; step < dist; ++step)
            {
                int nx = x + lastDx;
                int ny = y + lastDy;
                if (!isWalkable(nx, ny))
                    break;
                bool enemyBlock = false;
                for (auto &e : enemies)
                {
                    if (e.hp > 0 && e.pos == Point{nx, ny})
                    {
                        enemyBlock = true;
                        break;
                    }
                }
                if (enemyBlock)
                    break;
                x = nx;
                y = ny;
            }
            if (x != player.pos.x || y != player.pos.y)
            {
                player.pos.x = x;
                player.pos.y = y;
                message = "Dash!";
            }
            else
            {
                message = "Dash blocked!";
            }
        }
        else if (sk.name == "烈焰风暴")
        {
            int dmg = (int)(player.atk * (sk.value + lvMult * 1.0f));
            for (auto &e : enemies)
            {
                if (e.hp > 0 && abs(e.pos.x - player.pos.x) + abs(e.pos.y - player.pos.y) <= 7)
                {
                    e.hp -= calcEnemyDamage(dmg, e);
                    if (e.hp <= 0)
                        handleEnemyDeath(&e);
                }
            }
            message = "Firestorm!";
        }
        else if (sk.name == "治疗术")
        {
            float ratio = sk.value + lvMult * 0.03f;
            int heal = (int)(player.maxHp * ratio);
            player.hp = min(player.maxHp, player.hp + heal);
            message = "Healed " + to_string(heal) + " HP!";
        }
        else if (sk.name == "护盾")
        {
            float ratio = sk.value + lvMult * 0.05f;
            shieldHp = (int)(player.maxHp * ratio);
            shieldTurns = 6;
            message = "Shield gained!";
        }
        else if (sk.name == "狂暴")
        {
            float ratio = sk.value + lvMult * 0.05f;
            buffAttack = (int)(player.atk * ratio);
            buffAttackTurns = 6; // 独立计时
            message = "Attack boosted!";
        }
        else if (sk.name == "铁壁")
        {
            float ratio = sk.value + lvMult * 0.05f;
            buffDefense = (int)(player.def * ratio);
            buffDefenseTurns = 6; // 独立计时
            message = "Defense boosted!";
        }
        else if (sk.name == "冻结")
        {
            frozenCounter = (int)(sk.value + lvMult) + 1; // 基础 2 + 等级，最高5
            message = "Enemies frozen for " + to_string(frozenCounter) + " turns!";
        }
        else if (sk.name == "闪现")
        {
            if (lastDx == 0 && lastDy == 0)
            {
                message = "No direction to flash!";
                player.mp += sk.mpCost;
                activeCooldown[slot] = 0;
                return;
            }
            int dist = (int)(sk.value + lvMult);
            int nx = player.pos.x, ny = player.pos.y;
            // 计算理论目标点（无视障碍）
            for (int step = 0; step < dist; ++step)
            {
                int tx = nx + lastDx;
                int ty = ny + lastDy;
                if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT)
                    break;
                nx = tx;
                ny = ty;
            }
            // 如果最终位置不可行走（如墙），回退到最近的合法位置
            while (!isWalkable(nx, ny) && (nx != player.pos.x || ny != player.pos.y))
            {
                nx -= lastDx;
                ny -= lastDy;
            }
            if (nx == player.pos.x && ny == player.pos.y)
            {
                message = "Nowhere to flash!";
            }
            else
            {
                player.pos.x = nx;
                player.pos.y = ny;
                message = "Flash!";
            }
        }
    }

    void endTurnEffects()
    {
        for (int i = 0; i < 5; ++i)
            if (activeCooldown[i] > 0)
                activeCooldown[i]--;
        // 攻击 buff 独立递减
        if (buffAttackTurns > 0)
        {
            buffAttackTurns--;
            if (buffAttackTurns == 0)
                buffAttack = 0;
        }
        // 防御 buff 独立递减
        if (buffDefenseTurns > 0)
        {
            buffDefenseTurns--;
            if (buffDefenseTurns == 0)
                buffDefense = 0;
        }
        if (shieldTurns > 0)
        {
            shieldTurns--;
            if (shieldTurns == 0)
                shieldHp = 0;
        }
        if (frozenCounter > 0)
            frozenCounter--;
        int mpRegen = 1 + getPassiveLevel("魔力再生");
        player.mp = min(player.maxMp, player.mp + mpRegen);
    }

    void handleEnemyDeath(Entity *enemy)
    {
        int baseExp = enemy->maxHp / 2 + enemy->atk;
        if (enemy->isElite)
            baseExp *= 3;
        float expMult = 1.0f + getPassiveLevel("经验加成") * 0.2f;
        player.exp += (int)(baseExp * expMult);
        float goldMult = 1.0f + getPassiveLevel("金币加成") * 0.2f;
        player.gold += (int)(((enemy->isElite ? 10 : 3) + rand() % 5) * goldMult);
        if (enemy->isBoss)
            stats.killsBoss++;
        else if (enemy->isElite)
        {
            if (enemy->enemyType == ARCHER)
                stats.killsEliteArcher++;
            else
                stats.killsEliteMelee++;
        }
        else
        {
            if (enemy->enemyType == ARCHER)
                stats.killsArcher++;
            else
                stats.killsMelee++;
        }
        enemy->hp = -1; // 标记死亡
        checkLevelUp();
    }

    void rangedAttack()
    {
        int autoAim = getPassiveLevel("自动锁定");
        int pierce = getPassiveLevel("穿透射击");
        if (autoAim == 0 && lastDx == 0 && lastDy == 0)
        {
            message = "No direction.";
            return;
        }
        float remoteCostReduction = getPassiveLevel("远程专精") * 0.25f + 0.25f; // Lv1:0.50, Lv2:0.75, Lv3:1.00
        int cost = max(1, (int)((player.maxHp * 0.02 + 1) * (1.0f - remoteCostReduction)));
        if (player.hp <= cost)
        {
            message = "Not enough HP!";
            return;
        }
        player.hp -= cost;
        player.hp -= cost;
        int x = player.pos.x, y = player.pos.y;
        bool hit = false;
        if (autoAim)
        {
            int bestDist = 9999;
            Entity *target = nullptr;
            for (auto &e : enemies)
            {
                if (e.hp > 0 && canSee(player.pos, e.pos, autoAim * 6 + 6))
                {
                    int d = abs(e.pos.x - player.pos.x) + abs(e.pos.y - player.pos.y);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        target = &e;
                    }
                }
            }
            if (target)
            {
                float totalMult = 0.8f + player.rangedBonus;
                int dmg = calcEnemyDamage((int)((player.atk + buffAttack) * totalMult), *target);
                target->hp -= dmg;
                message = "Auto-aim shot for " + to_string(dmg) + " damage !";
                if (target->hp <= 0)
                    handleEnemyDeath(target);
                hit = true;
            }
        }
        else
        {
            int pierceCount = pierce; // 穿透次数（可穿墙、穿敌人）
            while (x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT)
            {
                // 如果撞墙且有穿透次数，则消耗一次穿透并继续前进
                if (map[y][x] == WALL)
                {
                    if (pierceCount > 0)
                    {
                        pierceCount--;
                        x += lastDx;
                        y += lastDy;
                        continue;
                    }
                    else
                        break; // 无穿透，子弹被墙阻挡
                }

                auto it = find_if(enemies.begin(), enemies.end(), [&](const Entity &e)
                                  { return e.hp > 0 && e.pos == Point{x, y}; });
                if (it != enemies.end())
                {
                    float totalMult = 0.8f + player.rangedBonus;
                    int dmg = calcEnemyDamage((int)((player.atk + buffAttack) * totalMult), *it);
                    it->hp -= dmg;
                    message = "You shoot for " + to_string(dmg) + " damage !";
                    if (it->hp <= 0)
                        handleEnemyDeath(&*it);
                    hit = true;

                    // 击中敌人消耗穿透次数，若次数用完则停止
                    if (pierceCount > 0)
                        pierceCount--;
                    else
                        break; // 无穿透，击中敌人后子弹消失
                }

                x += lastDx;
                y += lastDy;
            }
        }
        if (!hit)
            message = "The shot fades...";
    }

    void movePlayer(int dx, int dy)
    {
        int nx = player.pos.x + dx;
        int ny = player.pos.y + dy;
        if (!isWalkable(nx, ny))
            return;
        lastDx = dx;
        lastDy = dy;

        if (map[ny][nx] == TRAP)
        {
            int percent = min(10 + (floorNumber - 1), 30);
            int dmg = max(1, player.maxHp * percent / 100);
            player.hp -= dmg;
            message = "You stepped on a trap and took " + to_string(dmg) + " damage!";
            map[ny][nx] = FLOOR;
            if (player.hp <= 0)
            {
                gameOver = true;
                return;
            }
        }

        for (auto it = enemies.begin(); it != enemies.end(); ++it)
        {
            if (it->hp > 0 && it->pos == Point{nx, ny})
            {
                int totalAtk = (int)((player.atk + buffAttack) * (1.0f + player.meleeBonus));
                int dmg = calcEnemyDamage(totalAtk, *it);
                it->hp -= dmg;

                // 吸血效果
                int vampireLevel = getPassiveLevel("吸血");
                if (vampireLevel > 0)
                {
                    float vampireRatio = 0.05f + vampireLevel * 0.05f; // Lv1:10% Lv2:15% Lv3:20%
                    int heal = (int)(dmg * vampireRatio);
                    player.hp = min(player.maxHp, player.hp + heal);
                }

                if (it->hp <= 0)
                    handleEnemyDeath(&*it);
                return;
            }
        }

        player.pos.x = nx;
        player.pos.y = ny;

        auto itemIt = find_if(items.begin(), items.end(), [&](const MapItem &mi)
                              { return mi.pos == player.pos; });
        if (itemIt != items.end())
        {
            switch (itemIt->type)
            {
            case POTION:
                player.hp = min(player.maxHp, int(player.hp + player.maxHp * 0.1 + 15));
                break;
            case ATK_BOOST:
                player.atk += 2;
                break;
            case DEF_BOOST:
                player.def += 1;
                break;
            case GOLD:
                player.gold += itemIt->amount;
                break;
            }
            items.erase(itemIt);
        }

        if (shopAvailable && player.pos == shopPos)
            shop();
    }

    int calcreqExp()
    {
        int reqExp = player.level * 10 + (player.level / 10) * 100;
        int tier = player.level / 50;
        for (int i = 0; i < tier; ++i)
            reqExp *= 2;
        return reqExp;
    }

    void checkLevelUp()
    {
        int reqExp = calcreqExp();
        while (player.exp >= reqExp)
        {
            player.exp -= reqExp;
            player.level++;
            player.maxHp += 6;
            player.hp = player.maxHp;
            player.atk += 3;
            player.def += 1;
            player.maxMp += 3;
            player.mp = min(player.maxMp + 3, player.maxMp);
            message = "Level Up! You are now level " + to_string(player.level) + "!";
            if (player.level > stats.maxLevel)
                stats.maxLevel = player.level;
            reqExp = calcreqExp();
        }
    }

    void updateEnemies()
    {
        enemies.erase(remove_if(enemies.begin(), enemies.end(), [](const Entity &e)
                                { return e.hp <= 0; }),
                      enemies.end());
        // 重置未连续被攻击的敌人防御
        for (auto &e : enemies)
        {
            if (e.hp > 0 && e.lastHitTurn != gameTurn - 1)
            {
                e.def = e.originalDef;
                e.hitStreak = 0;
            }
        }
        if (floorNumber % 10 == 0 && stairs.x == -1)
        {
            bool bossAlive = false;
            for (auto &e : enemies)
                if (e.isBoss)
                {
                    bossAlive = true;
                    break;
                }
            if (!bossAlive)
            {
                for (int dy = -3; dy <= 3; ++dy)
                {
                    for (int dx = -3; dx <= 3; ++dx)
                    {
                        int sx = player.pos.x + dx, sy = player.pos.y + dy;
                        if (sx >= 0 && sy >= 0 && sx < MAP_WIDTH && sy < MAP_HEIGHT &&
                            map[sy][sx] == FLOOR && !(sx == player.pos.x && sy == player.pos.y))
                        {
                            stairs = {sx, sy};
                            map[sy][sx] = STAIRS_DOWN;
                            message = "A mysterious staircase appears...";
                            dy = 4;
                            break;
                        }
                    }
                }
            }
        }

        for (auto &e : enemies)
        {
            if (e.hp <= 0)
                continue;
            if (frozenCounter > 0)
                continue; // 冻结跳过行动
            if (e.enemyType == ARCHER && canSee(e.pos, player.pos, 6))
            {
                int dist = abs(e.pos.x - player.pos.x) + abs(e.pos.y - player.pos.y);
                if (dist > 1 && dist <= 4)
                {
                    int dmg = calcPlayerDamage(e.atk);
                    if (shieldHp > 0)
                    {
                        if (dmg >= shieldHp)
                        {
                            dmg -= shieldHp;
                            shieldHp = 0;
                        }
                        else
                        {
                            shieldHp -= dmg;
                            dmg = 0;
                        }
                    }
                    player.hp -= dmg;
                    if (dmg > 0)
                        message = "Archer shoots for " + to_string(dmg) + "!";
                    if (player.hp <= 0)
                    {
                        gameOver = true;
                        return;
                    }
                }
                else if (dist == 1)
                {
                    int dmg = calcPlayerDamage(e.atk);
                    if (shieldHp > 0)
                    {
                        if (dmg >= shieldHp)
                        {
                            dmg -= shieldHp;
                            shieldHp = 0;
                        }
                        else
                        {
                            shieldHp -= dmg;
                            dmg = 0;
                        }
                    }
                    player.hp -= dmg;
                    if (dmg > 0)
                        message = "Archer hits for " + to_string(dmg) + "!";
                    if (player.hp <= 0)
                    {
                        gameOver = true;
                        return;
                    }
                }
                else
                    moveEnemyTowards(e, player.pos);
            }
            else if (canSee(e.pos, player.pos, 6))
            {
                moveEnemyTowards(e, player.pos);
            }
            else
            {
                int dir = rand() % 4;
                int nx = e.pos.x + (dir == 0 ? -1 : (dir == 1 ? 1 : 0)), ny = e.pos.y + (dir == 2 ? -1 : (dir == 3 ? 1 : 0));
                if (isWalkable(nx, ny) && !(nx == player.pos.x && ny == player.pos.y))
                {
                    bool occ = false;
                    for (auto &o : enemies)
                        if (&o != &e && o.hp > 0 && o.pos == Point{nx, ny})
                            occ = true;
                    if (!occ)
                    {
                        e.pos.x = nx;
                        e.pos.y = ny;
                    }
                }
            }
            if (e.enemyType != ARCHER && e.hp > 0 &&
                abs(e.pos.x - player.pos.x) + abs(e.pos.y - player.pos.y) == 1)
            {
                int dmg = calcPlayerDamage(e.atk);
                if (shieldHp > 0)
                {
                    if (dmg >= shieldHp)
                    {
                        dmg -= shieldHp;
                        shieldHp = 0;
                    }
                    else
                    {
                        shieldHp -= dmg;
                        dmg = 0;
                    }
                }
                player.hp -= dmg;
                if (dmg > 0)
                    message = "Enemy hits for " + to_string(dmg) + "!";
                if (player.hp <= 0)
                {
                    gameOver = true;
                    return;
                }
            }
        }
        enemies.erase(remove_if(enemies.begin(), enemies.end(), [](const Entity &e)
                                { return e.hp <= 0; }),
                      enemies.end());
    }

    void moveEnemyTowards(Entity &e, const Point &target)
    {
        int dx = target.x - e.pos.x, dy = target.y - e.pos.y;
        int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
        int stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
        int nx = e.pos.x + stepX, ny = e.pos.y + stepY;
        if (isWalkable(nx, ny) && !(nx == player.pos.x && ny == player.pos.y) &&
            all_of(enemies.begin(), enemies.end(), [&](const Entity &o)
                   { return &o == &e || o.hp <= 0 || !(o.pos == Point{nx, ny}); }))
        {
            e.pos.x = nx;
            e.pos.y = ny;
        }
        else if (isWalkable(e.pos.x + stepX, e.pos.y) && !(e.pos.x + stepX == player.pos.x && e.pos.y == player.pos.y))
            e.pos.x += stepX;
        else if (isWalkable(e.pos.x, e.pos.y + stepY) && !(e.pos.x == player.pos.x && e.pos.y + stepY == player.pos.y))
            e.pos.y += stepY;
    }

    void shop()
    {
        bool inShop = true;
        while (inShop)
        {
            system("cls");
            cout << "========== SHOP ==========\n";
            cout << "Your gold: " << player.gold << "\n";
            cout << "HP: " << player.hp << "/" << player.maxHp
                 << " | ATK: " << player.atk << " | DEF: " << player.def << "\n";
            cout << "Melee Bonus: +" << (int)(player.meleeBonus * 100) << "% | Ranged Bonus: +" << (int)(player.rangedBonus * 100) << "%\n\n";
            cout << "1. Large Potion (restore 50% HP)   - 15 gold\n";
            cout << "2. Attack Boost (+3 ATK)          - 25 gold\n";
            cout << "3. Defense Boost (+2 DEF)         - 25 gold\n";
            int meleeCost = 40 + player.meleePurchaseCount * 30;
            cout << "4. Melee Boost (+30% melee ATK)   - " << meleeCost << " gold\n";
            int rangedCost = 40 + player.rangedPurchaseCount * 30;
            cout << "5. Ranged Boost (+20% ranged ATK) - " << rangedCost << " gold\n";
            cout << "6. Leave shop\n";
            if (!message.empty())
                cout << ">> " << message << " <<\n";
            char c;
            while (true)
            {
                c = _getch();
                if (c >= '1' && c <= '6')
                    break;
            }
            message.clear();
            switch (c)
            {
            case '1':
                if (player.gold >= 15)
                {
                    player.gold -= 15;
                    int before = player.hp;
                    player.hp = min(player.maxHp, player.hp + ((player.maxHp + 1) / 2));
                    message = "Restored " + to_string(player.hp - before) + " HP!";
                }
                else
                    message = "Not enough gold!";
                break;
            case '2':
                if (player.gold >= 25)
                {
                    player.gold -= 25;
                    player.atk += 3;
                    message = "ATK is now " + to_string(player.atk) + ".";
                }
                else
                    message = "Not enough gold!";
                break;
            case '3':
                if (player.gold >= 25)
                {
                    player.gold -= 25;
                    player.def += 2;
                    message = "DEF is now " + to_string(player.def) + ".";
                }
                else
                    message = "Not enough gold!";
                break;
            case '4':
                if (player.gold >= meleeCost)
                {
                    player.gold -= meleeCost;
                    player.meleeBonus += 0.3f;
                    player.meleePurchaseCount++;
                    message = "Melee bonus now +" + to_string((int)(player.meleeBonus * 100)) + "%.";
                }
                else
                    message = "Not enough gold! Need " + to_string(meleeCost) + " gold!";
                break;
            case '5':
                if (player.gold >= rangedCost)
                {
                    player.gold -= rangedCost;
                    player.rangedBonus += 0.2f;
                    player.rangedPurchaseCount++;
                    message = "Ranged bonus now +" + to_string((int)(player.rangedBonus * 100)) + "%.";
                }
                else
                    message = "Not enough gold! Need " + to_string(rangedCost) + " gold!";
                break;
            case '6':
                inShop = false;
                message = "You leave the shop.";
                break;
            }
        }
    }

    void skillSelection()
    {
        vector<int> candidates;
        bool hasAutoAim = false, hasPierce = false;
        int activeCount = 0, passiveCount = 0;
        for (auto &ps : playerSkills)
        {
            if (ps.name == "自动锁定" && ps.level > 0)
                hasAutoAim = true;
            if (ps.name == "穿透射击" && ps.level > 0)
                hasPierce = true;
            if (ps.type == ACTIVE)
                activeCount++;
            else if (ps.type == PASSIVE)
                passiveCount++;
        }

        for (int i = 0; i < (int)ALL_SKILLS.size(); ++i)
        {
            Skill &sk = ALL_SKILLS[i];
            int curLevel = 0;
            for (auto &ps : playerSkills)
                if (ps.name == sk.name)
                    curLevel = ps.level;
            if (curLevel < sk.maxLevel)
            {
                if (sk.name == "自动锁定" && hasPierce)
                    continue;
                if (sk.name == "穿透射击" && hasAutoAim)
                    continue;

                // 主动技能限制：已满5个且该技能未学习则跳过
                if (activeCount >= 5 && sk.type == ACTIVE && curLevel == 0)
                    continue;
                // 被动技能限制：已满5个且该技能未学习则跳过
                if (passiveCount >= 5 && sk.type == PASSIVE && curLevel == 0)
                    continue;

                candidates.push_back(i);
            }
        }
        if (candidates.empty())
            return;

        random_shuffle(candidates.begin(), candidates.end());
        if (candidates.size() > 3)
            candidates.resize(3);

        system("cls");
        cout << "Choose a skill:\n";
        for (int i = 0; i < (int)candidates.size(); ++i)
        {
            Skill &s = ALL_SKILLS[candidates[i]];
            cout << i + 1 << ". " << s.name << " (" << s.desc << ")";
            int cur = 0;
            for (auto &ps : playerSkills)
                if (ps.name == s.name)
                    cur = ps.level;
            if (cur > 0)
                cout << " [Lv" << cur << "->" << cur + 1 << "]";
            cout << "\n";
        }
        char ch;
        while (true)
        {
            ch = _getch();
            if (ch >= '1' && ch <= '0' + candidates.size())
                break; // 按实际候选数量限制
        }
        int idx = candidates[ch - '1'];
        bool found = false;
        for (auto &ps : playerSkills)
        {
            if (ps.name == ALL_SKILLS[idx].name)
            {
                ps.level++;
                found = true;
                break;
            }
        }
        if (!found)
        {
            Skill newSk = ALL_SKILLS[idx];
            newSk.level = 1;
            playerSkills.push_back(newSk);
        }
    }

    void saveGame()
    {
        ofstream out(SAVE_FILE);
        out << floorNumber << " " << player.hp << " " << player.maxHp << " " << player.atk << " " << player.def
            << " " << player.exp << " " << player.level << " " << player.gold << " " << player.mp << " " << player.maxMp << "\n";
        out << player.rangedBonus << " " << player.rangedPurchaseCount << " " << player.meleeBonus << " " << player.meleePurchaseCount << "\n";
        out << stats.deepestFloor << " " << stats.maxLevel << " ";
        out << stats.killsMelee << " " << stats.killsEliteMelee << " " << stats.killsArcher << " " << stats.killsEliteArcher << " " << stats.killsBoss << "\n";
        out << playerSkills.size() << "\n";
        for (auto &sk : playerSkills)
            out << sk.name << " " << sk.level << "\n";
        out.close();
    }

    void deleteSave() { remove(SAVE_FILE.c_str()); }

    void updatePermanentRecords()
    {
        Stats perma;
        ifstream in(RECORD_FILE);
        if (in)
        {
            in >> perma.deepestFloor >> perma.maxLevel >> perma.killsMelee >> perma.killsEliteMelee >> perma.killsArcher >> perma.killsEliteArcher >> perma.killsBoss;
            in.close();
        }
        else
            memset(&perma, 0, sizeof(perma));
        perma.deepestFloor = max(perma.deepestFloor, stats.deepestFloor);
        perma.maxLevel = max(perma.maxLevel, stats.maxLevel);
        perma.killsMelee = max(perma.killsMelee, stats.killsMelee);
        perma.killsEliteMelee = max(perma.killsEliteMelee, stats.killsEliteMelee);
        perma.killsArcher = max(perma.killsArcher, stats.killsArcher);
        perma.killsEliteArcher = max(perma.killsEliteArcher, stats.killsEliteArcher);
        perma.killsBoss = max(perma.killsBoss, stats.killsBoss);
        ofstream out(RECORD_FILE);
        out << perma.deepestFloor << " " << perma.maxLevel << " "
            << perma.killsMelee << " " << perma.killsEliteMelee << " "
            << perma.killsArcher << " " << perma.killsEliteArcher << " "
            << perma.killsBoss << "\n";
        out.close();
    }

    friend void showStatsFromSave();
    friend void showPermanentRecords();

    void draw()
    {
        system("cls");
        vector<string> display = map;
        for (auto &mi : items)
        {
            char sym = '+';
            if (mi.type == ATK_BOOST)
                sym = 'A';
            else if (mi.type == DEF_BOOST)
                sym = 'D';
            else if (mi.type == GOLD)
                sym = '$';
            display[mi.pos.y][mi.pos.x] = sym;
        }
        if (shopAvailable && shopPos.x != -1)
            display[shopPos.y][shopPos.x] = 'S';
        for (const auto &e : enemies)
            if (e.hp > 0)
                display[e.pos.y][e.pos.x] = e.symbol;
        display[player.pos.y][player.pos.x] = '@';

        for (const auto &row : display)
            cout << row << '\n';
        cout << "Floor: " << floorNumber;
        if (floorNumber % 10 == 0)
            cout << " [BOSS FLOOR]";
        cout << " | HP: " << player.hp << "/" << player.maxHp << " | MP: " << player.mp << "/" << player.maxMp
             << " | ATK: " << player.atk << " | DEF: " << player.def
             << " | LV: " << player.level
             << " | EXP: " << player.exp << "/" << calcreqExp()
             << " | Gold: " << player.gold
             << " | Enemies: " << enemies.size()
             << " | Range: +" << (int)(player.rangedBonus * 100) << "% | Melee: +" << (int)(player.meleeBonus * 100) << "%\n";
        if (shieldHp > 0)
            cout << "[Shield " << shieldHp << "] ";
        if (buffAttack > 0)
            cout << "[ATK+" << buffAttack << "] ";
        if (buffDefense > 0)
            cout << "[DEF+" << buffDefense << "] ";
        if (frozenCounter > 0)
            cout << "[Frozen " << frozenCounter << "] ";
        cout << "\n";
        if (!message.empty())
            cout << ">> " << message << " <<\n";
        cout << "Move: WASD / Range: F / Skills: 1-5 / Quit: Q  Items: + (potion) A (atk) D (def) $ (gold) S (shop)\n";
        cout << "Enemies: E/M (melee)  r/R (archer)  B (boss)\n";
        cout << "Skills: ";
        int skillIdx = 0;
        for (int s = 0; s < 5; ++s)
        { // 5个技能槽
            // 寻找第 s 个主动技能
            int target = -1;
            int idx = 0;
            for (int i = 0; i < (int)playerSkills.size(); ++i)
            {
                if (playerSkills[i].type == ACTIVE)
                {
                    if (idx == s)
                    {
                        target = i;
                        break;
                    }
                    idx++;
                }
            }
            if (target != -1)
            {
                Skill &sk = playerSkills[target];
                cout << "[" << (s + 1) << ":" << sk.name << " Lv" << sk.level;
                if (activeCooldown[s] > 0)
                {
                    cout << " CD:" << activeCooldown[s];
                }
                else
                {
                    cout << " Ready";
                }
                cout << "] ";
            }
            else
            {
                cout << "[" << (s + 1) << ":-] ";
            }
        }
        cout << "\n";
    }

public:
    DungeonGame() : gameOver(false), gameWon(false), floorNumber(1), gameTurn(0)
    {
        srand(static_cast<unsigned>(time(nullptr)));
        player = {{0, 0}, 30, 30, 7, 2, 0, 1, '@', false, false, MELEE, 0, 0.0f, 0, 0.0f, 0, 15, 15};
        memset(&stats, 0, sizeof(stats));
        for (int i = 0; i < 5; ++i)
            activeCooldown[i] = 0;
        shieldHp = 0;
        shieldTurns = 0;
        buffAttack = 0;
        buffDefense = 0;
        buffAttackTurns = 0;
        buffDefenseTurns = 0;
        frozenCounter = 0;
        lastDx = 0;
        lastDy = 0;
        initFloor();
    }

    bool loadGame()
    {
        ifstream in(SAVE_FILE);
        if (!in)
            return false;
        in >> floorNumber >> player.hp >> player.maxHp >> player.atk >> player.def >> player.exp >> player.level >> player.gold >> player.mp >> player.maxMp;
        in >> player.rangedBonus >> player.rangedPurchaseCount >> player.meleeBonus >> player.meleePurchaseCount;
        in >> stats.deepestFloor >> stats.maxLevel >> stats.killsMelee >> stats.killsEliteMelee >> stats.killsArcher >> stats.killsEliteArcher >> stats.killsBoss;
        int skCount;
        in >> skCount;
        playerSkills.clear();
        for (int i = 0; i < skCount; ++i)
        {
            string name;
            int lv;
            in >> name >> lv;
            for (auto &s : ALL_SKILLS)
            {
                if (s.name == name)
                {
                    Skill ns = s;
                    ns.level = lv;
                    playerSkills.push_back(ns);
                    break;
                }
            }
        }
        in.close();
        initFloor();
        return true;
    }

    void run()
    {
        while (!gameOver)
        {
            draw();
            char key = _getch();
            message.clear();
            if (key == 'q' || key == 'Q')
            {
                gameOver = true;
                break;
            }
            if (key >= '1' && key <= '5')
            {
                applySkill(key - '1');
                gameTurn++;
                updateEnemies();
                endTurnEffects();
                continue;
            }
            switch (key)
            {
            case 'w':
            case 'W':
                movePlayer(0, -1);
                break;
            case 's':
            case 'S':
                movePlayer(0, 1);
                break;
            case 'a':
            case 'A':
                movePlayer(-1, 0);
                break;
            case 'd':
            case 'D':
                movePlayer(1, 0);
                break;
            case 'f':
            case 'F':
                rangedAttack();
                break;
            default:
                continue;
            }
            gameTurn++;
            updateEnemies();
            endTurnEffects();
            if (gameOver)
                break;

            if (player.pos == stairs)
            {
                draw();
                if (floorNumber % 10 == 0)
                {
                    cout << "You found the stairs! [Y] go to floor " << floorNumber + 1
                         << ", [N] stay, [Q] leave dungeon: ";
                    char ch;
                    while (true)
                    {
                        ch = _getch();
                        if (ch == 'y' || ch == 'Y' || ch == 'n' || ch == 'N' || ch == 'q' || ch == 'Q')
                            break;
                    }
                    if (ch == 'y' || ch == 'Y')
                    {
                        floorNumber++;
                        if (floorNumber > stats.deepestFloor)
                            stats.deepestFloor = floorNumber;
                        player.hp = min(player.maxHp, player.hp + player.maxHp / 2 + 10);
                        skillSelection(); // 选择技能
                        saveGame();
                        initFloor();
                    }
                    else if (ch == 'q' || ch == 'Q')
                    {
                        gameWon = true;
                        updatePermanentRecords();
                        deleteSave();
                        gameOver = true;
                    }
                    else
                        message = "You decide to stay on this floor.";
                }
                else
                {
                    cout << "You found the stairs! Go to floor " << floorNumber + 1 << "? [Y/N]: ";
                    char ch;
                    while (true)
                    {
                        ch = _getch();
                        if (ch == 'y' || ch == 'Y' || ch == 'n' || ch == 'N')
                            break;
                    }
                    if (ch == 'y' || ch == 'Y')
                    {
                        floorNumber++;
                        if (floorNumber > stats.deepestFloor)
                            stats.deepestFloor = floorNumber;
                        player.hp = min(player.maxHp, player.hp + player.maxHp / 2 + 10);
                        saveGame();
                        if (floorNumber % 10 == 1)
                            skillSelection(); // 离开Boss层后给技能
                        initFloor();
                    }
                    else
                        message = "You decide to stay on this floor.";
                }
            }
        }

        updatePermanentRecords();
        system("cls");
        if (player.hp <= 0)
        {
            cout << "You have been slain... Game Over.\n";
            deleteSave();
        }
        else if (gameWon)
            cout << "You escaped the dungeon after floor " << floorNumber << "!\n";
        else
            cout << "You quit the dungeon.\n";
        cout << "Press any key to continue.\n";
        _getch();
    }
};

void showStatsFromSave()
{
    ifstream in("savegame.txt");
    if (!in)
    {
        cout << "No save file found.\n";
        return;
    }
    int floor, hp, maxHp, atk, def, exp, lv, gold, mp, maxMp;
    float rBonus, mBonus;
    int rCount, mCount;
    Stats st;
    in >> floor >> hp >> maxHp >> atk >> def >> exp >> lv >> gold >> mp >> maxMp;
    in >> rBonus >> rCount >> mBonus >> mCount;
    in >> st.deepestFloor >> st.maxLevel >> st.killsMelee >> st.killsEliteMelee >> st.killsArcher >> st.killsEliteArcher >> st.killsBoss;

    int skCount;
    in >> skCount;
    vector<pair<string, int>> skills;
    for (int i = 0; i < skCount; ++i)
    {
        string name;
        int lv;
        in >> name >> lv;
        skills.push_back({name, lv});
    }
    in.close();

    system("cls");
    cout << "===== CURRENT SAVE STATS =====\n";
    cout << "Floor: " << floor << "\n";
    cout << "HP: " << hp << "/" << maxHp << "  ATK: " << atk << "  DEF: " << def << "\n";
    cout << "Level: " << lv << "  Exp: " << exp << "  Gold: " << gold << "\n";
    cout << "Melee Bonus: +" << (int)(mBonus * 100) << "%  Ranged Bonus: +" << (int)(rBonus * 100) << "%\n";
    cout << "Kills - Melee: " << st.killsMelee << "  Elite Melee: " << st.killsEliteMelee
         << "  Archer: " << st.killsArcher << "  Elite Archer: " << st.killsEliteArcher
         << "  Boss: " << st.killsBoss << "\n";
    if (!skills.empty())
    {
        cout << "\nSkills:\n";
        for (auto &sk : skills)
        {
            cout << "  " << sk.first << " Lv" << sk.second << "\n";
        }
    }
}

void showPermanentRecords()
{
    ifstream in("records.txt");
    if (!in)
    {
        cout << "No records yet.\n";
        return;
    }
    Stats pr;
    in >> pr.deepestFloor >> pr.maxLevel >> pr.killsMelee >> pr.killsEliteMelee >> pr.killsArcher >> pr.killsEliteArcher >> pr.killsBoss;
    in.close();
    system("cls");
    cout << "===== PERMANENT RECORDS =====\n";
    cout << "Deepest Floor: " << pr.deepestFloor << "\n";
    cout << "Highest Level: " << pr.maxLevel << "\n";
    cout << "Total Kills -\n";
    cout << "  Melee (E): " << pr.killsMelee << "\n";
    cout << "  Elite Melee (M): " << pr.killsEliteMelee << "\n";
    cout << "  Archer (r): " << pr.killsArcher << "\n";
    cout << "  Elite Archer (R): " << pr.killsEliteArcher << "\n";
    cout << "  Boss (B): " << pr.killsBoss << "\n";
    int total = pr.killsMelee + pr.killsEliteMelee + pr.killsArcher + pr.killsEliteArcher + pr.killsBoss;
    cout << "Total Enemies Slain: " << total << "\n";
}

int main()
{
    bool hasSave = false;
    {
        ifstream test("savegame.txt");
        hasSave = test.good();
    }

    while (true)
    {
        system("cls");
        cout << "===== Dungeon Explorer =====\n";
        cout << "1. New Game\n";
        if (hasSave)
        {
            cout << "2. Continue\n";
            cout << "3. Current Save Stats\n";
        }
        cout << "4. Permanent Records\n";
        cout << "5. Exit\n";
        char c = _getch();
        if (c == '1')
        {
            if (hasSave)
            {
                remove("savegame.txt");
                hasSave = false;
            }
            DungeonGame game;
            game.run();
            ifstream test("savegame.txt");
            hasSave = test.good();
        }
        else if (c == '2' && hasSave)
        {
            DungeonGame game;
            if (game.loadGame())
            {
                game.run();
                ifstream test("savegame.txt");
                hasSave = test.good();
            }
            else
            {
                cout << "Save file corrupted. Starting new game...\n";
                remove("savegame.txt");
                hasSave = false;
                DungeonGame newGame;
                newGame.run();
                ifstream test("savegame.txt");
                hasSave = test.good();
            }
        }
        else if (c == '3' && hasSave)
        {
            showStatsFromSave();
            cout << "\nPress any key...";
            _getch();
        }
        else if (c == '4')
        {
            showPermanentRecords();
            cout << "\nPress any key...";
            _getch();
        }
        else if (c == '5')
            break;
    }
    return 0;
}