#include "scp294.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* s_text;
static char** s_line;
static int s_nlines;

static void load(void)
{
	if (s_text) return;
	FILE* f = fopen("romfs:/294.ini", "rb");
	if (!f) {
		s_text = calloc(1, 1);
		return;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	s_text = malloc(n + 1);
	n = (long)fread(s_text, 1, n, f);
	s_text[n] = 0;
	fclose(f);
	int count = 1;
	for (long i = 0; i < n; i++) count += s_text[i] == '\n';
	s_line = malloc(sizeof(char*) * count);
	char* p = s_text;
	while (p && *p) {
		char* e = strchr(p, '\n');
		if (e) *e = 0;
		size_t l = strlen(p);
		while (l && (p[l - 1] == '\r' || p[l - 1] == ' ' || p[l - 1] == '\t')) p[--l] = 0;
		s_line[s_nlines++] = p;
		p = e ? e + 1 : NULL;
	}
}

static void trimLower(const char* in, char* out, int size)
{
	while (*in == ' ') in++;
	int n = 0;
	for (; *in && n < size - 1; in++) out[n++] = (char)tolower((unsigned char)*in);
	while (n && out[n - 1] == ' ') n--;
	out[n] = 0;
}

int scp294Find(const char* input)
{
	load();
	char want[64];
	trimLower(input, want, sizeof(want));
	const char* w = want;
	if (!strncmp(w, "cup of ", 7)) w += 7;
	else if (!strncmp(w, "a cup of ", 9)) w += 9;
	if (!*w) return -1;
	for (int i = 0; i < s_nlines; i++) {
		const char* l = s_line[i];
		if (l[0] != '[') continue;

		const char* p = l + 1;
		while (*p && *p != ']') {
			char name[64];
			int n = 0;
			while (*p && *p != '|' && *p != ']' && n < 63) name[n++] = (char)tolower((unsigned char)*p++);
			name[n] = 0;
			if (!strcmp(name, w)) return i;
			if (*p == '|') p++;
		}
	}
	return -1;
}

const char* scp294Get(int drink, const char* key)
{
	load();
	if (drink < 0 || drink >= s_nlines) return "";
	size_t kl = strlen(key);
	for (int i = drink + 1; i < s_nlines && s_line[i][0] != '['; i++) {
		const char* l = s_line[i];
		if (strncasecmp(l, key, kl) != 0) continue;
		const char* p = l + kl;
		while (*p == ' ') p++;
		if (*p != '=') continue;
		p++;
		while (*p == ' ') p++;
		return p;
	}
	return "";
}
