#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "dat.h"
#include "fns.h"

char *arg0;

static void
_log(char const *fmt, va_list va, int err)
{
	if (arg0 != NULL)
		fprintf(stderr, "%s: ", arg0);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, err ? ": %s\n" : "\n", strerror(err));
	fflush(stderr);
}

void
err(int e, char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	_log( fmt, va, errno);
	exit(e);
}

void
errx(int e, char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	_log( fmt, va, 0);
	exit(e);
}

void
warn(char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	_log(fmt, va, errno);
}

void
warnx(char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	_log(fmt, va, 0);
}

char *
strsep(char **sp, char const *sep)
{
        char *s, *prev;

        if(*sp == NULL)
                return NULL;

        for(s = prev = *sp; strchr(sep, *s) == NULL; s++)
                continue;

        if(*s == '\0'){
                *sp = NULL;
        }else{
                *s = '\0';
                *sp = s + 1;
        }
        return prev;
}

long long
strtonum(char const *s, long long min, long long max, char const **errstr)
{
	long long ll = 0;
	char *end;

	assert(min < max);
	errno = 0;
	ll = strtoll(s, &end, 10);
	if ((errno == ERANGE && ll == LLONG_MIN) || ll < min) {
		if (errstr != NULL)
			*errstr = "too small";
		return 0;
	}
	if ((errno == ERANGE && ll == LLONG_MAX) || ll > max) {
		if (errstr != NULL)
			*errstr = "too large";
		return 0;
	}
	if (errno == EINVAL || *end != '\0') {
		if (errstr != NULL)
			*errstr = "invalid";
		return 0;
	}
	assert(errno == 0);
	if (errstr != NULL)
		*errstr = NULL;
	return ll;
}

char *
fopenread(char const *path)
{
	FILE *fp;
	size_t sz;
	int c;
	char *buf = NULL;
	void *mem;

	if((fp = fopen(path, "r")) == NULL)
		return NULL;
	for(sz = 2;; sz++){
		if((mem = realloc(buf, sz)) == NULL)
			goto Err;
		buf = mem;
		if((c = fgetc(fp)) == EOF)
			break;
		buf[sz - 2] = c;
	}
	if(ferror(fp))
		goto Err;
	buf[sz - 1] = '\0';
	return buf;
Err:
	fclose(fp);
	free(buf);
	return NULL;
}
