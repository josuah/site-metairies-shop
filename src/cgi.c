#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dat.h"
#include "fns.h"

char *cgivars[128];
char const *cgierror = "";

static int
init(void)
{
	static int done = 0;
	size_t i;
	char *query, *var;

	if(done)
		return 0;

	if((query = getenv("QUERY_STRING")) == nil){
		cgierror = "$QUERY_STRING is not set";
		return -1;
	}
	for(i = 0; (var = strsep(&query, "&")); i++){
		if(i+1 >= LEN(cgivars)){
			cgierror = "query string has too many vars";
			return -1;
		}
		cgivars[i] = var;
	}
	cgivars[i] = nil;

	done = 1;
	return 0;
}

char *
cgiquery(char const *key)
{
	size_t i;
	char **vp, *eq;

	if(init() == -1)
		return nil;

	for(vp = cgivars; *vp; vp++){
		if((eq = strchr(*vp, '=')) == nil){
			if(strcmp(*vp, key) == 0)
				return "";
		}else if(strncmp(*vp, key, eq-*vp) == 0){
			return eq + 1;
		}
	}
	cgierror = "no such key in query string";
	return nil;
}

long long
cgiquerynum(char const *key, long long min, long long max, char const **errstr)
{
	char *val;
	long long num;

	*errstr = "parameter not found";
	if((val = cgiquery(key)) == nil)
		return 0;
	num = strtonum(val, min, max, errstr);
	cgierror = *errstr;
	*errstr = nil;
	return num;
}

void
cgihead(void)
{
	fputs("Content-Type: text/html\n\n", stdout);
}
