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

// the host's network for IPCONFIG: its first IPv4 adapter that is up - the address, the netmask, the
// gateway, two DNS servers, the MAC and its name: 1 found, 0 none
#include <iphlpapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#endif
static int host_net(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4], uint8_t dns1[4], uint8_t dns2[4], uint8_t mac[6], char *name, int nmax)
{
	ULONG len = 32768; IP_ADAPTER_ADDRESSES *aa = (IP_ADAPTER_ADDRESSES *)malloc(len), *a; int found = 0;
	if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, aa, &len) == ERROR_BUFFER_OVERFLOW) { free(aa); aa = (IP_ADAPTER_ADDRESSES *)malloc(len); }
	if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, aa, &len) != NO_ERROR) { free(aa); return 0; }
	for (a = aa; a && !found; a = a->Next)
	{
		IP_ADAPTER_UNICAST_ADDRESS *u; IP_ADAPTER_DNS_SERVER_ADDRESS *d; int k = 0;
		if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) { continue; }
		for (u = a->FirstUnicastAddress; u; u = u->Next)
		{
			if (u->Address.lpSockaddr->sa_family != AF_INET) { continue; }
			memcpy(ip, &((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr, 4);
			{ unsigned long m = u->OnLinkPrefixLength ? 0xFFFFFFFFUL << (32 - u->OnLinkPrefixLength) : 0; mask[0] = (uint8_t)(m >> 24); mask[1] = (uint8_t)(m >> 16); mask[2] = (uint8_t)(m >> 8); mask[3] = (uint8_t)m; }
			found = 1; break;
		}
		if (!found) { continue; }
		if (a->FirstGatewayAddress && a->FirstGatewayAddress->Address.lpSockaddr->sa_family == AF_INET) { memcpy(gw, &((struct sockaddr_in *)a->FirstGatewayAddress->Address.lpSockaddr)->sin_addr, 4); }
		for (d = a->FirstDnsServerAddress; d && k < 2; d = d->Next)
		{
			if (d->Address.lpSockaddr->sa_family != AF_INET) { continue; }
			memcpy(k ? dns2 : dns1, &((struct sockaddr_in *)d->Address.lpSockaddr)->sin_addr, 4); k++;
		}
		memcpy(mac, a->PhysicalAddress, a->PhysicalAddressLength >= 6 ? 6 : a->PhysicalAddressLength);
		snprintf(name, (size_t)nmax, "%ls", a->FriendlyName);
	}
	free(aa);
	return found;
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

// the host's network for IPCONFIG: its first IPv4 interface that is up and isn't the loopback - the
// address, the netmask, the gateway (Linux: /proc/net/route), two DNS servers (/etc/resolv.conf), the
// MAC (Linux: /sys/class/net/NAME/address) and its name: 1 found, 0 none
#include <ifaddrs.h>
#include <net/if.h>
static int host_net(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4], uint8_t dns1[4], uint8_t dns2[4], uint8_t mac[6], char *name, int nmax)
{
	struct ifaddrs *ifs, *f; int found = 0, k = 0; FILE *fp; char line[256], path[300];
	if (getifaddrs(&ifs) != 0) { return 0; }
	for (f = ifs; f; f = f->ifa_next)
	{
		if (!f->ifa_addr || f->ifa_addr->sa_family != AF_INET || (f->ifa_flags & IFF_LOOPBACK) || !(f->ifa_flags & IFF_UP)) { continue; }
		memcpy(ip, &((struct sockaddr_in *)f->ifa_addr)->sin_addr, 4);
		if (f->ifa_netmask) { memcpy(mask, &((struct sockaddr_in *)f->ifa_netmask)->sin_addr, 4); }
		snprintf(name, (size_t)nmax, "%s", f->ifa_name);
		found = 1; break;
	}
	freeifaddrs(ifs);
	if (!found) { return 0; }
	if ((fp = fopen("/proc/net/route", "r")))
	{
		while (fgets(line, sizeof line, fp))
		{
			char ifn[64]; unsigned long dst, g;
			if (sscanf(line, "%63s %lx %lx", ifn, &dst, &g) == 3 && dst == 0 && strcmp(ifn, name) == 0) { memcpy(gw, &g, 4); break; }
		}
		fclose(fp);
	}
	if ((fp = fopen("/etc/resolv.conf", "r")))
	{
		while (fgets(line, sizeof line, fp) && k < 2)
		{
			char a[64]; struct in_addr in;
			if (sscanf(line, "nameserver %63s", a) == 1 && inet_pton(AF_INET, a, &in) == 1) { memcpy(k ? dns2 : dns1, &in, 4); k++; }
		}
		fclose(fp);
	}
	snprintf(path, sizeof path, "/sys/class/net/%s/address", name);
	if ((fp = fopen(path, "r")))
	{
		unsigned int m[6];
		if (fscanf(fp, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) { for (k = 0; k < 6; k++) { mac[k] = (uint8_t)m[k]; } }
		fclose(fp);
	}
	return 1;
}
#endif

#endif
