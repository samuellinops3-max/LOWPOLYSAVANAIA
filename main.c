/*
 * Batatinha Frita 1, 3 - main.c
 * Renderização por software (sem Tiny3D): triângulos desenhados direto no
 * framebuffer, no mesmo esquema do main.c do Cobra Arena (init_screen/present).
 * Câmera atrás da batatinha, mato low poly e animais com IA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32*1024*1024)
#define PI        3.14159265f
#define MUNDO     150.0f
#define N_ARVORES 60
#define N_ANIMAIS 14
#define DIST_CAM  9.0f
#define ALT_CAM   4.0f
#define NEAR      1.0f
#define FAR_D     180.0f
#define VEL_JOG   10.0f
#define VEL_ANIM  3.5f

typedef enum { VAGAR, COMER, BEBER, DORMIR, FUGIR } Estado;

typedef struct { float x, z, r; } Arvore;
typedef struct { float x, z, ang, timer, fome, sede, sono; Estado est; } Animal;

static Arvore arvores[N_ARVORES];
static Animal animais[N_ANIMAIS];
static float jx = 0, jz = 0, jang = 0;

/* câmera */
static float camX, camY, camZ, fwx, fwz;

/* framebuffer (mesmo esquema do Cobra Arena) */
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static float *zb;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

/* ---------- sistema ---------- */
static void sysutil_cb(u64 s, u64 p, void *u) {
    (void)p; (void)u;
    if (s == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void) {
    void *h = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, h);

    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 r = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        r = VIDEO_RESOLUTION_720;

    videoResolution res;
    videoGetResolution(r, &res);
    W = res.width;
    H = res.height;

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = r;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);

    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = (u32 *)memalign(64, W * H * 4);
    memset(back, 0, W * H * 4);
    zb = (float *)malloc(W * H * sizeof(float));
    gcmResetFlipStatus();
}

static void wait_flip(void) {
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void present(void) {
    if (!first_flip) wait_flip();
    else gcmResetFlipStatus();
    memcpy(fb[cur], back, W * H * 4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

/* ---------- utilidades ---------- */
static float frand(float a, float b) {
    return a + (b - a) * ((float)rand() / (float)RAND_MAX);
}
static float clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}
static float wrap(float a) {
    while (a >  PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}
static u32 shade(u32 c, float f) {
    u32 r = (u32)(((c >> 16) & 255) * f);
    u32 g = (u32)(((c >> 8) & 255) * f);
    u32 b = (u32)((c & 255) * f);
    return (r << 16) | (g << 8) | b;
}

/* ---------- projeção e rasterização ---------- */
static int proj(float wx, float wy, float wz, float *sx, float *sy, float *sz) {
    float vx = wx - camX, vy = wy - camY, vz = wz - camZ;
    float d = vx * fwx + vz * fwz;          /* profundidade */
    if (d < NEAR) return 0;
    float side = vx * fwz - vz * fwx;       /* eixo horizontal da câmera */
    float F = H * 0.9f;
    *sx = W * 0.5f + F * side / d;
    *sy = H * 0.5f - F * vy / d;
    *sz = d;
    return 1;
}

static void tri_s(float ax, float ay, float az,
                  float bx, float by, float bz,
                  float cx, float cy, float cz, u32 col) {
    float mnx = fminf(ax, fminf(bx, cx)), mxx = fmaxf(ax, fmaxf(bx, cx));
    float mny = fminf(ay, fminf(by, cy)), mxy = fmaxf(ay, fmaxf(by, cy));
    int x0 = (int)mnx, x1 = (int)mxx + 1;
    int y0 = (int)mny, y1 = (int)mxy + 1;
    int iW = (int)W, iH = (int)H;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > iW - 1) x1 = iW - 1;
    if (y1 > iH - 1) y1 = iH - 1;

    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (fabsf(area) < 1e-4f) return;
    float inv = 1.0f / area;

    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f;
            float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * inv;
            float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * az + w1 * bz + w2 * cz;
            int i = y * iW + x;
            if (z < zb[i]) {
                zb[i] = z;
                back[i] = col;
            }
        }
    }
}

/* caixa girada em torno do eixo Y (yaw) */
static void box_rot(float cx, float cy, float cz,
                    float hx, float hy, float hz, float yaw, u32 col) {
    static const int F[6][4] = {
        {2,3,7,6}, {0,1,5,4}, {4,5,7,6}, {0,1,3,2}, {1,3,7,5}, {0,2,6,4}
    };
    static const float SHD[6] = { 1.0f, 0.5f, 0.8f, 0.8f, 0.65f, 0.65f };
    float sx[8], sy[8], sz[8];
    float cs = cosf(yaw), sn = sinf(yaw);

    for (int c = 0; c < 8; c++) {
        float lx = (c & 1) ? hx : -hx;
        float ly = (c & 2) ? hy : -hy;
        float lz = (c & 4) ? hz : -hz;
        float rx = lx * cs - lz * sn;
        float rz = lx * sn + lz * cs;
        if (!proj(cx + rx, cy + ly, cz + rz, &sx[c], &sy[c], &sz[c])) return;
    }
    for (int f = 0; f < 6; f++) {
        u32 c = shade(col, SHD[f]);
        int a = F[f][0], b = F[f][1], d = F[f][2], e = F[f][3];
        tri_s(sx[a], sy[a], sz[a], sx[b], sy[b], sz[b], sx[d], sy[d], sz[d], c);
        tri_s(sx[a], sy[a], sz[a], sx[d], sy[d], sz[d], sx[e], sy[e], sz[e], c);
    }
}

/* árvore: tronco em caixa + copa em pirâmide de 6 lados */
static void desenha_arvore(Arvore *t) {
    float dx = t->x - jx, dz = t->z - jz;
    if (dx * dx + dz * dz > FAR_D * FAR_D) return;

    box_rot(t->x, 1.2f, t->z, 0.5f, 1.2f, 0.5f, 0, 0x7A4A1E);

    float ax, ay, az;
    if (!proj(t->x, 6.0f, t->z, &ax, &ay, &az)) return;
    float rx[6], ry[6], rz[6];
    for (int k = 0; k < 6; k++) {
        float a = k * (2 * PI / 6);
        if (!proj(t->x + cosf(a) * t->r, 2.4f, t->z + sinf(a) * t->r,
                  &rx[k], &ry[k], &rz[k])) return;
    }
    u32 verde = 0x2E7D32;
    for (int k = 0; k < 6; k++) {
        int k2 = (k + 1) % 6;
        u32 c = shade(verde, 0.7f + 0.05f * (k % 4));
        tri_s(ax, ay, az, rx[k], ry[k], rz[k], rx[k2], ry[k2], rz[k2], c);
    }
}

/* animal: corpo + cabeça (a cabeça abaixa quando come ou bebe) */
static void desenha_animal(Animal *a) {
    float dx = a->x - jx, dz = a->z - jz;
    if (dx * dx + dz * dz > FAR_D * FAR_D) return;

    u32 cor = 0x8D6E63;
    if (a->est == DORMIR) cor = 0x6D4C41;
    if (a->est == FUGIR)  cor = 0xD7A86E;

    box_rot(a->x, 0.9f, a->z, 1.0f, 0.6f, 1.5f, a->ang, cor);
    float hy = (a->est == COMER || a->est == BEBER) ? 0.5f : 1.4f;
    box_rot(a->x + sinf(a->ang) * 1.8f, hy,
            a->z + cosf(a->ang) * 1.8f, 0.5f, 0.5f, 0.5f, a->ang, cor);
}

/* ---------- mundo ---------- */
static void mundo_init(void) {
    srand(1234);
    for (int i = 0; i < N_ARVORES; i++) {
        arvores[i].x = frand(-MUNDO, MUNDO);
        arvores[i].z = frand(-MUNDO, MUNDO);
        arvores[i].r = frand(1.5f, 2.5f);
    }
    for (int i = 0; i < N_ANIMAIS; i++) {
        Animal *a = &animais[i];
        a->x = frand(-MUNDO * 0.8f, MUNDO * 0.8f);
        a->z = frand(-MUNDO * 0.8f, MUNDO * 0.8f);
        a->ang = frand(-PI, PI);
        a->est = VAGAR;
        a->timer = frand(2, 6);
        a->fome = frand(0, 0.5f);
        a->sede = frand(0, 0.5f);
        a->sono = frand(0, 0.5f);
    }
}

static void animais_update(float dt) {
    for (int i = 0; i < N_ANIMAIS; i++) {
        Animal *a = &animais[i];
        a->fome += dt * 0.02f;
        a->sede += dt * 0.03f;
        a->sono += dt * 0.01f;

        float dx = jx - a->x, dz = jz - a->z;
        float d = sqrtf(dx * dx + dz * dz);

        if (d < 15.0f) {
            a->est = FUGIR;
        } else if (a->est == FUGIR && d > 25.0f) {
            a->est = VAGAR;
            a->timer = 0;
        }

        if (a->est == VAGAR && a->timer <= 0) {
            if (a->sono > 0.9f)      { a->est = DORMIR; a->timer = 6; }
            else if (a->sede > 0.8f) { a->est = BEBER;  a->timer = 3; }
            else if (a->fome > 0.8f) { a->est = COMER;  a->timer = 4; }
            else                     { a->ang = frand(-PI, PI); a->timer = frand(2, 5); }
        }

        float vel = 0;
        switch (a->est) {
        case VAGAR:
            vel = VEL_ANIM * 0.5f;
            a->timer -= dt;
            break;
        case FUGIR:
            a->ang = atan2f(-dx, -dz);
            vel = VEL_ANIM * 1.6f;
            break;
        case COMER:
            a->timer -= dt;
            a->fome = clampf(a->fome - dt * 0.4f, 0, 1);
            if (a->timer <= 0 || a->fome <= 0.05f) { a->est = VAGAR; a->timer = 0; }
            break;
        case BEBER:
            a->timer -= dt;
            a->sede = clampf(a->sede - dt * 0.5f, 0, 1);
            if (a->timer <= 0 || a->sede <= 0.05f) { a->est = VAGAR; a->timer = 0; }
            break;
        case DORMIR:
            a->timer -= dt;
            a->sono = clampf(a->sono - dt * 0.3f, 0, 1);
            if (a->timer <= 0 || a->sono <= 0.05f) { a->est = VAGAR; a->timer = 0; }
            break;
        }

        if (vel > 0) {
            a->x += sinf(a->ang) * vel * dt;
            a->z += cosf(a->ang) * vel * dt;
        }
        a->x = clampf(a->x, -MUNDO, MUNDO);
        a->z = clampf(a->z, -MUNDO, MUNDO);
    }
}

/* ---------- jogador e câmera ---------- */
static void read_pad(float *lx, float *ly) {
    padInfo info;
    padData d;
    memset(&d, 0, sizeof(d));
    *lx = 0; *ly = 0;
    ioPadGetInfo(&info);
    if (!info.status[0]) return;
    ioPadGetData(0, &d);

    float ax = ((int)d.ANA_L_H - 128) / 128.0f;
    float ay = ((int)d.ANA_L_V - 128) / 128.0f;
    if (d.BTN_LEFT)  ax = -1;
    if (d.BTN_RIGHT) ax =  1;
    if (d.BTN_UP)    ay = -1;
    if (d.BTN_DOWN)  ay =  1;
    if (fabsf(ax) < 0.2f) ax = 0;
    if (fabsf(ay) < 0.2f) ay = 0;
    *lx = ax;
    *ly = ay;
}

static void jogador_update(float dt, float lx, float ly) {
    jang = wrap(jang + lx * 2.5f * dt);     /* vira com o analógico */
    float mov = -ly * VEL_JOG * dt;         /* ly pra cima = frente */
    jx += sinf(jang) * mov;
    jz += cosf(jang) * mov;
    jx = clampf(jx, -MUNDO, MUNDO);
    jz = clampf(jz, -MUNDO, MUNDO);
}

static void atualiza_camera(void) {
    fwx = sinf(jang);
    fwz = cosf(jang);
    camX = jx - fwx * DIST_CAM;
    camZ = jz - fwz * DIST_CAM;
    camY = ALT_CAM;
}

/* ---------- desenho ---------- */
static void fundo(void) {
    u32 meio = H / 2;
    u32 ceu = 0x87CEEB, grama = 0x5B8C3A;
    for (u32 y = 0; y < H; y++) {
        u32 c = (y < meio) ? ceu : grama;
        for (u32 x = 0; x < W; x++) back[y * W + x] = c;
    }
    for (u32 i = 0; i < W * H; i++) zb[i] = 1e9f;
}

static void desenha_cena(void) {
    for (int i = 0; i < N_ARVORES; i++) desenha_arvore(&arvores[i]);
    for (int i = 0; i < N_ANIMAIS; i++) desenha_animal(&animais[i]);
    /* batatinha */
    box_rot(jx, 1.0f, jz, 1.0f, 1.0f, 1.0f, jang, 0xE8B04B);
}

/* ---------- main ---------- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    mundo_init();

    const float dt = 1.0f / 30.0f;
    while (running) {
        sysUtilCheckCallback();

        float lx, ly;
        read_pad(&lx, &ly);
        jogador_update(dt, lx, ly);
        animais_update(dt);

        atualiza_camera();
        fundo();
        desenha_cena();
        present();
    }
    return 0;
}
