// compat.h - what differs between Windows and Unix (Linux, the Pi, macOS) for the vCPC: the
// sockets (Winsock), folders (dirent: MinGW has it, Visual C++'s cl doesn't - a small one here),
// the clock, a sleep, and the terminal (raw keys, ANSI codes out). vcpc.c uses only these names
// (c) 2026 Cyborg Unicorn Pty Ltd - MIT License
#ifndef VCPC_COMPAT_H
#define VCPC_COMPAT_H

#ifdef _WIN32
// ---------------------------------------------------------------- Windows
#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <io.h>
#include <direct.h>
#include <conio.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
typedef intptr_t ssize_t;
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#define strcasecmp _stricmp
#define MSG_NOSIGNAL 0
#define MSG_DONTWAIT 0                      // (the sockets are non-blocking already)
#define SOCK_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
#define SOCK_INPROGRESS(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
static int sock_errno(void) { return WSAGetLastError(); }
static void sock_close_fd(int fd) { closesocket((SOCKET)fd); }
static void sock_nonblock(int fd) { u_long one = 1; ioctlsocket((SOCKET)fd, FIONBIO, &one); }
static void net_start(void) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
static int make_dir(const char *p) { return _mkdir(p); }
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
static long long clock_ms(void) { return (long long)GetTickCount64(); }

// folders: opendir / readdir / closedir (MinGW's dirent.h, or this one for cl)
#if defined(__MINGW32__)
#include <dirent.h>
#else
struct dirent { char d_name[MAX_PATH]; };
typedef struct { HANDLE h; WIN32_FIND_DATAA fd; int first; struct dirent de; } DIR;
static DIR *opendir(const char *p)
{
	char pat[MAX_PATH + 4]; DIR *d = (DIR *)calloc(1, sizeof(DIR));
	snprintf(pat, sizeof pat, "%s\\*", p);
	d->h = FindFirstFileA(pat, &d->fd);
	if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
	d->first = 1;
	return d;
}
static struct dirent *readdir(DIR *d)
{
	if (!d->first && !FindNextFileA(d->h, &d->fd)) { return NULL; }
	d->first = 0;
	snprintf(d->de.d_name, sizeof d->de.d_name, "%s", d->fd.cFileName);
	return &d->de;
}
static int closedir(DIR *d) { FindClose(d->h); free(d); return 0; }
#endif

// the terminal: ANSI codes on (Windows 10 and later), keys one at a time with no echo; the arrow
// keys turned into ESC [ A / B / C / D, as a Unix terminal sends them
static DWORD tty_mode_out, tty_mode_in;
static void tty_raw_on(void)
{
	HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE), hi = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(ho, &tty_mode_out); GetConsoleMode(hi, &tty_mode_in);
	SetConsoleMode(ho, tty_mode_out | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
	SetConsoleMode(hi, tty_mode_in & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
}
static void tty_raw_off(void) { SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), tty_mode_out); SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), tty_mode_in); }
static int tty_read(unsigned char *b, int max)
{
	int n = 0;
	while (n < max - 3 && _kbhit())
	{
		int c = _getch();
		if (c == 0 || c == 0xE0)
		{
			int k = _getch(), a = k == 72 ? 'A' : k == 80 ? 'B' : k == 77 ? 'C' : k == 75 ? 'D' : 0;
			if (a) { b[n++] = 27; b[n++] = '['; b[n++] = (unsigned char)a; }
			continue;
		}
		b[n++] = (unsigned char)c;
	}
	return n;
}

#else
// ---------------------------------------------------------------- Unix: Linux, the Pi, macOS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0                      // (macOS: SIGPIPE is ignored instead)
#endif
#define SOCK_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define SOCK_INPROGRESS(e) ((e) == EINPROGRESS)
static int sock_errno(void) { return errno; }
static void sock_close_fd(int fd) { close(fd); }
static void sock_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }
static void net_start(void) { signal(SIGPIPE, SIG_IGN); }
static int make_dir(const char *p) { return mkdir(p, 0777); }
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
static long long clock_ms(void) { struct timeval t; gettimeofday(&t, NULL); return (long long)t.tv_sec * 1000 + t.tv_usec / 1000; }

static struct termios tty_old;
static void tty_raw_on(void)
{
	struct termios t;
	tcgetattr(0, &tty_old); t = tty_old;
	t.c_lflag &= ~(ICANON | ECHO | ISIG); t.c_iflag &= ~(IXON | ICRNL); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &t);
}
static void tty_raw_off(void) { tcsetattr(0, TCSANOW, &tty_old); }
static int tty_read(unsigned char *b, int max) { ssize_t n = read(0, b, (size_t)max); return n > 0 ? (int)n : 0; }
#endif

#endif
