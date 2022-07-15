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
	int matches;
};

static struct ref refs[NREFS];	/* all references in refer database */
static int refs_n;
static struct ref *cites[NREFS];	/* cited references */
static int cites_n;
static int inserted;		/* number of inserted references */
static int multiref;		/* allow specifying multiple references */
static int accumulate;		/* accumulate all references */
static int initials;		/* initials for authors' first name */
static int refauth;		/* use author-year citations */
static int sortall;		/* sort references */
static char *refmac;		/* citation macro name */
static char *refmac_auth;	/* author-year citation macro name */
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
	// 0, 1, 2: stdin, stdout, strerr respectively
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
			ref->id = -1;
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

static char fields[] = "LTABERJDVNPITOH";
static char fields_flag[] = "OP";
static char *kinds[] = {"Other", "Article", "Book", "In book", "Report"};

static int ref_kind(struct ref *r)
{
	if (r->keys['J'])
		return 1;
	if (r->keys['B'])
		return 3;
	if (r->keys['R'])
		return 4;
	if (r->keys['I'])
		return 2;
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

static char *lastname(char *name)
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

static int refcmp(struct ref *r1, struct ref *r2)
{
	if (!r2->nauth || (r1->keys['H'] && !r2->keys['H']))
		return -1;
	if (!r1->nauth || (!r1->keys['H'] && r2->keys['H']))
		return 1;
	return strcmp(lastname(r1->auth[0]), lastname(r2->auth[0]));
}

/* print all references */
static void ref_all(void)
{
	int i, j;
	struct ref **sorted;
	sorted = malloc(cites_n * sizeof(sorted[0]));
	memcpy(sorted, cites, cites_n * sizeof(sorted[0]));
	if (sortall == 'a') {
		for (i = 1; i < cites_n; i++) {
			for (j = i - 1; j >= 0 && refcmp(cites[i], sorted[j]) < 0; j--)
				sorted[j + 1] = sorted[j];
			sorted[j + 1] = cites[i];
		}
	}
	lnput(".]<\n", -1);
	for (i = 0; i < cites_n; i++)
		ref_ins(sorted[i], sorted[i]->id + 1);
	lnput(".]>", -1);
	free(sorted);
}

static int intcmp(void *v1, void *v2)
{
	return *(int *) v1 - *(int *) v2;
}

/* iterates over str, tries to match pattern,
   returns score of match
*/
static float getScore(char *str, char *pat)
{
	char *ctxt;
	float score = 0;
	float sat = 3;

	// keep original str because of strtok
	char tmp[strlen(str) + 1];
	strcpy(tmp, str);

	for (char *tok = strtok_r(tmp, " ", &ctxt);
			tok != NULL;
			tok = strtok_r(NULL, " ", &ctxt))
	{
		if (!strcmp(tok, pat)) {
			score += 1;
		}
	}

	if (score > 1) {
		// for every score above 1, divide by saturation (sat)
		score = ((score - 1) / sat) + 1;
	}
	return score;
}

static void strlower(char *str) {
	for (int i = 0; i < strlen(str); ++i) {
		str[i] = tolower(str[i]);
	}
}


/* the given label was referenced; add it to cites[]
   for each keyword(keys), get score, if duplicate score
   return first match,
   if not found, return -1*/
static int refer_seen(char *keywords)
{
	// keep original keywords because strtok
	char *ctxt;
	char keys[strlen(keywords) + 1];
	strcpy(keys, keywords);

	strlower(keys);
	float scores[refs_n];
	memset(scores, 0, refs_n);
	float lst = 0; // largest score
	int matches = 0;
	int idx = -1;

	// get score for each keyword
	for (char *tok = strtok_r(keys, " ", &ctxt);
			tok != NULL;
			tok = strtok_r(NULL, " ", &ctxt))
	{
		for (int i = 0; i < refs_n; ++i) {
			char *label = ref_label(&refs[i]);
			strlower(label);
			scores[i] += getScore(label, tok);
			if (scores[i] > lst) {
				lst = scores[i];
				idx = i;
				matches = 0;
			} 	else if (scores[i] == lst) {
				matches += 1;
			}
		}
	}

	if (idx == -1) {
		return -1;
	}

	if (matches > 1) {
		fprintf(stderr,
				"%d matches for keywords <%s> found, using first\n",
				matches, keys);
	}

	int *iden = &(refs[idx].id);
	if (*iden < 0) {
		*iden = cites_n++;
		cites[*iden] = &refs[idx]; // cites pointer = &refs
	}
	return *iden;
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

/* replace .[ .] macros with reference numbers or author-year */
static int refer_cite(int *id, char *s, int auth)
{
	char msg[256];
	// keywords: -o citation arguments
	char keywds[256];
	int nid = 0;
	int i = 0;
	msg[0] = '\0';
	while (!nid || multiref) {
		char *r = keywds;
		while (*s && strchr(" \t\n,", (unsigned char) *s))
			s++;
		while (*s && !strchr("\t\n,]", (unsigned char) *s))
			*r++ = *s++;
		*r = '\0';
		if (!strcmp("$LIST$", keywds)) {
			ref_all();
			break;
		}
		id[nid] = refer_seen(keywds);
		if (id[nid] < 0)
			fprintf(stderr, "refer: <%s> not found\n", keywds);
		else
			nid++;
		if (!*s || *s == '\n' || *s == ']')
			break;
	}
	if (!auth) {		/* numbered citations */
		/* sort references for cleaner reference intervals */
		qsort(id, nid, sizeof(id[0]), (void *) intcmp);
		while (i < nid) {
			int beg = i++;
			/* reading reference intervals */
			while (i < nid && id[i] == id[i - 1] + 1)
				i++;
			if (beg)
				sprintf(msg + strlen(msg), ",");
			if (beg == i - 1)
				sprintf(msg + strlen(msg), "%d", id[beg] + 1);
			else
				sprintf(msg + strlen(msg), "%d%s%d",
					id[beg] + 1, beg < i - 2 ? "\\-" : ",", id[i - 1] + 1);
		}
	} else if (nid) {	/* year + authors citations */
		struct ref *ref = cites[id[0]];
		sprintf(msg, "%s %d", ref->keys['D'] ? ref->keys['D'] : "-", ref->nauth);
		for (i = 0; i < ref->nauth; i++) {
			sprintf(msg + strlen(msg), " ");
			refer_quote(msg + strlen(msg), lastname(ref->auth[i]));
		}
	}
	lnput(msg, -1);
	return nid;
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

/* return 1 if mac is a citation macro */
static int refer_refmac(char *pat, char *mac)
{
	char *s = pat ? strstr(pat, mac) : NULL;
	if (!mac[0] || !s)
		return 0;
	return (s == pat || s[-1] == ',') &&
		(!s[strlen(mac)] || s[strlen(mac)] == ',');
}

static void refer_insert(int *id, int id_n)
{
	int i;
	for (i = 0; i < id_n; i++)
		ref_ins(cites[id[i]], ++inserted);
}

static void refer(void)
{
	char mac[256];
	int id[256];
	char *s, *r, *ln;
	while ((ln = lnget())) {
		int id_n = 0;
		/* multi-line citations: .[ rudi17 .] */
		if (ln[0] == '.' && ln[1] == '[') {
			lnput(ln + 2, slen(ln + 2, '\n'));
			if ((ln = lnget())) {
				id_n = refer_cite(id, ln, 0);
				while (ln && (ln[0] != '.' || ln[1] != ']'))
					ln = lnget();
				if (ln)
					lnput(ln + 2, -1);
			}
			if (!accumulate)
				refer_insert(id, id_n);
			continue;
		}
		/* single line citation .cite rudi17 */
		// checks for .cite
		if (ln[0] == '.' && !refer_reqname(mac, sizeof(mac), ln) &&
				(refer_refmac(refmac, mac) || refer_refmac(refmac_auth, mac))) {
			int i = 1;
			while (ln[i] && ln[i] != ' ')
				i++;
			while (ln[i] && ln[i] == ' ')
				i++;
			// write to stdout
			lnput(ln, i);
			id_n = refer_cite(id, ln + i, refer_refmac(refmac_auth, mac));
			while (ln[i] && ln[i] != '\n')
				i++;
			lnput(ln + i, -1);
			if (!accumulate)
				refer_insert(id, id_n);
			continue;
		}
		s = ln;
		r = s;
		/* inline citations \*[cite rudi17] */
		while ((r = strchr(r, '\\'))) {
			r++;
			if (refer_macname(mac, sizeof(mac), r - 1))
				continue;
			if (!refer_refmac(refmac, mac) && !refer_refmac(refmac_auth, mac))
				continue;
			if (!strchr(r, ']'))
				continue;
			r = strchr(r, ' ') + 1;
			lnput(s, r - s);
			id_n = refer_cite(id, r, refer_refmac(refmac_auth, mac));
			while (*r && *r != ' ' && *r != ']')
				r++;
			s = r;
		}
		lnput(s, -1);
		if (!accumulate)
			refer_insert(id, id_n);
	}
}

static char *usage =
	"Usage neatrefer [options] <input >output\n"
	"Options:\n"
	"\t-p bib	 \tspecify the database file\n"
	"\t-e		 \taccumulate references\n"
	"\t-m		 \tmerge multiple references in a single .[/.] block\n"
	"\t-i		 \tinitials for authors' first and middle names\n"
	"\t-o xy	 \tcitation macro (\\*[xy label])\n"
	"\t-a xy	 \tauthor-year citation macro (\\*[xy label])\n"
	"\t-sa		 \tsort by author last names\n";

int test() {
	char pat[] = " eat in";
	printf("result: %d\n", refer_seen(pat));
	return 0;
}

int main(int argc, char *argv[])
{
	/* return test(); */
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
			refmac_auth = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		case 's':
			sortall = (unsigned char) (argv[i][2] ? argv[i][2] : argv[++i][0]);
			break;
		default:
			fprintf(stderr, "%s", usage);
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
