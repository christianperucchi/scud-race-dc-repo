/*
 * SCUD RACE - Dreamcast tech demo (what-if 1998 build)
 * Course data: AM2 SET2 "scud" Ninja demo, converted for KallistiOS.
 *
 * Renders the full beginner course with VQ textures, baked lighting,
 * hardware table fog, per-cell frustum culling, near-plane clipping and
 * store-queue vertex submission.
 */
#include <kos.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dc/sound/stream.h>
#define F_PI 3.14159265f

/* ------------------------------------------------------------------ data */
typedef struct { float mn[3], mx[3]; uint32_t first, count; } cell_t;
typedef struct { uint16_t tex, mode; uint32_t voff, nv, ioff, ni; } batch_t;
typedef struct { float x, y, z, u, v; uint32_t argb; } wvert_t;
typedef struct { uint32_t fmt; uint16_t w, h; uint32_t off, size; } texent_t;

static cell_t  *cells;  static uint32_t ncells;
static batch_t *batches; static uint32_t nbatches;
static wvert_t *verts;  static uint16_t *idx;
static pvr_poly_hdr_t *bhdr;          /* one compiled header per batch */
static uint8_t *btr;                  /* 1 = translucent list */

typedef struct { float x, y, z, wl, wr; } ppt_t;
static ppt_t *pp; static uint32_t npath;
static float *pyaw, *pkap, *pslope;

static pvr_ptr_t font_tex;
static pvr_poly_hdr_t font_hdr;

/* -------------------------------------------------------------- helpers */
static void *load_file(const char *fn, size_t *sz) {
    file_t f = fs_open(fn, O_RDONLY);
    if(f < 0) { printf("cannot open %s\n", fn); arch_exit(); }
    size_t n = fs_total(f);
    void *p = fs_mmap(f);                 /* romdisk: zero-copy */
    if(!p || ((uintptr_t)p & 3)) {
        p = memalign(32, n);
        fs_read(f, p, n);
    }
    if(sz) *sz = n;
    return p;                              /* file left open while mmapped */
}

static void load_world(void) {
    uint8_t *p = load_file("/rd/world.bin", NULL);
    uint32_t *h = (uint32_t *)p;
    ncells = h[1]; nbatches = h[2];
    uint32_t nv = h[3];
    cells   = (cell_t *)(p + 20);
    batches = (batch_t *)((uint8_t *)cells + ncells * sizeof(cell_t));
    verts   = (wvert_t *)((uint8_t *)batches + nbatches * sizeof(batch_t));
    idx     = (uint16_t *)((uint8_t *)verts + nv * sizeof(wvert_t));
    printf("world: %lu cells, %lu batches, %lu verts\n",
           (unsigned long)ncells, (unsigned long)nbatches, (unsigned long)nv);
}

static void load_path(void) {
    uint8_t *p = load_file("/rd/path.bin", NULL);
    npath = ((uint32_t *)p)[1];
    pp = (ppt_t *)(p + 8);
    pyaw = malloc(npath * 4); pkap = malloc(npath * 4); pslope = malloc(npath * 4);
    for(uint32_t i = 0; i < npath; i++) {
        ppt_t *a = &pp[(i + npath - 2) % npath], *b = &pp[(i + 2) % npath];
        pyaw[i] = atan2f(b->x - a->x, b->z - a->z);
        float h = sqrtf((b->x-a->x)*(b->x-a->x) + (b->z-a->z)*(b->z-a->z));
        pslope[i] = (b->y - a->y) / (h > 0.1f ? h : 0.1f);
    }
    for(uint32_t i = 0; i < npath; i++) {
        float d = pyaw[(i + 3) % npath] - pyaw[(i + npath - 3) % npath];
        while(d > F_PI) d -= 2*F_PI; while(d < -F_PI) d += 2*F_PI;
        pkap[i] = d / 6.0f;
    }
    /* smooth curvature */
    for(int it = 0; it < 6; it++) for(uint32_t i = 0; i < npath; i++)
        pkap[i] = 0.25f*pkap[(i+npath-1)%npath] + 0.5f*pkap[i] + 0.25f*pkap[(i+1)%npath];
}

static void load_textures(void) {
    uint8_t *p = load_file("/rd/tex.bin", NULL);
    uint32_t n = ((uint32_t *)p)[1];
    texent_t *te = (texent_t *)(p + 8);
    uint8_t *blob = p + 8 + n * sizeof(texent_t);
    pvr_ptr_t *ptrs = malloc(n * sizeof(pvr_ptr_t));
    size_t total = 0;

    for(uint32_t i = 0; i < n; i++) {
        ptrs[i] = pvr_mem_malloc(te[i].size);
        pvr_txr_load(blob + te[i].off, ptrs[i], te[i].size);
        total += te[i].size;
    }
    printf("textures: %lu (%u KB VRAM)\n", (unsigned long)n, (unsigned)(total / 1024));

    /* compile one header per batch */
    bhdr = memalign(32, nbatches * sizeof(pvr_poly_hdr_t));
    btr  = malloc(nbatches);
    for(uint32_t b = 0; b < nbatches; b++) {
        texent_t *t = &te[batches[b].tex];
        uint32_t m = batches[b].mode;
        int tr = (m & 16) != 0;
        int pf = t->fmt & 3;
        int fmt = (pf == 0 ? PVR_TXRFMT_RGB565 : pf == 1 ? PVR_TXRFMT_ARGB1555
                                                       : PVR_TXRFMT_ARGB4444)
                  | PVR_TXRFMT_TWIDDLED | ((t->fmt & 4) ? PVR_TXRFMT_VQ_ENABLE : 0);
        pvr_poly_cxt_t cxt;
        pvr_poly_cxt_txr(&cxt, tr ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY,
                         fmt, t->w, t->h, ptrs[batches[b].tex], PVR_FILTER_BILINEAR);
        cxt.gen.culling  = PVR_CULLING_NONE;
        cxt.gen.fog_type = PVR_FOG_TABLE;
        cxt.txr.mipmap   = (t->fmt & 8) ? true : false;
        cxt.txr.uv_clamp = (pvr_uv_clamp_t)(((m & 1) ? PVR_UVCLAMP_U : 0) | ((m & 2) ? PVR_UVCLAMP_V : 0));
        cxt.txr.uv_flip  = (pvr_uv_flip_t)(((m & 4) ? PVR_UVFLIP_U : 0) | ((m & 8) ? PVR_UVFLIP_V : 0));
        if(tr) cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
        pvr_poly_compile(&bhdr[b], &cxt);
        btr[b] = tr;
    }

    /* HUD font */
    uint8_t *fp = load_file("/rd/font.bin", NULL);
    font_tex = pvr_mem_malloc(128 * 128 * 2);
    pvr_txr_load(fp, font_tex, 128 * 128 * 2);
    pvr_poly_cxt_t fc;
    pvr_poly_cxt_txr(&fc, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED,
                     128, 128, font_tex, PVR_FILTER_NONE);
    fc.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    pvr_poly_compile(&font_hdr, &fc);
}

/* ============================================================= render core */
#define SCR_W 640.0f
#define SCR_H 480.0f
#define FOCAL 415.7f
#define ZNEAR 0.6f
#define DRAW_DIST 600.0f

static float cam_e[3], cam_r[3], cam_u[3], cam_f[3];
static float viewm[12];                     /* world -> view, rows r,u,f + t */

static void look_at(const float *e, const float *t) {
    float f[3] = { t[0]-e[0], t[1]-e[1], t[2]-e[2] };
    float l = 1.0f / sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] *= l; f[1] *= l; f[2] *= l;
    float r[3] = { -f[2], 0.0f, f[0] };
    l = 1.0f / sqrtf(r[0]*r[0] + r[2]*r[2] + 1e-6f); r[0] *= l; r[2] *= l;
    float u[3] = { r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0] };
    memcpy(cam_e, e, 12); memcpy(cam_r, r, 12); memcpy(cam_u, u, 12); memcpy(cam_f, f, 12);
    const float *R[3] = { r, u, f };
    for(int i = 0; i < 3; i++) {
        viewm[i*4+0] = R[i][0]; viewm[i*4+1] = R[i][1]; viewm[i*4+2] = R[i][2];
        viewm[i*4+3] = -(R[i][0]*e[0] + R[i][1]*e[1] + R[i][2]*e[2]);
    }
}

typedef struct { float x, y, z, u, v; uint32_t c; } cvert_t;
static cvert_t *tv; static uint32_t tv_cap;
static uint32_t stat_tris, stat_cells, vb_used, vb_limit;

static inline void emit(const cvert_t *a, int eol) {
    pvr_vertex_t *v = pvr_dr_target();
    float iz = 1.0f / a->z;
    v->flags = eol ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    v->x = SCR_W*0.5f + FOCAL*a->x*iz; v->y = SCR_H*0.5f - FOCAL*a->y*iz; v->z = iz;
    v->u = a->u; v->v = a->v; v->argb = a->c; v->oargb = 0;
    pvr_dr_commit(v);
}
static inline uint32_t lerp_col(uint32_t a, uint32_t b, float t) {
    uint32_t o = 0;
    for(int s = 0; s < 32; s += 8) { float ca = (a>>s)&255, cb = (b>>s)&255; o |= ((uint32_t)(ca+(cb-ca)*t)&255) << s; }
    return o;
}
static inline void clip_edge(const cvert_t *a, const cvert_t *b, cvert_t *o) {
    float t = (ZNEAR - a->z) / (b->z - a->z);
    o->x = a->x+(b->x-a->x)*t; o->y = a->y+(b->y-a->y)*t; o->z = ZNEAR;
    o->u = a->u+(b->u-a->u)*t; o->v = a->v+(b->v-a->v)*t; o->c = lerp_col(a->c, b->c, t);
}
static inline int offscreen(const cvert_t *a, const cvert_t *b, const cvert_t *c) {
    const float kx = (SCR_W*0.5f)/FOCAL*1.05f, ky = (SCR_H*0.5f)/FOCAL*1.05f;
    if(a->x >  a->z*kx && b->x >  b->z*kx && c->x >  c->z*kx) return 1;
    if(a->x < -a->z*kx && b->x < -b->z*kx && c->x < -c->z*kx) return 1;
    if(a->y >  a->z*ky && b->y >  b->z*ky && c->y >  c->z*ky) return 1;
    if(a->y < -a->z*ky && b->y < -b->z*ky && c->y < -c->z*ky) return 1;
    return 0;
}

/* draw an indexed triangle list whose vertices are mapped to view space by m */
static void draw_mesh(const wvert_t *src, uint32_t nv, const uint16_t *ix, uint32_t ni,
                      const pvr_poly_hdr_t *hdr, const float *m) {
    if(vb_used + ni*32 + 64 > vb_limit || nv > tv_cap) return;
    for(uint32_t i = 0; i < nv; i++) {
        float x = src[i].x, y = src[i].y, z = src[i].z;
        tv[i].x = m[0]*x + m[1]*y + m[2]*z + m[3];
        tv[i].y = m[4]*x + m[5]*y + m[6]*z + m[7];
        tv[i].z = m[8]*x + m[9]*y + m[10]*z + m[11];
        tv[i].u = src[i].u; tv[i].v = src[i].v; tv[i].c = src[i].argb;
    }
    pvr_prim((void *)hdr, sizeof(pvr_poly_hdr_t)); vb_used += 32;
    for(uint32_t t = 0; t < ni; t += 3) {
        const cvert_t *a = &tv[ix[t]], *b = &tv[ix[t+1]], *c = &tv[ix[t+2]];
        int in = (a->z >= ZNEAR) + (b->z >= ZNEAR) + (c->z >= ZNEAR);
        if(!in || offscreen(a, b, c)) continue;
        if(in == 3) { emit(a,0); emit(b,0); emit(c,1); vb_used += 96; stat_tris++; continue; }
        const cvert_t *pv[3] = { a, b, c }; cvert_t out[4]; int n = 0;
        for(int k = 0; k < 3; k++) {
            const cvert_t *p = pv[k], *q = pv[(k+1)%3];
            int pin = p->z >= ZNEAR, qin = q->z >= ZNEAR;
            if(pin) out[n++] = *p;
            if(pin != qin) clip_edge(p, q, &out[n++]);
        }
        if(n == 3) { emit(&out[0],0); emit(&out[1],0); emit(&out[2],1); vb_used += 96; }
        else if(n == 4) { emit(&out[0],0); emit(&out[1],0); emit(&out[3],0); emit(&out[2],1); vb_used += 128; }
        stat_tris++;
    }
}

static float cell_dist;
static int cell_visible(const cell_t *c) {
    float cx = (c->mn[0]+c->mx[0])*0.5f - cam_e[0], cy = (c->mn[1]+c->mx[1])*0.5f - cam_e[1], cz = (c->mn[2]+c->mx[2])*0.5f - cam_e[2];
    float ex = (c->mx[0]-c->mn[0])*0.5f, ey = (c->mx[1]-c->mn[1])*0.5f, ez = (c->mx[2]-c->mn[2])*0.5f;
    float rad = sqrtf(ex*ex + ey*ey + ez*ez), d = sqrtf(cx*cx + cy*cy + cz*cz);
    cell_dist = d - rad;
    if(d - rad > DRAW_DIST) return 0;
    float vz = cx*cam_f[0] + cy*cam_f[1] + cz*cam_f[2];
    if(vz + rad < ZNEAR) return 0;
    float vx = cx*cam_r[0] + cy*cam_r[1] + cz*cam_r[2], vy = cx*cam_u[0] + cy*cam_u[1] + cz*cam_u[2];
    const float kx = (SCR_W*0.5f)/FOCAL, ky = (SCR_H*0.5f)/FOCAL;
    const float nx = 1.0f/sqrtf(1+kx*kx), ny = 1.0f/sqrtf(1+ky*ky);
    if(( vx - kx*vz)*nx > rad || (-vx - kx*vz)*nx > rad) return 0;
    if(( vy - ky*vz)*ny > rad || (-vy - ky*vz)*ny > rad) return 0;
    return 1;
}

/* ================================================================== cars */
typedef struct { uint32_t nb; batch_t *b; wvert_t *v; uint16_t *ix; pvr_poly_hdr_t *hdr; uint8_t *tr; } cmodel_t;
static cmodel_t cmodels[2];
static pvr_poly_hdr_t shadow_hdr;

static void load_cars(void) {
    uint8_t *p = load_file("/rd/cars.bin", NULL);
    uint32_t ntex = ((uint32_t *)p)[1], nmod = ((uint32_t *)p)[2];
    texent_t *te = (texent_t *)(p + 12);
    uint8_t *q = p + 12 + ntex * sizeof(texent_t);
    pvr_ptr_t *ptr = malloc(ntex * sizeof(pvr_ptr_t));
    for(uint32_t m = 0; m < nmod && m < 2; m++) {
        uint32_t *h = (uint32_t *)q; cmodel_t *cm = &cmodels[m];
        cm->nb = h[0]; uint32_t nv = h[1], ni = h[2];
        cm->b = (batch_t *)(q + 12);
        cm->v = (wvert_t *)((uint8_t *)cm->b + cm->nb * sizeof(batch_t));
        cm->ix = (uint16_t *)((uint8_t *)cm->v + nv * sizeof(wvert_t));
        q = (uint8_t *)cm->ix + ((ni * 2 + 3) & ~3);
    }
    uint8_t *blob = q;
    for(uint32_t i = 0; i < ntex; i++) { ptr[i] = pvr_mem_malloc(te[i].size); pvr_txr_load(blob + te[i].off, ptr[i], te[i].size); }
    for(uint32_t m = 0; m < nmod && m < 2; m++) {
        cmodel_t *cm = &cmodels[m];
        cm->hdr = memalign(32, cm->nb * sizeof(pvr_poly_hdr_t)); cm->tr = malloc(cm->nb);
        for(uint32_t b = 0; b < cm->nb; b++) {
            uint32_t md = cm->b[b].mode; int tr = (md & 16) != 0; pvr_poly_cxt_t cxt;
            if(md & 32) pvr_poly_cxt_col(&cxt, tr ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY);
            else {
                texent_t *t = &te[cm->b[b].tex]; int pf = t->fmt & 3;
                int fmt = (pf == 0 ? PVR_TXRFMT_RGB565 : pf == 1 ? PVR_TXRFMT_ARGB1555 : PVR_TXRFMT_ARGB4444)
                          | PVR_TXRFMT_TWIDDLED | ((t->fmt & 4) ? PVR_TXRFMT_VQ_ENABLE : 0);
                pvr_poly_cxt_txr(&cxt, tr ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY, fmt, t->w, t->h, ptr[cm->b[b].tex], PVR_FILTER_BILINEAR);
                cxt.txr.mipmap = (t->fmt & 8) ? true : false;
                cxt.txr.uv_clamp = (pvr_uv_clamp_t)(((md&1)?PVR_UVCLAMP_U:0)|((md&2)?PVR_UVCLAMP_V:0));
                cxt.txr.uv_flip  = (pvr_uv_flip_t)(((md&4)?PVR_UVFLIP_U:0)|((md&8)?PVR_UVFLIP_V:0));
                if(tr) cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
            }
            cxt.gen.culling = PVR_CULLING_NONE; cxt.gen.fog_type = PVR_FOG_TABLE;
            pvr_poly_compile(&cm->hdr[b], &cxt); cm->tr[b] = tr;
        }
    }
    pvr_poly_cxt_t sc; pvr_poly_cxt_col(&sc, PVR_LIST_TR_POLY);
    sc.gen.culling = PVR_CULLING_NONE; pvr_poly_compile(&shadow_hdr, &sc);
}

/* =============================================================== physics */
#define NCARS 4
#define DT (1.0f/60.0f)
typedef struct {
    int model, ai;
    float s, d, psi, v, r;        /* track coords, heading rel. to track, speed m/s, yaw rate */
    float rpm, thr, brk, slip, skill, lane;
    int gear, lap;
    float pos[3], yaw, pitch;     /* world pose for rendering */
    float m[12];                  /* model -> world */
} car_t;
static car_t car[NCARS];
static const float gear_top[7] = { 0, 21, 35, 49, 62, 74, 86 };
static const float gear_acc[7] = { 0, 11.5f, 8.8f, 6.8f, 5.2f, 4.1f, 3.2f };
static volatile float snd_crash_req = 0, snd_beep_f = 0; static volatile int snd_beep_n = 0;

static inline float wrapf(float s) { float n = (float)npath; while(s >= n) s -= n; while(s < 0) s += n; return s; }
static inline uint32_t pidx(float s) { return ((uint32_t)wrapf(s)) % npath; }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static float torque(float rpm) { float x = (rpm - 6000.0f) / 4500.0f; return clampf(1.0f - x*x*0.6f, 0.45f, 1.0f); }

static void car_physics(car_t *c, float steer, float thr, float brk, float r_cmd_ai, int use_ai_r) {
    uint32_t i = pidx(c->s);
    /* engine / gearbox (automatic) */
    float top = gear_top[c->gear];
    c->rpm = 1100.0f + 7300.0f * clampf(c->v / top, 0, 1.05f);
    if(c->rpm > 7900.0f && c->gear < 6) c->gear++;
    else if(c->gear > 1 && c->v < gear_top[c->gear-1] * 0.62f) c->gear--;
    float a = thr * gear_acc[c->gear] * torque(c->rpm);
    if(c->rpm > 8300.0f) a *= 0.2f;
    a -= 0.00036f * c->v * c->v + 0.12f;          /* aero drag + rolling */
    a -= brk * 15.0f;
    a -= 9.81f * pslope[i] * cosf(c->psi);        /* hills */
    c->v += a * DT; if(c->v < 0) c->v = 0;
    /* steering: bicycle model, grip limited */
    float r_cmd;
    if(use_ai_r) r_cmd = r_cmd_ai;
    else { float delta = steer * 0.42f / (1.0f + c->v / 22.0f); r_cmd = c->v / 2.5f * tanf(delta); }
    float alat_max = 13.5f, alat = fabsf(c->v * r_cmd);
    c->slip *= 0.9f;
    if(alat > alat_max && c->v > 5) {
        float lim = alat_max / c->v * (r_cmd > 0 ? 1 : -1);
        float over = (alat - alat_max) / alat_max;
        r_cmd = lim + (r_cmd - lim) * 0.35f;      /* drift: rotate a bit more than grip allows */
        c->slip = clampf(c->slip + over * 0.25f, 0, 1);
        c->v -= over * 2.5f * DT * c->v * 0.1f;
    }
    c->r += (r_cmd - c->r) * 0.25f;
    /* integrate in path coordinates (car keeps world heading unless it steers) */
    float kap = pkap[i];
    c->psi += (c->r - kap * c->v * cosf(c->psi)) * DT;
    c->psi = clampf(c->psi, -1.2f, 1.2f);
    c->s = wrapf(c->s + c->v * cosf(c->psi) * DT);
    if(pidx(c->s) < 20 && i > npath - 20) c->lap++;
    c->d += -c->v * sinf(c->psi) * DT;
    /* walls */
    float lim_hi = pp[i].wl - 1.1f, lim_lo = -pp[i].wr + 1.1f;
    if(c->d > lim_hi || c->d < lim_lo) {
        c->d = clampf(c->d, lim_lo, lim_hi);
        float hit = fabsf(sinf(c->psi)) * c->v;
        if(hit > 4.0f) {                      /* proper crash */
            if(!c->ai || hit > 10) snd_crash_req = fmaxf(snd_crash_req, clampf(hit / 25.0f, 0.2f, 1.0f) * (c->ai ? 0.4f : 1.0f));
            c->v *= 0.55f + 0.35f * cosf(c->psi); c->psi = -c->psi * 0.3f; c->r = 0;
        } else { c->v *= 0.994f; c->psi *= 0.6f; c->slip = fmaxf(c->slip, 0.35f); }
    }
    c->thr = thr; c->brk = brk;
}

static float track_dist(float a, float b) {           /* signed b - a along the loop */
    float d = b - a, n = (float)npath; if(d > n/2) d -= n; if(d < -n/2) d += n; return d;
}

static void ai_drive(car_t *c, float *thr, float *brk, float *r_cmd) {
    /* racing line: inside of the next corner */
    float ka = 0, kmax = 0;
    for(int k = 15; k <= 60; k += 5) ka += pkap[pidx(c->s + k)];
    ka /= 10.0f;
    uint32_t i = pidx(c->s);
    float half = (pp[i].wl + pp[i].wr) * 0.5f, centre = (pp[i].wl - pp[i].wr) * 0.5f;
    float line = centre - clampf(ka * 250.0f, -1, 1) * half * 0.55f + c->lane;
    /* avoid the car ahead */
    for(int j = 0; j < NCARS; j++) {
        if(&car[j] == c) continue;
        float ds = track_dist(c->s, car[j].s);
        if(ds > 0 && ds < 18 && fabsf(car[j].d - c->d) < 2.6f && c->v > car[j].v - 1)
            line = car[j].d + (car[j].d > centre ? -3.4f : 3.4f);
    }
    line = clampf(line, -pp[i].wr + 2.0f, pp[i].wl - 2.0f);
    float psi_des = atanf((line - c->d) / (c->v * 0.9f + 10.0f));   /* d grows with -psi */
    *r_cmd = pkap[i] * c->v + (-psi_des - c->psi) * 2.6f;
    /* target speed from curvature ahead + braking distance */
    float vt = 95.0f, alat = 12.0f * c->skill;
    for(int k = 0; k <= 160; k += 8) {
        float kk = fabsf(pkap[pidx(c->s + k)]); if(kk > kmax) kmax = kk;
        float vc = sqrtf(alat / (kk + 1e-4f));
        float va = sqrtf(vc*vc + 2.0f * 11.0f * k);
        if(va < vt) vt = va;
    }
    *thr = c->v < vt - 1.0f ? 1.0f : c->v < vt ? 0.5f : 0.0f;
    *brk = c->v > vt + 2.0f ? clampf((c->v - vt) / 8.0f, 0.2f, 1.0f) : 0.0f;
}

static void car_contacts(void) {
    for(int a = 0; a < NCARS; a++) for(int b = a + 1; b < NCARS; b++) {
        float ds = track_dist(car[a].s, car[b].s), dd = car[b].d - car[a].d;
        if(fabsf(ds) < 4.4f && fabsf(dd) < 2.1f) {
            float push = (2.1f - fabsf(dd)) * 0.5f * (dd >= 0 ? 1 : -1);
            car[a].d -= push; car[b].d += push;
            car_t *rear = ds > 0 ? &car[a] : &car[b], *front = ds > 0 ? &car[b] : &car[a];
            float dv = rear->v - front->v;
            if(dv > 0 && fabsf(ds) < 4.4f) { rear->v -= dv * 0.35f; front->v += dv * 0.25f; }
            float hit = fabsf(dv) + fabsf(push) * 6.0f;
            if((!car[a].ai || !car[b].ai) && hit > 2.0f) snd_crash_req = fmaxf(snd_crash_req, clampf(hit / 20.0f, 0.15f, 0.7f));
        }
    }
}

static void car_pose(car_t *c) {
    uint32_t i = pidx(c->s), j = (i + 1) % npath; float t = c->s - floorf(c->s);
    float px = pp[i].x + (pp[j].x - pp[i].x)*t, py = pp[i].y + (pp[j].y - pp[i].y)*t, pz = pp[i].z + (pp[j].z - pp[i].z)*t;
    float a = pyaw[i];
    float rx = -cosf(a), rz = sinf(a);                 /* +d direction (screen right) */
    c->pos[0] = px + rx * c->d; c->pos[1] = py; c->pos[2] = pz + rz * c->d;
    c->yaw = a + c->psi; c->pitch = atanf(pslope[i]);
    float cy = cosf(c->yaw), sy = sinf(c->yaw), cp = cosf(c->pitch), sp = sinf(c->pitch);
    float roll = clampf(-c->r * c->v * 0.004f, -0.06f, 0.06f), cr = cosf(roll), sr = sinf(roll);
    /* R = Ryaw * Rpitch * Rroll (columns are local axes in world) */
    float X[3] = { cy*cr + sy*sp*sr*0, -sr*0, -sy*cr }, Y[3], Z[3];
    /* local z (forward) */
    Z[0] = sy*cp; Z[1] = sp; Z[2] = cy*cp;
    /* local x (car left), yaw only then roll around Z */
    float X0[3] = { cy, 0, -sy };
    float Y0[3] = { -sy*sp, cp, -cy*sp };
    for(int k = 0; k < 3; k++) { X[k] = X0[k]*cr + Y0[k]*sr; Y[k] = Y0[k]*cr - X0[k]*sr; }
    c->m[0] = X[0]; c->m[1] = Y[0]; c->m[2] = Z[0]; c->m[3] = c->pos[0];
    c->m[4] = X[1]; c->m[5] = Y[1]; c->m[6] = Z[1]; c->m[7] = c->pos[1] + 0.02f;
    c->m[8] = X[2]; c->m[9] = Y[2]; c->m[10] = Z[2]; c->m[11] = c->pos[2];
}

static void mat_mul34(const float *a, const float *b, float *o) {   /* o = a * b (3x4 affine) */
    for(int r = 0; r < 3; r++) {
        for(int c = 0; c < 3; c++) o[r*4+c] = a[r*4]*b[c] + a[r*4+1]*b[4+c] + a[r*4+2]*b[8+c];
        o[r*4+3] = a[r*4]*b[3] + a[r*4+1]*b[7] + a[r*4+2]*b[11] + a[r*4+3];
    }
}

static void draw_car(car_t *c, int translucent) {
    float dx = c->pos[0]-cam_e[0], dy = c->pos[1]-cam_e[1], dz = c->pos[2]-cam_e[2];
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if(dist > 260.0f) return;
    float vz = dx*cam_f[0] + dy*cam_f[1] + dz*cam_f[2]; if(vz < -4.0f) return;
    float m[12]; mat_mul34(viewm, c->m, m);
    cmodel_t *cm = &cmodels[c->model];
    for(uint32_t b = 0; b < cm->nb; b++) if(cm->tr[b] == translucent)
        draw_mesh(cm->v + cm->b[b].voff, cm->b[b].nv, cm->ix + cm->b[b].ioff, cm->b[b].ni, &cm->hdr[b], m);
    if(translucent && dist < 120.0f) {               /* blob shadow */
        static wvert_t sv[4]; static const uint16_t si[6] = { 0,1,2, 2,1,3 };
        const float w = 0.95f, l = 2.15f, y = 0.07f;
        float xs[4] = { -w, w, -w, w }, zs[4] = { -l, -l, l, l };
        for(int k = 0; k < 4; k++) { sv[k].x = xs[k]; sv[k].y = y; sv[k].z = zs[k]; sv[k].u = sv[k].v = 0; sv[k].argb = 0x50000000; }
        draw_mesh(sv, 4, si, 6, &shadow_hdr, m);
    }
}

/* ================================================================= sound */
#define SR 22050
static volatile float snd_rpm = 1000, snd_thr = 0, snd_slip = 0, snd_wind = 0, snd_ai_rpm = 1000, snd_ai_vol = 0;
static int16_t sbuf[8192] __attribute__((aligned(32)));
static float ph1, ph2, lp1, lp2, nlp, nhp, crash_env, beep_ph; static uint32_t rng = 12345;
static void *snd_cb(snd_stream_hnd_t hnd, int req, int *recv) {
    int n = req / 2; if(n > 8192) n = 8192;
    if(snd_crash_req > 0) { if(snd_crash_req > crash_env) crash_env = snd_crash_req; snd_crash_req = 0; }
    float f1 = snd_rpm / 60.0f * 3.0f, f2 = snd_ai_rpm / 60.0f * 3.0f * 1.013f;
    float a1 = 0.30f + 0.22f * snd_thr, a2 = snd_ai_vol, sk = snd_slip * 0.55f, wd = snd_wind * 0.22f;
    for(int i = 0; i < n; i++) {
        ph1 += f1 / SR; if(ph1 >= 1) ph1 -= 1;
        ph2 += f2 / SR; if(ph2 >= 1) ph2 -= 1;
        float e1 = (ph1*2-1)*0.55f + ((ph1 < 0.5f) ? 0.25f : -0.25f) + ((fmodf(ph1*0.5f+0.25f,1.0f))*2-1)*0.3f;
        float e2 = (ph2*2-1)*0.6f + ((ph2 < 0.5f) ? 0.2f : -0.2f);
        lp1 += (e1 - lp1) * 0.33f; lp2 += (e2 - lp2) * 0.25f;
        rng = rng * 1664525u + 1013904223u; float nz = ((int32_t)rng >> 8) * (1.0f / 8388608.0f);
        nlp += (nz - nlp) * 0.12f; nhp += (nz - nhp) * 0.6f;
        float out = lp1 * a1 + lp2 * a2 + (nhp - nlp) * sk + nlp * wd + nz * crash_env * 0.9f;
        crash_env *= 0.99965f;
        if(snd_beep_n > 0) { beep_ph += snd_beep_f / SR; if(beep_ph >= 1) beep_ph -= 1; out += (beep_ph < 0.5f ? 0.35f : -0.35f); snd_beep_n--; }
        out = clampf(out, -1.0f, 1.0f);
        sbuf[i] = (int16_t)(out * 26000.0f);
    }
    *recv = n * 2; return sbuf;
}

/* =================================================================== HUD */
static void draw_text(float x, float y, float sc, uint32_t col, const char *s) {
    pvr_prim(&font_hdr, sizeof(font_hdr));
    for(; *s; s++, x += 8*sc) {
        int ch = *s; if(ch <= 32 || ch > 127) continue;
        float u0 = ((ch-32)%16)*8/128.0f, v0 = ((ch-32)/16)*12/128.0f, u1 = u0 + 8/128.0f, v1 = v0 + 12/128.0f, w = 8*sc, h = 12*sc;
        pvr_vertex_t *v;
        v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX; v->x = x;   v->y = y+h; v->z = 10; v->u = u0; v->v = v1; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
        v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX; v->x = x;   v->y = y;   v->z = 10; v->u = u0; v->v = v0; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
        v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX; v->x = x+w; v->y = y+h; v->z = 10; v->u = u1; v->v = v1; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
        v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX_EOL; v->x = x+w; v->y = y; v->z = 10; v->u = u1; v->v = v0; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
    }
}

/* ================================================================== main */
enum { CAM_CHASE, CAM_BONNET, CAM_HELI, CAM_TV, CAM_COUNT };
static const char *cam_names[CAM_COUNT] = { "CHASE", "BONNET", "HELI", "TV" };
#include <kos/dbgio.h>
#ifdef VIDEO
#define VIDEO_ON 1
#else
#define VIDEO_ON 0
#endif
#ifndef START_FRAME
#define START_FRAME 0
#endif
#ifndef END_FRAME
#define END_FRAME 0
#endif

KOS_INIT_FLAGS(INIT_DEFAULT);

int main(int argc, char **argv) {
    pvr_init_params_t params = { { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 }, 2048*1024, 0, 0, 0, 3, 0 };
    pvr_init(&params);
    vb_limit = 2048*1024 - 48*1024;
    pvr_set_bg_color(0.55f, 0.72f, 0.92f);
    pvr_fog_table_color(1.0f, 0.62f, 0.76f, 0.92f);
    pvr_fog_table_linear(230.0f, DRAW_DIST);

    load_world(); load_path(); load_textures(); load_cars();
    uint32_t maxnv = 0;
    for(uint32_t b = 0; b < nbatches; b++) if(batches[b].nv > maxnv) maxnv = batches[b].nv;
    for(int m = 0; m < 2; m++) for(uint32_t b = 0; b < cmodels[m].nb; b++) if(cmodels[m].b[b].nv > maxnv) maxnv = cmodels[m].b[b].nv;
    tv_cap = maxnv; tv = memalign(32, tv_cap * sizeof(cvert_t));

    snd_stream_init();
    snd_stream_hnd_t sh = snd_stream_alloc(snd_cb, 4096);
    snd_stream_volume(sh, 240);
    snd_stream_start(sh, SR, 0);

    /* grid: player (yellow Porsche) starts 4th */
    const float S0 = 40.0f;
    const int   mdl[NCARS]  = { 0, 1, 1, 0 };
    const float gs[NCARS]   = { S0, S0 + 13, S0 + 26, S0 + 26 };
    const float gd[NCARS]   = { 3.5f, -3.5f, 3.5f, -3.5f };
    const float skill[NCARS]= { 1.07f, 0.95f, 0.98f, 0.92f };
    for(int k = 0; k < NCARS; k++) {
        memset(&car[k], 0, sizeof(car_t));
        car[k].model = mdl[k]; car[k].s = gs[k]; car[k].d = gd[k]; car[k].gear = 1; car[k].rpm = 1100;
        car[k].ai = k != 0; car[k].skill = skill[k]; car[k].lane = (k & 1) ? -1.0f : 1.0f;
    }
    int autopilot = 1;                       /* attract mode until the player touches the pad */
    int cam = CAM_CHASE, frame = 0, race_frame = -180;
    float chase_yaw = 0; int first_cam = 1;
    uint32_t prev_btn = 0; uint64_t t0 = timer_ms_gettime64(); float fps = 0;
    int lap_frames = 0, best_lap = 0, finished = 0;
    char line[128];
    car_t *pc = &car[0];

    for(;;) {
        int render;
        /* ---------------- input (once per rendered frame) */
        uint32_t btn = 0; int jx = 0, lt = 0, rt = 0;
        maple_device_t *cdev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if(cdev) { cont_state_t *st = (cont_state_t *)maple_dev_status(cdev);
            if(st) { btn = st->buttons; jx = st->joyx; lt = st->ltrig; rt = st->rtrig; } }
#ifdef VIDEO
        btn = 0; jx = lt = rt = 0;
#endif
        uint32_t press = btn & ~prev_btn; prev_btn = btn;
        if(press & CONT_Y) cam = (cam + 1) % CAM_COUNT;
        if(autopilot && (press & (CONT_A | CONT_START) || abs(jx) > 40 || rt > 40)) autopilot = 0;
        if((btn & CONT_A) && (btn & CONT_B) && (btn & CONT_X) && (btn & CONT_Y) && (btn & CONT_START)) break;
      for(int sub = 0; sub < 2; sub++) {

        /* ---------------- race logic */
        if(race_frame < 0) {                  /* countdown: beeps at 3,2,1 and GO */
            if(race_frame == -180 || race_frame == -120 || race_frame == -60) { snd_beep_f = 880; snd_beep_n = SR/5; }
        }
        if(race_frame == 0) { snd_beep_f = 1760; snd_beep_n = SR/2; }
        for(int k = 0; k < NCARS; k++) {
            car_t *c = &car[k];
            float thr = 0, brk = 0, rc = 0, steer = 0; int use_r = 0;
            if(race_frame < 0) { thr = 0; brk = 1; }
            else if(c->ai || autopilot) { ai_drive(c, &thr, &brk, &rc); use_r = 1; }
            else {
                steer = -jx / 128.0f;
                thr = (btn & CONT_A) ? 1.0f : rt / 255.0f;
                brk = (btn & CONT_B) ? 1.0f : lt / 255.0f;
            }
            if(race_frame < 0) c->rpm = 1100 + (k == 0 ? 2500 * (0.5f + 0.5f * sinf(frame * 0.15f)) : 1500);
            else car_physics(c, steer, thr, brk, rc, use_r);
            car_pose(c);
        }
        if(race_frame >= 0) car_contacts();
        int lap_before = car[0].lap;
        if(race_frame >= 0) lap_frames++;
        if(car[0].lap != lap_before) { if(!best_lap || lap_frames < best_lap) best_lap = lap_frames; lap_frames = 0; }
        if(car[0].lap >= 2) finished = 1;
        race_frame++;

        /* ---------------- sound parameters */
        pc = &car[0];
        snd_rpm = pc->rpm; snd_thr = pc->thr; snd_slip = pc->slip * clampf(pc->v / 15.0f, 0, 1); snd_wind = clampf(pc->v / 80.0f, 0, 1);
        float nd = 1e9; int nk = 1;
        for(int k = 1; k < NCARS; k++) { float dx = car[k].pos[0]-pc->pos[0], dz = car[k].pos[2]-pc->pos[2], dd = dx*dx + dz*dz; if(dd < nd) { nd = dd; nk = k; } }
        snd_ai_rpm = car[nk].rpm; snd_ai_vol = 0.45f * clampf(1.0f - sqrtf(nd) / 70.0f, 0, 1);

#ifdef VIDEO
        /* scripted camera director for the attract video */
        { int t = race_frame;
          cam = t < 0 ? CAM_TV : t < 900 ? CAM_CHASE : t < 1500 ? CAM_BONNET : t < 1800 ? CAM_TV : t < 2400 ? CAM_CHASE : t < 2700 ? CAM_TV : CAM_CHASE; }
        if(frame >= START_FRAME) { char tl[160];
            sprintf(tl, "T %d %d %d %d %d %d %d %d %d %d %d\n", frame, race_frame, (int)snd_rpm, (int)(snd_thr*1000), (int)(snd_slip*1000),
                    (int)(snd_wind*1000), (int)(snd_crash_req*1000), (int)snd_ai_rpm, (int)(snd_ai_vol*1000), (int)(snd_beep_n > 0 ? snd_beep_f : 0), cam);
            dbgio_write_str(tl); }
#endif

        frame++;
      }

        /* ---------------- camera */
        float e[3], t[3];
        {
            car_t *c = &car[0]; float fy = c->yaw;
            if(first_cam) { chase_yaw = fy; first_cam = 0; }
            float dyaw = fy - chase_yaw; while(dyaw > F_PI) dyaw -= 2*F_PI; while(dyaw < -F_PI) dyaw += 2*F_PI;
            chase_yaw += dyaw * 0.22f;
            float fx = sinf(chase_yaw), fz = cosf(chase_yaw);
            switch(cam) {
            case CAM_CHASE:
                e[0] = c->pos[0] - fx*7.5f; e[1] = c->pos[1] + 2.7f; e[2] = c->pos[2] - fz*7.5f;
                t[0] = c->pos[0] + fx*6;    t[1] = c->pos[1] + 1.1f; t[2] = c->pos[2] + fz*6; break;
            case CAM_BONNET:
                e[0] = c->pos[0] + sinf(fy)*0.4f; e[1] = c->pos[1] + 1.15f; e[2] = c->pos[2] + cosf(fy)*0.4f;
                t[0] = c->pos[0] + sinf(fy)*25; t[1] = c->pos[1] + 0.9f; t[2] = c->pos[2] + cosf(fy)*25; break;
            case CAM_HELI:
                e[0] = c->pos[0] - fx*24; e[1] = c->pos[1] + 22; e[2] = c->pos[2] - fz*24;
                t[0] = c->pos[0] + fx*10; t[1] = c->pos[1];     t[2] = c->pos[2] + fz*10; break;
            default: {                       /* trackside TV camera: fixed every 120 m */
                float sc = floorf((c->s + 60.0f) / 120.0f) * 120.0f + 30.0f; uint32_t i = pidx(sc);
                float a = pyaw[i]; float side = pp[i].wl - 1.0f;
                e[0] = pp[i].x - cosf(a)*side; e[1] = pp[i].y + 4.5f; e[2] = pp[i].z + sinf(a)*side;
                t[0] = c->pos[0]; t[1] = c->pos[1] + 0.8f; t[2] = c->pos[2]; }
            }
        }
        look_at(e, t);
        {   /* underwater atmosphere */
            static float uw = 0, uw_set = -1; float target = (e[1] < 18.0f) ? 1.0f : 0.0f;
            uw += (target - uw) * 0.1f;
            if(fabsf(uw - uw_set) > 0.01f) {
                pvr_fog_table_color(1.0f, 0.62f - 0.52f*uw, 0.76f - 0.38f*uw, 0.92f - 0.44f*uw);
                pvr_fog_table_linear(230.0f - 190.0f*uw, DRAW_DIST - 340.0f*uw);
                pvr_set_bg_color(0.55f - 0.45f*uw, 0.72f - 0.34f*uw, 0.92f - 0.44f*uw);
                uw_set = uw;
            }
        }

        render = frame > START_FRAME;
        if(!render) continue;
        /* ---------------- render */
        stat_tris = 0; vb_used = 0;
        pvr_wait_ready(); pvr_scene_begin();
        static uint32_t vis[4096]; static float visd[4096]; uint32_t nvis = 0;
        for(uint32_t c = 0; c < ncells && nvis < 4096; c++) {
            if(!cell_visible(&cells[c])) continue;
            uint32_t k = nvis++;
            while(k > 0 && visd[k-1] > cell_dist) { vis[k] = vis[k-1]; visd[k] = visd[k-1]; k--; }
            vis[k] = c; visd[k] = cell_dist;
        }
        stat_cells = nvis;
        pvr_list_begin(PVR_LIST_OP_POLY);
        for(int k = 0; k < NCARS; k++) draw_car(&car[k], 0);          /* cars first: never dropped */
        for(uint32_t k = 0; k < nvis; k++) { cell_t *cl = &cells[vis[k]];
            for(uint32_t b = cl->first; b < cl->first + cl->count; b++)
                if(!btr[b]) draw_mesh(verts + batches[b].voff, batches[b].nv, idx + batches[b].ioff, batches[b].ni, &bhdr[b], viewm); }
        pvr_list_finish();
        pvr_list_begin(PVR_LIST_TR_POLY);
        for(int k = 0; k < NCARS; k++) draw_car(&car[k], 1);
        for(uint32_t k = 0; k < nvis; k++) { cell_t *cl = &cells[vis[k]];
            for(uint32_t b = cl->first; b < cl->first + cl->count; b++)
                if(btr[b]) draw_mesh(verts + batches[b].voff, batches[b].nv, idx + batches[b].ioff, batches[b].ni, &bhdr[b], viewm); }

        /* HUD */
        int rank = 1; float me = pc->lap * (float)npath + pc->s;
        for(int k = 1; k < NCARS; k++) if(car[k].lap * (float)npath + car[k].s > me) rank++;
        int lt_s = lap_frames / 60, lt_c = (lap_frames % 60) * 100 / 60;
        sprintf(line, "LAP %d/2   TIME %d'%02d\"%02d", pc->lap + 1 > 2 ? 2 : pc->lap + 1, lt_s / 60, lt_s % 60, lt_c);
        draw_text(24, 20, 1.5f, 0xFFFFFFFF, line);
        sprintf(line, "POS %d/4", rank); draw_text(500, 20, 2.0f, 0xFFFFE040, line);
        sprintf(line, "%3d", (int)(pc->v * 3.6f)); draw_text(470, 400, 4.0f, 0xFFFFFFFF, line);
        sprintf(line, "KM/H  GEAR %d", pc->gear); draw_text(470, 452, 1.0f, 0xFFFFD040, line);
        { /* rev bar */
          int bars = (int)((pc->rpm - 1000) / 7500.0f * 20); char rb[24]; for(int k = 0; k < 20; k++) rb[k] = k < bars ? '|' : '.'; rb[20] = 0;
          draw_text(470, 385, 1.0f, pc->rpm > 7600 ? 0xFFFF4040 : 0xFF80FF80, rb); }
        sprintf(line, "FPS %2.0f POLY %5lu  %s", fps, (unsigned long)stat_tris, cam_names[cam]);
        draw_text(24, 456, 1.0f, 0xC0FFFFFF, line);
        if(race_frame < 0) { const char *cd[3] = { "3", "2", "1" }; draw_text(296, 180, 6.0f, 0xFFFFE040, cd[(race_frame + 180) / 60]); }
        else if(race_frame < 60) draw_text(250, 180, 6.0f, 0xFF40FF40, "GO!");
        if(autopilot && (frame / 40) % 2 && race_frame > 120 && !VIDEO_ON) draw_text(256, 340, 1.5f, 0xFFFFFFFF, "PRESS START");
        if(finished) draw_text(230, 200, 3.0f, 0xFFFFE040, "FINISH");
        draw_text(24, 40, 1.0f, 0xFFB0B0B0, "SCUD RACE  DREAMCAST TECH DEMO 1998");
#ifdef VIDEO
        pvr_prim(&shadow_hdr, sizeof(shadow_hdr));
        for(int bit = 0; bit < 17; bit++) {
            int on = bit == 16 ? 1 : (frame >> bit) & 1; float x0 = 8 + bit * 16, y0 = 474;
            uint32_t col = on ? 0xFFFFFFFF : 0xFF000000; pvr_vertex_t *v;
            v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX; v->x = x0;      v->y = y0+6; v->z = 11; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
            v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX; v->x = x0;      v->y = y0;   v->z = 11; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
            v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX; v->x = x0+14;   v->y = y0+6; v->z = 11; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
            v = pvr_dr_target(); v->flags = PVR_CMD_VERTEX_EOL; v->x = x0+14; v->y = y0; v->z = 11; v->argb = col; v->oargb = 0; pvr_dr_commit(v);
        }
#endif
        pvr_list_finish();
        pvr_scene_finish();
        snd_stream_poll(sh);

        { static int rf = 0; if(++rf % 30 == 0) { uint64_t t1 = timer_ms_gettime64(); fps = 30000.0f / (float)(t1 - t0); t0 = t1; } }
        if(END_FRAME && frame >= END_FRAME) { dbgio_write_str("END\n"); break; }
    }
    snd_stream_destroy(sh); snd_stream_shutdown();
    return 0;
}
