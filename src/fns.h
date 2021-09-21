#define LEN(x) (sizeof(x) / sizeof(*(x)))

/* html */
void htmlprint(char *);
void htmlcat(char *);
void htmlcategories(void);
void htmltemplate(char *, Info *);

/* cgi */
extern char const *cgierror;
char *cgiquery(char const *);
long long cgiquerynum(char const *, long long, long long, char const **);

/* util */
extern char *arg0;
void err(int, char const *, ...);
void errx(int, char const *, ...);
void warn(char const *, ...);
void warnx(char const *, ...);
char *strsep(char **, char const *);
long long strtonum(char const *, long long, long long, char const **);
char *fopenread(char const *);

/* info */
Info *infoopen(char *);
void infoclose(Info *);
int infoset(Info *, char *, char *);
char *infoget(Info *, char *);
void infosort(Info *);
