/*	$Id$ */
/*
 * Copyright (c) 2013 Kristaps Dzonsons <kristaps@bsd.lv>,
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/param.h>

#include <assert.h>
#include <expat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"

struct	atom {
	FILE		*f;
	const char	*src;
	const char	*dst;
	XML_Parser	 p;
	char		 domain[MAXHOSTNAMELEN];
	char		 path[MAXPATHLEN];
	struct article	*sargs;
	int		 spos;
	int		 sposz;
	int		 hasid;
	size_t		 stack;
};

static	void	atomprint(FILE *f, const struct atom *arg, 
			const struct article *src);
static	void	entry_begin(void *userdata, const XML_Char *name, 
			const XML_Char **atts);
static	void	entry_empty(void *userdata, const XML_Char *name);
static	void	entry_end(void *userdata, const XML_Char *name);
static	void	id_begin(void *userdata, const XML_Char *name, 
			const XML_Char **atts);
static	void	id_end(void *userdata, const XML_Char *name);
static	int	scmp(const void *p1, const void *p2);
static	void	tmpl_begin(void *userdata, const XML_Char *name, 
			const XML_Char **atts);
static	void	tmpl_end(void *userdata, const XML_Char *name);
static	void	tmpl_text(void *userdata, 
			const XML_Char *s, int len);
static	void	up_begin(void *userdata, const XML_Char *name, 
			const XML_Char **atts);
static	void	up_end(void *userdata, const XML_Char *name);

static void
atomprint(FILE *f, const struct atom *arg, const struct article *src)
{
	char		 buf[1024];
	struct tm	*tm;

	tm = localtime(&src->time);
	strftime(buf, sizeof(buf), "%F", tm);

	fprintf(f, "<id>tag:%s,%s:%s/%s</id>\n", 
			arg->domain, buf, arg->path, src->src);
	fprintf(f, "<title>%s</title>\n", src->title);
	fprintf(f, "<updated>%sT00:00:00Z</updated>\n", buf);
	fprintf(f, "<author><name>%s</name></author>\n", src->author);
	fprintf(f, "<link rel=\"alternate\" "
			 "type=\"text/html\" "
			 "href=\"%s/%s\" />\n", arg->path, src->src);
}

int
atom(XML_Parser p, const char *templ, 
		int sz, char *src[], const char *dst)
{
	char		*buf;
	size_t		 ssz;
	int		 i, fd, rc;
	FILE		*f;
	struct atom	 larg;
	struct article	*sarg;

	ssz = 0;
	rc = 0;
	buf = NULL;
	fd = -1;
	f = NULL;

	memset(&larg, 0, sizeof(struct atom));
	sarg = xcalloc(sz, sizeof(struct article));

	getdomainname(larg.domain, MAXHOSTNAMELEN);
	if ('\0' == larg.domain[0])
		strlcpy(larg.domain, "localhost", MAXHOSTNAMELEN);
	strlcpy(larg.path, "/", MAXPATHLEN);

	for (i = 0; i < sz; i++)
		if ( ! grok(p, 1, src[i], &sarg[i]))
			goto out;

	qsort(sarg, sz, sizeof(struct article), scmp);

	if (NULL == (f = fopen(dst, "w"))) {
		perror(dst);
		goto out;
	} else if ( ! mmap_open(templ, &fd, &buf, &ssz))
		goto out;

	larg.sargs = sarg;
	larg.sposz = sz;
	larg.p = p;
	larg.src = templ;
	larg.dst = dst;
	larg.f = f;

	XML_ParserReset(p, NULL);
	XML_SetDefaultHandler(p, tmpl_text);
	XML_SetElementHandler(p, tmpl_begin, tmpl_end);
	XML_SetUserData(p, &larg);

	if (XML_STATUS_OK != XML_Parse(p, buf, (int)ssz, 1)) {
		fprintf(stderr, "%s:%zu:%zu: %s\n", templ, 
			XML_GetCurrentLineNumber(p),
			XML_GetCurrentColumnNumber(p),
			XML_ErrorString(XML_GetErrorCode(p)));
		goto out;
	} 

	fputc('\n', f);
	rc = 1;
out:
	for (i = 0; i < sz; i++)
		grok_free(&sarg[i]);
	mmap_close(fd, buf, ssz);
	if (NULL != f)
		fclose(f);

	free(sarg);
	return(rc);
}

static void
tmpl_text(void *userdata, const XML_Char *s, int len)
{
	struct atom	*arg = userdata;

	fprintf(arg->f, "%.*s", len, s);
}

static void
entry_begin(void *userdata, 
	const XML_Char *name, const XML_Char **atts)
{
	struct atom	*arg = userdata;

	arg->stack += 0 == strcasecmp(name, "entry");
}

static void
up_begin(void *userdata, 
	const XML_Char *name, const XML_Char **atts)
{
	struct atom	*arg = userdata;

	arg->stack += 0 == strcasecmp(name, "updated");
}

static void
id_begin(void *userdata, 
	const XML_Char *name, const XML_Char **atts)
{
	struct atom	*arg = userdata;

	arg->stack += 0 == strcasecmp(name, "id");
}

static void
tmpl_begin(void *userdata, 
	const XML_Char *name, const XML_Char **atts)
{
	struct atom	 *arg = userdata;
	time_t		  t;
	char		  buf[1024];
	const char	 *start;
	char		 *cp;
	struct tm	 *tm;
	const XML_Char	**attp;

	assert(0 == arg->stack);

	if (0 == strcasecmp(name, "updated")) {
		xmlprint(arg->f, name, atts);
		for (attp = atts; NULL != *attp; attp++)
			if (0 == strcasecmp(*attp, "data-sblg-updated"))
				break;
		if (NULL == *attp) 
			return;
		t = arg->sposz <= arg->spos ?
			time(NULL) :
			arg->sargs[arg->spos].time;
		tm = localtime(&t);
		strftime(buf, sizeof(buf), "%FT%TZ", tm);
		fprintf(arg->f, "%s", buf);
		arg->stack++;
		XML_SetDefaultHandler(arg->p, NULL);
		XML_SetElementHandler(arg->p, up_begin, up_end);
		return;
	} else if (0 == strcasecmp(name, "id")) {
		xmlprint(arg->f, name, atts);
		for (attp = atts; NULL != *attp; attp++)
			if (0 == strcasecmp(*attp, "data-sblg-id"))
				break;
		if (NULL == *attp) 
			return;
		arg->stack++;
		XML_SetDefaultHandler(arg->p, NULL);
		XML_SetElementHandler(arg->p, id_begin, id_end);
		return;
	} else if (0 == strcasecmp(name, "link")) {
		if (arg->spos > 0) {
			fprintf(stderr, "%s: link appears"
				"after entry\n", arg->src);
			XML_StopParser(arg->p, 0);
			return;
		}
		xmlprint(arg->f, name, atts);
		for (attp = atts; NULL != *attp; attp += 2) 
			if (0 == strcasecmp(attp[0], "rel"))
				if (0 == strcasecmp(attp[1], "self"))
					break;
		if (NULL == *attp)
			return;
		for (attp = atts; NULL != *attp; attp += 2) 
			if (0 == strcasecmp(attp[0], "href"))
				break;
		if (NULL == *attp) {
			fprintf(stderr, "%s: no href\n", arg->src);
			XML_StopParser(arg->p, 0);
			return;
		}
		if (NULL == (start = strcasestr(attp[1], "://"))) {
			fprintf(stderr, "%s: bad uri\n", arg->src);
			XML_StopParser(arg->p, 0);
			return;
		}
		strlcpy(arg->domain, start + 3, MAXHOSTNAMELEN);
		if (NULL == (cp = strchr(arg->domain, '/'))) {
			fprintf(stderr, "%s: bad uri\n", arg->src);
			XML_StopParser(arg->p, 0);
			return;
		}
		strlcpy(arg->path, cp, MAXPATHLEN);
		*cp = '\0';
		if (NULL == (cp = strrchr(arg->path, '/'))) {
			fprintf(stderr, "%s: bad uri\n", arg->src);
			XML_StopParser(arg->p, 0);
			return;
		}
		*cp = '\0';
		return;
	} else if (strcasecmp(name, "entry")) {
		xmlprint(arg->f, name, atts);
		return;
	}

	for (attp = atts; NULL != *attp; attp++)
		if (0 == strcasecmp(*attp, "data-sblg-entry"))
			break;

	if (NULL == *attp) {
		xmlprint(arg->f, name, atts);
		return;
	}

	arg->stack++;
	XML_SetDefaultHandler(arg->p, NULL);
	if (arg->sposz > arg->spos) {
		xmlprint(arg->f, name, atts);
		XML_SetDefaultHandler(arg->p, NULL);
		XML_SetElementHandler(arg->p, entry_begin, entry_end);
		atomprint(arg->f, arg, &arg->sargs[arg->spos++]);
	} else {
		XML_SetElementHandler(arg->p, entry_begin, entry_empty);
	}
}

static void
entry_empty(void *userdata, const XML_Char *name)
{
	struct atom	*arg = userdata;

	if (0 == strcasecmp(name, "entry") && 0 == --arg->stack) {
		XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
		XML_SetDefaultHandler(arg->p, tmpl_text);
	}
}

static void
tmpl_end(void *userdata, const XML_Char *name)
{
	struct atom	*arg = userdata;

	if ( ! xmlvoid(name))
		fprintf(arg->f, "</%s>", name);
}

static void
id_end(void *userdata, const XML_Char *name)
{
	struct atom	*arg = userdata;

	if (0 == strcasecmp(name, "id") && 0 == --arg->stack) {
		fprintf(arg->f, "tag:%s,2013:%s/%s</%s>", 
			arg->domain, arg->path, arg->dst, name);
		XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
		XML_SetDefaultHandler(arg->p, tmpl_text);
	}
}

static void
up_end(void *userdata, const XML_Char *name)
{
	struct atom	*arg = userdata;

	if (0 == strcasecmp(name, "updated") && 0 == --arg->stack) {
		fprintf(arg->f, "</%s>", name);
		XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
		XML_SetDefaultHandler(arg->p, tmpl_text);
	}
}

static void
entry_end(void *userdata, const XML_Char *name)
{
	struct atom	*arg = userdata;

	if (0 == strcasecmp(name, "entry") && 0 ==  --arg->stack) {
		fprintf(arg->f, "</%s>", name);
		XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
		XML_SetDefaultHandler(arg->p, tmpl_text);
	}
}

static int
scmp(const void *p1, const void *p2)
{
	const struct article *s1 = p1, *s2 = p2;

	return(difftime(s2->time, s1->time));
}