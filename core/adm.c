#include <stdio.h>
#include <stdlib.h>

#include "options.h"
#include "sys.h"
#include "log.h"
#include "comm.h"


#define NAME "hakit-adm"

//===================================================
// Command line arguments
//==================================================

const char *options_summary = "HAKit comm test";

const options_entry_t options_entries[] = {
	{ "debug",  'd', 0, OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "hosts",  'H', 0, OPTIONS_TYPE_STRING, &opt_hosts, "Comma-separated list of explicit host names", "HOST" },
	{ NULL }
};



static void sink_event(void *user_data, char *name, char *value)
{
	log_str("-- %s='%s'", name, value);
}


int main(int argc, char *argv[])
{
	log_init(NAME);

	if (options_parse(&argc, argv, NULL) != 0) {
		exit(1);
	}

	sys_init();

	if (comm_init()) {
		exit(2);
	}

	comm_monitor((comm_sink_func_t) sink_event, NULL);

	sys_run();

	return 0;
}
