/*
 * neatrefer - a small refer clone
 *
 * Copyright (C) 2011-2015 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the Modified BSD license.
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSZ		(1 << 21)
#define NREFS		(1 << 12)

struct ref {
	char *keys[128];	/* reference keys */
	char *auth[128];	/* authors */
	int id;			/* allocated reference id */
	int nauth;
};

static struct ref refs[NREFS];	/* all references in refer db */
static int nrefs;
static struct ref *added[NREFS];/* cited references */
static int nadded = 1;
static int inserted;		/* number of inserted references */
static int multiref;		/* allow specifying multiple references */
static int accumulate;		/* accumulate all references */

#define ref_label(ref)		((ref)->keys['L'])

static int xread(int fd, char *buf, int len)
{
	int nr = 0;
	while (nr < len) {
		int ret = read(fd, buf + nr, len - nr);
		if (ret <= 0)
			break;
		nr += ret;
	}
	return nr;
}

static int xwrite(int fd, char *buf, int len)
{
	int nw = 0;
	while (nw < len) {
		int ret = write(fd, buf + nw, len - nw);
		if (ret < 0)
			break;
		nw += ret;
	}
	return nw;
}

/* read a single refer record */
static char *db_ref(char *s, struct ref *ref)
{
	char *end;
	while (*s != '\n') {
		end = strchr(s, '\n');
		if (!end)
			return strchr(s, '\0');
		*end = '\0';
		if (s[0] == '%' && s[1] >= 'A' && s[1] <= 'Z') {
			char *r = s + 2;
			while (isspace(*r))
				r++;
			if (s[1] == 'A')
				ref->auth[ref->nauth++] = r;
			else
				ref->keys[(unsigned) s[1]] = r;
		}
		s = end + 1;
		while (*s == ' ' || *s == '\t')
			s++;
	}
	return s;
}

/* parse a refer-style bib file and fill refs[] */
static int db_parse(char *s)
{
	while (*s) {
		while (isspace(*s))
			s++;
		s = db_ref(s, &refs[nrefs++]);
	}
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

static char list[BUFSZ];

/* print the given reference */
static void ins_ref(int fd, struct ref *ref, int id)
{
	char *s = list;
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
	xwrite(fd, list, s - list);
}

/* print all references */
static void ins_all(int fd)
{
	int i;
	char *beg = ".]<\n";
	char *end = ".]>";
	xwrite(fd, beg, strlen(beg));
	for (i = 1; i < nadded; i++)
		ins_ref(fd, added[i], i);
	xwrite(fd, end, strlen(end));
}

/* strcpy from s to d; ignore the initial jump chars and stop at stop chars */
static void cut(char *d, char *s, char *jump, char *stop)
{
	while (strchr(jump, *s))
		s++;
	while (*s && !strchr(stop, *s))
		*d++ = *s++;
	*d = '\0';
}

static int intcmp(void *v1, void *v2)
{
	return *(int *) v1 - *(int *) v2;
}

/* the given label was referenced; add it to added[] */
static int refer_seen(char *label)
{
	int i;
	for (i = 0; i < nrefs; i++)
		if (ref_label(&refs[i]) && !strcmp(label, ref_label(&refs[i])))
			break;
	if (i == nrefs)
		return -1;
	if (!refs[i].id) {
		refs[i].id = nadded++;
		added[refs[i].id] = &refs[i];
	}
	return refs[i].id;
}

/* replace .[ .] macros with reference numbers */
static void refer_cite(int fd, char *b, char *e)
{
	char msg[128];
	char label[128];
	char *s, *r;
	int id[128];
	int nid = 0;
	int i;
	/* parse to see what is inside .[ and .]*/
	s = strchr(b, '\n') + 1;
	while (!nid || multiref) {
		r = label;
		while (s < e && (isspace(*s) || *s == ','))
			s++;
		while (s < e && !isspace(*s) && *s != ',')
			*r++ = *s++;
		*r = '\0';
		if (s >= e)
			break;
		if (!strcmp("$LIST$", label)) {
			ins_all(fd);
			break;
		}
		id[nid] = refer_seen(label);
		if (id[nid] < 0)
			fprintf(stderr, "refer: <%s> not found\n", label);
		else
			nid++;
	}
	/* read characters after .[ */
	cut(msg, b + 2, "", "\n");
	/* sort references for cleaner reference intervals */
	qsort(id, nid, sizeof(id[0]), (void *) intcmp);
	i = 0;
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
	/* read characters after .] */
	cut(msg + strlen(msg), e + 2, "", "\n");
	xwrite(fd, msg, strlen(msg));
	xwrite(fd, "\n", 1);
	/* insert the reference if not accumulating them */
	if (!accumulate)
		for (i = 0; i < nid; i++)
			ins_ref(fd, added[id[i]], ++inserted);
}

/* read the input s and write refer output to fd */
static void refer(int fd, char *s)
{
	char *l = s;
	char *b, *e;
	while (*s) {
		char *r = strchr(s, '\n');
		if (!r)
			break;
		if (r[1] == '.' && r[2] == '[') {
			b = strchr(r + 1, '\n');
			e = strchr(b + 1, '\n');
			while (e && (e[1] != '.' || e[2] != ']'))
				e = strchr(e + 1, '\n');
			if (!e)
				break;
			xwrite(fd, l, r - l + 1);
			s = strchr(e + 1, '\n');
			l = s + 1;
			refer_cite(fd, r + 1, e + 1);
		}
		s = r + 1;
	}
	xwrite(fd, l, strchr(l, '\0') - l);
}

static char buf[BUFSZ];
static char bib[BUFSZ];

static char *usage =
	"Usage neatrefer [options] <input >output\n"
	"Options:\n"
	"\t-p bib    \tspecify the database file\n"
	"\t-e        \taccumulate references\n"
	"\t-m        \tmerge multiple references in a single .[/.] block\n";

int main(int argc, char *argv[])
{
	char *bfile = NULL;
	int i;
	for (i = 1; i < argc; i++) {
		switch (argv[i][0] == '-' ? argv[i][1] : 'h') {
		case 'm':
			multiref = 1;
			break;
		case 'e':
			accumulate = 1;
			break;
		case 'p':
			bfile = argv[i][2] ? argv[i] + 2 : argv[++i];
			break;
		default:
			printf("%s", usage);
			return 1;
		}
	}
	if (bfile) {
		int fd = open(bfile, O_RDONLY);
		xread(fd, bib, sizeof(bib) - 1);
		db_parse(bib);
		close(fd);
	}
	xread(0, buf, sizeof(buf) - 1);
	refer(1, buf);
	return 0;
}
