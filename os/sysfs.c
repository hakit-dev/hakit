#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "sysfs.h"


int sysfs_write_str(char *path, char *str)
{
	FILE *f = NULL;
	int ret = -1;

	f = fopen(path, "w");
	if (f == NULL) {
		log_str("ERROR: Cannot open (write) '%s': %s", path, strerror(errno));
		goto DONE;
	}

	if (fprintf(f, "%s\n", str) < 0) {
		log_str("ERROR: Cannot write '%s': %s", path, strerror(errno));
		goto DONE;
	}

	ret = 0;

DONE:
	if (f != NULL) {
		fclose(f);
	}

	return ret;
}


char sysfs_write_int(char *path, int v)
{
	char str[16];

	snprintf(str, sizeof(str), "%d", v);
	return sysfs_write_str(path, str);
}


int sysfs_read_str(char *path, char *buf, int size)
{
	FILE *f = NULL;
	int ret = -1;

	f = fopen(path, "r");
	if (f == NULL) {
		log_str("ERROR: Cannot open (read) '%s': %s", path, strerror(errno));
		goto DONE;
	}

	if (fgets(buf, size, f) == NULL) {
		log_str("ERROR: Cannot read '%s': %s", path, strerror(errno));
		goto DONE;
	}

	ret = 0;

DONE:
	if (f != NULL) {
		fclose(f);
	}

	return ret;
}


int sysfs_read_int(char *path)
{
	char buf[32];

	if (sysfs_read_str(path, buf, sizeof(buf))) {
		return -1;
	}

	return atoi(buf);
}

