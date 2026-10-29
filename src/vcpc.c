// vcpc.c - the vCPC: CyborgOS with no CPC under it (a "Headless Horseman" node). The kernel
// (KERNEL.CYB - the same file the CPC, the Next and the MSX run: the scheduler, the command line,
// TOP) and the programs (.CYB, .COM - the same files) run on a Z80 (superzazu/z80, z80.c) as they
// do there; what the CPC's C0 (CYBORGOS.BIN) does in Z80 and firmware is done here in C: the 4MB
// memory (main RAM and 64 banks of 64K), the discs (the host's folders, as UniDOS's devices and
// drive letters), the consoles (a text page in each slot's bank), the network (the host's TCP
// sockets, behind the M4's ops) and the services (ports, claims, the store, the tables).
//
// The contract with the kernel is BDOS 132 (KERNEL.bas's header lists the ops); C0 here answers
// them as the MSX's and the Next's C0 do - the kernel runs a slot through RUN (op 12), C0 answers
// that slot's BDOS calls that are its own and hands the kernel the rest. The host it reports is 4
// (0 the CPC, 1 the Next, 3 the MSX), so the kernel's CPC paths (KRun) stay off.
//
// A slot's bank, as the stubs leave it on the CPC: #0000 jp WBOOT, #0005 jp the BDOS (#EF00, the
// top of the TPA), the BIOS jump table, KEY at #F681, the console's columns / rows / mode at #F687,
// the command line at #F68F, the program's name at #F6D8. Here the BDOS, the BIOS and the start-up
// are traps (the Z80 stops when it reaches #EF00-#EF47 and the call is answered in C), and all of a
// slot's state is in its bank, as on the CPC, so a bank is the whole slot (DUMP / REVIVE): its
// registers on its own stack with SP at #FFFE while it isn't running, the BDOS state at #EF48, its
// text page at #F700 and the cursor at #FF00.
//
//   make, or: cc -O2 -Isrc/z80 -o build/vcpc src/vcpc.c src/z80/z80.c   (Linux, the Pi, macOS)
//   src\build.bat                                         (Windows: Visual C++'s cl, or MinGW's gcc)
//   vcpc --sys DIR [--sys DIR2] --data DIR [--slots n] [--tty | --script "cmd;cmd" ...]
//
// (c) 2026 Cyborg Unicorn Pty Ltd - MIT License (src/z80: superzazu/z80, MIT - its LICENSE)

#ifndef _WIN32
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "compat.h"
#include "z80.h"

// ---- the machine
#define MAXS     20                     // the kernel's most slots
#define NBANKS   64                     // 64 banks of 64K: 4MB (with main RAM: 257 pages of 16K)
#define KB       0xEE00                 // the kernel's block (its #EE00)
#define BDOS     0xEF00
#define BIOS     0xEF03
#define BIOSN    17
#define START    0xEF40
#define TRAP_LO  0xEF00
#define TRAP_HI  0xEF48
#define VS       0xEF48                 // the slot's BDOS state: +0 the call waiting (1 the kernel's
                                        // answer, 2 a key: again), +1 a line being typed, +2 its DE,
                                        // +4 empty polls, +5 the DMA (2 bytes)
#define DPB      0xEF60
#define KEY      0xF681
#define CCOLS    0xF687
#define CROWS    0xF688
#define CMODE    0xF689
#define NMLEN    0xF6D8
#define PAGE     0xF700                 // the text page (2048 at most: up to #FEFF)
#define CURS     0xFF30                 // +0 x, +1 y, +2 ESC state, +3 ESC Y's row (#FF00-#FF27: the store's and scrollback's on the CPC)
#define SAVSP    0xFFFE                 // the registers' frame, while it isn't running
#define SLICE    70000                  // a slice: 1/50 s of a 3.5 MHz Z80 (run as fast as the host goes)
#define HOST     4                      // (0 the CPC, 1 the Next, 3 the MSX, 4 a vCPC)
#define STAMP    0x7643                 // the build's stamp (C0's SLOT, op 37): a vCPC's bank layout - REVIVE takes a dump only from the same

typedef struct {
	uint8_t *m;                         // its 64K
	z80 z;
	int used;                           // a slot's bank (or the kernel's), a store's, a RAM disc's
	int chars;                          // characters it printed (TOP)
} Bank;

static Bank banks[NBANKS];
static uint8_t mainram[65536];          // (C0's own: nothing in it here)
static int ns = 10;                     // slots
static int bd = 'A';                    // the boot drive
static int st[MAXS + 1], dv[MAXS + 1];
static int front = 1, hdr = 'A', hfile = 0, quit_now = 0, idle = 0, worked = 0;
static int keys[256], nkeys = 0, hots[32], nhots = 0;
static long long t0;
static int verbose = 0;

static uint8_t rb_cb(void *u, uint16_t a) { return ((uint8_t *)u)[a]; }
static void wb_cb(void *u, uint16_t a, uint8_t v) { ((uint8_t *)u)[a] = v; }
static uint8_t in_cb(z80 *z, uint8_t p) { (void)z; (void)p; return 0xFF; }
static void out_cb(z80 *z, uint8_t p, uint8_t v) { (void)z; (void)p; (void)v; }

static uint8_t *M(int t) { return banks[t].m; }
static int W(uint8_t *m, int a) { return m[a & 0xFFFF] | (m[(a + 1) & 0xFFFF] << 8); }
static void SW(uint8_t *m, int a, int v) { m[a & 0xFFFF] = v & 255; m[(a + 1) & 0xFFFF] = (v >> 8) & 255; }

static long ms_now(void) { return (long)(clock_ms() - t0); }
static unsigned long ticks_all(void) { return (unsigned long)(ms_now() * 3 / 10); }     // 300 a second
static int ticks(void) { return (int)(ticks_all() & 0xFFFF); }

// ---- the console: a slot's text page in its own bank (#F700, columns x rows from #F687), the
// codes Hyp.Con takes: CR, LF, BS (back), TAB, and VT52's ESC E H Y A B C D K J L M I, Amstrad's
// ESC l o d. Every byte also goes to the terminal when it is the slot in front (con_out)
static uint8_t top_cells[80 * 25];      // the screen while TOP shows (SAY)
static int dirty = 1;
static char shown[64][161];             // the terminal's rows as last drawn (tty_draw)
static int shown_rows = 0, shown_cols = 0, shown_valid = 0;

static void con_clear(uint8_t *m, int from, int to) { if (to > from) { memset(m + PAGE + from, ' ', to - from); } }
static void con_scroll_up(uint8_t *m, int W_, int H) { memmove(m + PAGE, m + PAGE + W_, W_ * (H - 1)); con_clear(m, W_ * (H - 1), W_ * H); }
static void con_scroll_down(uint8_t *m, int W_, int H, int r) { memmove(m + PAGE + (r + 1) * W_, m + PAGE + r * W_, W_ * (H - 1 - r)); con_clear(m, r * W_, (r + 1) * W_); }
static void con_delete_row(uint8_t *m, int W_, int H, int r) { memmove(m + PAGE + r * W_, m + PAGE + (r + 1) * W_, W_ * (H - 1 - r)); con_clear(m, (H - 1) * W_, W_ * H); }

static void con_size(int t)
{
	uint8_t *m = M(t);
	int W_ = m[CCOLS] ? m[CCOLS] : 80, H = m[CROWS] ? m[CROWS] : 24;
	if (W_ * H > 2048) { H = 2048 / W_; }
	con_clear(m, 0, W_ * H);
	m[CURS] = 0; m[CURS + 1] = 0; m[CURS + 2] = 0;
}

static void con_put(int t, int c)
{
	uint8_t *m = M(t);
	int W_ = m[CCOLS] ? m[CCOLS] : 80, H = m[CROWS] ? m[CROWS] : 24;
	int x = m[CURS], y = m[CURS + 1], e = m[CURS + 2];
	if (W_ * H > 2048) { H = 2048 / W_; }
	if (y >= H) { y = H - 1; }
	if (e == 1)
	{
		e = 0;
		switch (c)
		{
			case 'E': con_clear(m, 0, W_ * H); x = 0; y = 0; break;
			case 'H': x = 0; y = 0; break;
			case 'Y': e = 2; break;
			case 'A': if (y > 0) { y--; } break;
			case 'B': if (y < H - 1) { y++; } break;
			case 'C': if (x < W_ - 1) { x++; } break;
			case 'D': if (x > 0) { x--; } break;
			case 'K': con_clear(m, y * W_ + x, (y + 1) * W_); break;
			case 'J': con_clear(m, y * W_ + x, W_ * H); break;
			case 'L': con_scroll_down(m, W_, H, y); x = 0; break;
			case 'M': con_delete_row(m, W_, H, y); x = 0; break;
			case 'I': if (y > 0) { y--; } else { con_scroll_down(m, W_, H, 0); } break;
			case 'l': con_clear(m, y * W_, (y + 1) * W_); break;
			case 'o': con_clear(m, y * W_, y * W_ + x + 1); break;
			case 'd': con_clear(m, 0, y * W_ + x + 1); break;
			default: break;
		}
	}
	else if (e == 2) { m[CURS + 3] = (uint8_t)(c - 32); e = 3; }
	else if (e == 3)
	{
		e = 0;
		y = m[CURS + 3]; if (y >= H) { y = H - 1; }
		x = c - 32; if (x < 0) { x = 0; } if (x >= W_) { x = W_ - 1; }
	}
	else if (c == 27) { e = 1; }
	else if (c == 13) { x = 0; }
	else if (c == 10) { if (y < H - 1) { y++; } else { con_scroll_up(m, W_, H); } }
	else if (c == 8) { if (x > 0) { x--; } }
	else if (c == 9) { x = (x + 8) & ~7; if (x > W_ - 1) { x = W_ - 1; } }
	else if (c >= 32)
	{
		if (x >= W_) { x = 0; if (y < H - 1) { y++; } else { con_scroll_up(m, W_, H); } }
		m[PAGE + y * W_ + x] = (uint8_t)c;
		x++;
	}
	m[CURS] = (uint8_t)x; m[CURS + 1] = (uint8_t)y; m[CURS + 2] = (uint8_t)e;
	if (t == front) { dirty = 1; }
}
static void con_write(int t, const uint8_t *s, int n) { for (int i = 0; i < n; i++) { con_put(t, s[i]); } }

// the page as text: rows, right spaces trimmed (into buf)
static void con_text(int t, char *buf, int max)
{
	int n = 0, W_, H, r, c, e;
	const uint8_t *cells;
	if (t == 0) { W_ = 80; H = 25; cells = top_cells; }
	else
	{
		uint8_t *m = M(t);
		W_ = m[CCOLS] ? m[CCOLS] : 80; H = m[CROWS] ? m[CROWS] : 24;
		if (W_ * H > 2048) { H = 2048 / W_; }
		cells = m + PAGE;
	}
	for (r = 0; r < H; r++)
	{
		for (e = W_; e > 0 && cells[r * W_ + e - 1] == ' '; e--) { }
		for (c = 0; c < e && n < max - 2; c++) { buf[n++] = (char)(cells[r * W_ + c] >= 32 && cells[r * W_ + c] < 127 ? cells[r * W_ + c] : '.'); }
		if (n < max - 1) { buf[n++] = '\n'; }
	}
	while (n > 0 && buf[n - 1] == '\n') { n--; }
	buf[n] = 0;
}

// ---- the slots' frames: a slot that isn't running has its registers on its own stack, SP at
// #FFFE (as a CPC slot has after an interrupt or a far call), so its bank is all of it
static void push16(z80 *z, uint8_t *m, int v) { z->sp = (uint16_t)(z->sp - 2); SW(m, z->sp, v); }
static int pop16(z80 *z, uint8_t *m) { int v = W(m, z->sp); z->sp = (uint16_t)(z->sp + 2); return v; }

static void frame_save(int t)
{
	z80 *z = &banks[t].z;
	uint8_t *m = M(t);
	push16(z, m, z->pc);
	push16(z, m, (z->a << 8) | z80_get_f(z));
	push16(z, m, (z->b << 8) | z->c);
	push16(z, m, (z->d << 8) | z->e);
	push16(z, m, (z->h << 8) | z->l);
	push16(z, m, z->ix);
	push16(z, m, z->iy);
	push16(z, m, (z->a_ << 8) | z->f_);
	push16(z, m, (z->b_ << 8) | z->c_);
	push16(z, m, (z->d_ << 8) | z->e_);
	push16(z, m, (z->h_ << 8) | z->l_);
	push16(z, m, (z->i << 8) | z->r);
	push16(z, m, (z->iff1 ? 1 : 0) | (z->iff2 ? 2 : 0) | (z->halted ? 4 : 0) | (z->interrupt_mode << 4));
	SW(m, SAVSP, z->sp);
}
static void frame_load(int t)
{
	z80 *z = &banks[t].z;
	uint8_t *m = M(t);
	int v;
	z->sp = (uint16_t)W(m, SAVSP);
	v = pop16(z, m); z->iff1 = v & 1; z->iff2 = (v >> 1) & 1; z->halted = (v >> 2) & 1; z->interrupt_mode = (v >> 4) & 3;
	v = pop16(z, m); z->i = v >> 8; z->r = v & 255;
	v = pop16(z, m); z->h_ = v >> 8; z->l_ = v & 255;
	v = pop16(z, m); z->d_ = v >> 8; z->e_ = v & 255;
	v = pop16(z, m); z->b_ = v >> 8; z->c_ = v & 255;
	v = pop16(z, m); z->a_ = v >> 8; z->f_ = v & 255;
	z->iy = (uint16_t)pop16(z, m);
	z->ix = (uint16_t)pop16(z, m);
	v = pop16(z, m); z->h = v >> 8; z->l = v & 255;
	v = pop16(z, m); z->d = v >> 8; z->e = v & 255;
	v = pop16(z, m); z->b = v >> 8; z->c = v & 255;
	v = pop16(z, m); z->a = v >> 8; z80_set_f(z, (uint8_t)(v & 255));
	z->pc = (uint16_t)pop16(z, m);
}

// ---- the discs: devices (named host folders - SYS, CyborgOS's set, read only, may be several
// folders looked through in turn; DATA, this node's own) and the drive letters, each a path on a
// device ("DATA:", "DATA:GAMES/") - UniDOS's model, so CD, CV, DIR, MKDIR, COPY ... run unchanged.
// Names are 8.3 in capitals on the Z80's side; on the host they are found whatever their case and
// made in capitals. A file with an AMSDOS header (a .CYB) is read as CP/M sees it: the header left out
#define NDEV 8
#define NDIR 4
typedef struct { char name[12]; char dirs[NDIR][1024]; int ndirs; int ro; } Device;
static Device devs[NDEV];
static int ndevs = 0;
static char paths[27][256];             // A-Z: the letter's path ("" none)
static int del_folder = 0;

typedef struct { char n11[12]; char host[1280]; long size; int dir; time_t mtime; int ro; } Entry;   // ro: not in the device's first (writable) folder
typedef struct { FILE *f; long base, size; int ro, slot, used; char host[1280]; } Handle;
#define NH 64
static Handle hs[NH];
typedef struct { Entry *l; int n, i; } Search;
static Search srch[MAXS + 1];

static int lidx(int letter) { return letter == 'Z' ? 26 : (letter >= 'A' && letter <= 'P' ? letter - 'A' : (letter >= 'Q' && letter <= 'Y' ? letter - 'A' : -1)); }
static const char *pathOf(int letter) { int i = lidx(letter); return (i >= 0 && paths[i][0]) ? paths[i] : NULL; }

static int name83(const char *host, char *n11)
{
	char b[16] = "", e[8] = "";
	const char *dot = strrchr(host, '.');
	int lb = dot ? (int)(dot - host) : (int)strlen(host), le = dot ? (int)strlen(dot + 1) : 0, i;
	if (lb < 1 || lb > 8 || le > 3 || (dot && dot == host)) { return 0; }
	for (i = 0; i < lb; i++) { int c = toupper((unsigned char)host[i]); if (!(isalnum(c) || strchr("_-$!#%&'()@^{}~", c))) { return 0; } b[i] = (char)c; }
	for (i = 0; i < le; i++) { int c = toupper((unsigned char)dot[1 + i]); if (!(isalnum(c) || strchr("_-$!#%&'()@^{}~", c))) { return 0; } e[i] = (char)c; }
	snprintf(n11, 12, "%-8s%-3s", b, e);
	return 1;
}
static void nameDot(const char *n11, char *out)
{
	int i, n = 0;
	for (i = 0; i < 8 && n11[i] != ' '; i++) { out[n++] = n11[i]; }
	if (n11[8] != ' ') { out[n++] = '.'; for (i = 8; i < 11 && n11[i] != ' '; i++) { out[n++] = n11[i]; } }
	out[n] = 0;
}
static int match11(const char *pat, const char *n) { for (int i = 0; i < 11; i++) { if (pat[i] != '?' && pat[i] != n[i]) { return 0; } } return 1; }
static long amsdos(const uint8_t *b, int n)
{
	int s = 0, i;
	if (n < 128) { return -1; }
	for (i = 0; i < 67; i++) { s += b[i]; }
	if ((s & 0xFFFF) != (b[67] | (b[68] << 8)) || s == 0) { return -1; }
	return b[64] | (b[65] << 8) | (b[66] << 16);
}

// a path "DEV:A/B/" -> its device and the parts after it
static Device *parse(const char *p, char rel[16][16], int *nrel)
{
	char d[16], via[600]; int i = 0, n = 0, k = 0;
	const char *c = strchr(p, ':');
	*nrel = 0;
	if (c && c - p == 1 && isalpha((unsigned char)p[0]) && pathOf(toupper((unsigned char)p[0])) && !(strlen(pathOf(toupper((unsigned char)p[0]))) == 2 && pathOf(toupper((unsigned char)p[0]))[0] == toupper((unsigned char)p[0])))
	{
		// a drive letter as the start ("A:HELP/", UniDOS's): that letter's path, then the rest
		snprintf(via, sizeof via, "%s%s", pathOf(toupper((unsigned char)p[0])), c + 1);
		return parse(via, rel, nrel);
	}
	if (!c || c - p > 11) { return NULL; }
	for (i = 0; p + i < c; i++) { d[i] = (char)toupper((unsigned char)p[i]); } d[i] = 0;
	for (i = 0; i < ndevs && strcmp(devs[i].name, d) != 0; i++) { }
	if (i >= ndevs) { return NULL; }
	for (c++; ; c++)
	{
		if (*c == '/' || *c == 0) { if (k > 0 && n < 16) { rel[n][k] = 0; if (strcmp(rel[n], ".") != 0) { n++; } } k = 0; if (*c == 0) { break; } }
		else if (k < 15 && n < 16) { rel[n][k++] = (char)toupper((unsigned char)*c); }
	}
	*nrel = n;
	return &devs[i];
}
// the host folders of a path (each of the device's that has it), found whatever their case
static int dirsOf(Device *d, char rel[16][16], int nrel, int first_only, char out[NDIR][1280])
{
	int k, i, n = 0;
	for (k = 0; k < d->ndirs && (!first_only || k == 0); k++)
	{
		char cur[1280]; int ok = 1;
		snprintf(cur, sizeof cur, "%s", d->dirs[k]);
		for (i = 0; i < nrel && ok; i++)
		{
			DIR *dp; struct dirent *de; char hit[256] = ""; struct stat sb;
			if (strcmp(rel[i], "..") == 0) { char *s = strrchr(cur, '/'); if (s && s != cur) { *s = 0; } continue; }
			if (!(dp = opendir(cur))) { ok = 0; break; }
			while ((de = readdir(dp))) { if (strcasecmp(de->d_name, rel[i]) == 0) { snprintf(hit, sizeof hit, "%s", de->d_name); break; } }
			closedir(dp);
			if (!hit[0]) { ok = 0; break; }
			size_t l = strlen(cur); snprintf(cur + l, sizeof cur - l, "/%s", hit);
			if (stat(cur, &sb) != 0 || !S_ISDIR(sb.st_mode)) { ok = 0; }
		}
		if (ok) { snprintf(out[n++], 1280, "%s", cur); }
	}
	return n;
}
static int cmp_entry(const void *a, const void *b) { return strcmp(((const Entry *)a)->n11, ((const Entry *)b)->n11); }
// the files of a path's folder (8.3 names only; a name in the first device folder hides the others')
static Entry *list(const char *p, int withDirs, int *count)
{
	char rel[16][16], ds[NDIR][1280]; int nrel, nd, k, n = 0, cap = 64, j;
	Device *d = parse(p, rel, &nrel);
	Entry *l;
	*count = -1;
	if (!d) { return NULL; }
	nd = dirsOf(d, rel, nrel, 0, ds);
	l = calloc(cap, sizeof(Entry));
	for (k = 0; k < nd; k++)
	{
		DIR *dp = opendir(ds[k]); struct dirent *de;
		if (!dp) { continue; }
		while ((de = readdir(dp)))
		{
			char n11[12]; struct stat sb; char h[1280];
			if (!name83(de->d_name, n11)) { continue; }
			snprintf(h, sizeof h, "%s/%s", ds[k], de->d_name);
			if (stat(h, &sb) != 0) { continue; }
			if (S_ISDIR(sb.st_mode) && !withDirs) { continue; }
			for (j = 0; j < n && strcmp(l[j].n11, n11) != 0; j++) { }
			if (j < n) { continue; }
			if (n >= cap) { cap *= 2; l = realloc(l, cap * sizeof(Entry)); }
			memcpy(l[n].n11, n11, 12); snprintf(l[n].host, sizeof l[n].host, "%s", h);
			l[n].size = sb.st_size; l[n].dir = S_ISDIR(sb.st_mode); l[n].mtime = sb.st_mtime;
			l[n].ro = d->ro || strncmp(ds[k], d->dirs[0], strlen(d->dirs[0])) != 0;
			n++;
		}
		closedir(dp);
	}
	qsort(l, n, sizeof(Entry), cmp_entry);
	*count = n;
	return l;
}
static int find(const char *p, const char *n11, Entry *out)
{
	int n, i, r = 0; Entry *l = list(p, 0, &n);
	for (i = 0; i < n; i++) { if (strcmp(l[i].n11, n11) == 0) { if (out) { *out = l[i]; } r = 1; break; } }
	free(l);
	return r;
}
static int devRo(const char *p) { char rel[16][16]; int n; Device *d = parse(p, rel, &n); return !d || d->ro; }
// where a new file goes: the path's folder in the device's first (writable) folder
static int newPath(const char *p, const char *n11, char *out)
{
	char rel[16][16], ds[NDIR][1280], nm[16]; int nrel;
	Device *d = parse(p, rel, &nrel);
	if (!d || d->ro || dirsOf(d, rel, nrel, 1, ds) < 1) { return 0; }
	nameDot(n11, nm);
	snprintf(out, 1280, "%s/%s", ds[0], nm);
	return 1;
}

static int h_open(const char *host, int ro, int slot)
{
	int i; uint8_t b[128]; long len, base = 0, h; struct stat sb;
	for (i = 1; i < NH && hs[i].used; i++) { }
	if (i >= NH) { return 0; }
	hs[i].f = fopen(host, ro ? "rb" : "r+b");
	if (!hs[i].f && !ro) { hs[i].f = fopen(host, "rb"); ro = 1; }
	if (!hs[i].f) { return 0; }
	fstat(fileno(hs[i].f), &sb); len = sb.st_size;
	if (fread(b, 1, 128, hs[i].f) == 128 && (h = amsdos(b, 128)) >= 0) { base = 128; len = sb.st_size - 128; if (h > 0 && h < len) { len = h; } }
	hs[i].base = base; hs[i].size = len; hs[i].ro = ro; hs[i].slot = slot; hs[i].used = 1;
	snprintf(hs[i].host, sizeof hs[i].host, "%s", host);
	return i;
}
static void h_close(int i) { if (i > 0 && i < NH && hs[i].used) { fclose(hs[i].f); hs[i].used = 0; } }
static int h_read(int i, long rec, uint8_t *buf)
{
	long off = rec * 128; size_t n;
	if (!hs[i].used || off >= hs[i].size) { return 0; }
	memset(buf, 0x1A, 128);
	n = (size_t)((hs[i].size - off) < 128 ? (hs[i].size - off) : 128);
	fseek(hs[i].f, hs[i].base + off, SEEK_SET);
	if (fread(buf, 1, n, hs[i].f) != n) { return 0; }
	return 1;
}
static int h_write(int i, long rec, const uint8_t *buf)
{
	long off = rec * 128;
	if (!hs[i].used || hs[i].ro) { return 0; }
	if (off > hs[i].size) { uint8_t z[128] = { 0 }; fseek(hs[i].f, hs[i].base + hs[i].size, SEEK_SET); for (long k = hs[i].size; k < off; k += 128) { fwrite(z, 1, (size_t)((off - k) < 128 ? (off - k) : 128), hs[i].f); } }
	fseek(hs[i].f, hs[i].base + off, SEEK_SET);
	if (fwrite(buf, 1, 128, hs[i].f) != 128) { return 0; }
	fflush(hs[i].f);
	if (off + 128 > hs[i].size) { hs[i].size = off + 128; }
	return 1;
}

// ---- BDOS 131's services that are C0's (Hyp.Svc, the CPC's service page)
#define CL_N 16
#define PT_N 8
#define NT_N 8
typedef struct { int slot, unit; char name[9]; } Claim;
typedef struct { int used, slot, state, caller; char name[9]; uint8_t msg[128]; int len; } Port;
typedef struct { int used, slot, fd, state; uint8_t ip[4]; uint8_t *rx; int nrx, caprx; } Sock;
static Claim claims[CL_N]; static int nclaims = 0;
static Port ports[PT_N];
static Sock socks[NT_N];
static int net_on = 1, look_state = 255; static uint8_t look_ip[4];
static int store_bank[MAXS + 1];        // a slot's store: a free bank (0: none)

static void name8(uint8_t *m, int a, char *out)
{
	int i, n = 8;
	for (i = 0; i < 8; i++) { out[i] = (char)toupper(m[(a + i) & 0xFFFF]); }
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == 0)) { n--; }
	out[n] = 0;
}
static int inUse(int t) { return t >= 1 && t <= ns && st[t] >= 2; }
static int freeBank(void) { for (int b = ns + 1; b < NBANKS; b++) { if (!banks[b].used) { return b; } } return 0; }

static int storeSize(int t, int k)
{
	if (store_bank[t]) { banks[store_bank[t]].used = 0; store_bank[t] = 0; }
	if (!k) { return 0; }
	if (k > 64) { return 255; }
	int b = freeBank();
	if (!b) { return 255; }
	banks[b].used = 3; memset(banks[b].m, 0, 65536); store_bank[t] = b;
	return 0;
}

static void sock_close(int i)
{
	if (!socks[i].used) { return; }
	if (socks[i].fd >= 0) { sock_close_fd(socks[i].fd); }
	free(socks[i].rx);
	memset(&socks[i], 0, sizeof(Sock)); socks[i].fd = -1;
}
// the sockets' news: connected, data, closed (called each pass of the main loop)
static void net_poll(void)
{
	for (int i = 0; i < NT_N; i++)
	{
		Sock *s = &socks[i];
		if (!s->used || s->fd < 0) { continue; }
		if (s->state == 1)
		{
			fd_set w; struct timeval tv = { 0, 0 };
			FD_ZERO(&w); FD_SET(s->fd, &w);
			if (select(s->fd + 1, NULL, &w, NULL, &tv) > 0)
			{
				int err = 0; socklen_t l = sizeof err;
				getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &l);
				s->state = err ? 7 : 0;
				idle = 0;
			}
			continue;
		}
		if (s->state == 0 || s->state == 2)
		{
			for (;;)
			{
				uint8_t b[2048]; int n = (int)recv(s->fd, (char *)b, sizeof b, MSG_DONTWAIT);
				if (n > 0)
				{
					if (s->nrx + n > s->caprx) { s->caprx = (int)(s->nrx + n) * 2; s->rx = realloc(s->rx, s->caprx); }
					memcpy(s->rx + s->nrx, b, n); s->nrx += (int)n; idle = 0;
					continue;
				}
				if (n == 0) { s->state = 3; idle = 0; }
				else if (!SOCK_WOULDBLOCK(sock_errno())) { s->state = 3; idle = 0; }
				break;
			}
		}
	}
}

static int net_op(int t, int p, int op)
{
	uint8_t *m = M(t); int i, k;
	if (!net_on) { return 255; }
	if (op == 21)
	{
		char h[64]; int a = W(m, p + 2), len = m[(p + 4) & 0xFFFF]; struct addrinfo hints, *res = NULL;
		if (len > 63) { len = 63; }
		for (i = 0; i < len; i++) { h[i] = (char)m[(a + i) & 0xFFFF]; } h[len] = 0;
		memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET;
		if (getaddrinfo(h, NULL, &hints, &res) == 0 && res)
		{
			memcpy(look_ip, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4); look_state = 0; freeaddrinfo(res);
		}
		else { look_state = 8; }
		return 1;
	}
	if (op == 23)
	{
		for (i = 0; i < NT_N && socks[i].used; i++) { }
		if (i >= NT_N) { return 255; }
		memset(&socks[i], 0, sizeof(Sock)); socks[i].used = 1; socks[i].slot = t; socks[i].fd = -1;
		return i;
	}
	k = m[(p + 1) & 0xFFFF];
	if (op == 22)
	{
		uint8_t blk[16] = { 0 }; int d = W(m, p + 2);
		if (k == 255) { blk[0] = (uint8_t)look_state; memcpy(blk + 4, look_ip, 4); }
		else
		{
			if (k >= NT_N || !socks[k].used || socks[k].slot != t) { return 255; }
			blk[0] = (uint8_t)socks[k].state; blk[2] = socks[k].nrx & 255; blk[3] = (socks[k].nrx > 65535 ? 255 : (socks[k].nrx >> 8) & 255);
			memcpy(blk + 4, socks[k].ip, 4);
		}
		for (i = 0; i < 16; i++) { m[(d + i) & 0xFFFF] = blk[i]; }
		return blk[0];
	}
	if (k >= NT_N || !socks[k].used || socks[k].slot != t) { return 255; }
	Sock *s = &socks[k];
	if (op == 24)
	{
		struct sockaddr_in a; int one = 1;
		for (i = 0; i < 4; i++) { s->ip[i] = m[(p + 2 + i) & 0xFFFF]; }
		s->fd = (int)socket(AF_INET, SOCK_STREAM, 0);
		if (s->fd < 0) { s->state = 7; return 0; }
		setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
		sock_nonblock(s->fd);
		memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((uint16_t)W(m, p + 6)); memcpy(&a.sin_addr, s->ip, 4);
		s->state = 1;
		if (connect(s->fd, (struct sockaddr *)&a, sizeof a) == 0) { s->state = 0; }
		else if (!SOCK_INPROGRESS(sock_errno())) { s->state = 7; }
		return 0;
	}
	if (op == 25)
	{
		int src = W(m, p + 2), n = W(m, p + 4); uint8_t b[256];
		if (n > 240) { n = 240; }
		if (s->fd < 0 || s->state == 3 || s->state > 5) { return 255; }
		for (i = 0; i < n; i++) { b[i] = m[(src + i) & 0xFFFF]; }
		for (int sent = 0; sent < n; )
		{
			int r = (int)send(s->fd, (const char *)b + sent, n - sent, MSG_NOSIGNAL);
			if (r > 0) { sent += (int)r; continue; }
			if (r < 0 && SOCK_WOULDBLOCK(sock_errno())) { fd_set w; FD_ZERO(&w); FD_SET(s->fd, &w); select(s->fd + 1, NULL, &w, NULL, NULL); continue; }
			s->state = 3; return 255;
		}
		return 0;
	}
	if (op == 26)
	{
		int to = W(m, p + 2), max = W(m, p + 4), got;
		if (max > 2048) { max = 2048; }
		got = s->nrx < max ? s->nrx : max;
		for (i = 0; i < got; i++) { m[(to + i) & 0xFFFF] = s->rx[i]; }
		memmove(s->rx, s->rx + got, s->nrx - got); s->nrx -= got;
		SW(m, p + 6, got);
		return 0;
	}
	if (op == 27) { sock_close(k); return 0; }
	return 255;
}

static int port_find(const char *nm) { for (int i = 0; i < PT_N; i++) { if (ports[i].used && strcmp(ports[i].name, nm) == 0) { return i; } } return -1; }
static int port_op(int t, int p, int op)
{
	uint8_t *m = M(t); char nm[9]; int a = W(m, p + 10), n = m[(p + 12) & 0xFFFF], q, i;
	name8(m, p + 2, nm);
	if (n > 128) { n = 128; }
	q = port_find(nm);
	switch (op)
	{
		case 28:
			if (q >= 0) { return ports[q].slot == t ? 0 : 2; }
			for (i = 0; i < PT_N && ports[i].used; i++) { }
			if (i >= PT_N) { return 255; }
			memset(&ports[i], 0, sizeof(Port)); ports[i].used = 1; ports[i].slot = t; strcpy(ports[i].name, nm);
			return 0;
		case 29: if (q < 0 || ports[q].slot != t) { return 2; } ports[q].used = 0; return 0;
		case 30:
			if (q < 0) { return 2; }
			if (ports[q].state != 0) { return 1; }
			ports[q].state = 1; ports[q].caller = t; ports[q].len = n;
			for (i = 0; i < n; i++) { ports[q].msg[i] = m[(a + i) & 0xFFFF]; }
			idle = 0;
			return 0;
		case 31:
			if (q < 0 || ports[q].caller != t) { return 2; }
			if (ports[q].state != 3) { return 1; }
			for (i = 0; i < ports[q].len; i++) { m[(a + i) & 0xFFFF] = ports[q].msg[i]; }
			m[(p + 12) & 0xFFFF] = (uint8_t)ports[q].len; ports[q].state = 0; ports[q].caller = 0;
			return 0;
		case 32:
			if (q < 0 || ports[q].slot != t) { return 2; }
			if (ports[q].state != 1) { return 1; }
			for (i = 0; i < ports[q].len; i++) { m[(a + i) & 0xFFFF] = ports[q].msg[i]; }
			m[(p + 12) & 0xFFFF] = (uint8_t)ports[q].len; m[(p + 13) & 0xFFFF] = (uint8_t)ports[q].caller; ports[q].state = 2;
			return 0;
		case 33:
			if (q < 0 || ports[q].slot != t || ports[q].state != 2) { return 2; }
			if (!inUse(ports[q].caller)) { ports[q].state = 0; ports[q].caller = 0; return 0; }
			ports[q].len = n; for (i = 0; i < n; i++) { ports[q].msg[i] = m[(a + i) & 0xFFFF]; }
			ports[q].state = 3; idle = 0;
			return 0;
	}
	return 255;
}

static int svc_info(int t, int p)
{
	uint8_t *m = M(t); int s = m[(p + 1) & 0xFFFF], i, n;
	switch (s)
	{
		case 0: return net_on ? 6 : 255;
		case 1: net_on = 1; return 0;
		case 2: net_on = 0; for (i = 0; i < NT_N; i++) { sock_close(i); } return 0;
		case 3: SW(m, p + 6, ticks()); return 0;
		case 4: { unsigned long v = ticks_all(); SW(m, p + 6, (int)(v & 0xFFFF)); SW(m, p + 8, (int)((v >> 16) & 0xFFFF)); return 0; }
		case 5: return HOST;
		case 6: return bd;
		case 7: n = 0; for (i = 1; i <= ns; i++) { if (i != t && st[i] >= 2 && dv[i] == m[(p + 2) & 0xFFFF]) { n++; } } return n;
		case 8: { int a = W(m, p + 2); for (i = 0; i < 64; i++) { m[(a + i) & 0xFFFF] = 0; } m[a & 0xFFFF] = net_on ? 2 : 0; m[(a + 1) & 0xFFFF] = 255;
			m[(a + 2) & 0xFFFF] = 127; m[(a + 5) & 0xFFFF] = 1; m[(a + 6) & 0xFFFF] = 255;
			const char *msg = "vCPC: the host's network"; for (i = 0; msg[i]; i++) { m[(a + 28 + i) & 0xFFFF] = (uint8_t)msg[i]; } return net_on ? 2 : 0; }
		case 9: return 0;
		case 10: { int a = W(m, p + 2); n = 0; for (i = 0; i < 64; i++) { m[(a + i) & 0xFFFF] = 0; }
			for (i = 0; i < NT_N; i++) { if (!socks[i].used) { continue; } n++; int o = a + i * 8; m[o & 0xFFFF] = (uint8_t)socks[i].slot; m[(o + 1) & 0xFFFF] = (uint8_t)socks[i].state;
				SW(m, o + 2, socks[i].nrx > 65535 ? 65535 : socks[i].nrx); for (int j = 0; j < 4; j++) { m[(o + 4 + j) & 0xFFFF] = socks[i].ip[j]; } } return n; }
		case 11: { time_t now = time(NULL); struct tm *tm = localtime(&now); char b[32]; snprintf(b, sizeof b, "%02d:%02d:%02d %04d-%02d-%02d", tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
			m[(p + 6) & 0xFFFF] = (uint8_t)strlen(b); for (i = 0; b[i]; i++) { m[(p + 7 + i) & 0xFFFF] = (uint8_t)b[i]; } return 0; }
	}
	return 255;
}

static int svc_call(int t, int p)
{
	uint8_t *m = M(t); int op = m[p], i;
	if (op == 10)
	{
		int k = m[(p + 1) & 0xFFFF], src = W(m, p + 2), ad = W(m, p + 4), to = W(m, p + 6), n = W(m, p + 8);
		if (n > 2048) { n = 2048; }
		if (k == 3) { if (src < 0 || src > ns) { return 255; } for (i = 0; i < n; i++) { m[(to + i) & 0xFFFF] = M(src)[(ad + i) & 0xFFFF]; } return 0; }
		if (k == 4) { if (!inUse(src)) { return 255; } for (i = 0; i < n; i++) { M(src)[(ad + i) & 0xFFFF] = m[(to + i) & 0xFFFF]; } return 0; }
		if (k == 1)
		{
			// the ROMs: the M4's name in ROM 6 (a background ROM: NET's "NET M4" finds it), nothing elsewhere
			static const uint8_t m4[17] = { 1, 2, 1, 0, 0x08, 0xC0, 0, 0, 'M', '4', ' ', 'B', 'O', 'A', 'R', 'D' | 0x80, 0 };
			for (i = 0; i < n; i++) { int o = (ad + i) & 0x3FFF; m[(to + i) & 0xFFFF] = (src == 6 && o < 17) ? m4[o] : 0xFF; }
			return 0;
		}
		for (i = 0; i < n; i++) { m[(to + i) & 0xFFFF] = 0; }
		return 0;
	}
	if (op == 11) { return 0; }
	if (op == 13 || op == 14)
	{
		int blk = W(m, p + 2), a = W(m, p + 6), n = W(m, p + 8), o = blk * 2048;
		if (n > 2048) { n = 2048; }
		if (!store_bank[t] || o + n > 65536) { return 255; }
		uint8_t *s = M(store_bank[t]);
		for (i = 0; i < n; i++) { if (op == 13) { s[o + i] = m[(a + i) & 0xFFFF]; } else { m[(a + i) & 0xFFFF] = s[o + i]; } }
		return 0;
	}
	if (op == 15)
	{
		int k = m[(p + 1) & 0xFFFF], d = W(m, p + 2);
		if (k == 4)
		{
			for (i = 0; i < CL_N * 10; i++) { m[(d + i) & 0xFFFF] = 0; }
			for (i = 0; i < nclaims; i++) { int o = d + i * 10; m[o & 0xFFFF] = (uint8_t)claims[i].slot; m[(o + 1) & 0xFFFF] = (uint8_t)claims[i].unit; for (int j = 0; j < 8; j++) { m[(o + 2 + j) & 0xFFFF] = (uint8_t)(claims[i].name[j] ? claims[i].name[j] : ' '); if (!claims[i].name[j]) { for (; j < 8; j++) { m[(o + 2 + j) & 0xFFFF] = ' '; } } } }
		}
		return 0;
	}
	if (op >= 16 && op <= 19)
	{
		int u = m[(p + 1) & 0xFFFF], c = -1; char nm[9];
		name8(m, p + 2, nm);
		for (i = 0; i < nclaims; i++) { if (claims[i].unit == u && strcmp(claims[i].name, nm) == 0) { c = i; } }
		if (op == 16) { if (c >= 0) { return claims[c].slot == t ? 0 : claims[c].slot; } if (nclaims >= CL_N) { return 255; } claims[nclaims].slot = t; claims[nclaims].unit = u; strcpy(claims[nclaims].name, nm); nclaims++; return 0; }
		if (op == 17) { if (c < 0 || claims[c].slot != t) { return 1; } claims[c] = claims[--nclaims]; return 0; }
		if (op == 18) { int had = c >= 0 ? claims[c].slot : 0; if (c >= 0) { claims[c].slot = t; } else if (nclaims < CL_N) { claims[nclaims].slot = t; claims[nclaims].unit = u; strcpy(claims[nclaims].name, nm); nclaims++; } else { return 255; } return had == t ? 0 : had; }
		return c < 0 ? 254 : (claims[c].slot == t ? 0 : claims[c].slot);
	}
	if (op == 20) { return svc_info(t, p); }
	if (op >= 21 && op <= 27) { return net_op(t, p, op); }
	if (op >= 28 && op <= 33) { return port_op(t, p, op); }
	if (op == 34)
	{
		int k = m[(p + 1) & 0xFFFF];
		if (k >= PT_N) { return 255; }
		for (i = 0; i < 8; i++) { m[(p + 2 + i) & 0xFFFF] = (uint8_t)(ports[k].used && ports[k].name[i] ? ports[k].name[i] : ' '); if (ports[k].used && !ports[k].name[i]) { for (; i < 8; i++) { m[(p + 2 + i) & 0xFFFF] = ' '; } } }
		m[(p + 10) & 0xFFFF] = (uint8_t)(ports[k].used ? ports[k].slot : 0);
		return 0;
	}
	return 255;
}

// slot t's claims, sockets, ports, files, its store gone (its program ended, killed, reset)
static void shut(int t)
{
	int i;
	for (i = 0; i < nclaims; ) { if (claims[i].slot == t) { claims[i] = claims[--nclaims]; } else { i++; } }
	for (i = 0; i < NT_N; i++) { if (socks[i].used && socks[i].slot == t) { sock_close(i); } }
	for (i = 0; i < PT_N; i++) { if (ports[i].used && ports[i].slot == t) { ports[i].used = 0; } if (ports[i].used && ports[i].caller == t) { ports[i].state = 0; ports[i].caller = 0; } }
	for (i = 1; i < NH; i++) { if (hs[i].used && hs[i].slot == t) { h_close(i); } }
	if (srch[t].l) { free(srch[t].l); srch[t].l = NULL; }
}

// ---- a slot's bank as the stubs leave it, and a program's start
static void install(int t)
{
	uint8_t *m = M(t); int i;
	memset(m, 0, 0x100);
	m[0] = 0xC3; m[1] = (BIOS + 3) & 255; m[2] = (BIOS + 3) >> 8;    // jp WBOOT ((#0001): the table's second entry)
	m[5] = 0xC3; m[6] = BDOS & 255; m[7] = BDOS >> 8;                // jp the BDOS (#0006: the top of the TPA)
	m[0x38] = 0xFB; m[0x39] = 0xC9;                                  // (no interrupts here: ei, ret)
	memset(m + 0x5D, ' ', 11); memset(m + 0x6D, ' ', 11);
	memset(m + TRAP_LO, 0x76, TRAP_HI - TRAP_LO);                    // (traps: never run)
	memset(m + VS, 0, 8);
	m[VS + 5] = 0x80;                                                // (the DMA)
	z80_init(&banks[t].z);
	banks[t].z.read_byte = rb_cb; banks[t].z.write_byte = wb_cb; banks[t].z.port_in = in_cb; banks[t].z.port_out = out_cb;
	banks[t].z.userdata = m;
	(void)i;
}
static void launch(int t)
{
	uint8_t *m = M(t); z80 *z = &banks[t].z;
	m[0xF2FC] = 0; m[0xF2FD] = 0;                                    // under the first stack: #0000 (a ret ends it)
	z->pc = START; z->sp = 0xF2FC; z->halted = 0;
	memset(m + VS, 0, 5); m[VS + 5] = 0x80; m[VS + 6] = 0;
	if (t > 0) { frame_save(t); }
}

// a trap answered: A, HL (and B = H) back to the caller, then its RET
static void ret_to(int t, int a, int hl)
{
	z80 *z = &banks[t].z; uint8_t *m = M(t);
	z->a = (uint8_t)a; z->h = (uint8_t)(hl >> 8); z->l = (uint8_t)hl; z->b = (uint8_t)(hl >> 8);
	z->pc = (uint16_t)W(m, z->sp); z->sp = (uint16_t)(z->sp + 2);
}

typedef struct { int kind; int a; int cw; int fn; int de; int retry; } Res;  // kind 0 in the bank, 1 C0's, 2 the kernel's
static Res done(int t, int a, int hl) { Res r = { 0, a, 0, 0, 0, 0 }; ret_to(t, a, hl); return r; }
static Res c0(int t, int a, int cw) { Res r = { 1, a, cw, 0, 0, 0 }; ret_to(t, a, a); return r; }
static Res kern(int fn, int de, int retry) { Res r = { 2, 0, 0, fn, de, retry }; return r; }

static void out(int t, const uint8_t *s, int n) { banks[t].chars += n; con_write(t, s, n); }
static int key_get(int t, int take) { uint8_t *m = M(t); int k = m[KEY]; if (k && take) { m[KEY] = 0; } return k; }
static Res poll_(int t)
{
	uint8_t *m = M(t);
	m[VS + 4]++;
	if (m[VS + 4] == 0) { return c0(t, 0, 2); }      // every 256th empty poll: the kernel's turn (its keys reach KEY then)
	return done(t, 0, 0);
}
static int dma(int t) { return W(M(t), VS + 5); }

// 10: a line typed into the buffer at DE - BS / DEL rub out, Control + U / X the line, Control + C
// at its start ends the program, CR / LF end it. Kept in the buffer itself (and VS) while it waits
static Res line_in(int t, int de)
{
	uint8_t *m = M(t); int k, L;
	if (!m[VS + 1] || W(m, VS + 2) != de) { m[VS + 1] = 1; SW(m, VS + 2, de); m[(de + 1) & 0xFFFF] = 0; }
	for (;;)
	{
		k = key_get(t, 1);
		if (!k) { return kern(129, de, 1); }
		L = m[(de + 1) & 0xFFFF];
		if (k == 13 || k == 10) { m[VS + 1] = 0; uint8_t c = 13; out(t, &c, 1); return done(t, 0, 0); }
		if (k == 8 || k == 127) { if (L) { m[(de + 1) & 0xFFFF] = (uint8_t)(L - 1); out(t, (const uint8_t *)"\b \b", 3); } continue; }
		if (k == 21 || k == 24) { while (L) { L--; out(t, (const uint8_t *)"\b \b", 3); } m[(de + 1) & 0xFFFF] = 0; continue; }
		if (k == 3 && !L) { m[VS + 1] = 0; return kern(0, 0, 0); }
		if (k < 32 || k > 126 || L >= m[de]) { continue; }
		m[(de + 2 + L) & 0xFFFF] = (uint8_t)k; m[(de + 1) & 0xFFFF] = (uint8_t)(L + 1);
		{ uint8_t c = (uint8_t)k; out(t, &c, 1); }
	}
}

// the start-up: the default FCBs (#005C, #006C) from the command tail at #0080, then #0100
static void fcb_name(uint8_t *m, int a, const char *s, int n)
{
	int d = 0, i, j = 0, dot = -1;
	memset(m + a, 0, 16); memset(m + a + 1, ' ', 11);
	if (n >= 2 && s[1] == ':' && toupper((unsigned char)s[0]) >= 'A' && toupper((unsigned char)s[0]) <= 'P') { d = toupper((unsigned char)s[0]) - 64; s += 2; n -= 2; }
	m[a] = (uint8_t)d;
	for (i = 0; i < n; i++) { if (s[i] == '.') { dot = i; break; } }
	for (i = 0; i < (dot < 0 ? n : dot) && j < 8; i++) { if (s[i] == '*') { for (; j < 8; j++) { m[a + 1 + j] = '?'; } break; } m[a + 1 + j++] = (uint8_t)toupper((unsigned char)s[i]); }
	if (dot >= 0) { for (i = dot + 1, j = 0; i < n && j < 3; i++) { if (s[i] == '*') { for (; j < 3; j++) { m[a + 9 + j] = '?'; } break; } m[a + 9 + j++] = (uint8_t)toupper((unsigned char)s[i]); } }
}
static void startup(int t)
{
	uint8_t *m = M(t); char s[128]; int n = m[0x80], i, w = 0, k = 0, st0[2] = { 0, 0 }, len[2] = { 0, 0 };
	if (n > 127) { n = 127; }
	for (i = 0; i < n; i++) { s[i] = (char)m[0x81 + i]; } s[n] = 0;
	for (i = 0; i < n && w < 2; )
	{
		while (i < n && s[i] == ' ') { i++; }
		if (i >= n) { break; }
		st0[w] = i; k = 0; while (i < n && s[i] != ' ') { i++; k++; } len[w++] = k;
	}
	fcb_name(m, 0x5C, s + st0[0], len[0]);
	fcb_name(m, 0x6C, s + st0[1], len[1]);
	banks[t].z.pc = 0x100;
}

// ---- CP/M's file calls (an FCB: the drive, the name, EX, S1, S2, RC, d0-d15 - the handle here at
// d0-d1, #C5 at d2 - CR, r0-r2)
static int letterOf(int t, int d) { return d ? 64 + d : (dv[t] ? dv[t] : bd); }
static int fcb_op(int t, int fn, int a)
{
	uint8_t *m = M(t); int drv = letterOf(t, m[a & 0xFFFF] & 31), i, h = 0; const char *p = pathOf(drv);
	char n11[12]; uint8_t buf[128]; Entry e; long rec;
	for (i = 0; i < 11; i++) { n11[i] = (char)(m[(a + 1 + i) & 0xFFFF] & 127); } n11[11] = 0;
#define FB(o) m[(a + (o)) & 0xFFFF]
	if (!p) { return 255; }
	if (fn != 15 && fn != 17 && fn != 18 && fn != 19 && fn != 22 && fn != 23 && fn != 16)
	{
		h = FB(16) | (FB(17) << 8);
		if (!(FB(18) == 0xC5 && h > 0 && h < NH && hs[h].used && hs[h].slot == t))
		{
			h = 0;
			if (find(p, n11, &e)) { h = h_open(e.host, e.ro, t); if (h) { FB(16) = h & 255; FB(17) = h >> 8; FB(18) = 0xC5; } }   // (an FCB copied, or never opened: found by its name)
		}
	}
	switch (fn)
	{
		case 15:
			if (!find(p, n11, &e) || !(h = h_open(e.host, e.ro, t))) { return 255; }
			FB(16) = h & 255; FB(17) = h >> 8; FB(18) = 0xC5; FB(12) = 0; FB(14) = 0; FB(32) = 0;
			FB(15) = (uint8_t)((hs[h].size + 127) / 128 > 128 ? 128 : (hs[h].size + 127) / 128);
			return 0;
		case 16:
			h = FB(16) | (FB(17) << 8);
			if (FB(18) == 0xC5 && h > 0 && h < NH && hs[h].used && hs[h].slot == t) { h_close(h); FB(18) = 0; }
			return 0;
		case 17: case 18:
		{
			int d = dma(t);
			if (fn == 17)
			{
				int n, j, k = 0; Entry *l = list(p, 0, &n);
				if (srch[t].l) { free(srch[t].l); }
				srch[t].l = calloc(n > 0 ? n : 1, sizeof(Entry)); srch[t].n = 0; srch[t].i = 0;
				for (j = 0; j < n; j++) { if (match11(n11, l[j].n11)) { srch[t].l[k++] = l[j]; } }
				srch[t].n = k; free(l);
			}
			if (!srch[t].l || srch[t].i >= srch[t].n) { return 255; }
			e = srch[t].l[srch[t].i++];
			for (i = 0; i < 32; i++) { m[(d + i) & 0xFFFF] = 0; }
			for (i = 0; i < 11; i++) { m[(d + 1 + i) & 0xFFFF] = (uint8_t)e.n11[i]; }
			m[(d + 15) & 0xFFFF] = (uint8_t)((e.size + 127) / 128 > 128 ? 128 : (e.size + 127) / 128);
			return 0;
		}
		case 19:
		{
			int n, j, any = 0; Entry *l;
			if (devRo(p)) { return 255; }
			l = list(p, del_folder, &n); del_folder = 0;
			for (j = 0; j < n; j++) { if (match11(n11, l[j].n11) && !l[j].ro) { any = 1; if (l[j].dir) { rmdir(l[j].host); } else { unlink(l[j].host); } } }
			free(l);
			return any ? 0 : 255;
		}
		case 20:
			if (!h) { return 9; }
			rec = ((FB(14) & 63) * 32 + (FB(12) & 31)) * 128L + (FB(32) & 127);
			if (!h_read(h, rec, buf)) { return 1; }
			for (i = 0; i < 128; i++) { m[(dma(t) + i) & 0xFFFF] = buf[i]; }
			rec++; FB(32) = rec & 127; FB(12) = (rec >> 7) & 31; FB(14) = (rec >> 12) & 63;
			return 0;
		case 21:
			if (!h) { return 9; }
			rec = ((FB(14) & 63) * 32 + (FB(12) & 31)) * 128L + (FB(32) & 127);
			for (i = 0; i < 128; i++) { buf[i] = m[(dma(t) + i) & 0xFFFF]; }
			if (!h_write(h, rec, buf)) { return 2; }
			rec++; FB(32) = rec & 127; FB(12) = (rec >> 7) & 31; FB(14) = (rec >> 12) & 63;
			return 0;
		case 22:
		{
			char hp[1280]; FILE *f;
			if (devRo(p)) { return 255; }
			if (find(p, n11, &e) && !e.ro) { snprintf(hp, sizeof hp, "%s", e.host); } else if (!newPath(p, n11, hp)) { return 255; }   // (a set's file: a copy of its own made in the node's folder)
			if (!(f = fopen(hp, "wb"))) { return 255; }
			fclose(f);
			if (!(h = h_open(hp, 0, t))) { return 255; }
			FB(16) = h & 255; FB(17) = h >> 8; FB(18) = 0xC5; FB(12) = 0; FB(14) = 0; FB(32) = 0; FB(15) = 0;
			return 0;
		}
		case 23:
		{
			char nn[12], np[1280], nd[16], *sl;
			for (i = 0; i < 11; i++) { nn[i] = (char)(FB(17 + i) & 127); } nn[11] = 0;
			if (devRo(p) || !find(p, n11, &e) || e.ro || find(p, nn, NULL)) { return 255; }
			snprintf(np, sizeof np, "%s", e.host); sl = strrchr(np, '/'); nameDot(nn, nd);
			if (sl) { snprintf(sl + 1, sizeof np - (size_t)(sl + 1 - np), "%s", nd); }
			return rename(e.host, np) == 0 ? 0 : 255;
		}
		case 33: case 34:
			if (!h) { return 9; }
			rec = FB(33) | (FB(34) << 8) | ((FB(35) & 3) << 16);
			FB(32) = rec & 127; FB(12) = (rec >> 7) & 31; FB(14) = (rec >> 12) & 63;
			if (fn == 33) { if (!h_read(h, rec, buf)) { return 1; } for (i = 0; i < 128; i++) { m[(dma(t) + i) & 0xFFFF] = buf[i]; } return 0; }
			for (i = 0; i < 128; i++) { buf[i] = m[(dma(t) + i) & 0xFFFF]; }
			return h_write(h, rec, buf) ? 0 : 2;
		case 35:
		{
			long size;
			if (h) { size = hs[h].size; } else if (find(p, n11, &e)) { size = e.size; } else { return 255; }
			rec = (size + 127) / 128;
			FB(33) = rec & 255; FB(34) = (rec >> 8) & 255; FB(35) = (rec >> 16) & 3;
			return 0;
		}
		case 36:
			rec = ((FB(14) & 63) * 32 + (FB(12) & 31)) * 128L + (FB(32) & 127);
			FB(33) = rec & 255; FB(34) = (rec >> 8) & 255; FB(35) = (rec >> 16) & 3;
			return 0;
	}
#undef FB
	return 255;
}

// 30: CyborgOS's UniDOS calls - the block at DE (72 bytes): Byte(0) #C0 + the call, Byte(1) a letter
static int uni(int t, int de)
{
	uint8_t *m = M(t); int op = m[de & 0xFFFF] - 0xC0, L = m[(de + 1) & 0xFFFF], i, n;
#define UB(o) m[(de + (o)) & 0xFFFF]
	char s[256]; Entry *l;
	switch (op)
	{
		case 0: return 0;
		case 1: { const char *p = pathOf(L); n = p ? (int)strlen(p) : 0; UB(2) = (uint8_t)n; for (i = 0; i < n; i++) { UB(3 + i) = (uint8_t)p[i]; } return 0; }
		case 2:
		{
			char rel[16][16], ds[NDIR][1280]; int nrel; Device *d;
			n = UB(2); for (i = 0; i < n && i < 250; i++) { s[i] = (char)toupper(UB(3 + i)); } s[i] = 0;
			if (lidx(L) < 0) { return 2; }
			if (!s[0]) { paths[lidx(L)][0] = 0; return 0; }
			if (s[strlen(s) - 1] != '/' && s[strlen(s) - 1] != ':') { strcat(s, "/"); }
			if (!(d = parse(s, rel, &nrel)) || dirsOf(d, rel, nrel, 0, ds) < 1) { return 2; }
			snprintf(paths[lidx(L)], sizeof paths[0], "%s", s);
			return 0;
		}
		case 3:
		{
			int k = 0;
			for (i = 0; i < ndevs; i++) { int len = (int)strlen(devs[i].name); for (int j = 0; j < len && k < 63; j++) { UB(2 + k++) = (uint8_t)(devs[i].name[j] | (j == len - 1 ? 0x80 : 0)); } }
			for (; k < 64; k++) { UB(2 + k) = 255; }
			return 0;
		}
		case 4:
		{
			char nm[12]; int k = 0;
			for (i = 2; i < 12; i++) { int c = UB(i); nm[k++] = (char)(c & 127); if (c & 128) { break; } } nm[k] = 0;
			for (i = 8; i < 72; i++) { UB(i) = 0; }
			for (i = 0; i < ndevs && strcmp(devs[i].name, nm) != 0; i++) { }
			if (i >= ndevs) { return 1; }
			UB(8) = (uint8_t)(1 | 2 | (devs[i].ro ? 4 : 0) | 32);
			long fr = 1024L * 1024, all = 4096L * 1024;
			for (k = 0; k < 4; k++) { UB(10 + k) = (fr >> (8 * k)) & 255; UB(14 + k) = (all >> (8 * k)) & 255; }
			const char *ab = devs[i].ro ? "CyborgOS (host)" : "vCPC (host)"; n = (int)strlen(ab);
			for (k = 0; k < n; k++) { UB(18 + k) = (uint8_t)(ab[k] | (k == n - 1 ? 0x80 : 0)); }
			return 0;
		}
		case 5:
		{
			const char *p;
			if (L) { p = pathOf(L); } else { n = UB(28); for (i = 0; i < n && i < 250; i++) { s[i] = (char)toupper(UB(29 + i)); } s[i] = 0; p = s; }
			if (!p) { return 255; }
			l = list(p, 1, &n);
			if (n < 0) { return 2; }
			i = UB(2) | (UB(3) << 8);
			if (i >= n) { free(l); return 1; }
			{
				Entry *e = &l[i]; struct tm *tm = localtime(&e->mtime); int k;
				UB(4) = (uint8_t)((e->dir ? 16 : 0) | (e->ro ? 1 : 0));
				for (k = 0; k < 4; k++) { UB(5 + k) = (e->size >> (8 * k)) & 255; }
				UB(9) = (tm->tm_year + 1900) & 255; UB(10) = (tm->tm_year + 1900) >> 8; UB(11) = (uint8_t)(tm->tm_mon + 1); UB(12) = (uint8_t)tm->tm_mday;
				UB(13) = (uint8_t)tm->tm_hour; UB(14) = (uint8_t)tm->tm_min; UB(15) = (uint8_t)tm->tm_sec;
				for (k = 0; k < 11; k++) { UB(16 + k) = (uint8_t)e->n11[k]; }
			}
			free(l);
			return 0;
		}
		case 6:
		{
			char rel[16][16], ds[NDIR][1280], full[1300]; int nrel; Device *d;
			n = UB(2); for (i = 0; i < n && i < 250; i++) { s[i] = (char)toupper(UB(3 + i)); } s[i] = 0;
			if (n > 0 && s[n - 1] == '/') { s[n - 1] = 0; }
			if (!(d = parse(s, rel, &nrel)) || d->ro || nrel < 1) { return 2; }
			if (dirsOf(d, rel, nrel - 1, 1, ds) < 1) { return 2; }
			snprintf(full, sizeof full, "%s/%s", ds[0], rel[nrel - 1]);
			return make_dir(full) == 0 ? 0 : 2;
		}
		case 7: return 255;
		case 8: del_folder = 1; return 0;
	}
#undef UB
	return 255;
}

// ---- a slot's BDOS: answered in the bank (the stub's calls), by C0 (counted, may end the turn), or
// handed to the kernel (0, 131's ops 0-9, a key's wait - #81 -, anything else)
static Res bdos(int t, z80 *z)
{
	uint8_t *m = M(t); int fn = z->c, de = (z->d << 8) | z->e, k, i;
	uint8_t c;
	switch (fn)
	{
		case 0: return kern(0, de, 0);
		case 1: k = key_get(t, 1); if (!k) { return kern(129, de, 1); } c = (uint8_t)k; out(t, &c, 1); return done(t, k, k);
		case 2: c = z->e; out(t, &c, 1); return done(t, 0, 0);
		case 3: return done(t, 0x1A, 0x1A);
		case 4: case 5: case 28: case 29: case 37: return done(t, 0, 0);
		case 6:
			if (z->e == 0xFF) { k = key_get(t, 1); return k ? done(t, k, k) : poll_(t); }
			if (z->e == 0xFE) { return key_get(t, 0) ? done(t, 0xFF, 0xFF) : poll_(t); }
			c = z->e; out(t, &c, 1); return done(t, 0, 0);
		case 7: return done(t, m[3], m[3]);
		case 8: m[3] = z->e; return done(t, 0, 0);
		case 9: { uint8_t b[4096]; int n = 0; for (i = de; m[i & 0xFFFF] != '$' && n < 4096; i++) { b[n++] = m[i & 0xFFFF]; } out(t, b, n); return done(t, 0, 0); }
		case 10: return line_in(t, de);
		case 11: return key_get(t, 0) ? done(t, 0xFF, 0xFF) : poll_(t);
		case 12: return c0(t, 0x22, 2);
		case 13: case 32: return c0(t, 0, 0);
		case 14: dv[t] = 65 + z->e; return c0(t, 0, 0);
		case 24: return c0(t, dv[t] ? (1 << ((dv[t] - 65) & 7)) & 255 : 1, 0);
		case 25: return c0(t, dv[t] ? dv[t] - 65 : 0, 0);
		case 26: SW(m, VS + 5, de); return c0(t, 0, 0);
		case 27: return done(t, 0, DPB);
		case 30: if (m[de & 0xFFFF] < 0xC0) { return done(t, 0, 0); } return c0(t, uni(t, de), 0);
		case 31: { static const uint8_t d[15] = { 36, 0, 3, 7, 0, 179, 0, 63, 0, 0xC0, 0, 16, 0, 0, 0 }; memcpy(m + DPB, d, 15); return done(t, 0, DPB); }
		case 40: return c0(t, fcb_op(t, 34, de), 2);
		case 49: k = m[de & 0xFFFF]; if (k == 0x1A) { return done(t, m[CCOLS] - 1, m[CCOLS] - 1); } if (k == 0x1C) { return done(t, m[CROWS] - 1, m[CROWS] - 1); } return done(t, 0, 0);
		case 130: return c0(t, 255, 2);
		case 131:
			k = m[de & 0xFFFF];
			if (k == 10 || k == 11 || k >= 13) { i = svc_call(t, de); return c0(t, i, (i == 1 && k >= 30) ? 3 : 0); }
			if (k == 12) { return c0(t, storeSize(t, W(m, de + 2)), 0); }
			return kern(131, de, 0);
	}
	if (fn >= 15 && fn <= 36) { return c0(t, fcb_op(t, fn, de), 0); }
	return kern(fn, de, 0);
}
static Res bios(int t, int i, z80 *z)
{
	int k;
	switch (i)
	{
		case 0: case 1: return kern(0, 0, 0);
		case 2: return key_get(t, 0) ? done(t, 0xFF, 0xFF) : poll_(t);
		case 3: k = key_get(t, 1); if (!k) { return kern(129, 0, 1); } return done(t, k, k);
		case 4: { uint8_t c = z->c; out(t, &c, 1); return done(t, 0, 0); }
		case 7: return done(t, 0x1A, 0x1A);
		case 9: return done(t, 0, 0);
		case 12: SW(M(t), VS + 5, (z->b << 8) | z->c); return done(t, 0, 0);
		case 13: case 14: return done(t, 1, 1);
		case 15: return done(t, 0xFF, 0xFF);
		case 16: return done(t, z->c, (z->b << 8) | z->c);
	}
	return done(t, 0, 0);
}
static Res call(int t)
{
	z80 *z = &banks[t].z;
	if (verbose && z->pc == BDOS) { fprintf(stderr, "[%d] bdos %d de=%04X (%02X)\n", t, z->c, (z->d << 8) | z->e, M(t)[((z->d << 8) | z->e) & 0xFFFF]); }
	if (z->pc == START) { Res r = { 0, 0, 0, 0, 0, 0 }; startup(t); return r; }
	if (z->pc >= BIOS && z->pc < BIOS + BIOSN * 3) { return bios(t, (z->pc - BIOS) / 3, z); }
	return bdos(t, z);
}

// run(t, max): instructions until max T, a trap, or a HALT - 0 the slice used, 1 a trap, 2 a HALT.
// A slice ends only where an interrupt could be taken (interrupts on), as on the CPC, where the
// slot is switched out by its interrupt: with them off, SP may be something else than a stack (an
// NTC program's thread), and the registers' frame pushed there would land in its code
static int run(int t, long max)
{
	z80 *z = &banks[t].z;
	unsigned long start = z->cyc;                // (the core's count wraps - at 4G on Windows, where an
	for (;;)                                     // unsigned long is 32 bits: only differences are taken)
	{
		if (z->pc >= TRAP_LO && z->pc < TRAP_HI) { return 1; }
		if (z->halted) { return 2; }
		if ((unsigned long)(z->cyc - start) >= (unsigned long)max && (t == 0 || (z->iff1 && !z->iff_delay))) { return 0; }
		z80_step(z);
	}
}

// RUN t a k n: slot t runs (a: its last call's answer) up to n And 255 slices, its BDOS calls that
// are C0's answered here (up to n \ 256): r0 0 its slices used, 1 a call for the kernel (r1 the
// function, r2 DE), 2 C0 answered one that ends the turn (r1 the answer), 3 so did it and the slot
// sleeps a tick; r3 the characters, r4 its key byte (k), r5 its runs, r6 the runs its slice ended
static int kw[16];
static void run_slot(int t, int a, int k, int n)
{
	uint8_t *m = M(t); int slices = n & 255, calls = n >> 8, used = 0, runs = 0, chars0 = banks[t].chars, why;
	Res r;
	frame_load(t);
	if (m[VS] == 1) { ret_to(t, a, a); }
	m[VS] = 0;
	kw[8] = 0;
	for (;;)
	{
		runs++;
		why = run(t, SLICE);
		if (why == 0) { used++; if (used >= slices) { kw[8] = 0; break; } continue; }
		if (why == 2) { banks[t].z.halted = 0; used++; kw[8] = 0; break; }    // (a HALT: the turn given up)
		r = call(t);
		if (r.kind == 2) { kw[8] = 1; kw[9] = r.fn; kw[10] = r.de; m[VS] = (uint8_t)(r.retry ? 2 : 1); break; }
		if (r.kind == 0) { continue; }
		calls--;
		if (r.cw || calls <= 0) { kw[8] = r.cw ? r.cw : 2; kw[9] = r.a; break; }
	}
	kw[11] = banks[t].chars - chars0;
	if (k) { kw[12] = m[KEY]; }
	kw[13] = runs; kw[14] = used;
	frame_save(t);
}

// ---- the kernel's asking: BDOS 132, its block at #EE00 (ops: KERNEL.bas's header)
static const int MODES[6][2] = { { 80, 25 }, { 80, 24 }, { 88, 23 }, { 96, 21 }, { 64, 32 }, { 84, 24 } };   // Hyp.Scr: 2, S, W, UW, P, F

static void serve(void)
{
	uint8_t *K = M(0); int i, op, t;
	int in[8];
	for (i = 0; i < 8; i++) { in[i] = W(K, KB + i * 2); }
	op = in[0]; t = in[1];
	if (verbose > 1 && op != 22 && op != 12 && op != 3 && op != 1) { fprintf(stderr, "[K] op %d %d %d %d %d\n", op, t, in[2], in[3], in[4]); }
	for (i = 8; i < 16; i++) { kw[i] = W(K, KB + i * 2); }   // (the answers kept: an op sets only its own - as C0's Serve)
	kw[8] = 0;
	switch (op)
	{
		case 1: kw[8] = M(t)[in[2]]; break;
		case 2: M(t)[in[2]] = (uint8_t)in[3]; break;
		case 3: kw[8] = W(M(t), in[2]); break;
		case 4: SW(M(t), in[2], in[3]); break;
		case 5: for (i = 0; i < in[3] && i < 224; i++) { M(t)[(in[2] + i) & 0xFFFF] = K[KB + 32 + i]; } break;
		case 6: for (i = 0; i < in[3] && i < 224; i++) { K[KB + 32 + i] = M(t)[(in[2] + i) & 0xFFFF]; } break;
		case 7: con_write(t, K + KB + 32, in[2] < 224 ? in[2] : 224); break;
		case 8: break;                                   // CONX: the cursor's flash, a page shown
		case 9: { const int *s = MODES[t >= 0 && t < 6 ? t : 0]; if (in[2] == 0) { kw[8] = s[0]; } else if (in[2] == 1) { kw[8] = s[1]; } if (in[2] == 2) { dirty = 1; shown_valid = 0; } break; }
		case 10: break;                                  // INKS
		case 11:                                         // SAY n col row: TOP's lines
			for (i = 0; i < t && in[2] + i < 80 && in[3] < 25; i++) { top_cells[in[3] * 80 + in[2] + i] = K[KB + 32 + i]; }
			if (front == 0) { dirty = 1; }
			break;
		case 12: run_slot(t, in[2], in[3], in[4]); worked = 1; break;
		case 13:                                         // FILE f x t: 100 open the name, 102 close, 103 load
		{
			int f = t, x = in[2], s = in[3], a = in[4]; const char *p = pathOf(hdr);
			if (f == 100)
			{
				char nm[32], n11[12]; Entry e; int n = x < 30 ? x : 30, ok = 0;
				for (i = 0; i < n; i++) { nm[i] = (char)toupper(K[KB + 32 + i]); } nm[n] = 0;
				if (hfile) { h_close(hfile); hfile = 0; }
				if (p && name83(nm, n11) && find(p, n11, &e)) { ok = 1; }
				else if (p && n > 4 && strcmp(nm + n - 4, ".CYB") == 0)
				{
					strcpy(nm + n - 4, ".COM");
					if (name83(nm, n11) && find(p, n11, &e)) { ok = 1; for (i = 0; i < n; i++) { K[KB + 32 + i] = (uint8_t)nm[i]; } }
				}
				kw[8] = 255;
				if (ok && (hfile = h_open(e.host, 1, 0))) { kw[8] = 0; }
			}
			else if (f == 102) { if (hfile) { h_close(hfile); hfile = 0; } }
			else if (f == 103)
			{
				long n = hs[hfile].used ? hs[hfile].size : 0;
				if (n > x) { n = x; } if (n > 65536 - a) { n = 65536 - a; }
				if (hfile) { fseek(hs[hfile].f, hs[hfile].base, SEEK_SET); n = (long)fread(M(s) + a, 1, (size_t)n, hs[hfile].f); }
				kw[8] = (int)n;
			}
			else { kw[8] = 255; }
			break;
		}
		case 14: kw[8] = pathOf(t) ? (hdr = t, 0xFFFF) : 0; break;     // DRIVE d: True when it is there
		case 15: shut(t); break;
		case 16: kw[8] = svc_call(t, in[2]); break;
		case 17: kw[8] = 255; break;                     // RAMDISC: (to come: its driver in a page)
		case 18: kw[8] = storeSize(t, in[2]); break;
		case 19: install(t); break;
		case 20:                                         // MEMINFO: the tables, the top, pages, RAM K, free pages
		{
			int fr = 0; for (i = 1; i < NBANKS; i++) { if (!banks[i].used) { fr += 4; } }
			kw[8] = 0x9738; kw[9] = 0xA5F0; kw[10] = 1 + NBANKS * 4; kw[11] = 64 + NBANKS * 64; kw[12] = fr;   // (pages as the CPC's: 1 main RAM, 4 a bank)
			break;
		}
		case 21:                                         // MEMMAP p: up to 32 pages from page p (the CPC's numbering: 1 main
		{                                                // RAM, 2 on the banks' 4 each) - B a slot's or the kernel's,
			int n = 0;                                   // U a store's, - free (MEM's legend)
			for (i = 0; i < 32; i++)
			{
				int pg = t + i, b = (pg - 2) / 4;
				if (pg < 2 || b >= NBANKS) { break; }
				K[KB + 32 + n++] = (uint8_t)(b == 0 || banks[b].used == 1 ? 'B' : (banks[b].used == 3 ? 'U' : '-'));
			}
			kw[8] = n;
			break;
		}
		case 22:                                         // HOT w: a system key, else (w) a key
			if (nhots) { kw[8] = hots[0]; memmove(hots, hots + 1, --nhots * sizeof(int)); }
			else
			{
				if (t) { kw[9] = nkeys ? keys[0] : 0; kw[10] = 0; kw[11] = 0; if (nkeys) { memmove(keys, keys + 1, --nkeys * sizeof(int)); } }
				if (!worked && !nkeys) { idle++; } else { idle = 0; }
				worked = 0;
			}
			break;
		case 23: idle += 2; break;                       // FRAME: nothing runs
		case 24: st[t] = in[2]; if (t >= 1 && t <= ns) { banks[t].used = in[2] ? 1 : 0; } break;
		case 25: if (front != t) { front = t; dirty = 1; shown_valid = 0; } break;
		case 26: kw[8] = ns; kw[9] = bd; kw[10] = HOST; kw[11] = 0; kw[12] = 70; kw[13] = 0; break;   // BOOT
		case 27: quit_now = 1; break;
		case 28: kw[8] = dv[t]; break;
		case 29: dv[t] = in[2]; break;
		case 30: launch(t); break;
		case 31: if (!dv[t]) { dv[t] = bd; } hdr = dv[t]; kw[8] = pathOf(hdr) ? 0xFFFF : 0; break;
		case 32: kw[9] = 0; kw[10] = 0; kw[8] = nkeys ? keys[0] : 0; if (nkeys) { memmove(keys, keys + 1, --nkeys * sizeof(int)); } break;
		case 33: kw[8] = bd; break;
		case 37:                                         // SLOT t g v: its DMA, a file open, the build's stamp (DUMP / REVIVE)
		{
			int g = in[2], open_ = 0;
			if (g == 1) { SW(M(t), VS + 5, in[3]); }
			if (g == 2) { memset(M(t) + 0xFF00, 0, 32); storeSize(t, 0); }      // (after REVIVE: no store; the rest of a vCPC slot is all in its bank)
			for (i = 1; i < NH; i++) { if (hs[i].used && hs[i].slot == t) { open_ = 1; } }
			kw[8] = W(M(t), VS + 5); kw[9] = open_ ? 0xFFFF : 0; kw[10] = STAMP;
			break;
		}
		default: break;
	}
	kw[15] = ticks();
	for (i = 8; i < 16; i++) { SW(K, KB + i * 2, kw[i]); }
}

// step(ms): the kernel runs (the slots through it) for about ms, or until it is idle: 0 quit
static int step(int msec)
{
	z80 *z = &banks[0].z; uint8_t *K = M(0); long until = ms_now() + msec; int n = 0, why;
	idle = 0;
	while (!quit_now)
	{
		why = run(0, 1000000);
		if (why == 2) { z->halted = 0; continue; }
		if (why == 0) { if (ms_now() > until) { return 1; } continue; }
		if (z->pc == BDOS)
		{
			if (z->c == 132) { serve(); }
			else if (z->c == 0) { quit_now = 1; return 0; }
			else if (z->c == 2) { uint8_t c = z->e; con_write(front ? front : 1, &c, 1); }
			else if (z->c == 9) { uint8_t b[2048]; int k = 0; for (int a = (z->d << 8) | z->e; K[a & 0xFFFF] != '$' && k < 2048; a++) { b[k++] = K[a & 0xFFFF]; } con_write(front ? front : 1, b, k); }
		}
		ret_to(0, 0, 0);
		if (idle > 1) { return 1; }
		if ((++n & 63) == 0 && ms_now() > until) { return 1; }
	}
	return 0;
}

static int boot(const char *kernel)
{
	FILE *f; uint8_t buf[65536]; long n; Entry e; const char *pz = pathOf('Z');
	if (!kernel)
	{
		if ((pz && find(pz, "KERNEL  CYB", &e)) || (pathOf('A') && find(pathOf('A'), "KERNEL  CYB", &e))) { kernel = e.host; }
		else { fprintf(stderr, "vcpc: no KERNEL.CYB on Z: or A:\n"); return 0; }
	}
	if (!(f = fopen(kernel, "rb"))) { fprintf(stderr, "vcpc: %s: %s\n", kernel, strerror(errno)); return 0; }
	n = (long)fread(buf, 1, sizeof buf, f); fclose(f);
	install(0);
	if (amsdos(buf, (int)n) >= 0) { memcpy(M(0) + 0x100, buf + 128, (size_t)(n - 128 < KB - 0x100 ? n - 128 : KB - 0x100)); }
	else { memcpy(M(0) + 0x100, buf, (size_t)(n < KB - 0x100 ? n : KB - 0x100)); }
	banks[0].used = 2;
	banks[0].z.pc = 0x100; banks[0].z.sp = 0xF2FC;
	return 1;
}

// ---- the outside: keys typed (to the slot in front, as a keyboard), system keys (a slot to the
// front: 1-20; TOP: 99)
static void type_key(int k) { if (nkeys < 256) { keys[nkeys++] = k & 255; } idle = 0; }
static void type_str(const char *s) { for (; *s; s++) { type_key((unsigned char)*s); } }
static void hot(int n) { if (nhots < 32) { hots[nhots++] = n; } idle = 0; }

static void run_for(int msec)
{
	long end = ms_now() + msec;
	while (!quit_now && ms_now() < end)
	{
		net_poll();
		if (step(20) && idle) { sleep_ms(2); }
	}
}

// the terminal: the screen in front drawn with ANSI codes; keys in raw mode - Control + T then a
// digit brings that slot to the front (0: 10), T TOP, Q quits
static void tty_restore(void) { tty_raw_off(); printf("\033[?25h\n"); fflush(stdout); }
// the screen in front drawn on the terminal: only the rows that changed since the last time (a copy
// of what is on it is kept), each one in place (no clearing: no flicker), the cursor hidden while
// drawing and then put where the slot's is - all of it in one write
static void tty_draw(void)
{
	static char out[64 * 200 + 64];
	char buf[8192], *rows[64]; int t = front, n = 0, r, nrows = 0, x = 0, y = 0, cols = 80, h = 25;
	con_text(t, buf, sizeof buf);
	if (t) { cols = M(t)[CCOLS] ? M(t)[CCOLS] : 80; h = M(t)[CROWS] ? M(t)[CROWS] : 24; if (cols * h > 2048) { h = 2048 / cols; } x = M(t)[CURS]; y = M(t)[CURS + 1]; }
	if (h > 64) { h = 64; } if (cols > 160) { cols = 160; }
	for (char *p = buf, *start = buf; nrows < 64; p++) { if (*p == '\n' || *p == 0) { int end = (*p == 0); *p = 0; rows[nrows++] = start; start = p + 1; if (end) { break; } } }
	if (!shown_valid || shown_rows != h || shown_cols != cols) { n += sprintf(out + n, "\033[2J"); memset(shown, 0, sizeof shown); shown_valid = 1; shown_rows = h; shown_cols = cols; }
	n += sprintf(out + n, "\033[?25l");
	for (r = 0; r < h; r++)
	{
		const char *row = r < nrows ? rows[r] : "";
		if (strcmp(row, shown[r]) == 0) { continue; }
		n += sprintf(out + n, "\033[%d;1H%.*s\033[K", r + 1, cols, row);
		snprintf(shown[r], sizeof shown[r], "%s", row);
	}
	n += sprintf(out + n, "\033[%d;%dH\033[?25h", y + 1, x + 1);
	fwrite(out, 1, (size_t)n, stdout);
	fflush(stdout);
	dirty = 0;
}
static void tty_main(void)
{
	int pre = 0; long last = 0;
	tty_raw_on(); atexit(tty_restore);
	while (!quit_now)
	{
		unsigned char b[64]; int n = tty_read(b, sizeof b);
		for (int i = 0; i < n; i++)
		{
			int c = b[i];
			if (pre) { pre = 0; if (c >= '1' && c <= '9') { hot(c - '0'); } else if (c == '0') { hot(10); } else if (c == 't' || c == 'T') { hot(99); } else if (c == 'q' || c == 'Q') { quit_now = 1; } continue; }
			if (c == 20) { pre = 1; continue; }                       // Control + T
			if (c == 27 && i + 2 < n && b[i + 1] == '[') { int a = b[i + 2]; i += 2; type_key(a == 'A' ? 240 : a == 'B' ? 241 : a == 'D' ? 242 : a == 'C' ? 243 : 0); continue; }
			if (c == 27) { type_key(0xFC); continue; }                // ESC (the firmware's #FC: the kernel makes it #1B)
			if (c == 10) { c = 13; }
			if (c == 8) { c = 127; }
			type_key(c);
		}
		net_poll();
		if (step(20) && idle) { sleep_ms(2); }
		if (dirty && ms_now() - last > 33) { tty_draw(); last = ms_now(); }
	}
}

static void usage(void)
{
	fprintf(stderr,
		"vcpc - CyborgOS headless (a vCPC: the Headless Horseman)\n"
		"  --sys DIR     CyborgOS's set (KERNEL.CYB, the commands, HELP/): device SYS, read only; again for\n"
		"                more folders looked through in turn (the Next's CYBORGOS and CYBORGOS/C)\n"
		"  --data DIR    this node's own files: device DATA (made if not there)\n"
		"  --slots n     slots (1-20, 10)\n"
		"  --tty         the terminal is its screen and keyboard (Control + T, then 1-9 / 0 / T / Q)\n"
		"  --script S    commands typed in turn (; between), each given --wait ms (1500), then the screens\n"
		"  --show LIST   with --script: the slots whose screens are printed (1,2 ...; 0 TOP; all: all)\n");
}

int main(int argc, char **argv)
{
	int i, tty = 0, wait = 1500; const char *script = NULL, *show = NULL, *kernel = NULL;
	char sysdirs[NDIR][1024]; int nsys = 0; const char *data = "vcpc-data";
	t0 = clock_ms();
	net_start();
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--sys") && i + 1 < argc && nsys < NDIR) { snprintf(sysdirs[nsys++], 1024, "%s", argv[++i]); }
		else if (!strcmp(argv[i], "--data") && i + 1 < argc) { data = argv[++i]; }
		else if (!strcmp(argv[i], "--slots") && i + 1 < argc) { ns = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--tty")) { tty = 1; }
		else if (!strcmp(argv[i], "--script") && i + 1 < argc) { script = argv[++i]; }
		else if (!strcmp(argv[i], "--wait") && i + 1 < argc) { wait = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--show") && i + 1 < argc) { show = argv[++i]; }
		else if (!strcmp(argv[i], "--kernel") && i + 1 < argc) { kernel = argv[++i]; }
		else if (!strcmp(argv[i], "-v")) { verbose = 1; }
		else if (!strcmp(argv[i], "-vv")) { verbose = 2; }
		else { usage(); return 1; }
	}
	if (ns < 1) { ns = 1; } if (ns > MAXS) { ns = MAXS; }
	if (!nsys) { usage(); return 1; }
	for (i = 0; i < NBANKS; i++) { banks[i].m = calloc(1, 65536); }
	for (i = 0; i < NH; i++) { hs[i].used = 0; }
	for (i = 0; i < NT_N; i++) { socks[i].fd = -1; }
	// the devices and the letters: A the node's own (DATA:), B and Z CyborgOS's set (SYS:)
	snprintf(devs[0].name, 12, "SYS"); devs[0].ro = 1; devs[0].ndirs = nsys;
	for (i = 0; i < nsys; i++) { snprintf(devs[0].dirs[i], 1024, "%s", sysdirs[i]); }
	// DATA: the node's own folder, with CyborgOS's set under it (seen, read only: a write makes the
	// node's own copy) - so A: is the CPC's CYBORGOS folder (the apps, HELP/) and the node's files
	snprintf(devs[1].name, 12, "DATA"); devs[1].ndirs = 1 + nsys; snprintf(devs[1].dirs[0], 1024, "%s", data);
	for (i = 0; i < nsys; i++) { snprintf(devs[1].dirs[1 + i], 1024, "%s", sysdirs[i]); }
	make_dir(data);
	ndevs = 2;
	strcpy(paths[lidx('A')], "DATA:"); strcpy(paths[lidx('Z')], "SYS:");
	for (i = 1; i <= ns; i++) { banks[i].m[CCOLS] = 80; banks[i].m[CROWS] = 24; banks[i].m[CMODE] = 1; con_size(i); install(i); }
	memset(top_cells, ' ', sizeof top_cells);
	(void)mainram;
	if (!boot(kernel)) { return 2; }
	if (tty) { tty_main(); return 0; }
	run_for(400);
	if (script)
	{
		char cmd[256]; const char *p = script;
		while (*p && !quit_now)
		{
			int n = 0;
			while (*p && *p != ';' && n < 250) { cmd[n++] = *p++; }
			cmd[n] = 0; if (*p == ';') { p++; }
			if (cmd[0] == '^' && cmd[1]) { hot(toupper((unsigned char)cmd[1]) == 'T' ? 99 : atoi(cmd + 1)); }   // ^2: slot 2 to the front, ^T: TOP
			else if (cmd[0] == '~' && cmd[1]) { run_for(atoi(cmd + 1)); continue; }                         // ~3000: 3 seconds more, typing nothing
			else if (cmd[0] == '?' && cmd[1])                                                              // ?2: slot 2's last line, now
			{
				char buf[8192], *nl; con_text(atoi(cmd + 1), buf, sizeof buf); nl = strrchr(buf, '\n');
				printf("--- slot %d: %s\n", atoi(cmd + 1), nl ? nl + 1 : buf); fflush(stdout); continue;
			}
			else { type_str(cmd); type_key(13); }
			run_for(wait);
		}
	}
	else { run_for(wait); }
	{
		char buf[8192], list[256];
		snprintf(list, sizeof list, "%s", show ? show : "front");
		for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ","))
		{
			int from, to;
			if (!strcmp(tok, "all")) { from = 1; to = ns; } else if (!strcmp(tok, "front")) { from = to = front; } else { from = to = atoi(tok); }
			for (int t = from; t <= to; t++) { con_text(t, buf, sizeof buf); printf("=== slot %d%s\n%s\n", t, t == front ? " (front)" : "", buf); }
		}
	}
	return 0;
}
