/*
 * Batatinha Frita 1, 3 - main.c
 * Lógica do jogo: jogador em 3D, câmera atrás do personagem,
 * mundo de mato com árvores e animais com IA (vagar, comer, beber, dormir, fugir).
 *
 * As funções hw_* são as que falam com PS3/Tiny3D e pad.
 * Confira com o main.c do Cobra Arena (que já roda) e ajuste
 * os nomes/índices se o seu projeto usar outros.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <tiny3d.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>

#define N_ANIMAIS 16
#define N_ARVORES 50
#define MUNDO     120.0f
#define PI        3.14159265f
#define VEL_JOG   12.0f
#define VEL_ANIM  4.0f
#define DIST_CAM  12.0f
#define ALTURA_CAM 5.0f

typedef enum { VAGAR, COMER, BEBER, DORMIR, FUGIR } Estado;

typedef struct {
    float x, z, ang;
    Estado est;
    float timer;
    float fome, sede, sono;
} Animal;

typedef struct { float x, z, r; } Arvore;

static Animal animais[N_ANIMAIS];
static Arvore arvores[N_ARVORES];

/* jogador */
static float jx = 0, jz = 0, jang = 0;

static int sair = 0;

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

/* ---------- hardware (ajustar com o Cobra Arena) ---------- */
static void hw_init(void) {
    tiny3d_Init(1024 * 1024);
    ioPadInit(7);
}

static void sysutil_cb(uint64_t status, uint64_t param, void *userdata) {
    (void)param; (void)userdata;
    /* evento de saída (XMB/menu): sem isso o jogo pode voltar pro XMB */
    if (status == SYSUTIL_EXIT_GAME) sair = 1;
}

/* lê o analógico esquerdo (-1..1) e START */
static void hw_read_pad(float *lx, float *ly, int *start) {
    padInfo info;
    padData data;
    *lx = 0; *ly = 0; *start = 0;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &data);
        /* índices reais dos analógicos/botões: copie do Cobra Arena */
        *lx = ((float)data.button[2] - 128.0f) / 128.0f; /* ANALOG LX */
        *ly = ((float)data.button[3] - 128.0f) / 128.0f; /* ANALOG LY */
        *start = data.button[3] ? 0 : 0;                 /* troque pelo botão START */
    }
    /* deadzone */
    if (fabsf(*lx) < 0.15f) *lx = 0;
    if (fabsf(*ly) < 0.15f) *ly = 0;
}

/* vértice já transformado para a câmera (feito na CPU) */
static void hw_vertex_cam(float x, float y, float z, uint32_t cor);

static void hw_tri(float ax, float ay, float az,
                   float bx, float by, float bz,
                   float cx, float cy, float cz, uint32_t cor) {
    tiny3d_SetPolygon(TINY3D_TRIANGLES);
    hw_vertex_cam(ax, ay, az, cor);
    hw_vertex_cam(bx, by, bz, cor);
    hw_vertex_cam(cx, cy, cz, cor);
    tiny3d_End();
}

static void hw_vertex_cam(float x, float y, float z, uint32_t cor) {
    /* câmera atrás do jogador: eye = jogador - frente * DIST_CAM */
    float cx = jx - sinf(jang) * DIST_CAM;
    float cz = jz - cosf(jang) * DIST_CAM;
    float cy = ALTURA_CAM;
    float dx = x - cx, dy = y - cy, dz = z - cz;
    /* gira para a frente da câmera ficar em +Z */
    float rx = dx * cosf(jang) - dz * sinf(jang);
    float rz = dx * sinf(jang) + dz * cosf(jang);
    tiny3d_VertexPos(rx, dy, rz);
    tiny3d_VertexColor(cor);
}

/* caixa low poly (cubo de 6 faces) */
static void hw_box(float x, float y, float z, float s, uint32_t cor) {
    float h = s * 0.5f;
    float x0 = x - h, x1 = x + h, y0 = y, y1 = y + s, z0 = z - h, z1 = z + h;
    uint32_t c_top = cor, c_side = cor & 0xFFFFFF00 | 0xC0;
    /* topo */
    hw_tri(x0,y1,z0, x1,y1,z0, x1,y1,z1, c_top);
    hw_tri(x0,y1,z0, x1,y1,z1, x0,y1,z1, c_top);
    /* laterais */
    hw_tri(x0,y0,z0, x1,y0,z0, x1,y1,z0, c_side);
    hw_tri(x0,y0,z0, x1,y1,z0, x0,y1,z0, c_side);
    hw_tri(x0,y0,z1, x1,y0,z1, x1,y1,z1, c_side);
    hw_tri(x0,y0,z1, x1,y1,z1, x0,y1,z1, c_side);
    hw_tri(x0,y0,z0, x0,y0,z1, x0,y1,z1, c_side);
    hw_tri(x0,y0,z0, x0,y1,z1, x0,y1,z0, c_side);
    hw_tri(x1,y0,z0, x1,y0,z1, x1,y1,z1, c_side);
    hw_tri(x1,y0,z0, x1,y1,z1, x1,y1,z0, c_side);
}

/* árvore: tronco + copa em cone */
static void hw_arvore(float x, float z, float r) {
    hw_box(x, 0, z, r * 0.4f, 0x7A4A1EFF);
    float base = 2.0f, topo = r * 3.0f;
    uint32_t verde = 0x2E7D32FF;
    for (int i = 0; i < 6; i++) {
        float a0 = i * (2 * PI / 6), a1 = (i + 1) * (2 * PI / 6);
        hw_tri(x, topo, z,
               x + cosf(a0) * r, base, z + sinf(a0) * r,
               x + cosf(a1) * r, base, z + sinf(a1) * r, verde);
    }
}

static void hw_chao(void) {
    uint32_t verde = 0x5B8C3AFF;
    float m = MUNDO;
    hw_tri(-m,0,-m,  m,0,-m,  m,0,m, verde);
    hw_tri(-m,0,-m,  m,0,m,  -m,0,m, verde);
}

static void hw_frame_begin(void) { tiny3d_Clear(0x87CEEBFF, TINY3D_CLEAR_ALL); }
static void hw_frame_end(void)   { tiny3d_Flip(); }

/* ---------- jogo ---------- */
static void mundo_init(void) {
    srand(1234);
    for (int i = 0; i < N_ARVORES; i++) {
        arvores[i].x = frand(-MUNDO, MUNDO);
        arvores[i].z = frand(-MUNDO, MUNDO);
        arvores[i].r = frand(2.0f, 4.0f);
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

        /* fugir do jogador */
        if (d < 15.0f) {
            a->est = FUGIR;
        } else if (a->est == FUGIR && d > 25.0f) {
            a->est = VAGAR;
            a->timer = 0;
        }

        /* escolhe o que fazer */
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
            /* vira para o lado oposto ao jogador */
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
        /* não sai do mundo */
        a->x = clampf(a->x, -MUNDO, MUNDO);
        a->z = clampf(a->z, -MUNDO, MUNDO);
    }
}

static void jogador_update(float dt, float lx, float ly) {
    jang = wrap(jang - lx * 2.5f * dt);            /* vira com o analógico */
    float mov = -ly * VEL_JOG * dt;                /* ly pra cima = frente */
    jx += sinf(jang) * mov;
    jz += cosf(jang) * mov;
    jx = clampf(jx, -MUNDO, MUNDO);
    jz = clampf(jz, -MUNDO, MUNDO);
}

static void hw_desenha_animal(Animal *a) {
    uint32_t cor = 0x8D6E63FF;
    if (a->est == DORMIR) cor = 0x6D4C41FF;
    if (a->est == FUGIR)  cor = 0xD7A86EFF;
    /* corpo */
    hw_box(a->x, 0.5f, a->z, 2.0f, cor);
    /* cabeça à frente */
    hw_box(a->x + sinf(a->ang) * 1.2f, 1.2f, a->z + cosf(a->ang) * 1.2f, 1.0f, cor);
}

static void jogo_desenha(void) {
    hw_chao();
    for (int i = 0; i < N_ARVORES; i++)
        hw_arvore(arvores[i].x, arvores[i].z, arvores[i].r);
    for (int i = 0; i < N_ANIMAIS; i++)
        hw_desenha_animal(&animais[i]);
    /* personagem: batatinha */
    hw_box(jx, 0, jz, 2.0f, 0xE8B04BFF);
}

/* ---------- main ---------- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    hw_init();
    sysRegisterCallback(EVENT_SLOT0, sysutil_cb, NULL);
    mundo_init();

    const float dt = 1.0f / 60.0f;
    while (!sair) {
        sysUtilCheckCallback();

        float lx, ly;
        int start;
        hw_read_pad(&lx, &ly, &start);

        jogador_update(dt, lx, ly);
        animais_update(dt);

        hw_frame_begin();
        jogo_desenha();
        hw_frame_end();
    }

    return 0;
}
