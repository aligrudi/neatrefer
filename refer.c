/*
 * NEATREFER - A REFER CLONE FOR NEATROFF
 *
 * Copyright (C) 2011-2017 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NREFS		(1 << 14)
#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

struct ref {
	char *keys[128];	/* reference keys */
	char *auth[128];	/* authors */
	int id;			/* allocated reference id */
	int nauth;
};

static struct ref refs[NREFS];	/* all references in refer database */
static int refs_n;
static struct ref *cites[NREFS];	/* cited references */
static int cites_n = 1;
static int inserted;		/* number of inserted references */
static int multiref;		/* allow specifying multiple references */
static int accumulate;		/* accumulate all references */
static int initials;		/* initials for authors' first name */
static int refauth;		/* use author-year citations */
static char *refmac;		/* citation macro name */
static FILE *refdb;		/* the database file */

#define ref_label(ref)		((ref)->keys['L'])

/* the next input line */
static char *lnget(void)
{
	static char buf[1024];
	return fgets(buf, sizeof(buf), stdin);
}

/* write an output line */
static void lnput(char *s, int n)
{
	write(1, s, n >= 0 ? n : strlen(s));
}

/* the next refer database input line */
static char *dbget(void)
{
	static char buf[1024];
	return refdb ? fgets(buf, sizeof(buf), refdb) : NULL;
}

static char *sdup(char *s)
{
	char *e = strchr(s, '\n') ? strchr(s, '\n') : strchr(s, '\0');
	char *r;
	int n = e - s;
	r = malloc(n + 1);
	memcpy(r, s, n);
	r[n] = '\0';
	return r;
}

/* format author names as J. Smith */
static char *ref_author(char *ref)
{
	char *res;
	char *out;
	char *beg;
	if (!initials)
		return sdup(ref);
	res = malloc(strlen(ref) + 32);
	out = res;
	while (1) {
		while (*ref == ' ' || *ref == '.')
			ref++;
		if (*ref == '\0')
			break;
		beg = ref;
		while (*ref && *ref != ' ' && *ref != '.')
			ref++;
		if (out != res)
			*out++ = ' ';
		if (islower((unsigned char) *beg) || *ref == '\0') {
			while (beg < ref)
				*out++ = *beg++;
		} else {				/* initials */
			do {
				*out++ = *beg++;
				*out++ = '.';
				while (beg < ref && *beg != '-')
					beg++;
				if (*beg == '-')	/* handling J.-K. Smith */
					*out++ = *beg++;
			} while (beg < ref);
		}
	}
	*out = '\0';
	return res;
}

/* strip excess whitespace */
static void rstrip(char *s)
{
	int i;
	int last = -1;
	for (i = 0; s[i]; i++)
		if (s[i] != ' ' && s[i] != '\n')
			last = i;
	s[last + 1] = '\0';
}

/* read a single refer record */
static void db_ref(struct ref *ref, char *ln)
{
	do {
		if (ln[0] == '%' && ln[1] >= 'A' && ln[1] <= 'Z') {
			char *r = ln + 2;
			while (isspace((unsigned char) *r))
				r++;
			rstrip(r);
			if (ln[1] == 'A')
				ref->auth[ref->nauth++] = ref_author(r);
			else
				ref->keys[(unsigned char) ln[1]] = sdup(r);
		}
	} while ((ln = dbget()) && ln[0] != '\n');
}

/* parse a refer-style bib file and fill refs[] */
static int db_parse(void)
{
	char *ln;
	while ((ln = dbget()))
		if (ln[0] != '\n')
			db_ref(&refs[refs_n++], ln);
	return 0;
}

static char fields[] = "LTABRJDVNPITO";
static char fields_flag[] = "OP";
static char *kinds[] = {"Other", "Article", "Book", "In book", "Report"};

static int ref_kind(struct ref *r)
{
	if (r->keys['J'])
		return 1;
	if (r->keys['B'])
		return 3;
	if (r->keys['I'])
		return 2;
	if (r->keys['R'])
		return 4;
	return 0;
}

/* print the given reference */
static void ref_ins(struct ref *ref, int id)
{
	char buf[1 << 12];
	char *s = buf;
	int kind = ref_kind(ref);
	int j;
	s += sprintf(s, ".ds [F %d\n", id);
	s += sprintf(s, ".]-\n");
	if (ref->nauth) {
		s += sprintf(s, ".ds [A ");
		for (j = 0; j < ref->nauth; j++)
			s += sprintf(s, "%s%s", j ? ", " : "", ref->auth[j]);
		s += sprintf(s, "\n");
	}
	for (j = 'B'; j <= 'Z'; j++) {
		char *val = ref->keys[j];
		if (!val || !strchr(fields, j))
			continue;
		s += sprintf(s, ".ds [%c %s\n", j, val ? val : "");
		if (strchr(fields_flag, j))
			s += sprintf(s, ".nr [%c 1\n", j);
	}
	s += sprintf(s, ".][ %d %s\n", kind, kinds[kind]);
	lnput(buf, s - buf);
}

/* print all references */
static void ref_all(void)
{
	int i;
	lnput(".]<\n", -1);
	for (i = 1; i < cites_n; i++)
		ref_ins(cites[i], i);
	lnput(".]>", -1);
}

static int intcmp(void *v1, void *v2)
{
	return *(int *) v1 - *(int *) v2;
}

/* the given label was referenced; add it to cites[] */
static int refer_seen(char *label)
{
	int i;
	for (i = 0; i < refs_n; i++)
		if (ref_label(&refs[i]) && !strcmp(label, ref_label(&refs[i])))
			break;
	if (i == refs_n)
		return -1;
	if (!refs[i].id) {
		refs[i].id = cites_n++;
		cites[refs[i].id] = &refs[i];
	}
	return refs[i].id;
}

static char *refer_lastname(char *name)
{
	char *last = name;
	while (*name) {
		if (!islower((unsigned char) last[0]))
			last = name;
		while (*name && *name != ' ')
			if (*name++ == '\\')
				name++;
		while (*name == ' ')
			name++;
	}
	return last;
}

static void refer_quote(char *d, char *s)
{
	if (!strchr(s, ' ') && s[0] != '"') {
		strcpy(d, s);
	} else {
		*d++ = '"';
		while (*s) {
			if (*s == '"')
				*d++ = '"';
			*d++ = *s++;
		}
		*d++ = '"';
		*d = '\0';
	}
}

/* replace .[ .] macros with reference numbers */
static void refer_cite(char *s)
{
	char msg[256];
	char label[256];
	int id[256];
	int nid = 0;
	int i = 0;
	while (!nid || multiref) {
		char *r = label;
		while (*s && strchr(" \t\n,", (unsigned char) *s))
			s++;
		while (*s && !strchr(" \t\n,]", (unsigned char) *s))
			*r++ = *s++;
		*r = '\0';
		if (!strcmp("$LIST$", label)) {
			ref_all();
			break;
		}
		id[nid] = refer_seen(label);
		if (id[nid] < 0)
			fprintf(stderr, "refer: <%s> not found\n", label);
		else
			nid++;
		if (!*s || *s == '\n' || *s == ']')
			break;
	}
	if (!refauth) {		/* numbered citations */
		/* sort references for cleaner reference intervals */
		qsort(id, nid, sizeof(id[0]), (void *) intcmp);
		msg[0] = '\0';
		while (i < nid) {
			int beg = i++;
			/* reading reference intervals */
			while (i < nid && id[i] == id[i - 1] + 1)
				i++;
			if (beg)
				sprintf(msg + strlen(msg), ",");
			if (beg == i - 1)
				sprintf(msg + strlen(msg), "%d", id[beg]);
			else
				sprintf(msg + strlen(msg), "%d%s%d",
					id[beg], beg < i - 2 ? "\\-" : ",", id[i - 1]);
		}
	} else {		/* year + authors citations */
		struct ref *cur = cites[id[0]];
		sprintf(msg, "%s %d", cur->keys['D'] ? cur->keys['D'] : "-", cur->nauth);
		for (i = 0; i < cur->nauth; i++) {
			sprintf(msg + strlen(msg), " ");
			refer_quote(msg + strlen(msg), refer_lastname(cur->auth[i]));
		}
	}
	lnput(msg, -1);
	if (!accumulate)
		for (i = 0; i < nid; i++)
			ref_ins(cites[id[i]], ++inserted);
}

static int slen(char *s, int delim)
{
	char *r = strchr(s, delim);
	return r ? r - s : strchr(s, '\0') - s;
}

static int refer_reqname(char *mac, int maclen, char *s)
{
	int i = 0;
	if (*s++ != '.')
		return 1;
	for (i = 0; i < maclen && *s && *s != ' '; i++)
		mac[i] = *s++;
	mac[i] = '\0';
	return *s != ' ';
}

static int refer_macname(char *mac, int maclen, char *s)
{
	int i = 0;
	if (*s++ != '\\')
		return 1;
	if (*s++ != '*')
		return 1;
	if (*s++ != '[')
		return 1;
	for (i = 0; i < maclen && *s && *s != ' '; i++)
		mac[i] = *s++;
	mac[i] = '\0';
	return *s != ' ';
}

static void refer(void)
{
	char mac[256];
	char *s, *r, *ln;
	while ((ln = lnget())) {
		/* multi-line citations: .[ rudi17 .] */
		if (ln[0] == '.' && ln[1] == '[') {
			lnput(ln + 2, slen(ln + 2, '\n'));
			if ((ln = lnget())) {
				refer_cite(ln);
				while (ln && (ln[0] != '.' || ln[1] != ']'))
					ln = lnget();
				if (ln)
					lnput(ln + 2, -1);
			}
			continue;
		}
		/* single line citation .cite rudi17 */
		if (ln[0] == '.' && !refer_reqname(mac, sizeof(mac), ln) &&
				strstr(refmac, mac)) {
			int i = 1;
			while (ln[i] && ln[i] != ' ')
				i++;
			while (ln[i] && ln[i] == ' ')
				i++;
			lnput(ln, i);
			refer_cite(ln + i);
			while (ln[i] && ln[i] != ' ' && ln[i] != '\n')
				i++;
			lnput(ln + i, -1);
			continue;
		}
		s = ln;
		r = s;
		/* inline citations \*[cite rudi17] */
		while ((r = strchr(r, '\\'))) {
			r++;
			if (refer_macname(mac, sizeof(mac), r - 1))
				continue;
			if (!strstr(refmac, mac))
				continue;
			if (!strchr(r, ']'))
				continue;
			r = strchr(r, ' ') + 1;
			lnput(s, r - s);
			refer_cite(r);
			while (*r && *r != ' ' && *r != ']')
				r++;
			s = r;
		}
		lnput(s, -1);
	}
}

static char *usage =
	"Usage neatrefer [options] <input >output\n"
	"Options:\n"
	"\t-p bib    \tspecify the database file\n"
	"\t-e        \taccumulate references\n"
	"\t-m        \tmerge multiple references in a single .[/.] block\n"
	"\t-i        \tinitials for authors' first and middle names\n"
	"\t-o xy     \tcitation macro (\\*[xy label])\n"
	"\t-a        \tuse author-year citation style\n";

int main(int argc, char *argv[])
{
	int i, j;
	for (i = 1; i < argc; i++) {
		switch (argv[i][0] == '-' ? argv[i][1] : 'h') {
		case 'm':
			multiref = 1;
			break;
		case 'e':
			accumulate = 1;
			break;
		case 'p':
			refdb = fopen(argv[i][2] ? argv[i] + 2 : argv[++i], "r");
			if (refdb) {
				db_parse();
				fclose(refdb);
			}
			refdb = NULL;
			break;
		case 'o':
			refmac = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 'i':
			initials = 1;
			break;
		case 'a':
			refauth = 1;
			break;
		default:
			printf("%s", usage);
			return 1;
		}
	}
	if (refauth && multiref) {
		fprintf(stderr, "refer: cannot use -m with -a\n");
		return 1;
	}
	refer();
	for (i = 0; i < refs_n; i++)
		for (j = 0; j < LEN(refs[i].keys); j++)
			if (refs[i].keys[j])
				free(refs[i].keys[j]);
	for (i = 0; i < refs_n; i++)
		for (j = 0; j < LEN(refs[i].auth); j++)
			if (refs[i].auth[j])
				free(refs[i].auth[j]);
	return 0;
}
