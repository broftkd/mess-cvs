#include <conio.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <unistd.h>
#include "mamece.h"
#include "strconv.h"

void abort(void)
{
	exit(-1);
}

void raise(int signo)
{
	exit(-1);
}

int _getch(void)
{
	return 0;
}

int _kbhit(void)
{
	return 0;
}

void mkdir(const char *dir)
{
	CreateDirectory(A2T(dir));
}

// --------------------------------------------------------------------------

int stat( const char *path, struct stat *buffer )
{
	DWORD dwAttributes;
	int st_mode;

	dwAttributes = GetFileAttributes(A2T(path));
	if (dwAttributes == 0xffffffff)
		return -1;

	st_mode = (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) ? S_IFDIR : 0;
	buffer->st_mode = st_mode;
	return 0;
}

clock_t clock(void)
{
	return (clock_t) GetTickCount();
}

time_t time(time_t *t)
{
	return (time_t) NULL;
}

struct tm *localtime(const time_t *t)
{
	return NULL;
}

struct tm *gmtime(const time_t *t)
{
	return NULL;
}

time_t mktime( struct tm *timeptr )
{
	return 0;
}

char *strerror(int errnum)
{
	return "Error";
}

char *strdup(const char *s)
{
	char *news;
	news = malloc(strlen(s) + 1);
	if (news)
		strcpy(news, s);
	return news;
}

void system(const char *command)
{
}

#ifndef NDEBUG
int _fail(const char *exp, const char *file, int lineno)
{
	char buffer[1024];
	LPCTSTR lpMsg;

	snprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), "assertion failed: %s line %i: %s", file, lineno, exp);
	lpMsg = A2T(buffer);

	OutputDebugString(lpMsg);
	MessageBox(NULL, lpMsg, NULL, MB_OK);
	exit(-1);
	return -1;
}
#endif

// --------------------------------------------------------------------------
// Malloc redefine

#undef malloc
#undef realloc

static size_t outofmemory = 0;

void *mamece_malloc(size_t sz)
{
	void *ptr;
	ptr = malloc(sz);
	if (!ptr)
		outofmemory = sz;
	return ptr;
}

void *mamece_realloc(void *ptr, size_t sz)
{
	ptr = realloc(ptr, sz);
	if (!ptr)
		outofmemory = sz;
	return ptr;
}

size_t outofmemory_occured(void)
{
	size_t o;
	o = outofmemory;
	outofmemory = 0;
	return o;
}
