/* Copyright (C) Piotr Durlej */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <err.h>

#include "umake.h"

static char *mfnames[] =
{
	"umakefile",
	"Makefile",
	"makefile",
};

static const char *defmk = "/usr/mk/default.mk";
static char *mfname;
static int sflag;
static int nflag;
static int vflag;
static int fail;
static int depth;

int makebyname(const char *name);

int docmd(const char *cmd, const char *src, const char *target)
{
	char buf[4096];
	const char *sp;
	int noecho = 0;
	int iexit = 0;
	int f = 0;
	char *dp;
	
	for (dp = buf, sp = cmd; *sp; sp++)
		switch (*sp)
		{
		case '$':
			switch (*++sp)
			{
			case '@':
				strcpy(dp, target);
				dp += strlen(target);
				break;
			case '<':
				strcpy(dp, src);
				dp += strlen(src);
				break;
			default:
				*dp++ = '$';
				*dp++ = *sp;
			}
			break;
		default:
			*dp++ = *sp;
		}
	
	sp = buf;
	while (*sp == '-' || *sp == '@')
		switch (*sp++)
		{
		case '-':
			iexit = 1;
			break;
		case '@':
			noecho = 1;
			break;
		}
	*dp++ = 0;
	
	if (!noecho && !sflag)
	{
		fputs(sp, stderr);
		fputc('\n', stderr);
	}

	if (nflag)
		return 0;

	f = !!system(sp);
	if (iexit)
		return 0;
	return f;
}

static void trace(struct rule *r, const char *src, const char *target)
{
	char **input;

	if (!vflag)
		return;
	
	if (src == NULL)
		src = "-";
	
	warnx("making %s: %s (%s)", target, src, r->output);
	if (r->input)
		for (input = r->input; *input; input++)
			warnx("making %s: input %s", target, *input);
}

static int older(const struct timespec *ts1, const struct timespec *ts2)
{
	if (ts1->tv_sec < ts2->tv_sec)
		return 1;
	if (ts1->tv_sec > ts2->tv_sec)
		return 0;
	return ts1->tv_nsec < ts2->tv_nsec;
}

int make(struct rule *r, const char *src, const char *target)
{
	struct timespec stv = { 0, 0};
	struct stat st;
	char **input;
	char **cmd;
	int f = 0;
	
	if (r->done)
		return 0;
	
	if (++depth > 10)
	{
		warnx("recursion limit exceeded");
		return 1;
	}
	
	if (src == NULL && r->input)
		src = r->input[0];
	if (target == NULL)
		target = r->output;
	
	trace(r, src, target);
	
	for (input = r->input; *input; input++)
	{
		f |= makebyname(*input);
		if (!stat(*input, &st) && older(&stv, &st.st_mtim))
			stv = st.st_mtim;
	}
	if (!stat(src, &st) && older(&stv, &st.st_mtim))
		stv = st.st_mtim;
	
	if (f)
	{
		depth--;
		return 1;
	}
	
	if (src != NULL && !stat(target, &st) && !older(&st.st_mtim, &stv))
	{
		depth--;
		return 0;
	}
	
	if (r->cmds)
		for (cmd = r->cmds; *cmd; cmd++)
			docmd(*cmd, src, target);
	
	if (*r->output != '.')
		r->done = 1;

	depth--;
	return 0;
}

int makebyname(const char *name)
{
	struct rule *r;
	const char *tx;
	const char *p;
	size_t blen;
	size_t xlen;
	char *src;
	
	for (r = rules; r; r = r->next)
		if (*r->output != '.' && !strcmp(r->output, name))
			return make(r, NULL, NULL);
	
	tx = strchr(name, '.');
	if (tx != NULL)
		for (r = rules; r; r = r->next)
		{
			if (*r->output != '.')
				continue;
			
			p = strchr(r->output + 1, '.');
			if (p == NULL)
				continue;
			
			if (strcmp(tx, p))
				continue;
			
			xlen = p - r->output;
			blen = tx - name;
			
			src = malloc(blen + p - r->output + 1);
			if (src == NULL)
				err(1, NULL);
			
			memcpy(src, name, tx - name);
			memcpy(src + blen, r->output, xlen);
			src[blen + xlen] = 0;
			
			return make(r, src, name);
		}
	else
		for (r = rules; r; r = r->next)
		{
			if (*r->output != '.')
				continue;
			
			p = strchr(r->output + 1, '.');
			if (p != NULL)
				continue;
			
			xlen = p - r->output;
			blen = tx - name;
			
			if (asprintf(&src, "%s%s", name, r->output) < 0)
				err(1, NULL);
			
			return make(r, src, name);
		}
	
	if (!access(name, 0))
		return 0;
	
	warnx("%s: No rule to make target", name);
	errno = EINVAL;
	fail = 1;
	return -1;
}

int main(int argc, char **argv)
{
	struct rule *r;
	int i;
	int c;
	
	for (i = 0; i < sizeof mfnames; i++)
		if (!access(mfnames[i], 0))
		{
			mfname = mfnames[i];
			break;
		}
	
	while (c = getopt(argc, argv, "nsvd:f:"), c > 0)
		switch (c)
		{
		case 'd':
			defmk = optarg;
			break;
		case 'f':
			mfname = optarg;
			break;
		case 's':
			sflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			return 1;
		}
	
	argv += optind;
	argc -= optind;
	
	setvar("CPPFLAGS",	"");
	setvar("CFLAGS",	"-g");
	setvar("LD",		"ld");
	setvar("AS",		"as");
	setvar("CC",		"cc");
	
	if (load(defmk))
		return 1;
	if (load(mfname))
		return 1;
	
	if (!argc)
	{
		struct rule *def = NULL;
		
		for (r = rules; r->next; r = r->next)
			if (*r->output != '.')
				def = r;
		
		if (def == NULL)
			errx(1, "no default target");
		make(def, NULL, NULL);
	}
	
	for (i = 0; i < argc; i++)
		makebyname(argv[i]);
	
	return fail;
}
