#include <stdio.h>
#include <stdlib.h>

#include "options.h"
#include "sys.h"
#include "log.h"
#include "comm.h"


#define NAME "hakit-test"


//===================================================
// Command line arguments
//==================================================

int opt_mode = 0;

const char *options_summary = "HAKit test";

const options_entry_t options_entries[] = {
	{ "debug",  'd', 0, OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "daemon", 'D', 0, OPTIONS_TYPE_NONE, &opt_daemon,  "Run in background as a daemon" },
	{ "mode",   'm', 0, OPTIONS_TYPE_INT,  &opt_mode,    "Test mode" },
	{ NULL }
};


static void sink_event(comm_t *comm, char *name, char *value)
{
	log_str("-- sink_event %s='%s'", name, value);
}


int main(int argc, char *argv[])
{
	comm_t comm;

	log_init(NAME);

	if (options_parse(&argc, argv, NULL) != 0) {
		exit(1);
	}

	if (comm_init(&comm, 5678)) {
		exit(2);
	}

	sys_init();

	log_str("Test mode: %d", opt_mode);

	if (opt_mode) {
		comm_sink_register(&comm, "A", (comm_sink_func_t) sink_event, &comm);
		comm_source_register(&comm, "B", 0);
	}
	else {
		comm_sink_register(&comm, "B", (comm_sink_func_t) sink_event, &comm);
		comm_source_register(&comm, "A", 0);
	}

	sys_run();

	return 0;
}
