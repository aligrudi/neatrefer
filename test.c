#include <stdio.h>
#include <string.h>

int test(char *s)
{
	char oth[strlen(s) + 1];
	char *r = oth;
	while (*s && strchr(" \t\n,", (unsigned char) *s))
		s++;
	while (*s && !strchr("\t\n,]", (unsigned char) *s))
		*r++ = *s++;
	*r = '\0';
	for (char *tok = strtok(oth, " "); tok != NULL; tok = strtok(NULL, " ")) {
		printf("%s\n", tok);
	}
	return 0;
}

int main(void)
{
	char s[] = " this is my string\n";
	return test(s);
}

