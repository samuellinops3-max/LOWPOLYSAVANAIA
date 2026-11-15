// Animal Life Savanna - Low Poly - PS3 - PSL1GHT
// Analógico esquerdo = andar, X = correr / atacar, Quadrado = comer / beber, R1 = trocar animal
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

#define CB_SIZE (0x100000)
#define HOST_SIZE (32*1024*1024)
#define WORLD 3600.0f
#define PI 3.14159265f
#define MAX_TREE 180
#define MAX_ANIMAL 28
#define MAX_ROCK 60
#define MAX_GRASS 300

typedef struct { float x,y; int type; float s; } Tree;
typedef struct { float x,y; float s; int type; } Rock;
typedef struct { float x,y; float s; } Grass;
typedef struct {
  float x,y,ang,tgt,spd;
  int type, alive, hp, hunger, thirst, tick, state, tx, ty;
  int isPlayer;
} Animal;

static gcmContextData *ctx;
static u32 W,H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur=0, first_flip=1;
static volatile int running=1;
static void sysutil_cb(u64 st,u64 p,void *u){ (void)p;(void)u; if(st==SYSUTIL_EXIT_GAME) running=0; }

static void init_screen(void){
  void *host=memalign(1024*1024,HOST_SIZE);
  rsxInit(&ctx,CB_SIZE,HOST_SIZE,host);
  videoState st; videoGetState(VIDEO_PRIMARY,0,&st);
  u32 resid=st.displayMode.resolution;
  if(videoGetResolutionAvailability(VIDEO_PRIMARY,VIDEO_RESOLUTION_720,VIDEO_ASPECT_AUTO,0)) resid=VIDEO_RESOLUTION_720;
  videoResolution res; videoGetResolution(resid,&res); W=res.width; H=res.height;
  videoConfiguration vc; memset(&vc,0,sizeof(vc));
  vc.resolution=resid; vc.format=VIDEO_BUFFER_FORMAT_XRGB; vc.aspect=VIDEO_ASPECT_AUTO; vc.pitch=W*4;
  videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);
  gcmSetFlipMode(GCM_FLIP_VSYNC);
  for(int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64,W*H*4); gcmAddressToOffset(fb[i],&fboff[i]); gcmSetDisplayBuffer(i,fboff[i],W*4,W,H); }
  back=(u32*)memalign(64,W*H*4); memset(back,0,W*H*4); gcmResetFlipStatus();
}
static void wait_flip(void){ while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus(); }
static void present(void){
  if(!first_flip) wait_flip(); else gcmResetFlipStatus();
  memcpy(fb[cur],back,W*H*4); gcmSetFlip(ctx,cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur^=1; first_flip=0;
}
static void fill_rect(int x,int y,int w,int h,u32 c){
  int x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h;
  if(x1>(int)W) x1=W; if(y1>(int)H) y1=H;
  for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
  if(r<1) r=1;
  if(cx+r<0||cx-r>=(int)W||cy+r<0||cy-r>=(int)H) return;
  for(int dy=-r;dy<=r;dy++){ int y=cy+dy; if(y<0||y>=(int)H) continue; int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw, x1=cx+hw; if(x0<0)x0=0; if(x1>=(int)W)x1=W-1; u32 *p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c; }
}
static void tri(int x0,int y0,int x1,int y1,int x2,int y2,u32 c){
  // triangulo low poly preenchido por scanline
  int miny=y0<y1?(y0<y2?y0:y2):(y1<y2?y1:y2);
  int maxy=y0>y1?(y0>y2?y0:y2):(y1>y2?y1:y2);
  if(miny<0) miny=0; if(maxy>=(int)H) maxy=H-1;
  for(int y=miny;y<=maxy;y++){
    float t1=0,t2=0; int cnt=0; float xs[3];
    // interseccao com arestas
    int ax[3]={x0,x1,x2}, ay[3]={y0,y1,y2};
    for(int e=0;e<3;e++){
      int xa=ax[e], ya=ay[e], xb=ax[(e+1)%3], yb=ay[(e+1)%3];
      if((y>=ya && y<yb)||(y>=yb && y<ya)){
        if(yb==ya) continue;
        float t=(float)(y-ya)/(float)(yb-ya);
        xs[cnt++]=xa+(xb-xa)*t;
        if(cnt>=3) break;
      }
    }
    if(cnt>=2){
      int xa=(int)(xs[0]<xs[1]?xs[0]:xs[1]);
      int xb=(int)(xs[0]>xs[1]?xs[0]:xs[1]);
      if(xa<0) xa=0; if(xb>=(int)W) xb=W-1;
      u32 *p=back+y*W; for(int x=xa;x<=xb;x++) p[x]=c;
    }
    (void)t1;(void)t2;
  }
}
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[]={
  {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},
  {'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},
  {'4',"#.#" "#.#" "###" "..#" "..#"}, {'5',"###" "#.." "###" "..#" "###"},
  {'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},
  {'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},
  {'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},
  {'C',"###" "#.." "#.." "#.." "###"},{'D',"##." "#.#" "#.#" "#.#" "##."},
  {'E',"###" "#.." "###" "#.." "###"},{'F',"###" "#.." "###" "#.." "#.."},
  {'G',"###" "#.." "#.#" "#.#" "###"},{'H',"#.#" "#.#" "###" "#.#" "#.#"},
  {'I',"###" ".#." ".#." ".#." "###"},{'L',"#.." "#.." "#.." "#.." "###"},
  {'M',"#.#" "###" "###" "#.#" "#.#"},{'N',"#.#" "###" "###" "#.#" "#.#"},
  {'O',"###" "#.#" "#.#" "#.#" "###"},{'P',"###" "#.#" "###" "#.." "#.."},
  {'R',"##." "#.#" "##." "#.#" "#.#"},{'S',"###" "#.." "###" "..#" "###"},
  {'T',"###" ".#." ".#." ".#." ".#."},{'U',"#.#" "#.#" "#.#" "#.#" "###"},
  {'V',"#.#" "#.#" "#.#" "#.#" ".#."},{'W',"#.#" "#.#" "###" "###" "#.#"},
  {'X',"#.#" "#.#" ".#." "#.#" "#.#"},{'Y',"#.#" "#.#" ".#." ".#." ".#."},
  {'Z',"###" "..#" ".#." "#.." "###"},{'/',"..#" "..#" ".#." "#.." "#.."},
  {'-', "###" "..." "..." "..." "..."},{' ', "..." "..." "..." "..." "..."},
  {':',"..." ".#." "..." ".#." "..."},{'.',"..." "..." "..." "..." ".#."},
  {'%',"##." ".#." ".#." "#.." "##."},
};
static const char *find_glyph(char c){ for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL; }
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc - sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){
  for(;*s;s++){ const char *g=find_glyph(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc; }
}
static void draw_text_up(const char *s,int x,int y,int sc,u32 c){ char b[96]; int n=0; for(;s[n]&&n<90;n++) b[n]=(s[n]>='a'&&s[n]<='z')?s[n]-32:s[n]; b[n]=0; draw_text(b,x,y,sc,c); }

static Tree trees[MAX_TREE];
static Rock rocks[MAX_ROCK];
static Grass grass[MAX_GRASS];
static Animal animals[MAX_ANIMAL];
static float camx=WORLD/2, camy=WORLD/2;
static float pad_ax=0, pad_ay=0, pad_mag=0;
static int pad_run=0, pad_eat=0, pad_switch=0, pad_switch_prev=0;
static int playerIdx=0;
static int frame=0;
static int dayTime=0;
static float frand(void){ return (float)rand()/(float)RAND_MAX; }

static int isInRiver(float x,float y){
  // rio serpenteando na savana - 2 rios
  float r1 = 1200 + 350*sinf(x*0.0012f) + 180*sinf(x*0.0025f);
  if(fabsf(y - r1) < 110) return 1;
  float r2 = 2600 + 300*sinf(x*0.001f+2.0f);
  if(fabsf(y - r2) < 85) return 1;
  // lago
  float dx=x-2900, dy=y-600; if(dx*dx+dy*dy < 380*380) return 1;
  return 0;
}
static void spawn_world(void){
  for(int i=0;i<MAX_TREE;i++){
    float x=frand()*WORLD, y=frand()*WORLD;
    int tries=0; while(isInRiver(x,y)&&tries<10){ x=frand()*WORLD; y=frand()*WORLD; tries++; }
    trees[i].x=x; trees[i].y=y; trees[i].type=rand()%3; trees[i].s=0.8f+frand()*0.8f;
  }
  for(int i=0;i<MAX_ROCK;i++){ rocks[i].x=frand()*WORLD; rocks[i].y=frand()*WORLD; rocks[i].s=0.6f+frand()*1.0f; rocks[i].type=rand()%2; }
  for(int i=0;i<MAX_GRASS;i++){ grass[i].x=frand()*WORLD; grass[i].y=frand()*WORLD; grass[i].s=0.5f+frand(); }
  // animais: 0 leao, 1 zebra, 2 girafa, 3 elefante, 4 antilope, 5 rinoceronte, 6 hiena
  int types[28]={0,0,1,1,1,1,2,2,2,3,3,4,4,4,4,5,6,6,1,2,4,1,3,5,4,6,2,1};
  for(int i=0;i<MAX_ANIMAL;i++){
    animals[i].x=400+frand()*(WORLD-800); animals[i].y=400+frand()*(WORLD-800);
    animals[i].ang=frand()*2*PI; animals[i].tgt=animals[i].ang; animals[i].spd=1.0f;
    animals[i].type=types[i]; animals[i].alive=1; animals[i].hp=100; animals[i].hunger=70; animals[i].thirst=70; animals[i].tick=0; animals[i].state=0;
    animals[i].isPlayer=(i==0);
  }
  playerIdx=0; camx=animals[0].x; camy=animals[0].y;
}
static const char* animal_name(int t){
  if(t==0) return "LEAO"; if(t==1) return "ZEBRA"; if(t==2) return "GIRAFA"; if(t==3) return "ELEFANTE"; if(t==4) return "ANTILOPE"; if(t==5) return "RINOCERONTE"; return "HIENA";
}
static u32 animal_body(int t){
  if(t==0) return 0x00E8A020; if(t==1) return 0x00FFFFFF; if(t==2) return 0x00F2C14E; if(t==3) return 0x0080809A; if(t==4) return 0x00D9A066; if(t==5) return 0x009A9AA0; return 0x008A6A3A;
}
static u32 animal_dark(int t){
  if(t==0) return 0x00B8740A; if(t==1) return 0x00111111; if(t==2) return 0x00B8740A; if(t==3) return 0x005A5A6A; if(t==4) return 0x009A6A3A; if(t==5) return 0x006A6A70; return 0x005A4A2A;
}
static float animal_size(int t){
  if(t==0) return 1.0f; if(t==1) return 0.9f; if(t==2) return 1.5f; if(t==3) return 1.9f; if(t==4) return 0.7f; if(t==5) return 1.4f; return 0.85f;
}
static void read_pad(void){
  padInfo info; padData d; pad_ax=pad_ay=pad_mag=0; pad_run=pad_eat=pad_switch=0;
  ioPadGetInfo(&info);
  if(info.status[0]){
    ioPadGetData(0,&d);
    float ax=((int)d.ANA_L_H-128)/128.0f, ay=((int)d.ANA_L_V-128)/128.0f;
    if(d.BTN_LEFT) ax=-1; if(d.BTN_RIGHT) ax=1; if(d.BTN_UP) ay=-1; if(d.BTN_DOWN) ay=1;
    pad_ax=ax; pad_ay=ay; pad_mag=sqrtf(ax*ax+ay*ay);
    pad_run=d.BTN_CROSS||d.BTN_R1;
    pad_eat=d.BTN_SQUARE||d.BTN_TRIANGLE;
    pad_switch=d.BTN_R2||d.BTN_L1;
  }
}
static void update_animals(void){
  for(int i=0;i<MAX_ANIMAL;i++){
    Animal *a=&animals[i]; if(!a->alive) continue; a->tick++;
    if(a->isPlayer){
      if(pad_mag>0.25f) a->tgt=atan2f(pad_ay,pad_ax);
      float d=a->tgt-a->ang; while(d>PI) d-=2*PI; while(d<-PI) d+=2*PI; if(d>0.12f) d=0.12f; if(d<-0.12f) d=-0.12f; a->ang+=d;
      float sp=pad_run?4.2f:2.2f; if(pad_mag<0.25f) sp=0;
      float nx=a->x+cosf(a->ang)*sp, ny=a->y+sinf(a->ang)*sp;
      if(nx>20&&nx<WORLD-20&&ny>20&&ny<WORLD-20){ a->x=nx; a->y=ny; }
      // fome / sede cai devagar
      if(a->tick%120==0){ if(a->hunger>0) a->hunger--; if(a->thirst>0) a->thirst--; }
      if(pad_eat){
        if(isInRiver(a->x,a->y)){ a->thirst+=2; if(a->thirst>100) a->thirst=100; }
        else {
          // comer grama se herbivoro, cacar se leao/hiena
          if(a->type==0||a->type==6){
            for(int j=0;j<MAX_ANIMAL;j++){ if(j==i) continue; Animal *b=&animals[j]; if(!b->alive) continue; float dx=b->x-a->x, dy=b->y-a->y; if(dx*dx+dy*dy<55*55){ b->hp-=2; if(b->hp<=0){ b->alive=0; a->hunger+=35; if(a->hunger>100) a->hunger=100; } } }
          } else { a->hunger+=1; if(a->hunger>100) a->hunger=100; }
        }
      }
      if(a->hunger<=0||a->thirst<=0){ a->hp--; if(a->hp<=0) a->alive=0; }
      else if(a->hp<100 && a->tick%180==0) a->hp++;
    } else {
      // IA - vida de animal
      if(a->tick%90==0){
        int r=rand()%100;
        if(r<35){ a->state=0; a->tgt=frand()*2*PI; }
        else if(r<70){ a->state=1; // ir beber
          a->tx=1200+rand()%1800; a->ty=1200+350*sinf(a->tx*0.0012f); a->tgt=atan2f(a->ty-a->y,a->tx-a->x);
        } else { a->state=2; a->tgt=frand()*2*PI; }
      }
      // foge de predador
      for(int j=0;j<MAX_ANIMAL;j++){
        Animal *p=&animals[j]; if(!p->alive||!p->isPlayer) continue;
        if((p->type==0||p->type==6)&&(a->type!=0&&a->type!=6)){
          float dx=a->x-p->x, dy=a->y-p->y; if(dx*dx+dy*dy<260*260){ a->tgt=atan2f(dy,dx); a->state=3; }
        }
      }
      float d=a->tgt-a->ang; while(d>PI) d-=2*PI; while(d<-PI) d+=2*PI; if(d>0.06f) d=0.06f; if(d<-0.06f) d=-0.06f; a->ang+=d;
      float sp=1.1f; if(a->state==3) sp=3.4f; else if(a->state==1) sp=1.6f;
      float nx=a->x+cosf(a->ang)*sp, ny=a->y+sinf(a->ang)*sp;
      if(nx>20&&nx<WORLD-20&&ny>20&&ny<WORLD-20){ a->x=nx; a->y=ny; }
      if(a->tick%150==0){ if(a->hunger<100) a->hunger++; if(isInRiver(a->x,a->y)&&a->thirst<100) a->thirst+=10; }
    }
    if(a->x<20) a->x=20; if(a->x>WORLD-20) a->x=WORLD-20; if(a->y<20) a->y=20; if(a->y>WORLD-20) a->y=WORLD-20;
  }
  // trocar de animal com R1/R2 - vira outro animal vivo
  if(pad_switch && !pad_switch_prev){
    animals[playerIdx].isPlayer=0;
    for(int k=1;k<=MAX_ANIMAL;k++){ int ni=(playerIdx+k)%MAX_ANIMAL; if(animals[ni].alive){ playerIdx=ni; animals[ni].isPlayer=1; break; } }
  }
  pad_switch_prev=pad_switch;
  Animal *p=&animals[playerIdx];
  camx += (p->x-camx)*0.12f; camy += (p->y-camy)*0.12f;
  dayTime++; if(dayTime>36000) dayTime=0;
}
static void draw_ground(float sc,float hw,float hh){
  // savana low poly - patches grandes de cor, sem textura pesada
  u32 n=W*H; for(u32 i=0;i<n;i++) back[i]=0x00C9A83A;
  // patches
  for(int gx=-2;gx<8;gx++) for(int gy=-2;gy<8;gy++){
    int px=(int)(((gx*400)-camx)*sc+hw), py=(int)(((gy*400)-camy)*sc+hh);
    int sz=(int)(400*sc);
    if(px+sz<0||px>(int)W||py+sz<0||py>(int)H) continue;
    u32 col=((gx+gy)%2==0)?0x00D9B83A:0x00C9A83A;
    if(((gx*7+gy*13)%5)==0) col=0x00B8A02E;
    fill_rect(px,py,sz,sz,col);
  }
  // rios low poly - faixas
  for(int x=0;x<(int)W;x+=8){
    float wx=camx+(x-hw)/sc;
    float ry1=1200+350*sinf(wx*0.0012f)+180*sinf(wx*0.0025f);
    float ry2=2600+300*sinf(wx*0.001f+2.0f);
    int y1=(int)((ry1-camy)*sc+hh), y2=(int)((ry2-camy)*sc+hh);
    fill_rect(x,y1- (int)(110*sc),8,(int)(220*sc),0x002A9AD6);
    fill_rect(x,y2- (int)(85*sc),8,(int)(170*sc),0x002A9AD6);
    // brilho da agua
    fill_rect(x,y1,8,2,0x00BFEFFF);
    fill_rect(x,y2,8,2,0x00BFEFFF);
  }
  // lago
  int lx=(int)((2900-camx)*sc+hw), ly=(int)((600-camy)*sc+hh), lr=(int)(380*sc);
  circle(lx,ly,lr,0x002A9AD6); circle(lx,ly,lr-6,0x003AB4E8);
  // areia em volta do rio/lago
  circle(lx,ly,lr+10,0x00E8D48A); circle(lx,ly,lr,0x002A9AD6);
  // grama low poly - triangulos pequenos
  for(int i=0;i<MAX_GRASS;i++){
    int px=(int)((grass[i].x-camx)*sc+hw), py=(int)((grass[i].y-camy)*sc+hh);
    if(px<-20||px>(int)W+20||py<-20||py>(int)H+20) continue;
    int s=(int)(8*grass[i].s*sc)+2;
    tri(px,py-s,px-s,py+s,px+s,py+s,0x007AB83A);
  }
  // pedras low poly
  for(int i=0;i<MAX_ROCK;i++){
    int px=(int)((rocks[i].x-camx)*sc+hw), py=(int)((rocks[i].y-camy)*sc+hh);
    if(px<-40||px>(int)W+40||py<-40||py>(int)H+40) continue;
    int s=(int)(16*rocks[i].s*sc)+4;
    tri(px,py-s,px-s,py+s,px+s,py+s,0x00888888);
    tri(px-s,py+s,px+s,py+s,px,py+s+4,0x00666666);
  }
}
static void draw_tree(Tree *t,float sc,float hw,float hh){
  int px=(int)((t->x-camx)*sc+hw), py=(int)((t->y-camy)*sc+hh);
  if(px<-80||px>(int)W+80||py<-100||py>(int)H+100) return;
  int s=(int)(46*t->s*sc)+8;
  // sombra low poly
  circle(px,py+s, s, 0x00A8902A);
  // tronco
  fill_rect(px-3,py,6,s,0x007A4A1A);
  // copa low poly - 3 triangulos
  if(t->type==0){ // acacia
    tri(px-s,py-6,px+s,py-6,px,py-22,0x004A8A2A);
    tri(px-s+6,py-10,px+s-6,py-10,px,py-30,0x005AA83A);
  } else if(t->type==1){ // palmeira / arvore alta
    tri(px-s/2,py-4,px+s/2,py-4,px,py-38,0x003A7A2A);
    tri(px-s,py-2,px,py-18,px+s,py-2,0x004A9A3A);
  } else { // arbusto
    tri(px-s,py,px+s,py,px,py-28,0x005A9A2A);
    circle(px,py-18,s/2,0x006AA83A);
  }
}
static void draw_animal(Animal *a,float sc,float hw,float hh){
  int px=(int)((a->x-camx)*sc+hw), py=(int)((a->y-camy)*sc+hh);
  if(px<-90||px>(int)W+90||py<-90||py>(int)H+90) return;
  float sz=animal_size(a->type);
  int s=(int)(18*sz*sc)+6;
  u32 body=animal_body(a->type), dark=animal_dark(a->type);
  // sombra
  circle(px,py+s+6,s+2,0x00A8902A);
  float ca=cosf(a->ang), sa=sinf(a->ang);
  int hx=px+(int)(ca*s*1.2f), hy=py+(int)(sa*s*1.2f);
  int tx=px-(int)(ca*s*1.0f), ty=py-(int)(sa*s*1.0f);
  // corpo low poly - triangulo + circulo
  tri(px-s,py-s/2,px+s,py-s/2,px,py+s,body);
  circle(px,py,s,body);
  circle(px,py,s-4,dark);
  circle(px,py,s-7,body);
  // pernas - 4 triangulos pequenos
  for(int l=0;l<4;l++){
    int lx=px+(l%2? s/2:-s/2), ly=py+(l<2? -s/2:s/2);
    tri(lx-3,ly,lx+3,ly,lx,ly+7,dark);
  }
  // cabeca low poly
  circle(hx,hy,s*3/4,body);
  tri(hx,hy-s*3/4,hx+s/2,hy,hx,hy+s*3/4,dark);
  // orelhas / chifre
  if(a->type==0){ // juba leao
    circle(hx,hy,s*3/4+3,0x007A4A0A); circle(hx,hy,s*3/4-1,body);
  } else if(a->type==2){ // pescoco girafa
    int nx=hx+(int)(ca*s), ny=hy+(int)(sa*s)-s;
    tri(hx,hy,nx,ny,nx+6,ny+6,body); circle(nx,ny,s/2,body);
    circle(nx+2,ny-2,2,0x00FFFFFF); circle(nx+3,ny-1,1,0x00000000);
  } else if(a->type==3){ // tromba elefante
    int nx=hx+(int)(ca*s), ny=hy+(int)(sa*s)+s/2;
    tri(hx,hy,nx,ny,nx+4,ny+4,dark);
    tri(hx-s/2,hy-s/2,hx,hy-s, hx,hy,dark);
  } else if(a->type==5){ // chifre rinoceronte
    tri(hx,hy-s/2,hx+s/3,hy-s,hx+s/2,hy,dark);
  }
  // olho
  circle(hx+3,hy-3,3,0x00FFFFFF); circle(hx+4,hy-3,1,0x00000000);
  // zebra listras
  if(a->type==1){
    fill_rect(px-s/2,py-2,s,2,0x00111111);
    fill_rect(px-s/2,py+4,s,2,0x00111111);
  }
  // marca do jogador
  if(a->isPlayer){
    tri(px,py-s-22,px-10,py-s-8,px+10,py-s-8,0x00FFD23F);
    circle(px,py-s-14,3,0x00FFD23F);
  }
  // barra de vida
  if(a->hp<100){
    fill_rect(px-18,py-s-18,36,5,0x00333333);
    fill_rect(px-18,py-s-18,(int)(36*a->hp/100),5,0x00FF4D4D);
  }
}
static void render(void){
  frame++;
  float sc=(float)W/1280.0f; float hw=W/2.0f, hh=H/2.0f;
  draw_ground(sc,hw,hh);
  // ordena por Y pra ficar certo - desenha arvores e animais juntos por Y simples: arvores primeiro, depois animais
  for(int i=0;i<MAX_TREE;i++) draw_tree(&trees[i],sc,hw,hh);
  // animais ordenados por Y (bolha simples, poucos)
  int order[MAX_ANIMAL]; for(int i=0;i<MAX_ANIMAL;i++) order[i]=i;
  for(int i=0;i<MAX_ANIMAL;i++) for(int j=i+1;j<MAX_ANIMAL;j++) if(animals[order[j]].y<animals[order[i]].y){ int t=order[i]; order[i]=order[j]; order[j]=t; }
  for(int k=0;k<MAX_ANIMAL;k++){ Animal *a=&animals[order[k]]; if(a->alive) draw_animal(a,sc,hw,hh); }
  // HUD - low poly limpo
  int ui=(int)(4*sc+0.5f); if(ui<3) ui=3;
  Animal *p=&animals[playerIdx];
  // caixa superior esquerda
  fill_rect(18,16,300,112,0x00000000);
  draw_text_up(animal_name(p->type),28,24,ui+1,0x00FFD23F);
  char buf[64];
  sprintf(buf,"HP %d",p->hp); draw_text_up(buf,28,52,ui,0x00FFFFFF);
  sprintf(buf,"FOME %d",p->hunger); draw_text_up(buf,28,74,ui,0x0066FF66);
  sprintf(buf,"SEDE %d",p->thirst); draw_text_up(buf,150,74,ui,0x0055D6FF);
  // fome/sede barras
  fill_rect(28,96,110,8,0x00333333); fill_rect(28,96,(int)(110*p->hunger/100),8,0x0066FF66);
  fill_rect(150,96,110,8,0x00333333); fill_rect(150,96,(int)(110*p->thirst/100),8,0x0055D6FF);
  // ajuda
  draw_text_up("ANALOGICO ANDAR  X CORRER  QUADRADO COMER/BEBER  R2 TROCAR ANIMAL",24,H-30,ui,0x00FFFFFF);
  // bussola / mapa mini
  int mx=W-130, my=20, ms=110;
  fill_rect(mx,my,ms,ms,0x00000000);
  fill_rect(mx+2,my+2,ms-4,ms-4,0x002A5A2A);
  for(int i=0;i<MAX_ANIMAL;i++){ int dx=(int)((animals[i].x/WORLD)*ms), dy=(int)((animals[i].y/WORLD)*ms); if(animals[i].isPlayer) circle(mx+dx,my+dy,4,0x00FFD23F); else if(animals[i].alive) circle(mx+dx,my+dy,3,0x00FFFFFF); }
  // rio no minimapa
  for(int x=0;x<ms;x++){ int ry=(int)((1200+350*sinf((x/(float)ms*WORLD)*0.0012f))/WORLD*ms); fill_rect(mx+x,my+ry,2,2,0x002A9AD6); }
  if(!p->alive){
    int bw=text_width("VOCE MORREU",ui*2)+60, bh=90;
    fill_rect(W/2-bw/2,H/2-bh/2,bw,bh,0x00000000);
    draw_text_up("VOCE MORREU",W/2-text_width("VOCE MORREU",ui*2)/2,H/2-20,ui*2,0x00FF4D4D);
    draw_text_up("R2 TROCAR DE ANIMAL",W/2-text_width("R2 TROCAR DE ANIMAL",ui)/2,H/2+20,ui,0x00FFFFFF);
  }
}
int main(int argc,char **argv){
  (void)argc;(void)argv;
  init_screen();
  ioPadInit(7);
  sysUtilRegisterCallback(0,sysutil_cb,NULL);
  srand(20261115);
  spawn_world();
  while(running){
    sysUtilCheckCallback();
    read_pad();
    update_animals();
    // se player morreu, permite trocar rapido
    if(!animals[playerIdx].alive && pad_switch && !pad_switch_prev){
      // ja tratado em update, mas garante
    }
    render();
    present();
  }
  return 0;
}