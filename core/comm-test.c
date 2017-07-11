#include <stdio.h>
#include <stdlib.h>

#include "options.h"
#include "sys.h"
#include "log.h"
#include "comm.h"


#define NAME "hakit-comm-test"


//===================================================
// Command line arguments
//==================================================

int opt_mode = 0;

const char *options_summary = "HAKit comm test";

static const options_entry_t options_entries[] = {
	{ "debug",  'd', 0, OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "mode",   'm', 0, OPTIONS_TYPE_INT,  &opt_mode,    "Test mode" },
	{ NULL }
};


static void sink_event(void *user_data, char *name, char *value)
{
	log_str("-- sink_event %s='%s'", name, value);
}


int main(int argc, char *argv[])
{
	log_init(NAME);

	if (options_parse(options_entries, &argc, argv) != 0) {
		exit(1);
	}

	sys_init();

	if (comm_init()) {
		exit(2);
	}

	log_str("Test mode: %d", opt_mode);

	if (opt_mode) {
		comm_sink_register("A", (comm_sink_func_t) sink_event, NULL);
		comm_source_register("B", 0);
	}
	else {
		comm_sink_register("B", (comm_sink_func_t) sink_event, NULL);
		comm_source_register("A", 0);
	}

	sys_run();

	return 0;
}
