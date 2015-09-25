/*
 * HAKit - The Home Automation Kit - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * Signal history tool
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>


#define HISTORY_DIR "/tmp"
#define HISTORY_FILE_PREFIX "hakit-history-"


typedef struct {
	char *name;
	int id;
} history_sym_t;


static long long history_read_value(unsigned char *buf, int len)
{
	long long value = (buf[0] & 0x80) ? -1:0;
	int i;

	for (i = 0; i < len; i++) {
		int sft = 8 * (len-i-1);
		unsigned long long mask0 = 0xFFULL << sft;
		unsigned long long mask = (((unsigned long long) buf[i]) << sft);
		value = (value & ~mask0) | mask;
	}

	return value;
}


static char *history_read_str(FILE *fin)
{
	char *str = NULL;
	int size = 0;
	int len = 0;

	while (!feof(fin)) {
		int c = fgetc(fin);
		if (c < 0) {
			return NULL;
		}

		if (size < (len+1)) {
			size += 20;
			str = realloc(str, size);
		}

		str[len] = c;

		/* NUL termination character reached: we return string */
		if (c == 0) {
			return str;
		}

		len++;
	}

	/* if we reach this point, the NUL termination character was not found,
	   so we return with an error */

	if (str != NULL) {
		free(str);
		str = NULL;
	}

	return str;
}


static void history_dump_tstamp(FILE *fout, long long tstamp)
{
	time_t t = tstamp;
	struct tm *lt;
	char str[20];

	lt = localtime(&t);
	strftime(str, sizeof(str), "%F %T", lt);
	fputs(str, fout);
	fputs(" ", fout);
}


static void history_dump_sym(FILE *fout, history_sym_t *sym)
{
	if (sym == NULL) {
		fputs("????", fout);
	}
	else {
		fputs(sym->name, fout);
	}

	fputs(" = ", fout);
}


static int history_dump_file(char *fname, FILE *fout)
{
	FILE *fin;
	long long tstamp = 0;
	history_sym_t *syms = NULL;
	int nsyms = 0;
	history_sym_t *cur_sym = NULL;
	int ret = -1;

	fprintf(fout, "# History file: %s\n", fname);

	fin = fopen(fname, "r");
	if (fin == NULL) {
		fprintf(stderr, "ERROR: Cannot open file '%s': %s\n", fname, strerror(errno));
		return -1;
	}

	while (!feof(fin)) {
		int c = fgetc(fin);
		if (c < 0) {
			if (errno) {
				fprintf(stderr, "ERROR: Cannot read file '%s' (op): %s\n", fname, strerror(errno));
				goto failed;
			}
			break;
		}
		
		unsigned char op = c;

		if (op & 0x80) {
			unsigned char v = op & 0x3F;

			if (op & 0x40) {  // Set relative time stamp
				tstamp += v;
			}
			else {  // Log short value
				history_dump_tstamp(fout, tstamp);
				history_dump_sym(fout, cur_sym);
				fprintf(fout, "%u\n", v);
			}
		}
		else {
			int len = (op >> 4) + 1;
			unsigned char buf[len];
			int id;
			char *str = NULL;
			long long dt;
			int i;

			if (len > 0) {
				int rlen = fread(buf, 1, len, fin);
				if (rlen < 0) {
					fprintf(stderr, "ERROR: Cannot read file '%s' (value): %s\n", fname, strerror(errno));
					goto failed;
				}
			}

			switch (op & 0x0F) {
			case 0x00:  // Declare signal
				id = history_read_value(buf, len);
				str = history_read_str(fin);
				if (str == NULL) {
					fprintf(stderr, "ERROR: Cannot read file '%s' (string): %s\n", fname, strerror(errno));
					goto failed;
				}

				//fprintf(fout, "# Declare signal '%s' as %d\n", str, id);

				/* Feed table of symbols */
				syms = realloc(syms, sizeof(history_sym_t) * (nsyms+1));
				cur_sym = &syms[nsyms++];
				cur_sym->name = str;
				cur_sym->id = id;
				break;
			case 0x01:  // Select signal
				id = history_read_value(buf, len);
				//fprintf(fout, "# Select signal %d\n", id);

				for (i = 0; i < nsyms; i++) {
					if (syms[i].id == id) {
						cur_sym = &syms[i];
						break;
					}
				}
				break;
			case 0x02:  // Set absolute time stamp
				tstamp = history_read_value(buf, len);
				//fprintf(fout, "# Absolute time stamp: %lld\n", tstamp);
				break;
			case 0x03:  // Set relative time stamp
				dt = history_read_value(buf, len);
				tstamp += dt;
				//fprintf(fout, "# Relative time stamp: +%lld => %lld\n", dt, tstamp);
				break;
			case 0x04:  // Log long value
				history_dump_tstamp(fout, tstamp);
				history_dump_sym(fout, cur_sym);
				fprintf(fout, "%lld\n", history_read_value(buf, len));
				break;
			case 0x05:  // Log string value
				str = history_read_str(fin);
				history_dump_tstamp(fout, tstamp);
				history_dump_sym(fout, cur_sym);
				fprintf(fout, "\"%s\"\n", str);
				free(str);
				break;
			default:
				break;
			}
		}
	}

	ret = 0;

failed:
	fclose(fin);

	if (syms != NULL) {
		int i;

		for (i = 0; i < nsyms; i++) {
			free(syms[i].name);
		}

		free(syms);
	}

	return ret;
}


static int qsort_str(const void *p1, const void *p2)
{
	return strcmp(*((char **) p1), *((char **) p2));
}


static int history_dump(FILE *fout)
{
	DIR *d;
	struct dirent *ent;
	char **files = NULL;
	int nfiles = 0;
	int i;

	d = opendir(HISTORY_DIR);
	if (d == NULL) {
		fprintf(stderr, "ERROR: Cannot access directory '" HISTORY_DIR "': %s\n", strerror(errno));
		return -1;
	}

	while ((ent = readdir(d)) != NULL) {
		if (strncmp(ent->d_name, HISTORY_FILE_PREFIX, strlen(HISTORY_FILE_PREFIX)) == 0) {
			int i = nfiles++;
			files = realloc(files, sizeof(char *) * nfiles);
			files[i] = strdup(ent->d_name);
		}
	}

	closedir(d);

	qsort(files, nfiles, sizeof(char *), qsort_str);

	for (i = 0; i < nfiles; i++) {
		char *fname = files[i];
		char path[strlen(HISTORY_DIR)+strlen(fname)+2];
		snprintf(path, sizeof(path), HISTORY_DIR "/%s", fname);
		history_dump_file(path, fout);
	}

	for (i = 0; i < nfiles; i++) {
		free(files[i]);
	}
	free(files);

	return 0;
}


int main(int argc, char *argv[])
{
	int ret;

	ret = history_dump(stdout);

	return ret ? 1:0;
}
