/*
 * Fazenda Simulator GS - PSP (PSPSDK) - 3D, mundo aberto
 * Controles:
 *   Analogico  : andar          L / R : girar camera
 *   X          : plantar / colher / pescar
 *   Quadrado   : comprar semente (mercado)    Circulo : vender tudo (mercado)
 *   Esq/Dir    : trocar o tipo de semente
 *   Triangulo  : falar com moradores, entrar na casa/loja/prefeitura, coletar
 *   Start      : menu (salvar / sair)         Select : ajuda
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspaudio.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("FazendaSimulatorGS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define BUF_W   512
#define SCR_W   480
#define SCR_H   272
#define FRAME   0x88000
#define PI_F    3.14159265f
#define WORLD   95.0f
#define NPLOTS  24
#define NTREES  90
#define NCHICK  8
#define NDROPS  40
#define PEN_X   (-4.0f)
#define PEN_Z   (16.0f)

typedef struct { float nx, ny, nz, x, y, z; } Vtx;
#define VTX_TYPE (GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static unsigned int __attribute__((aligned(16))) list[262144];
static Vtx __attribute__((aligned(16))) cube[36];
#define GTILE 12.0f
#define GN    16
static Vtx __attribute__((aligned(16))) groundA[GN * GN / 2 * 6];
static Vtx __attribute__((aligned(16))) groundB[GN * GN / 2 * 6];

/* ---------- callbacks de saida ---------- */
static volatile int running = 1;
static int exit_cb(int a, int b, void *c) { running = 0; return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

/* ---------- cubo unitario (base em y=0, centrado em x/z) ---------- */
static void build_cube(void) {
    static const float F[6][9] = {
        { 1, 0, 0,   0, 1, 0,   0, 0, 1},   /* +X */
        {-1, 0, 0,   0, 0, 1,   0, 1, 0},   /* -X */
        { 0, 1, 0,   0, 0, 1,   1, 0, 0},   /* +Y */
        { 0,-1, 0,   1, 0, 0,   0, 0, 1},   /* -Y */
        { 0, 0, 1,   1, 0, 0,   0, 1, 0},   /* +Z */
        { 0, 0,-1,   0, 1, 0,   1, 0, 0}    /* -Z */
    };
    static const int S[6][2] = {{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    int i = 0;
    for (int f = 0; f < 6; f++) {
        const float *d = F[f];
        for (int k = 0; k < 6; k++) {
            float su = S[k][0] * 0.5f, sv = S[k][1] * 0.5f;
            cube[i].nx = d[0]; cube[i].ny = d[1]; cube[i].nz = d[2];
            cube[i].x = d[0]*0.5f + d[3]*su + d[6]*sv;
            cube[i].y = 0.5f + d[1]*0.5f + d[4]*su + d[7]*sv;
            cube[i].z = d[2]*0.5f + d[5]*su + d[8]*sv;
            i++;
        }
    }
    sceKernelDcacheWritebackAll();
}

/* chao: ladrilhos pequenos (pecas gigantes bugam no PSP) */
static void build_ground(void) {
    int na = 0, nb = 0;
    for (int i = 0; i < GN; i++) {
        for (int j = 0; j < GN; j++) {
            float x0 = -96.0f + i * GTILE, z0 = -96.0f + j * GTILE;
            float x1 = x0 + GTILE, z1 = z0 + GTILE;
            float xs[6] = { x0, x1, x1, x0, x1, x0 };
            float zs[6] = { z0, z0, z1, z0, z1, z1 };
            Vtx *v = ((i + j) & 1) ? &groundB[nb] : &groundA[na];
            for (int k = 0; k < 6; k++) {
                v[k].nx = 0; v[k].ny = 1; v[k].nz = 0;
                v[k].x = xs[k]; v[k].y = 0; v[k].z = zs[k];
            }
            if ((i + j) & 1) nb += 6; else na += 6;
        }
    }
    sceKernelDcacheWritebackAll();
}

static unsigned int groundC1 = 0xff3c9a3c, groundC2 = 0xff328a34;

static void draw_ground(void) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuColor(groundC1);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundA);
    sceGuColor(groundC2);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundB);
}

/* desenha caixa: posicao (base), escala, rotacao Y, cor ABGR */
static void box(float x, float y, float z, float sx, float sy, float sz,
                float ry, unsigned int color) {
    ScePspFVector3 p = {x, y, z}, s = {sx, sy, sz};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&p);
    sceGumRotateY(ry);
    sceGumScale(&s);
    sceGumUpdateMatrix();
    sceGuColor(color);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, 36, 0, cube);
}

/* ---------- audio (musica + efeitos) ---------- */
#define STEP 15000
static volatile int musicOn = 1;
static volatile int sfxQ = 0;

static const unsigned char MEL[][2] = {
    {64,1},{67,1},{72,1},{67,1}, {62,1},{67,1},{71,1},{67,1},
    {64,1},{69,1},{72,1},{69,1}, {65,1},{69,1},{72,1},{69,1},
    {64,1},{67,1},{72,1},{67,1}, {62,1},{67,1},{71,1},{67,1},
    {65,1},{64,1},{62,1},{60,1}, {60,4}
};
#define NMEL ((int)(sizeof(MEL) / sizeof(MEL[0])))
static const unsigned char BASS[8] = { 48, 43, 45, 41, 48, 43, 41, 48 };
static const float SFXF[6][3] = { {0,0,0}, {660,880,0}, {523,659,784}, {988,1319,0}, {440,0,0}, {160,0,0} };
static const int   SFXMS[6][3] = { {0,0,0}, {50,70,0}, {60,60,90}, {70,170,0}, {40,0,0}, {180,0,0} };

/* 1=plantar/comprar 2=colher 3=moeda 4=clique 5=erro */
static void play_sfx(int t) { sfxQ = t; }

static float midi_freq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

static int audio_thread(SceSize args, void *argp) {
    static short buf[1024 * 2] __attribute__((aligned(64)));
    float mfreq[40], bfreq[8];
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    for (int i = 0; i < NMEL; i++) mfreq[i] = midi_freq(MEL[i][0]);
    for (int i = 0; i < 8; i++) bfreq[i] = midi_freq(BASS[i]);
    int mi = 0, mpos = 0, songStep = 0, sType = 0, sPos = 0;
    float mph = 0, bph = 0, sph = 0;
    while (running) {
        for (int i = 0; i < 1024; i++) {
            float out = 0;
            if (musicOn) {
                int nlen = MEL[mi][1] * STEP;
                float env = 1.0f - 0.6f * ((float)mpos / (float)nlen);
                float att = (mpos < 400) ? (mpos / 400.0f) : 1.0f;
                mph += mfreq[mi] / 44100.0f; if (mph >= 1.0f) mph -= 1.0f;
                bph += bfreq[(songStep / 4) % 8] / 44100.0f; if (bph >= 1.0f) bph -= 1.0f;
                out += (4.0f * fabsf(mph - 0.5f) - 1.0f) * env * att * 0.22f;
                out += (4.0f * fabsf(bph - 0.5f) - 1.0f) * 0.14f;
                if (++mpos >= nlen) {
                    mpos = 0; songStep += MEL[mi][1];
                    if (++mi >= NMEL) { mi = 0; songStep = 0; }
                }
            }
            if (sfxQ) { sType = sfxQ; sfxQ = 0; sPos = 0; }
            if (sType) {
                int ms = sPos * 1000 / 44100, acc = 0, seg = -1;
                for (int k = 0; k < 3; k++) {
                    int d = SFXMS[sType][k];
                    if (d == 0) break;
                    if (ms < acc + d) { seg = k; break; }
                    acc += d;
                }
                if (seg < 0) sType = 0;
                else {
                    sph += SFXF[sType][seg] / 44100.0f; if (sph >= 1.0f) sph -= 1.0f;
                    float dec = 1.0f - 0.5f * (float)(ms - acc) / (float)SFXMS[sType][seg];
                    out += ((sph < 0.5f) ? 1.0f : -1.0f) * 0.14f * dec;
                    sPos++;
                }
            }
            float o = out * 30000.0f;
            if (o > 32000.0f) o = 32000.0f;
            if (o < -32000.0f) o = -32000.0f;
            buf[i * 2] = buf[i * 2 + 1] = (short)o;
        }
        sceAudioOutputBlocking(ch, 0x5000, buf);
    }
    sceAudioChRelease(ch);
    return 0;
}

/* ---------- estado do jogo ---------- */
#define NTYPES 4
#define NCOW   3
#define NPIG   3
#define NFRUIT 6
#define NITEMS 13
#define NITEMS_OLD 9
#define BARN_X (-14.0f)
#define BARN_Z (-10.0f)
#define COW_X  (-28.0f)
#define COW_Z  (4.0f)
#define SHOP_X (14.0f)
#define SHOP_Z (-16.0f)
#define HOUSE_X (-14.0f)
#define HOUSE_Z (-24.0f)
#define LAKE_X  (-34.0f)
#define LAKE_Z  (-32.0f)
#define LAKE_HX 10.0f
#define LAKE_HZ 7.0f
#define ORCH_X  38.0f
#define ORCH_Z  (-2.0f)
#define HALL_X  0.0f
#define HALL_Z  (-58.0f)
#define SEASON_LEN 300.0f

enum { IT_TOMATE = 0, IT_TRIGO = 1, IT_OVO = 4, IT_LEITE = 5, IT_CARNE = 6, IT_PEIXE = 7, IT_FRUTA = 8,
       IT_PAO = 9, IT_QUEIJO = 10, IT_SUCO = 11, IT_MOLHO = 12 };

typedef struct { float x, z; int stage; float t; int type; } Plot;
typedef struct { float x, z, tx, tz, ang, t, prod; } Animal;
typedef struct { float x, z, t; int ripe; } FruitTree;
typedef struct { int item, qty, reward; } Mission;

static const char *cropName[NTYPES] = { "Tomate", "Trigo", "Milho", "Cenoura" };
static const int   cropSeed[NTYPES] = { 5, 3, 8, 6 };
static const float cropGrow[NTYPES] = { 12.0f, 8.0f, 16.0f, 14.0f };
static const char *itemName[NITEMS] = { "Tomate", "Trigo", "Milho", "Cenoura", "Ovo", "Leite", "Carne", "Peixe", "Fruta", "Pao", "Queijo", "Suco", "Molho" };
static const int   itemSell[NITEMS] = { 12, 7, 22, 15, 4, 9, 20, 14, 10, 30, 28, 40, 45 };
static const int   shopPrice[4]     = { 80, 120, 90, 200 };
static const int   recIn[4]         = { IT_TRIGO, IT_LEITE, IT_FRUTA, IT_TOMATE };
static const int   recQty[4]        = { 3, 2, 3, 3 };
static const int   recOut[4]        = { IT_PAO, IT_QUEIJO, IT_SUCO, IT_MOLHO };
static const int   houseCost[3]     = { 0, 300, 800 };
static const char *houseName[3]     = { "Cabana", "Casa", "Mansao" };
static const char *seasonName[4]    = { "Primavera", "Verao", "Outono", "Inverno" };
static const char *npcName[3]       = { "Maria", "Joao", "Ana" };
static const float npcX[3]          = { -5.0f, 5.0f, -5.0f };
static const float npcZ[3]          = { -34.0f, -42.0f, -50.0f };

static Plot plots[NPLOTS];
static Animal chick[NCHICK], cow[NCOW], pig[NPIG];
static FruitTree ftree[NFRUIT];
static Mission mis[3];
static float treeX[NTREES], treeZ[NTREES], treeH[NTREES];
static float px = 0, pz = 0, cam = 0;
static int money = 50, selCrop = 0, showHelp = 1;
static int seeds[NTYPES] = { 5, 5, 0, 0 };
static int inv[NITEMS];
static int coopEggs, penMilk, penMeat;
static int boots, fert, feed, landUp, houseLvl = 1, missionsDone;
static int raining = 0;
static float weatherT = 30.0f;
static int mode = 0, menuSel = 0;   /* 0 jogo, 1 menu, 2 loja, 3 casa, 4 prefeitura */
static char menuMsg[40] = "";
static float gameTime = 0;
static char msg[64] = "";
static float msgTimer = 0;
static int fishState = 0;           /* 0 parado, 1 esperando, 2 mordeu */
static float fishTimer = 0;
static float sleepFade = 0;
static int facingRight = 1, moving = 0;
static float walkPhase = 0;
static unsigned int seed_rng = 12345;

static const float MKT_X = 0.0f, MKT_Z = -14.0f;

static float frand(void) {
    seed_rng = seed_rng * 1664525u + 1013904223u;
    return (float)((seed_rng >> 8) & 0xFFFF) / 65535.0f;
}

static float dist2d(float ax, float az, float bx, float bz) {
    float dx = ax - bx, dz = az - bz;
    return sqrtf(dx * dx + dz * dz);
}

#define NEAR(x, z, r) (dist2d(px, pz, (x), (z)) < (r))

static int nplots(void) { return landUp ? NPLOTS : 12; }
static int season(void) { return ((int)(gameTime / SEASON_LEN)) % 4; }
static float season_mult(void) { static const float m[4] = { 1.25f, 1.0f, 1.0f, 0.5f }; return m[season()]; }
static float fruit_mult(void)  { static const float m[4] = { 0.8f, 1.0f, 1.5f, 0.0f }; return m[season()]; }

static void say(const char *s) {
    strncpy(msg, s, sizeof(msg) - 1); msg[sizeof(msg) - 1] = 0; msgTimer = 3.0f;
}

static void new_mission(int v) {
    int it;
    do {
        it = (int)(frand() * NITEMS);
        if (it >= NITEMS) it = NITEMS - 1;
    } while (it == IT_PEIXE && season() == 3);
    int q = (itemSell[it] >= 14) ? 2 + (int)(frand() * 3) : 3 + (int)(frand() * 4);
    int rw = (q * itemSell[it] * 17 / 10 / 5) * 5;
    if (rw < 20) rw = 20;
    mis[v].item = it; mis[v].qty = q; mis[v].reward = rw;
}

static void init_animals(Animal *a, int n, float cx, float cz, float half) {
    for (int i = 0; i < n; i++) {
        a[i].x = cx + (frand() * 2 - 1) * half;
        a[i].z = cz + (frand() * 2 - 1) * half;
        a[i].tx = a[i].x; a[i].tz = a[i].z;
        a[i].ang = frand() * 6.28f;
        a[i].t = frand() * 3.0f;
        a[i].prod = frand() * 20.0f;
    }
}

static void init_world(void) {
    for (int i = 0; i < NPLOTS; i++) {
        if (i < 12) { plots[i].x = 8.0f + (i % 4) * 4.0f; plots[i].z = -4.0f + (i / 4) * 4.0f; }
        else { int j = i - 12; plots[i].x = 8.0f + (j % 4) * 4.0f; plots[i].z = 12.0f + (j / 4) * 4.0f; }
        plots[i].stage = 0; plots[i].t = 0; plots[i].type = 0;
    }
    for (int i = 0; i < NTREES; i++) {
        float x, z;
        do {
            x = (frand() * 2 - 1) * (WORLD - 3);
            z = (frand() * 2 - 1) * (WORLD - 3);
        } while (dist2d(x, z, 0, 0) < 30.0f || dist2d(x, z, COW_X, COW_Z) < 14.0f ||
                 dist2d(x, z, PEN_X, PEN_Z) < 14.0f || dist2d(x, z, SHOP_X, SHOP_Z) < 10.0f ||
                 dist2d(x, z, HOUSE_X, HOUSE_Z) < 12.0f || dist2d(x, z, LAKE_X, LAKE_Z) < 17.0f ||
                 dist2d(x, z, ORCH_X, ORCH_Z) < 16.0f || dist2d(x, z, 0, -48.0f) < 24.0f);
        treeX[i] = x; treeZ[i] = z; treeH[i] = 3.0f + frand() * 3.0f;
    }
    for (int i = 0; i < NFRUIT; i++) {
        ftree[i].x = ORCH_X + (i % 3) * 5.0f - 5.0f;
        ftree[i].z = ORCH_Z + (i / 3) * 6.0f - 3.0f;
        ftree[i].t = frand() * 45.0f;
        ftree[i].ripe = 0;
    }
    init_animals(chick, NCHICK, PEN_X, PEN_Z, 6.0f);
    init_animals(cow, NCOW, COW_X, COW_Z, 6.0f);
    init_animals(pig, NPIG, COW_X, COW_Z, 6.0f);
    for (int v = 0; v < 3; v++) new_mission(v);
}

/* ---------- salvar / carregar ---------- */
#define SAVE_MAGIC 0x31475A46u
#define SAVE_DIR   "ms0:/PSP/SAVEDATA/FAZENDAGS"
#define SAVE_FILE  SAVE_DIR "/save.dat"

typedef struct {
    unsigned int magic; int version;
    int money, selCrop, coopEggs, penMilk, penMeat, raining;
    int boots, fert, feed, landUp, musicOn, houseLvl, missionsDone;
    int seeds[NTYPES], inv[NITEMS_OLD];
    int mItem[3], mQty[3], mReward[3];
    float px, pz, cam, gameTime, weatherT;
    int stage[NPLOTS], ptype[NPLOTS]; float ptime[NPLOTS];
    float ftT[NFRUIT]; int ftRipe[NFRUIT];
} SaveData;
typedef struct { unsigned int magic; int inv2[4]; } SaveExtra;

static int save_game(void) {
    SaveData d; memset(&d, 0, sizeof(d));
    d.magic = SAVE_MAGIC; d.version = 3;
    d.money = money; d.selCrop = selCrop; d.coopEggs = coopEggs; d.penMilk = penMilk;
    d.penMeat = penMeat; d.raining = raining; d.boots = boots; d.fert = fert; d.feed = feed;
    d.landUp = landUp; d.musicOn = musicOn; d.houseLvl = houseLvl; d.missionsDone = missionsDone;
    for (int i = 0; i < NTYPES; i++) d.seeds[i] = seeds[i];
    for (int i = 0; i < NITEMS_OLD; i++) d.inv[i] = inv[i];
    for (int i = 0; i < 3; i++) { d.mItem[i] = mis[i].item; d.mQty[i] = mis[i].qty; d.mReward[i] = mis[i].reward; }
    d.px = px; d.pz = pz; d.cam = cam; d.gameTime = gameTime; d.weatherT = weatherT;
    for (int i = 0; i < NPLOTS; i++) { d.stage[i] = plots[i].stage; d.ptype[i] = plots[i].type; d.ptime[i] = plots[i].t; }
    for (int i = 0; i < NFRUIT; i++) { d.ftT[i] = ftree[i].t; d.ftRipe[i] = ftree[i].ripe; }
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    int fd = sceIoOpen(SAVE_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;
    int w = sceIoWrite(fd, &d, sizeof(d));
    SaveExtra e; e.magic = SAVE_MAGIC;
    for (int i = 0; i < 4; i++) e.inv2[i] = inv[NITEMS_OLD + i];
    int w2 = sceIoWrite(fd, &e, sizeof(e));
    sceIoClose(fd);
    return w == (int)sizeof(d) && w2 == (int)sizeof(e);
}

static int load_game(void) {
    SaveData d;
    int fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0777);
    if (fd < 0) return 0;
    int r = sceIoRead(fd, &d, sizeof(d));
    SaveExtra e; int r2 = sceIoRead(fd, &e, sizeof(e));
    sceIoClose(fd);
    if (r != (int)sizeof(d) || d.magic != SAVE_MAGIC || d.version != 3) return 0;
    money = d.money; selCrop = d.selCrop; coopEggs = d.coopEggs; penMilk = d.penMilk;
    penMeat = d.penMeat; raining = d.raining; boots = d.boots; fert = d.fert; feed = d.feed;
    landUp = d.landUp; musicOn = d.musicOn; houseLvl = d.houseLvl; missionsDone = d.missionsDone;
    if (selCrop < 0 || selCrop >= NTYPES) selCrop = 0;
    if (houseLvl < 1 || houseLvl > 3) houseLvl = 1;
    for (int i = 0; i < NTYPES; i++) seeds[i] = d.seeds[i];
    for (int i = 0; i < NITEMS_OLD; i++) inv[i] = d.inv[i];
    if (r2 == (int)sizeof(e) && e.magic == SAVE_MAGIC)
        for (int i = 0; i < 4; i++) inv[NITEMS_OLD + i] = e.inv2[i];
    for (int i = 0; i < 3; i++) {
        mis[i].item = d.mItem[i]; mis[i].qty = d.mQty[i]; mis[i].reward = d.mReward[i];
        if (mis[i].item < 0 || mis[i].item >= NITEMS || mis[i].qty < 1) new_mission(i);
    }
    px = d.px; pz = d.pz; cam = d.cam; gameTime = d.gameTime; weatherT = d.weatherT;
    for (int i = 0; i < NPLOTS; i++) {
        plots[i].stage = d.stage[i]; plots[i].type = d.ptype[i]; plots[i].t = d.ptime[i];
        if (plots[i].stage < 0 || plots[i].stage > 3) plots[i].stage = 0;
        if (plots[i].type < 0 || plots[i].type >= NTYPES) plots[i].type = 0;
    }
    for (int i = 0; i < NFRUIT; i++) { ftree[i].t = d.ftT[i]; ftree[i].ripe = d.ftRipe[i] ? 1 : 0; }
    return 1;
}

/* ---------- logica ---------- */
static int near_area(float cx, float cz) { return fabsf(px - cx) < 9.0f && fabsf(pz - cz) < 9.0f; }
static int at_market(void) { return NEAR(MKT_X, MKT_Z, 6.0f); }
static int near_shop(void) { return NEAR(SHOP_X, SHOP_Z, 6.0f); }
static int near_house(void) { return NEAR(HOUSE_X, HOUSE_Z + 3.0f, 6.0f); }
static int near_hall(void) { return NEAR(HALL_X, HALL_Z + 3.0f, 8.0f); }
static int near_barn(void) { return NEAR(BARN_X, BARN_Z + 3.5f, 6.5f); }

static float lake_dist(float x, float z) {
    float dx = fabsf(x - LAKE_X) - LAKE_HX, dz = fabsf(z - LAKE_Z) - LAKE_HZ;
    if (dx < 0) dx = 0;
    if (dz < 0) dz = 0;
    return sqrtf(dx * dx + dz * dz);
}
static int in_lake(float x, float z) {
    return fabsf(x - LAKE_X) < LAKE_HX && fabsf(z - LAKE_Z) < LAKE_HZ;
}
static int near_lake(void) { return lake_dist(px, pz) < 3.5f; }

static int nearest_plot(void) {
    int best = -1; float bd = 3.0f;
    for (int i = 0; i < nplots(); i++) {
        float d = dist2d(plots[i].x, plots[i].z, px, pz);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_fruit(void) {
    int best = -1; float bd = 3.0f;
    for (int i = 0; i < NFRUIT; i++) {
        float d = dist2d(ftree[i].x, ftree[i].z, px, pz);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_npc(void) {
    for (int i = 0; i < 3; i++) if (NEAR(npcX[i], npcZ[i], 3.5f)) return i;
    return -1;
}

static void animal_wander(Animal *a, float cx, float cz, float half, float speed, float dt) {
    a->t -= dt;
    if (a->t <= 0) {
        a->tx = cx + (frand() * 2 - 1) * half;
        a->tz = cz + (frand() * 2 - 1) * half;
        a->t = 2.0f + frand() * 4.0f;
    }
    float dx = a->tx - a->x, dz = a->tz - a->z;
    float d = sqrtf(dx * dx + dz * dz);
    if (d > 0.2f) {
        a->x += dx / d * speed * dt;
        a->z += dz / d * speed * dt;
        a->ang = atan2f(dx, dz);
    }
}

/* crescimento e producao (usado ao jogar e ao dormir) */
static void grow_world(float dt, float gm) {
    float fm = feed ? 1.5f : 1.0f;
    for (int i = 0; i < NCHICK; i++) {
        chick[i].prod += dt * fm;
        if (chick[i].prod >= 25.0f) { chick[i].prod = 0; if (coopEggs < 24) coopEggs++; }
    }
    for (int i = 0; i < NCOW; i++) {
        cow[i].prod += dt * fm;
        if (cow[i].prod >= 40.0f) { cow[i].prod = 0; if (penMilk < 12) penMilk++; }
    }
    for (int i = 0; i < NPIG; i++) {
        pig[i].prod += dt * fm;
        if (pig[i].prod >= 60.0f) { pig[i].prod = 0; if (penMeat < 12) penMeat++; }
    }
    for (int i = 0; i < nplots(); i++) {
        if (plots[i].stage >= 1 && plots[i].stage < 3) {
            plots[i].t += dt * gm;
            if (plots[i].t >= cropGrow[plots[i].type]) { plots[i].t = 0; plots[i].stage++; }
        }
    }
    for (int i = 0; i < NFRUIT; i++) {
        if (!ftree[i].ripe) {
            ftree[i].t += dt * fruit_mult();
            if (ftree[i].t >= 45.0f) { ftree[i].t = 0; ftree[i].ripe = 1; }
        }
    }
}

static void do_sleep(void) {
    float tod = fmodf(gameTime, 150.0f);
    float skip = 150.0f - tod;
    if (skip < 40.0f) skip += 150.0f;
    gameTime += skip;
    grow_world(skip * (0.2f + 0.2f * houseLvl), (fert ? 1.5f : 1.0f) * season_mult());
    raining = 0; weatherT = 40.0f + frand() * 40.0f;
    sleepFade = 2.0f;
    say(save_game() ? "Dormiu bem! Jogo salvo." : "Dormiu bem! (erro ao salvar)");
    play_sfx(2);
}

static int deliver(int v) {
    if (inv[mis[v].item] < mis[v].qty) return 0;
    inv[mis[v].item] -= mis[v].qty;
    money += mis[v].reward;
    missionsDone++;
    char b[64]; sprintf(b, "Missao feita! +$%d", mis[v].reward);
    say(b); play_sfx(3);
    new_mission(v);
    return 1;
}

static void fishing_press(void) {
    if (season() == 3) { say("O lago esta congelado!"); play_sfx(5); fishState = 0; return; }
    if (fishState == 0) {
        fishState = 1; fishTimer = 2.0f + frand() * 4.0f; say("Lancou a linha..."); play_sfx(4);
    } else if (fishState == 1) {
        fishState = 0; say("Cedo demais! O peixe fugiu."); play_sfx(5);
    } else {
        fishState = 0; inv[IT_PEIXE]++; say("Pegou um peixe!"); play_sfx(2);
    }
}

/* ---------- menus ---------- */
static void menu_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 4) % 5; play_sfx(4); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 5; play_sfx(4); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) mode = 0;
        else if (menuSel == 1) { musicOn = !musicOn; play_sfx(4); }
        else if (menuSel == 2) {
            strcpy(menuMsg, save_game() ? "Jogo salvo!" : "Erro ao salvar!");
        } else if (menuSel == 3) {
            if (save_game()) running = 0;
            else strcpy(menuMsg, "Erro ao salvar!");
        } else running = 0;
    }
}

static int shop_owned(int i) { return i == 0 ? boots : i == 1 ? fert : i == 2 ? feed : landUp; }

static void shop_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 4) % 5; play_sfx(4); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 5; play_sfx(4); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 4) { mode = 0; return; }
        if (shop_owned(menuSel)) { strcpy(menuMsg, "Voce ja comprou!"); play_sfx(5); }
        else if (money < shopPrice[menuSel]) { strcpy(menuMsg, "Dinheiro insuficiente!"); play_sfx(5); }
        else {
            money -= shopPrice[menuSel];
            if (menuSel == 0) boots = 1;
            else if (menuSel == 1) fert = 1;
            else if (menuSel == 2) feed = 1;
            else landUp = 1;
            strcpy(menuMsg, "Comprado!"); play_sfx(3);
        }
    }
}

static void house_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 2) % 3; play_sfx(4); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 3; play_sfx(4); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) { do_sleep(); mode = 0; }
        else if (menuSel == 1) {
            if (houseLvl >= 3) { strcpy(menuMsg, "Casa no nivel maximo!"); play_sfx(5); }
            else if (money < houseCost[houseLvl]) { strcpy(menuMsg, "Dinheiro insuficiente!"); play_sfx(5); }
            else {
                money -= houseCost[houseLvl]; houseLvl++; play_sfx(3);
                if (houseLvl == 3) { money += 500; strcpy(menuMsg, "Casa dos sonhos! +$500"); }
                else strcpy(menuMsg, "Casa melhorada!");
            }
        } else mode = 0;
    }
}

static void hall_update(unsigned int pressed) {
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        int n = 0;
        for (int v = 0; v < 3; v++) n += deliver(v);
        if (n == 0) { strcpy(menuMsg, "Nada para entregar ainda."); play_sfx(5); }
        else strcpy(menuMsg, "Pedidos entregues!");
    }
}

static void barn_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   { menuSel = (menuSel + 4) % 5; play_sfx(4); }
    if (pressed & PSP_CTRL_DOWN) { menuSel = (menuSel + 1) % 5; play_sfx(4); }
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) { mode = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 4) { mode = 0; return; }
        if (inv[recIn[menuSel]] >= recQty[menuSel]) {
            inv[recIn[menuSel]] -= recQty[menuSel];
            inv[recOut[menuSel]]++;
            sprintf(menuMsg, "Fabricou %s!", itemName[recOut[menuSel]]);
            play_sfx(2);
        } else {
            sprintf(menuMsg, "Faltam %d %s!", recQty[menuSel] - inv[recIn[menuSel]], itemName[recIn[menuSel]]);
            play_sfx(5);
        }
    }
}

static void update(float dt, const SceCtrlData *pad, unsigned int pressed) {
    gameTime += dt;
    if (msgTimer > 0) msgTimer -= dt;

    if (pad->Buttons & PSP_CTRL_LTRIGGER) cam -= 1.8f * dt;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) cam += 1.8f * dt;

    float dx = (pad->Lx - 128) / 128.0f, dy = (pad->Ly - 128) / 128.0f;
    if (fabsf(dx) < 0.2f) dx = 0;
    if (fabsf(dy) < 0.2f) dy = 0;
    moving = (dx != 0 || dy != 0);
    if (moving) {
        float fx = sinf(cam), fz = cosf(cam);
        float rx = -cosf(cam), rz = sinf(cam);
        float mx = fx * (-dy) + rx * dx, mz = fz * (-dy) + rz * dx;
        float speed = (boots ? 12.5f : 9.0f) * dt;
        float ox = px, oz = pz;
        px += mx * speed; pz += mz * speed;
        if (px < -WORLD) px = -WORLD;
        if (px > WORLD) px = WORLD;
        if (pz < -WORLD) pz = -WORLD;
        if (pz > WORLD) pz = WORLD;
        if (in_lake(px, pz)) { px = ox; pz = oz; }
        float side = mx * rx + mz * rz;
        if (side > 0.15f) facingRight = 1;
        else if (side < -0.15f) facingRight = 0;
        walkPhase += dt * 8.0f;
    } else walkPhase = 0;

    weatherT -= dt;
    if (weatherT <= 0) {
        raining = !raining;
        weatherT = raining ? (15.0f + frand() * 15.0f) : (40.0f + frand() * 40.0f);
        if (raining) say(season() == 3 ? "Comecou a nevar!" : "Comecou a chover! Plantas crescem 2x.");
        else say(season() == 3 ? "A neve parou." : "A chuva parou.");
    }

    for (int i = 0; i < NCHICK; i++) animal_wander(&chick[i], PEN_X, PEN_Z, 6.5f, 1.2f, dt);
    for (int i = 0; i < NCOW; i++) animal_wander(&cow[i], COW_X, COW_Z, 6.5f, 0.8f, dt);
    for (int i = 0; i < NPIG; i++) animal_wander(&pig[i], COW_X, COW_Z, 6.5f, 1.0f, dt);
    grow_world(dt, ((raining && season() != 3) ? 2.0f : 1.0f) * (fert ? 1.5f : 1.0f) * season_mult());

    if (fishState == 1) {
        fishTimer -= dt;
        if (fishTimer <= 0) { fishState = 2; fishTimer = 1.3f; say("Mordeu! Aperte X!"); play_sfx(1); }
    } else if (fishState == 2) {
        fishTimer -= dt;
        if (fishTimer <= 0) { fishState = 0; say("O peixe escapou..."); }
    }
    if (fishState && !near_lake()) fishState = 0;

    if (pressed & PSP_CTRL_LEFT)  { selCrop = (selCrop + NTYPES - 1) % NTYPES; play_sfx(4); }
    if (pressed & PSP_CTRL_RIGHT) { selCrop = (selCrop + 1) % NTYPES; play_sfx(4); }
    if (pressed & PSP_CTRL_SELECT) showHelp = !showHelp;
    if (pressed & PSP_CTRL_START) { mode = 1; menuSel = 0; menuMsg[0] = 0; return; }

    if (pressed & PSP_CTRL_CROSS) {
        int i = nearest_plot(), f = nearest_fruit();
        if (i >= 0) {
            if (plots[i].stage == 0) {
                if (seeds[selCrop] > 0) {
                    seeds[selCrop]--; plots[i].stage = 1; plots[i].t = 0; plots[i].type = selCrop;
                    say("Plantou!"); play_sfx(1);
                } else { say("Sem sementes deste tipo!"); play_sfx(5); }
            } else if (plots[i].stage == 3) {
                inv[plots[i].type]++; plots[i].stage = 0; say("Colheu!"); play_sfx(2);
            } else say("Ainda crescendo...");
        } else if (f >= 0) {
            if (ftree[f].ripe) { ftree[f].ripe = 0; ftree[f].t = 0; inv[IT_FRUTA] += 2; say("Colheu 2 frutas!"); play_sfx(2); }
            else say(season() == 3 ? "A arvore dorme no inverno." : "Fruta ainda verde...");
        } else if (near_lake()) fishing_press();
        else { say("Nada para fazer aqui."); play_sfx(5); }
    }

    if (pressed & PSP_CTRL_SQUARE) {
        if (!at_market()) { say("Va ate o mercado."); play_sfx(5); }
        else if (money >= cropSeed[selCrop]) {
            money -= cropSeed[selCrop]; seeds[selCrop]++;
            say("Comprou 1 semente!"); play_sfx(1);
        } else { say("Dinheiro insuficiente."); play_sfx(5); }
    }

    if (pressed & PSP_CTRL_CIRCLE) {
        if (!at_market()) { say("Va ate o mercado."); play_sfx(5); }
        else {
            int total = 0;
            for (int t = 0; t < NITEMS; t++) total += inv[t] * itemSell[t];
            if (total > 0) {
                money += total;
                for (int t = 0; t < NITEMS; t++) inv[t] = 0;
                char b[48]; sprintf(b, "Vendeu tudo! +$%d", total); say(b); play_sfx(3);
            } else { say("Nada para vender."); play_sfx(5); }
        }
    }

    if (pressed & PSP_CTRL_TRIANGLE) {
        int v = nearest_npc();
        if (v >= 0) {
            if (!deliver(v)) {
                char b[64]; sprintf(b, "Faltam %d %s!", mis[v].qty - inv[mis[v].item], itemName[mis[v].item]);
                say(b); play_sfx(5);
            }
        } else if (near_hall()) { mode = 4; menuMsg[0] = 0; play_sfx(4); return; }
        else if (near_barn()) { mode = 5; menuSel = 0; menuMsg[0] = 0; play_sfx(4); return; }
        else if (near_house()) { mode = 3; menuSel = 0; menuMsg[0] = 0; play_sfx(4); return; }
        else if (near_shop()) { mode = 2; menuSel = 0; menuMsg[0] = 0; play_sfx(4); return; }
        else if (near_area(PEN_X, PEN_Z)) {
            if (coopEggs > 0) { inv[IT_OVO] += coopEggs; coopEggs = 0; say("Coletou os ovos!"); play_sfx(2); }
            else say("Sem ovos por enquanto.");
        } else if (near_area(COW_X, COW_Z)) {
            if (penMilk + penMeat > 0) {
                inv[IT_LEITE] += penMilk; inv[IT_CARNE] += penMeat; penMilk = penMeat = 0;
                say("Coletou leite e carne!"); play_sfx(2);
            } else say("Nada para coletar ainda.");
        } else say("Va a casa, loja, vila ou aos animais.");
    }
}

/* ---------- personagens 2D (sprites) ---------- */
#define SPR_W 12
#define SPR_H 18
#define SPR_MAXV 1700

typedef struct { unsigned int color; float x, y, z; } CV;
#define CV_TYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
typedef struct { unsigned int hat, band, shirt, pants, boots, skin; int tool; } Pal;
typedef struct { CV v[SPR_MAXV]; int n; } __attribute__((aligned(16))) Mesh;

static const char *SPR_A[SPR_H] = {
    "....cccc....",
    "...cccccc...",
    ".cccccccccc.",
    "...dddddd...",
    "...ssssss.gg",
    "...sesses.gg",
    "...ssmmss.t.",
    "....ssss..t.",
    "..aaaaaaaat.",
    ".aaaaaaaaas.",
    ".aaaaaaaaat.",
    ".saaaaaaaat.",
    "..aaaaaaaat.",
    "..bbbbbbbbt.",
    "..bbb..bbbt.",
    "..bbb..bbbt.",
    "..kkk..kkk..",
    "..kkk..kkk.."
};
static const char *SPR_B[4] = {
    "..bbbbbbbbt.",
    "...bb..bb.t.",
    "...kk..kk...",
    "...kkk.kkk.."
};

static Mesh __attribute__((aligned(16))) meshPlayer[2][2];   /* [passo][0=direita,1=esquerda] */
static Mesh __attribute__((aligned(16))) meshNpc[3];

static unsigned int pal_color(char ch, const Pal *p) {
    switch (ch) {
    case 'c': return p->hat;   case 'd': return p->band;
    case 'a': return p->shirt; case 'b': return p->pants;
    case 'k': return p->boots; case 's': return p->skin;
    case 'e': return 0xff202020; case 'm': return 0xff3030a0;
    case 't': return p->tool ? 0xff3a6a9a : 0;
    case 'g': return p->tool ? 0xffc0c0c0 : 0;
    default: return 0;
    }
}

static char spr_px(int r, int c, int frameB) {
    if (r < 0 || r >= SPR_H || c < 0 || c >= SPR_W) return '.';
    if (frameB && r >= 14) return SPR_B[r - 14][c];
    return SPR_A[r][c];
}

static void add_quad(Mesh *m, float x0, float yb, float x1, float yt, unsigned int col) {
    if (m->n + 6 > SPR_MAXV) return;
    CV *v = &m->v[m->n];
    float xs[6] = { x0, x1, x1, x0, x1, x0 };
    float ys[6] = { yb, yb, yt, yb, yt, yt };
    for (int k = 0; k < 6; k++) { v[k].color = col; v[k].x = xs[k]; v[k].y = ys[k]; v[k].z = 0; }
    m->n += 6;
}

static void build_mesh(Mesh *m, const Pal *p, int frameB, int flip) {
    const float ps = 0.12f;
    float sg = flip ? 1.0f : -1.0f;
    m->n = 0;
    for (int r = -1; r <= SPR_H; r++) {
        for (int c = -1; c <= SPR_W; c++) {
            unsigned int col = pal_color(spr_px(r, c, frameB), p);
            if (!col) {
                if (pal_color(spr_px(r - 1, c, frameB), p) || pal_color(spr_px(r + 1, c, frameB), p) ||
                    pal_color(spr_px(r, c - 1, frameB), p) || pal_color(spr_px(r, c + 1, frameB), p))
                    col = 0xff101830;
                else continue;
            }
            add_quad(m, sg * (c - 6) * ps, (SPR_H - r - 1) * ps, sg * (c + 1 - 6) * ps, (SPR_H - r) * ps, col);
        }
    }
}

static void build_sprites(void) {
    Pal pl = { 0xff40d0f0, 0xff2020c0, 0xff2828d8, 0xffd04020, 0xff203050, 0xff90b8f0, 1 };
    Pal np[3] = {
        { 0xff3070e0, 0xff2050a0, 0xffc050e0, 0xff80402a, 0xff101830, 0xffa0c0f8, 0 },
        { 0xff404040, 0xff606060, 0xff30a030, 0xff506080, 0xff202020, 0xff80a8e0, 0 },
        { 0xffe0e0e0, 0xffd09050, 0xfff0a030, 0xff404080, 0xff202020, 0xffa0c0f8, 0 }
    };
    for (int f = 0; f < 2; f++) for (int fl = 0; fl < 2; fl++) build_mesh(&meshPlayer[f][fl], &pl, f, fl);
    for (int i = 0; i < 3; i++) build_mesh(&meshNpc[i], &np[i], 0, 0);
    sceKernelDcacheWritebackAll();
}

static void draw_sprite(const Mesh *m, float x, float z) {
    ScePspFVector3 p = { x, 0.0f, z };
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&p);
    sceGumRotateY(cam);
    sceGumUpdateMatrix();
    sceGumDrawArray(GU_TRIANGLES, CV_TYPE, m->n, 0, m->v);
}

/* ---------- desenho do mundo ---------- */
static void fence_line(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1;
    float len = sqrtf(dx * dx + dz * dz);
    float mx = (x1 + x2) * 0.5f, mz = (z1 + z2) * 0.5f;
    if (dist2d(px, pz, mx, mz) > len * 0.5f + 45.0f) return;
    float ang = atan2f(dx, dz);
    int n = (int)(len / 6.0f);
    if (n < 1) n = 1;
    box(mx, 0.35f, mz, 0.12f, 0.12f, len, ang, 0xff3a6a9a);
    box(mx, 0.75f, mz, 0.12f, 0.12f, len, ang, 0xff3a6a9a);
    for (int i = 0; i <= n; i++)
        box(x1 + dx * i / n, 0, z1 + dz * i / n, 0.25f, 1.0f, 0.25f, 0, 0xff2a4a70);
}

static void fence_rect(float cx, float cz, float half) {
    fence_line(cx - half, cz - half, cx + half, cz - half);
    fence_line(cx + half, cz - half, cx + half, cz + half);
    fence_line(cx + half, cz + half, cx - half, cz + half);
    fence_line(cx - half, cz + half, cx - half, cz - half);
}

static void draw_crop(const Plot *p) {
    float x = p->x, z = p->z;
    if (p->stage == 0) return;
    if (p->stage == 1) { box(x, 0.2f, z, 0.4f, 0.4f, 0.4f, 0, 0xff30c030); return; }
    int ripe = (p->stage == 3);
    switch (p->type) {
    case 0:
        if (!ripe) box(x, 0.2f, z, 0.8f, 1.0f, 0.8f, 0, 0xff20b040);
        else {
            box(x, 0.2f, z, 1.0f, 1.2f, 1.0f, 0, 0xff20a030);
            box(x, 1.2f, z, 0.9f, 0.7f, 0.9f, 0, 0xff1e50e8);
        }
        break;
    case 1:
        for (int k = 0; k < 4; k++) {
            float ox = (k % 2) ? 0.6f : -0.6f, oz = (k / 2) ? 0.6f : -0.6f;
            if (!ripe) box(x + ox, 0.2f, z + oz, 0.25f, 1.2f, 0.25f, 0, 0xff40c070);
            else       box(x + ox, 0.2f, z + oz, 0.25f, 1.6f, 0.25f, 0, 0xff30b0e0);
        }
        break;
    case 2:
        if (!ripe) box(x, 0.2f, z, 0.5f, 1.8f, 0.5f, 0, 0xff30a030);
        else {
            box(x, 0.2f, z, 0.5f, 2.4f, 0.5f, 0, 0xff30a030);
            box(x + 0.35f, 1.2f, z, 0.3f, 0.9f, 0.3f, 0, 0xff30e0ff);
        }
        break;
    default:
        box(x, 0.2f, z, 0.8f, ripe ? 0.7f : 0.6f, 0.8f, 0, 0xff30b040);
        if (ripe) box(x, 0.15f, z, 0.45f, 0.35f, 0.45f, 0, 0xff2080ff);
        break;
    }
}

static void leg(const Animal *a, float lx, float lz, float w, float h, unsigned int col) {
    float c = cosf(a->ang), s = sinf(a->ang);
    box(a->x + lx * c + lz * s, 0, a->z - lx * s + lz * c, w, h, w, a->ang, col);
}

static void draw_cow(const Animal *a) {
    box(a->x, 0.5f, a->z, 1.1f, 0.9f, 1.8f, a->ang, 0xfff0f0f0);
    box(a->x, 1.38f, a->z, 0.6f, 0.1f, 0.8f, a->ang, 0xff202020);
    box(a->x + sinf(a->ang) * 1.1f, 0.9f, a->z + cosf(a->ang) * 1.1f, 0.6f, 0.6f, 0.6f, a->ang, 0xff6080a0);
    leg(a, -0.35f, -0.6f, 0.2f, 0.5f, 0xff303030); leg(a, 0.35f, -0.6f, 0.2f, 0.5f, 0xff303030);
    leg(a, -0.35f, 0.6f, 0.2f, 0.5f, 0xff303030);  leg(a, 0.35f, 0.6f, 0.2f, 0.5f, 0xff303030);
}

static void draw_pig(const Animal *a) {
    box(a->x, 0.3f, a->z, 0.9f, 0.6f, 1.3f, a->ang, 0xffb0a0ff);
    box(a->x + sinf(a->ang) * 0.8f, 0.35f, a->z + cosf(a->ang) * 0.8f, 0.6f, 0.5f, 0.5f, a->ang, 0xffb0a0ff);
    box(a->x + sinf(a->ang) * 1.1f, 0.4f, a->z + cosf(a->ang) * 1.1f, 0.3f, 0.25f, 0.15f, a->ang, 0xff8080e0);
    leg(a, -0.3f, -0.4f, 0.18f, 0.3f, 0xff8080e0); leg(a, 0.3f, -0.4f, 0.18f, 0.3f, 0xff8080e0);
    leg(a, -0.3f, 0.4f, 0.18f, 0.3f, 0xff8080e0);  leg(a, 0.3f, 0.4f, 0.18f, 0.3f, 0xff8080e0);
}

static unsigned int foliage(int i) {
    static const unsigned int aut[3] = { 0xff2080e0, 0xff30b0f0, 0xff2040c0 };
    switch (season()) {
    case 0: return (i % 3 == 0) ? 0xffa0c8ff : 0xff40b858;
    case 1: return 0xff208030;
    case 2: return aut[i % 3];
    default: return 0xfff4f4f4;
    }
}

static void draw_fruit_tree(const FruitTree *t, int idx) {
    unsigned int leaf = (season() == 0) ? 0xffc0a0ff : foliage(idx);
    unsigned int fruit = (idx % 2) ? 0xff2080ff : 0xff2020e0;
    box(t->x, 0, t->z, 0.7f, 2.2f, 0.7f, 0, 0xff204060);
    box(t->x, 1.8f, t->z, 3.0f, 2.4f, 3.0f, 0, leaf);
    if (t->ripe) {
        box(t->x + 1.55f, 2.4f, t->z, 0.5f, 0.5f, 0.5f, 0, fruit);
        box(t->x - 1.55f, 2.8f, t->z, 0.5f, 0.5f, 0.5f, 0, fruit);
        box(t->x, 2.6f, t->z + 1.55f, 0.5f, 0.5f, 0.5f, 0, fruit);
        box(t->x, 2.2f, t->z - 1.55f, 0.5f, 0.5f, 0.5f, 0, fruit);
    }
}

static void small_house(float x, float z, unsigned int wall, unsigned int roof) {
    box(x, 0, z, 5.0f, 3.0f, 5.0f, 0, wall);
    box(x, 3.0f, z, 5.6f, 1.0f, 5.6f, 0, roof);
    box(x, 0, z + 2.55f, 1.0f, 2.0f, 0.12f, 0, 0xff203050);
    box(x + 1.6f, 1.2f, z + 2.55f, 0.8f, 0.8f, 0.1f, 0, 0xfff0e0a0);
}

static void draw_player_house(void) {
    float x = HOUSE_X, z = HOUSE_Z;
    box(x, 0, z, 6.0f, 3.5f, 6.0f, 0, 0xffa8c8e8);
    if (houseLvl >= 2) box(x, 3.5f, z, 5.4f, 2.8f, 5.4f, 0, 0xffb8d8f0);
    float ry = (houseLvl >= 2) ? 6.3f : 3.5f;
    box(x, ry, z, 6.8f, 1.2f, 6.8f, 0, 0xff204080);
    box(x, 0, z + 3.05f, 1.2f, 2.2f, 0.15f, 0, 0xff203050);
    box(x - 2.0f, 1.2f, z + 3.05f, 1.0f, 1.0f, 0.12f, 0, 0xfff0e0a0);
    box(x + 2.0f, 1.2f, z + 3.05f, 1.0f, 1.0f, 0.12f, 0, 0xfff0e0a0);
    if (houseLvl >= 2) {
        box(x, 4.3f, z + 2.75f, 1.0f, 1.0f, 0.12f, 0, 0xfff0e0a0);
        box(x + 2.0f, ry + 0.8f, z - 1.5f, 0.8f, 2.0f, 0.8f, 0, 0xff303060);
    }
    if (houseLvl >= 3) {
        box(x + 6.0f, 0, z, 4.0f, 3.0f, 5.0f, 0, 0xffd0e0f0);
        box(x + 6.0f, 3.0f, z, 4.6f, 1.0f, 5.6f, 0, 0xff204080);
        box(x, 0, z + 4.4f, 5.0f, 0.2f, 2.4f, 0, 0xff3a6a9a);
        box(x - 2.2f, 0, z + 5.4f, 0.3f, 2.4f, 0.3f, 0, 0xffe0e0e0);
        box(x + 2.2f, 0, z + 5.4f, 0.3f, 2.4f, 0.3f, 0, 0xffe0e0e0);
        box(x, 2.4f, z + 4.6f, 5.4f, 0.2f, 2.8f, 0, 0xff204080);
        for (int i = 0; i < 5; i++)
            box(x - 2.6f + i * 1.3f, 0, z + 6.4f, 0.4f, 0.5f, 0.4f, 0, (i % 2) ? 0xff30e0ff : 0xff2020e0);
    }
}

static void draw_hall(void) {
    box(HALL_X, 0, HALL_Z, 12.0f, 5.0f, 6.0f, 0, 0xffe0e0e8);
    box(HALL_X, 5.0f, HALL_Z, 12.8f, 1.2f, 6.8f, 0, 0xff303050);
    box(HALL_X, 6.2f, HALL_Z, 2.2f, 3.5f, 2.2f, 0, 0xffd0d0d8);
    box(HALL_X, 9.7f, HALL_Z, 0.15f, 3.0f, 0.15f, 0, 0xff202020);
    box(HALL_X + 0.7f, 11.4f, HALL_Z, 1.2f, 0.8f, 0.1f, 0, 0xff2020d0);
    for (int i = 0; i < 4; i++) box(HALL_X - 4.0f + i * 2.7f, 0, HALL_Z + 3.3f, 0.5f, 4.0f, 0.5f, 0, 0xfff0f0f0);
    box(HALL_X, 0, HALL_Z + 3.05f, 2.0f, 3.0f, 0.15f, 0, 0xff204070);
    box(HALL_X, 4.0f, HALL_Z + 3.1f, 5.0f, 0.8f, 0.1f, 0, 0xff30e0ff);
}

static void draw_scene(void) {
    static const unsigned int g1[4] = { 0xff50b050, 0xff3c9a3c, 0xff3a8ac0, 0xfff0f0f0 };
    static const unsigned int g2[4] = { 0xff48a848, 0xff328a34, 0xff3078b0, 0xffe0e4e8 };
    static const float skyR[4] = { 110, 90, 190, 170 }, skyG[4] = { 190, 170, 160, 190 }, skyB[4] = { 235, 245, 130, 205 };
    int sn = season();
    groundC1 = g1[sn]; groundC2 = g2[sn];

    float day = 0.5f + 0.5f * cosf(gameTime * 2.0f * PI_F / 150.0f); /* 1=dia */
    float b = 0.25f + 0.75f * day;
    if (raining) b *= 0.75f;

    unsigned int sky = 0xff000000
        | ((unsigned int)(skyB[sn] * b) << 16) | ((unsigned int)(skyG[sn] * b) << 8) | (unsigned int)(skyR[sn] * b);
    sceGuClearColor(sky);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(70.0f, 16.0f / 9.0f, 1.0f, 200.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = { px - sinf(cam) * 11.0f, 7.0f, pz - cosf(cam) * 11.0f };
    ScePspFVector3 ctr = { px, 1.5f, pz };
    ScePspFVector3 up  = { 0, 1, 0 };
    sceGumLookAt(&eye, &ctr, &up);

    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    ScePspFVector3 dir = { 0.4f, 1.0f, 0.3f };
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &dir);
    unsigned int lc = 0xff000000 | ((unsigned int)(165*b) << 16) | ((unsigned int)(165*b) << 8) | (unsigned int)(165*b);
    sceGuLightColor(0, GU_DIFFUSE, lc);
    sceGuAmbient(0xff000000 | ((unsigned int)(105*b) << 16) | ((unsigned int)(105*b) << 8) | (unsigned int)(105*b));
    sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);

    draw_ground();
    box(0, 0.04f, -28.0f, 3.0f, 0.08f, 56.0f, 0, 0xff6f8fa8);

    /* canteiros e cercas */
    if (NEAR(16, 0, 45)) {
        for (int i = 0; i < nplots(); i++) {
            box(plots[i].x, 0.06f, plots[i].z, 3.0f, 0.12f, 3.0f, 0, 0xff2a4a6b);
            draw_crop(&plots[i]);
        }
    }
    fence_line(5, -8, 23, -8);
    fence_line(23, -8, 23, 8);
    fence_line(23, 8, 5, 8);
    fence_line(5, 8, 5, -8);
    if (landUp) {
        fence_line(5, 10, 23, 10);
        fence_line(23, 10, 23, 24);
        fence_line(23, 24, 5, 24);
        fence_line(5, 24, 5, 10);
    }

    /* celeiro, casa do jogador */
    if (NEAR(BARN_X, BARN_Z, 70)) {
        box(BARN_X, 0, BARN_Z, 8, 4.5f, 6, 0, 0xff2828b0);
        box(BARN_X, 4.5f, BARN_Z, 8.4f, 1.5f, 6.4f, 0, 0xff303030);
        box(BARN_X, 0, BARN_Z + 3.05f, 3.2f, 3.4f, 0.15f, 0, 0xffe0e8f0);
        box(BARN_X, 0, BARN_Z + 3.12f, 0.2f, 3.4f, 0.1f, 0, 0xff2828b0);
        box(BARN_X, 4.8f, BARN_Z + 3.15f, 3.6f, 0.9f, 0.1f, 0, 0xff30e0ff);
        box(BARN_X + 5.2f, 0, BARN_Z + 2.0f, 1.4f, 1.0f, 1.4f, 0, 0xff30c0e0);
        box(BARN_X - 5.2f, 0, BARN_Z + 2.4f, 1.4f, 1.0f, 1.4f, 0, 0xff30c0e0);
        box(BARN_X + 6.5f, 0, BARN_Z - 1.5f, 2.2f, 7.0f, 2.2f, 0, 0xffc8c8c8);
        box(BARN_X + 6.5f, 7.0f, BARN_Z - 1.5f, 2.5f, 0.8f, 2.5f, 0, 0xff303030);
    }
    if (NEAR(HOUSE_X, HOUSE_Z, 70)) draw_player_house();

    /* mercado e loja */
    if (NEAR(MKT_X, MKT_Z, 70)) {
        box(MKT_X - 2, 0, MKT_Z, 0.4f, 3, 0.4f, 0, 0xff204060);
        box(MKT_X + 2, 0, MKT_Z, 0.4f, 3, 0.4f, 0, 0xff204060);
        box(MKT_X, 1.0f, MKT_Z, 4.4f, 0.8f, 1.5f, 0, 0xff3090d0);
        box(MKT_X, 3.0f, MKT_Z, 5.5f, 0.3f, 3.0f, 0, 0xff2020d0);
        box(SHOP_X, 0, SHOP_Z, 6.0f, 3.2f, 4.0f, 0, 0xffc06020);
        box(SHOP_X, 3.2f, SHOP_Z, 6.6f, 0.6f, 4.6f, 0, 0xff2020a0);
        box(SHOP_X, 3.8f, SHOP_Z + 2.0f, 3.0f, 1.0f, 0.2f, 0, 0xff30e0ff);
        box(SHOP_X, 0, SHOP_Z + 2.05f, 1.2f, 2.0f, 0.15f, 0, 0xff203050);
    }

    /* lago */
    if (NEAR(LAKE_X, LAKE_Z, 70)) {
        box(LAKE_X, 0.02f, LAKE_Z, LAKE_HX * 2.0f, 0.1f, LAKE_HZ * 2.0f, 0, (sn == 3) ? 0xfff0e0c8 : 0xffc07030);
        box(LAKE_X + LAKE_HX, 0.1f, LAKE_Z, 3.0f, 0.25f, 1.2f, 0, 0xff3a6a9a);
    }

    /* pomar */
    if (NEAR(ORCH_X, ORCH_Z, 70)) {
        fence_rect(ORCH_X, ORCH_Z, 9.0f);
        for (int i = 0; i < NFRUIT; i++) draw_fruit_tree(&ftree[i], i);
    }

    /* vila */
    if (NEAR(0, -48, 75)) {
        small_house(-12.0f, -34.0f, 0xffc8d8f0, 0xff2040a0);
        small_house(12.0f, -42.0f, 0xffb0e0c0, 0xff204080);
        small_house(-12.0f, -50.0f, 0xfff0d0b0, 0xff303090);
        draw_hall();
    }

    /* galinheiro */
    if (NEAR(PEN_X, PEN_Z, 70)) {
        fence_rect(PEN_X, PEN_Z, 8.0f);
        box(PEN_X - 5, 0, PEN_Z - 5, 3.0f, 2.0f, 3.0f, 0, 0xff4070c0);
        box(PEN_X - 5, 2.0f, PEN_Z - 5, 3.4f, 0.8f, 3.4f, 0, 0xff202080);
        for (int i = 0; i < NCHICK; i++) {
            Animal *c = &chick[i];
            box(c->x, 0.15f, c->z, 0.5f, 0.4f, 0.7f, c->ang, 0xfff0f0f0);
            box(c->x + sinf(c->ang) * 0.4f, 0.45f, c->z + cosf(c->ang) * 0.4f,
                0.25f, 0.3f, 0.25f, c->ang, 0xff2020e0);
        }
        for (int i = 0; i < coopEggs && i < 24; i++)
            box(PEN_X + 3.0f + (i % 6) * 0.5f, 0.05f, PEN_Z + 6.0f - (i / 6) * 0.5f,
                0.25f, 0.3f, 0.25f, 0, 0xffd0f0ff);
    }

    /* curral */
    if (NEAR(COW_X, COW_Z, 70)) {
        fence_rect(COW_X, COW_Z, 8.0f);
        box(COW_X - 5, 0, COW_Z - 5, 4.0f, 2.5f, 3.0f, 0, 0xff2a3a90);
        box(COW_X - 5, 2.5f, COW_Z - 5, 4.4f, 0.8f, 3.4f, 0, 0xff202020);
        for (int i = 0; i < NCOW; i++) draw_cow(&cow[i]);
        for (int i = 0; i < NPIG; i++) draw_pig(&pig[i]);
        for (int i = 0; i < penMilk && i < 12; i++)
            box(COW_X + 2.0f + (i % 6) * 0.5f, 0.05f, COW_Z + 6.5f - (i / 6) * 0.5f,
                0.3f, 0.5f, 0.3f, 0, 0xfff8f8f8);
        for (int i = 0; i < penMeat && i < 12; i++)
            box(COW_X - 3.0f + (i % 6) * 0.5f, 0.05f, COW_Z + 6.5f - (i / 6) * 0.5f,
                0.35f, 0.25f, 0.35f, 0, 0xff4040d0);
    }

    /* arvores */
    for (int i = 0; i < NTREES; i++) {
        float tdx = treeX[i] - px, tdz = treeZ[i] - pz;
        if (tdx * tdx + tdz * tdz > 58.0f * 58.0f) continue;
        box(treeX[i], 0, treeZ[i], 0.8f, treeH[i], 0.8f, 0, 0xff204060);
        box(treeX[i], treeH[i] - 0.5f, treeZ[i], 3.2f, 2.8f, 3.2f, 0.4f * i, foliage(i));
    }

    /* chuva ou neve */
    if (raining) {
        for (int i = 0; i < NDROPS; i++) {
            float ox = ((i * 37) % 31) - 15.0f;
            float oz = ((i * 53) % 29) - 14.0f;
            if (sn == 3) {
                float y = 12.0f - fmodf((i * 1.3f) + gameTime * 3.5f, 12.0f);
                box(px + ox + sinf(gameTime + i) * 0.6f, y, pz + oz, 0.14f, 0.14f, 0.14f, 0, 0xffffffff);
            } else {
                float y = 14.0f - fmodf((i * 1.7f) + gameTime * 18.0f, 14.0f);
                box(px + ox, y, pz + oz, 0.05f, 0.7f, 0.05f, 0, 0xffffc8a0);
            }
        }
    }

    /* sombras */
    box(px, 0.1f, pz, 1.0f, 0.02f, 0.6f, 0, 0xff000000);
    for (int i = 0; i < 3; i++)
        if (NEAR(npcX[i], npcZ[i], 80)) box(npcX[i], 0.1f, npcZ[i], 1.0f, 0.02f, 0.6f, 0, 0xff000000);

    /* personagens 2D (sprites, sem luz) */
    sceGuDisable(GU_LIGHTING);
    for (int i = 0; i < 3; i++)
        if (NEAR(npcX[i], npcZ[i], 80)) draw_sprite(&meshNpc[i], npcX[i], npcZ[i]);
    draw_sprite(&meshPlayer[moving ? (((int)walkPhase) & 1) : 0][facingRight ? 0 : 1], px, pz);
}

static const unsigned char font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x56,0x20,0x50},
    {0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},
    {0x2A,0x1C,0x7F,0x1C,0x2A},
    {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00},
    {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x72,0x49,0x49,0x49,0x46},
    {0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x31},
    {0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},
    {0x46,0x49,0x49,0x29,0x1E},
    {0x00,0x00,0x14,0x00,0x00},
    {0x00,0x40,0x34,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},
    {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E},
    {0x7C,0x12,0x11,0x12,0x7C},
    {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},
    {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03},
    {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},
    {0x61,0x59,0x49,0x4D,0x43},
    {0x00,0x7F,0x41,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x41,0x7F},
    {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x03,0x07,0x08,0x00},
    {0x20,0x54,0x54,0x78,0x40},
    {0x7F,0x28,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x28},
    {0x38,0x44,0x44,0x28,0x7F},
    {0x38,0x54,0x54,0x54,0x18},
    {0x00,0x08,0x7E,0x09,0x02},
    {0x18,0xA4,0xA4,0x9C,0x78},
    {0x7F,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x40,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},
    {0x7C,0x04,0x78,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38},
    {0xFC,0x18,0x24,0x24,0x18},
    {0x18,0x24,0x24,0x18,0xFC},
    {0x7C,0x08,0x04,0x04,0x08},
    {0x48,0x54,0x54,0x54,0x24},
    {0x04,0x04,0x3F,0x44,0x24},
    {0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},
    {0x4C,0x90,0x90,0x90,0x7C},
    {0x44,0x64,0x54,0x4C,0x44},
    {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x77,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},
    {0x08,0x04,0x08,0x10,0x08}
};

typedef struct { unsigned int color; short x, y, z; } V2;

static void rect2d(int x0, int y0, int x1, int y1, unsigned int color) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v[0].color = color; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].color = color; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

/* texto desenhado pela GPU (fonte 5x7), funciona em qualquer emulador */
static void ui_text(int x, int y, int sc, unsigned int color, const char *s) {
    int n = 0;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p; if (c < 32 || c > 126) c = 32;
        for (int col = 0; col < 5; col++) {
            unsigned char b = font5x7[c - 32][col]; int in = 0;
            for (int r = 0; r < 8; r++) {
                int bit = (b >> r) & 1;
                if (bit && !in) n++;
                in = bit;
            }
        }
    }
    if (n == 0) return;
    V2 *v = (V2 *)sceGuGetMemory(n * 2 * sizeof(V2));
    int k = 0, cx = x;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p; if (c < 32 || c > 126) c = 32;
        for (int col = 0; col < 5; col++) {
            unsigned char b = font5x7[c - 32][col]; int start = -1;
            for (int r = 0; r <= 8; r++) {
                int bit = (r < 8) ? ((b >> r) & 1) : 0;
                if (bit && start < 0) start = r;
                else if (!bit && start >= 0) {
                    v[k].color = color; v[k].x = cx + col * sc; v[k].y = y + start * sc; v[k].z = 0; k++;
                    v[k].color = color; v[k].x = cx + (col + 1) * sc; v[k].y = y + r * sc; v[k].z = 0; k++;
                    start = -1;
                }
            }
        }
        cx += 6 * sc;
    }
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, k, 0, v);
}

static void ui_shadow(int x, int y, int sc, unsigned int color, const char *s) {
    ui_text(x, y, sc, color, s);
}

static void window(int x0, int y0, int x1, int y1) {
    rect2d(x0, y0, x1, y1, 0xf060c0f0);
    rect2d(x0 + 4, y0 + 4, x1 - 4, y1 - 4, 0xf0201008);
}

static void draw_ui(void) {
    char buf[96];
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    /* HUD */
    rect2d(0, 0, 372, 76, 0x90000000);
    sprintf(buf, "$%d Ovo:%d Leite:%d Carne:%d", money, inv[IT_OVO], inv[IT_LEITE], inv[IT_CARNE]);
    ui_shadow(4, 3, 2, 0xff40e0ff, buf);
    sprintf(buf, "< %s >  Sem:%d  Col:%d", cropName[selCrop], seeds[selCrop], inv[selCrop]);
    ui_shadow(4, 21, 2, 0xffffffff, buf);
    sprintf(buf, "Dia %d %s %s", (int)(gameTime / 150.0f) + 1, seasonName[season()],
            raining ? (season() == 3 ? "NEVE" : "CHUVA") : "");
    ui_shadow(4, 39, 2, 0xffc0ffc0, buf);
    sprintf(buf, "Peixe:%d Fruta:%d Missoes:%d", inv[IT_PEIXE], inv[IT_FRUTA], missionsDone);
    ui_shadow(4, 57, 2, 0xffffffff, buf);

    if (mode == 0) {
        if (showHelp) {
            static const char *help[6] = {
                "Analog:andar  L/R:camera  < >:semente",
                "X:plantar/colher/pescar  Quad:comprar semente",
                "Circ:vender tudo (mercado)",
                "Tri:falar/entrar/coletar (casa, celeiro, vila,",
                "   loja, galinheiro, curral)   START:menu",
                "SELECT:ajuda"
            };
            rect2d(0, 78, 276, 136, 0x90000000);
            for (int i = 0; i < 6; i++) ui_shadow(4, 82 + i * 9, 1, 0xffffffff, help[i]);
        }
        if (msgTimer > 0) {
            int w = (int)strlen(msg) * 12, x = (480 - w) / 2;
            rect2d(x - 6, 184, x + w + 6, 206, 0xa0000000);
            ui_shadow(x, 188, 2, 0xffffffff, msg);
        }
        const char *pr = NULL; char pb[64], sub[64]; sub[0] = 0;
        int v = nearest_npc();
        if (v >= 0) {
            sprintf(pb, "%s: %d %s ($%d)", npcName[v], mis[v].qty, itemName[mis[v].item], mis[v].reward);
            pr = pb;
            sprintf(sub, "Tri: entregar   (voce tem %d)", inv[mis[v].item]);
        } else if (near_hall()) {
            pr = "PREFEITURA  Tri: abrir quadro";
        } else if (near_barn()) {
            pr = "CELEIRO  Tri: fabricar produtos";
        } else if (near_house()) {
            sprintf(pb, "CASA nivel %d  Tri: abrir", houseLvl); pr = pb;
        } else if (at_market()) {
            pr = "MERCADO  Quad:comprar  Circ:vender";
            sprintf(sub, "Semente %s: $%d   (< > troca o tipo)", cropName[selCrop], cropSeed[selCrop]);
        } else if (near_shop()) {
            pr = "LOJA  Tri: abrir";
        } else {
            int i = nearest_plot(), f = nearest_fruit();
            if (i >= 0) {
                if (plots[i].stage == 0) { sprintf(pb, "X: plantar %s", cropName[selCrop]); pr = pb; }
                else if (plots[i].stage == 3) pr = "X: colher";
                else pr = "Crescendo...";
            } else if (f >= 0) {
                pr = ftree[f].ripe ? "X: colher fruta" : "Fruta amadurecendo...";
            } else if (near_lake()) {
                if (season() == 3) pr = "Lago congelado!";
                else if (fishState == 0) pr = "X: pescar";
                else if (fishState == 1) pr = "Esperando o peixe...";
                else pr = "PEIXE! Aperte X!";
            } else if (near_area(PEN_X, PEN_Z)) {
                sprintf(pb, "Tri: coletar ovos (%d)", coopEggs); pr = pb;
            } else if (near_area(COW_X, COW_Z)) {
                sprintf(pb, "Tri: coletar leite:%d carne:%d", penMilk, penMeat); pr = pb;
            }
        }
        if (pr) {
            int w = (int)strlen(pr) * 12, x = (480 - w) / 2;
            int top = sub[0] ? 222 : 238;
            rect2d(x - 6, top, x + w + 6, 266, 0xa0000000);
            if (sub[0]) {
                int w2 = (int)strlen(sub) * 6;
                ui_shadow((480 - w2) / 2, 227, 1, 0xffc0c0c0, sub);
            }
            ui_shadow(x, 243, 2, 0xff40e0ff, pr);
        }
    } else if (mode == 1) {
        static const char *opt[5] = { "Continuar", "", "Salvar", "Salvar e sair", "Sair sem salvar" };
        rect2d(0, 0, 480, 272, 0x80000000);
        window(96, 40, 384, 228);
        ui_shadow(180, 50, 2, 0xff40e0ff, "== MENU ==");
        for (int i = 0; i < 5; i++) {
            int y = 76 + i * 20;
            if (i == menuSel) rect2d(106, y - 3, 374, y + 17, 0xc0a06020);
            if (i == 1) sprintf(buf, "%s Musica: %s", i == menuSel ? ">" : " ", musicOn ? "ligada" : "desligada");
            else sprintf(buf, "%s %s", i == menuSel ? ">" : " ", opt[i]);
            ui_shadow(110, y, 2, 0xffffffff, buf);
        }
        ui_shadow(108, 184, 1, 0xffc0c0c0, "Cima/Baixo + X     START volta");
        if (menuMsg[0]) ui_shadow(110, 196, 2, 0xff60ff60, menuMsg);
    } else if (mode == 2) {
        static const char *nm[5] = { "Botas velozes", "Adubo (+50% cresc.)", "Racao (+50% animais)",
                                     "Ampliar terreno (+12)", "Sair" };
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 36, 444, 246);
        ui_shadow(180, 46, 2, 0xff40e0ff, "== LOJA ==");
        for (int i = 0; i < 5; i++) {
            int y = 70 + i * 22;
            if (i == menuSel) rect2d(46, y - 3, 434, y + 17, 0xc0a06020);
            ui_shadow(56, y, 2, 0xffffffff, nm[i]);
            if (i < 4) {
                if (shop_owned(i)) ui_shadow(330, y, 2, 0xff60ff60, "COMPRADO");
                else { sprintf(buf, "$%d", shopPrice[i]); ui_shadow(330, y, 2, 0xff40e0ff, buf); }
            }
        }
        sprintf(buf, "Seu dinheiro: $%d", money);
        ui_shadow(56, 184, 2, 0xffffffff, buf);
        ui_shadow(56, 204, 1, 0xffc0c0c0, "Cima/Baixo + X comprar   START/O sair");
        if (menuMsg[0]) ui_shadow(56, 218, 2, 0xff60ff60, menuMsg);
    } else if (mode == 3) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 40, 444, 232);
        ui_shadow(190, 48, 2, 0xff40e0ff, "== CASA ==");
        sprintf(buf, "Nivel %d/3: %s", houseLvl, houseName[houseLvl - 1]);
        ui_shadow(56, 72, 2, 0xffc0ffc0, buf);
        for (int i = 0; i < 3; i++) {
            int y = 98 + i * 22;
            if (i == menuSel) rect2d(46, y - 3, 434, y + 17, 0xc0a06020);
            if (i == 0) strcpy(buf, "Dormir ate amanhecer");
            else if (i == 1) {
                if (houseLvl >= 3) strcpy(buf, "Casa no nivel maximo");
                else sprintf(buf, "Melhorar casa ($%d)", houseCost[houseLvl]);
            } else strcpy(buf, "Sair");
            ui_shadow(56, y, 2, 0xffffffff, buf);
        }
        ui_shadow(56, 170, 1, 0xffc0c0c0, "Dormir passa a noite, salva o jogo e a fazenda produz.");
        ui_shadow(56, 180, 1, 0xffc0c0c0, "Casa melhor = mais producao durante o sono. Nivel 3 = premio!");
        if (menuMsg[0]) ui_shadow(56, 196, 2, 0xff60ff60, menuMsg);
    } else if (mode == 5) {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(36, 36, 444, 246);
        ui_shadow(170, 46, 2, 0xff40e0ff, "== CELEIRO ==");
        for (int i = 0; i < 5; i++) {
            int y = 70 + i * 22;
            if (i == menuSel) rect2d(46, y - 3, 434, y + 17, 0xc0a06020);
            if (i < 4) {
                sprintf(buf, "%d %s > %s", recQty[i], itemName[recIn[i]], itemName[recOut[i]]);
                ui_shadow(56, y, 2, 0xffffffff, buf);
                sprintf(buf, "Tem:%d", inv[recIn[i]]);
                ui_shadow(340, y, 2, inv[recIn[i]] >= recQty[i] ? 0xff60ff60 : 0xff8080ff, buf);
            } else ui_shadow(56, y, 2, 0xffffffff, "Sair");
        }
        sprintf(buf, "Pao:%d Queijo:%d Suco:%d Molho:%d", inv[IT_PAO], inv[IT_QUEIJO], inv[IT_SUCO], inv[IT_MOLHO]);
        ui_shadow(56, 184, 1, 0xffffffff, buf);
        ui_shadow(56, 198, 1, 0xffc0c0c0, "Produtos valem mais! Venda no mercado ou use nas missoes.");
        ui_shadow(56, 210, 1, 0xffc0c0c0, "Cima/Baixo + X fabricar   START/O sair");
        if (menuMsg[0]) ui_shadow(56, 224, 2, 0xff60ff60, menuMsg);
    } else {
        rect2d(0, 0, 480, 272, 0x80000000);
        window(24, 30, 456, 242);
        ui_shadow(150, 38, 2, 0xff40e0ff, "== PREFEITURA ==");
        for (int v = 0; v < 3; v++) {
            int have = inv[mis[v].item];
            sprintf(buf, "%s: %d %s %d/%d $%d", npcName[v], mis[v].qty, itemName[mis[v].item],
                    have > mis[v].qty ? mis[v].qty : have, mis[v].qty, mis[v].reward);
            ui_shadow(40, 66 + v * 26, 2, have >= mis[v].qty ? 0xff60ff60 : 0xffffffff, buf);
        }
        sprintf(buf, "Missoes concluidas: %d", missionsDone);
        ui_shadow(40, 150, 2, 0xff40e0ff, buf);
        ui_shadow(40, 176, 1, 0xffc0c0c0, "X: entregar tudo que der     START/O: sair");
        ui_shadow(40, 188, 1, 0xffc0c0c0, "Fale com os moradores na vila para ver os pedidos.");
        if (menuMsg[0]) ui_shadow(40, 204, 2, 0xff60ff60, menuMsg);
    }

    if (sleepFade > 0) {
        float a = sleepFade > 1.0f ? 1.0f : sleepFade;
        rect2d(0, 0, 480, 272, ((unsigned int)(a * 255.0f) << 24));
        if (sleepFade > 0.6f) ui_shadow(170, 124, 2, 0xffffffff, "Dormindo... Zzz");
    }

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

int main(void) {
    setup_callbacks();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_cube();
    build_ground();
    build_sprites();
    init_world();
    if (load_game()) say("Jogo carregado!");

    int at = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000,
                                   PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    if (at >= 0) sceKernelStartThread(at, 0, NULL);

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME, BUF_W);
    sceGuDepthBuffer((void *)(FRAME * 2), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    SceCtrlData pad; memset(&pad, 0, sizeof(pad));
    unsigned int oldButtons = 0;
    unsigned int last = sceKernelGetSystemTimeLow();

    while (running) {
        unsigned int now = sceKernelGetSystemTimeLow();
        float dt = (now - last) / 1000000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (sleepFade > 0) sleepFade -= dt;

        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldButtons;
        oldButtons = pad.Buttons;

        if (mode == 1) menu_update(pressed);
        else if (mode == 2) shop_update(pressed);
        else if (mode == 3) house_update(pressed);
        else if (mode == 4) hall_update(pressed);
        else if (mode == 5) barn_update(pressed);
        else update(dt, &pad, pressed);

        sceGuStart(GU_DIRECT, list);
        draw_scene();
        draw_ui();
        sceGuFinish();
        sceGuSync(0, 0);

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
