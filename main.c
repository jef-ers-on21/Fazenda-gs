/*
 * Fazenda Simulator GS - PSP (PSPSDK) - 3D, mundo aberto
 * Controles:
 *   Analogico  : andar          L / R : girar camera
 *   X          : plantar / colher no canteiro mais proximo
 *   Quadrado   : comprar semente (perto do mercado, $5)
 *   Circulo    : vender colheita ($12) e ovos ($4) no mercado
 *   Triangulo  : coletar ovos (no galinheiro, a frente)
 *   Start      : menu (continuar / salvar / salvar e sair)
 *   Select     : mostrar/esconder ajuda
 *   Chuva faz as plantas crescerem 2x mais rapido!
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
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
#define NPLOTS  12
#define NTREES  90
#define NCHICK  8
#define NDROPS  60
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
static int running = 1;
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

static void draw_ground(void) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuColor(0xff3c9a3c);
    sceGumDrawArray(GU_TRIANGLES, VTX_TYPE, GN * GN / 2 * 6, 0, groundA);
    sceGuColor(0xff328a34);
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

/* ---------- estado do jogo ---------- */
typedef struct { float x, z; int stage; float t; } Plot;
static Plot plots[NPLOTS];
static float treeX[NTREES], treeZ[NTREES], treeH[NTREES];
static float px = 0, pz = 0, pang = 0, cam = 0;
static int money = 50, seeds = 5, crops = 0, showHelp = 1;
static float gameTime = 0;
static char msg[64] = "";
static float msgTimer = 0;

static const float MKT_X = 0.0f, MKT_Z = -14.0f;

typedef struct { float x, z, tx, tz, ang, t, egg; } Chicken;
static Chicken chick[NCHICK];
static int eggs = 0, coopEggs = 0;
static int raining = 0;
static float weatherT = 30.0f;
static int paused = 0, menuSel = 0;
static char menuMsg[40] = "";

static unsigned int seed_rng = 12345;
static float frand(void) {
    seed_rng = seed_rng * 1664525u + 1013904223u;
    return (float)((seed_rng >> 8) & 0xFFFF) / 65535.0f;
}

static void init_world(void) {
    for (int i = 0; i < NPLOTS; i++) {
        plots[i].x = 8.0f + (i % 4) * 4.0f;
        plots[i].z = -4.0f + (i / 4) * 4.0f;
        plots[i].stage = 0; plots[i].t = 0;
    }
    for (int i = 0; i < NTREES; i++) {
        float x, z;
        do {
            x = (frand() * 2 - 1) * (WORLD - 3);
            z = (frand() * 2 - 1) * (WORLD - 3);
        } while (sqrtf(x*x + z*z) < 30.0f);
        treeX[i] = x; treeZ[i] = z; treeH[i] = 3.0f + frand() * 3.0f;
    }
    for (int i = 0; i < NCHICK; i++) {
        chick[i].x = PEN_X + (frand() * 2 - 1) * 6.0f;
        chick[i].z = PEN_Z + (frand() * 2 - 1) * 6.0f;
        chick[i].tx = chick[i].x; chick[i].tz = chick[i].z;
        chick[i].ang = frand() * 6.28f;
        chick[i].t = frand() * 3.0f;
        chick[i].egg = frand() * 25.0f;
    }
}

static void say(const char *s) {
    strncpy(msg, s, sizeof(msg) - 1); msg[sizeof(msg) - 1] = 0; msgTimer = 3.0f;
}

/* ---------- salvar / carregar ---------- */
#define SAVE_MAGIC 0x31475A46u
#define SAVE_DIR   "ms0:/PSP/SAVEDATA/FAZENDAGS"
#define SAVE_FILE  SAVE_DIR "/save.dat"

typedef struct {
    unsigned int magic; int version;
    int money, seeds, crops, eggs, coopEggs, raining;
    float px, pz, cam, gameTime, weatherT;
    int stage[NPLOTS]; float ptime[NPLOTS];
} SaveData;

static int save_game(void) {
    SaveData d; memset(&d, 0, sizeof(d));
    d.magic = SAVE_MAGIC; d.version = 1;
    d.money = money; d.seeds = seeds; d.crops = crops; d.eggs = eggs;
    d.coopEggs = coopEggs; d.raining = raining;
    d.px = px; d.pz = pz; d.cam = cam; d.gameTime = gameTime; d.weatherT = weatherT;
    for (int i = 0; i < NPLOTS; i++) { d.stage[i] = plots[i].stage; d.ptime[i] = plots[i].t; }
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    int fd = sceIoOpen(SAVE_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;
    int w = sceIoWrite(fd, &d, sizeof(d));
    sceIoClose(fd);
    return w == (int)sizeof(d);
}

static int load_game(void) {
    SaveData d;
    int fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0777);
    if (fd < 0) return 0;
    int r = sceIoRead(fd, &d, sizeof(d));
    sceIoClose(fd);
    if (r != (int)sizeof(d) || d.magic != SAVE_MAGIC || d.version != 1) return 0;
    money = d.money; seeds = d.seeds; crops = d.crops; eggs = d.eggs;
    coopEggs = d.coopEggs; raining = d.raining;
    px = d.px; pz = d.pz; cam = d.cam; gameTime = d.gameTime; weatherT = d.weatherT;
    for (int i = 0; i < NPLOTS; i++) {
        plots[i].stage = d.stage[i]; plots[i].t = d.ptime[i];
        if (plots[i].stage < 0 || plots[i].stage > 3) plots[i].stage = 0;
    }
    return 1;
}

static void menu_update(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP)   menuSel = (menuSel + 3) % 4;
    if (pressed & PSP_CTRL_DOWN) menuSel = (menuSel + 1) % 4;
    if (pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) { paused = 0; return; }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) paused = 0;
        else if (menuSel == 1) {
            strcpy(menuMsg, save_game() ? "Jogo salvo!" : "Erro ao salvar!");
        } else if (menuSel == 2) {
            if (save_game()) running = 0;
            else strcpy(menuMsg, "Erro ao salvar!");
        } else running = 0;
    }
}

static int nearest_plot(void) {
    int best = -1; float bd = 3.0f;
    for (int i = 0; i < NPLOTS; i++) {
        float dx = plots[i].x - px, dz = plots[i].z - pz;
        float d = sqrtf(dx*dx + dz*dz);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void update(float dt, const SceCtrlData *pad, unsigned int pressed) {
    gameTime += dt;
    if (msgTimer > 0) msgTimer -= dt;

    if (pad->Buttons & PSP_CTRL_LTRIGGER) cam -= 1.8f * dt;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) cam += 1.8f * dt;

    float dx = (pad->Lx - 128) / 128.0f, dy = (pad->Ly - 128) / 128.0f;
    if (fabsf(dx) < 0.2f) dx = 0;
    if (fabsf(dy) < 0.2f) dy = 0;
    if (dx != 0 || dy != 0) {
        float fx = sinf(cam), fz = cosf(cam);
        float rx = -cosf(cam), rz = sinf(cam);
        float mx = fx * (-dy) + rx * dx, mz = fz * (-dy) + rz * dx;
        float speed = 9.0f * dt;
        px += mx * speed; pz += mz * speed;
        pang = atan2f(mx, mz);
        if (px < -WORLD) px = -WORLD;
        if (px > WORLD) px = WORLD;
        if (pz < -WORLD) pz = -WORLD;
        if (pz > WORLD) pz = WORLD;
    }

    weatherT -= dt;
    if (weatherT <= 0) {
        raining = !raining;
        weatherT = raining ? (15.0f + frand() * 15.0f) : (40.0f + frand() * 40.0f);
        say(raining ? "Comecou a chover! Plantas crescem 2x." : "A chuva parou.");
    }
    float gm = raining ? 2.0f : 1.0f;

    for (int i = 0; i < NCHICK; i++) {
        Chicken *c = &chick[i];
        c->t -= dt;
        if (c->t <= 0) {
            c->tx = PEN_X + (frand() * 2 - 1) * 6.5f;
            c->tz = PEN_Z + (frand() * 2 - 1) * 6.5f;
            c->t = 2.0f + frand() * 4.0f;
        }
        float cdx = c->tx - c->x, cdz = c->tz - c->z;
        float cd = sqrtf(cdx * cdx + cdz * cdz);
        if (cd > 0.2f) {
            c->x += cdx / cd * 1.2f * dt;
            c->z += cdz / cd * 1.2f * dt;
            c->ang = atan2f(cdx, cdz);
        }
        c->egg += dt;
        if (c->egg >= 25.0f) { c->egg = 0; if (coopEggs < 24) coopEggs++; }
    }

    for (int i = 0; i < NPLOTS; i++) {
        if (plots[i].stage >= 1 && plots[i].stage < 3) {
            plots[i].t += dt * gm;
            if (plots[i].t >= 12.0f) { plots[i].t = 0; plots[i].stage++; }
        }
    }

    if (pressed & PSP_CTRL_SELECT) showHelp = !showHelp;
    if (pressed & PSP_CTRL_START) { paused = 1; menuSel = 0; menuMsg[0] = 0; return; }

    if (pressed & PSP_CTRL_TRIANGLE) {
        if (fabsf(px - PEN_X) < 9.0f && fabsf(pz - PEN_Z) < 9.0f) {
            if (coopEggs > 0) {
                eggs += coopEggs; coopEggs = 0; say("Coletou os ovos!");
            } else say("Sem ovos por enquanto.");
        } else say("Va ate o galinheiro (a frente).");
    }

    if (pressed & PSP_CTRL_CROSS) {
        int i = nearest_plot();
        if (i < 0) say("Chegue perto de um canteiro.");
        else if (plots[i].stage == 0) {
            if (seeds > 0) { seeds--; plots[i].stage = 1; plots[i].t = 0; say("Plantou!"); }
            else say("Sem sementes! Compre no mercado.");
        } else if (plots[i].stage == 3) {
            plots[i].stage = 0; crops++; say("Colheu!");
        } else say("Ainda crescendo...");
    }

    float mdx = px - MKT_X, mdz = pz - MKT_Z;
    int atMarket = sqrtf(mdx*mdx + mdz*mdz) < 6.0f;
    if (pressed & PSP_CTRL_SQUARE) {
        if (!atMarket) say("Va ate o mercado.");
        else if (money >= 5) { money -= 5; seeds++; say("Comprou 1 semente (-$5)"); }
        else say("Dinheiro insuficiente.");
    }
    if (pressed & PSP_CTRL_CIRCLE) {
        if (!atMarket) say("Va ate o mercado.");
        else if (crops > 0 || eggs > 0) {
            money += crops * 12 + eggs * 4; crops = 0; eggs = 0; say("Vendeu tudo!");
        }
        else say("Nada para vender.");
    }
}

static void fence_line(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1;
    float len = sqrtf(dx * dx + dz * dz);
    float ang = atan2f(dx, dz);
    float mx = (x1 + x2) * 0.5f, mz = (z1 + z2) * 0.5f;
    int n = (int)(len / 4.0f);
    if (n < 1) n = 1;
    box(mx, 0.35f, mz, 0.12f, 0.12f, len, ang, 0xff3a6a9a);
    box(mx, 0.75f, mz, 0.12f, 0.12f, len, ang, 0xff3a6a9a);
    for (int i = 0; i <= n; i++)
        box(x1 + dx * i / n, 0, z1 + dz * i / n, 0.25f, 1.0f, 0.25f, 0, 0xff2a4a70);
}

static void draw_scene(void) {
    /* ciclo dia/noite: 150 s */
    float day = 0.5f + 0.5f * cosf(gameTime * 2.0f * PI_F / 150.0f); /* 1=dia */
    float b = 0.25f + 0.75f * day;
    if (raining) b *= 0.7f;

    unsigned int sky = 0xff000000
        | ((unsigned int)(235 * b) << 16) | ((unsigned int)(190 * b) << 8) | (unsigned int)(110 * b);
    sceGuClearColor(sky);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(70.0f, 16.0f / 9.0f, 0.5f, 200.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = { px - sinf(cam) * 11.0f, 7.0f, pz - cosf(cam) * 11.0f };
    ScePspFVector3 ctr = { px, 1.5f, pz };
    ScePspFVector3 up  = { 0, 1, 0 };
    sceGumLookAt(&eye, &ctr, &up);

    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    ScePspFVector3 dir = { -0.4f, -1.0f, -0.3f };
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &dir);
    unsigned int lc = 0xff000000 | ((unsigned int)(255*b) << 16) | ((unsigned int)(255*b) << 8) | (unsigned int)(255*b);
    sceGuLightColor(0, GU_DIFFUSE, lc);
    sceGuAmbient(0xff000000 | ((unsigned int)(90*b) << 16) | ((unsigned int)(90*b) << 8) | (unsigned int)(90*b));
    sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);

    /* chao */
    draw_ground();
    /* estrada ate o mercado */
    box(0, 0.02f, -7, 3, 0.05f, 14, 0, 0xff6f8fa8);

    /* canteiros e plantas */
    for (int i = 0; i < NPLOTS; i++) {
        Plot *p = &plots[i];
        box(p->x, 0.03f, p->z, 3.0f, 0.15f, 3.0f, 0, 0xff2a4a6b);
        if (p->stage == 1) box(p->x, 0.15f, p->z, 0.4f, 0.4f, 0.4f, 0, 0xff30c030);
        else if (p->stage == 2) box(p->x, 0.15f, p->z, 0.8f, 1.0f, 0.8f, 0, 0xff20b040);
        else if (p->stage == 3) {
            box(p->x, 0.15f, p->z, 1.0f, 1.2f, 1.0f, 0, 0xff20a030);
            box(p->x, 1.2f, p->z, 0.9f, 0.7f, 0.9f, 0, 0xff1e50e8); /* fruto */
        }
    }

    /* celeiro */
    box(-14, 0, -10, 8, 4.5f, 6, 0, 0xff2828b0);
    box(-14, 4.5f, -10, 8.4f, 1.5f, 6.4f, 0, 0xff303030);
    /* casa */
    box(-14, 0, 8, 6, 3.5f, 6, 0, 0xffa8c8e8);
    box(-14, 3.5f, 8, 6.6f, 1.2f, 6.6f, 0, 0xff204080);
    /* mercado */
    box(MKT_X - 2, 0, MKT_Z, 0.4f, 3, 0.4f, 0, 0xff204060);
    box(MKT_X + 2, 0, MKT_Z, 0.4f, 3, 0.4f, 0, 0xff204060);
    box(MKT_X, 1.0f, MKT_Z, 4.4f, 0.8f, 1.5f, 0, 0xff3090d0);
    box(MKT_X, 3.0f, MKT_Z, 5.5f, 0.3f, 3.0f, 0, 0xff2020d0);

    /* cerca da plantacao */
    fence_line(5, -8, 23, -8);
    fence_line(23, -8, 23, 8);
    fence_line(23, 8, 5, 8);
    fence_line(5, 8, 5, -8);

    /* galinheiro: cerca, casinha e galinhas */
    fence_line(PEN_X - 8, PEN_Z - 8, PEN_X + 8, PEN_Z - 8);
    fence_line(PEN_X + 8, PEN_Z - 8, PEN_X + 8, PEN_Z + 8);
    fence_line(PEN_X + 8, PEN_Z + 8, PEN_X - 8, PEN_Z + 8);
    fence_line(PEN_X - 8, PEN_Z + 8, PEN_X - 8, PEN_Z - 8);
    box(PEN_X - 5, 0, PEN_Z - 5, 3.0f, 2.0f, 3.0f, 0, 0xff4070c0);
    box(PEN_X - 5, 2.0f, PEN_Z - 5, 3.4f, 0.8f, 3.4f, 0, 0xff202080);
    for (int i = 0; i < NCHICK; i++) {
        Chicken *c = &chick[i];
        box(c->x, 0.15f, c->z, 0.5f, 0.4f, 0.7f, c->ang, 0xfff0f0f0);
        box(c->x + sinf(c->ang) * 0.4f, 0.45f, c->z + cosf(c->ang) * 0.4f,
            0.25f, 0.3f, 0.25f, c->ang, 0xff2020e0);
    }
    for (int i = 0; i < coopEggs && i < 24; i++)
        box(PEN_X + 3.0f + (i % 6) * 0.5f, 0.05f, PEN_Z + 6.0f - (i / 6) * 0.5f,
            0.25f, 0.3f, 0.25f, 0, 0xffd0f0ff);

    /* arvores */
    for (int i = 0; i < NTREES; i++) {
        float tdx = treeX[i] - px, tdz = treeZ[i] - pz;
        if (tdx * tdx + tdz * tdz > 90.0f * 90.0f) continue;
        box(treeX[i], 0, treeZ[i], 0.8f, treeH[i], 0.8f, 0, 0xff204060);
        box(treeX[i], treeH[i] - 0.5f, treeZ[i], 3.2f, 2.8f, 3.2f, 0.4f * i, 0xff208030);
    }

    /* jogador */
    box(px, 0.0f, pz, 0.9f, 1.4f, 0.6f, pang, 0xffd04020);
    box(px, 1.4f, pz, 0.6f, 0.6f, 0.6f, pang, 0xff90b8f0);
    box(px, 2.0f, pz, 0.8f, 0.15f, 0.8f, pang, 0xff40a0e0); /* chapeu */

    /* chuva */
    if (raining) {
        for (int i = 0; i < NDROPS; i++) {
            float ox = ((i * 37) % 31) - 15.0f;
            float oz = ((i * 53) % 29) - 14.0f;
            float y = 14.0f - fmodf((i * 1.7f) + gameTime * 18.0f, 14.0f);
            box(px + ox, y, pz + oz, 0.05f, 0.7f, 0.05f, 0, 0xffffc8a0);
        }
    }

    sceGuDisable(GU_LIGHTING);
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
    ui_text(x + sc, y + sc, sc, 0xff000000, s);
    ui_text(x, y, sc, color, s);
}

static int at_market(void) {
    float dx = px - MKT_X, dz = pz - MKT_Z;
    return sqrtf(dx * dx + dz * dz) < 6.0f;
}

static void draw_ui(void) {
    char buf[96];
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    /* HUD */
    rect2d(0, 0, 372, 40, 0x90000000);
    sprintf(buf, "$%d  Sem:%d  Col:%d  Ovos:%d", money, seeds, crops, eggs);
    ui_shadow(4, 3, 2, 0xff40e0ff, buf);
    sprintf(buf, "Dia %d  %s  Galinheiro:%d", (int)(gameTime / 150.0f) + 1,
            raining ? "CHUVA" : "Sol", coopEggs);
    ui_shadow(4, 21, 2, 0xffffffff, buf);

    if (showHelp && !paused) {
        static const char *help[5] = {
            "Analog: andar   L/R: camera",
            "X: plantar / colher",
            "Quad: comprar semente (mercado)",
            "Circ: vender tudo (mercado)",
            "Tri: ovos   START: menu   SELECT: ajuda"
        };
        rect2d(0, 42, 262, 92, 0x90000000);
        for (int i = 0; i < 5; i++) ui_shadow(4, 46 + i * 9, 1, 0xffffffff, help[i]);
    }

    if (!paused) {
        if (msgTimer > 0) {
            int w = (int)strlen(msg) * 12, x = (480 - w) / 2;
            rect2d(x - 6, 212, x + w + 6, 234, 0xa0000000);
            ui_shadow(x, 216, 2, 0xffffffff, msg);
        }
        const char *pr = NULL; char pb[48];
        if (at_market()) pr = "MERCADO  Quad:comprar $5  Circ:vender";
        else {
            int i = nearest_plot();
            if (i >= 0) {
                if (plots[i].stage == 0) pr = "X: plantar semente";
                else if (plots[i].stage == 3) pr = "X: colher";
                else pr = "Crescendo...";
            } else if (fabsf(px - PEN_X) < 9.0f && fabsf(pz - PEN_Z) < 9.0f) {
                sprintf(pb, "Tri: coletar ovos (%d)", coopEggs); pr = pb;
            }
        }
        if (pr) {
            int w = (int)strlen(pr) * 12, x = (480 - w) / 2;
            rect2d(x - 6, 242, x + w + 6, 266, 0xa0000000);
            ui_shadow(x, 247, 2, 0xff40e0ff, pr);
        }
    } else {
        static const char *opt[4] = { "Continuar", "Salvar", "Salvar e sair", "Sair sem salvar" };
        rect2d(0, 0, 480, 272, 0x80000000);
        rect2d(100, 60, 380, 208, 0xf060c0f0);
        rect2d(104, 64, 376, 204, 0xf0201008);
        ui_shadow(180, 70, 2, 0xff40e0ff, "== MENU ==");
        for (int i = 0; i < 4; i++) {
            int y = 94 + i * 20;
            if (i == menuSel) rect2d(110, y - 3, 370, y + 17, 0xc0a06020);
            sprintf(buf, "%s %s", i == menuSel ? ">" : " ", opt[i]);
            ui_shadow(116, y, 2, 0xffffffff, buf);
        }
        ui_shadow(112, 176, 1, 0xffc0c0c0, "Cima/Baixo + X     START volta");
        if (menuMsg[0]) ui_shadow(116, 186, 2, 0xff60ff60, menuMsg);
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
    init_world();
    if (load_game()) say("Jogo carregado!");

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

        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldButtons;
        oldButtons = pad.Buttons;

        if (paused) menu_update(pressed);
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
