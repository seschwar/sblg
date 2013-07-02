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
#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <expat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "extern.h"

static	const char *elemvoid[] = {
	"area",
	"base",
	"br",
	"col",
	"command",
	"embed",
	"hr",
	"img",
	"input",
	"keygen",
	"link",
	"meta",
	"param",
	"source",
	"track",
	"wbr",
	NULL
};

int
mmap_open(const char *f, int *fd, char **buf, size_t *sz)
{
	struct stat	 st;

	*fd = -1;
	*buf = NULL;
	*sz = 0;

	if (-1 == (*fd = open(f, O_RDONLY, 0))) {
		warn("%s", f);
		goto out;
	} else if (-1 == fstat(*fd, &st)) {
		warn("%s", f);
		goto out;
	} else if ( ! S_ISREG(st.st_mode)) {
		warnx("%s: not a regular file", f);
		goto out;
	} else if (st.st_size >= (1U << 31)) {
		warnx("%s: too large", f);
		goto out;
	}

	*sz = (size_t)st.st_size;
	*buf = mmap(NULL, *sz, PROT_READ, MAP_FILE|MAP_SHARED, *fd, 0);

	if (NULL == *buf) {
		warn("%s", f);
		goto out;
	}

	return(1);
out:
	mmap_close(*fd, *buf, *sz);
	return(0);
}

void
mmap_close(int fd, void *buf, size_t sz)
{

	if (NULL != buf)
		munmap(buf, sz);
	if (-1 != fd)
		close(fd);
}

void
xmlprint(FILE *f, const XML_Char *s, const XML_Char **atts)
{

	fprintf(f, "<%s", s);
	for ( ; NULL != *atts; atts += 2)
		fprintf(f, " %s=\"%s\"", atts[0], atts[1]);
	if (xmlvoid(s))
		fprintf(f, " /");
	fputc('>', f);
}

int
xmlvoid(const XML_Char *s)
{
	const char	**cp;

	for (cp = (const char **)elemvoid; NULL != *cp; cp++)
		if (0 == strcasecmp(s, *cp))
			return(1);

	return(0);
}