#include <stdio.h>
#include <stdlib.h>

#include "options.h"
#include "sys.h"
#include "log.h"
#include "proc.h"


#define NAME "hakit-proc-test"


//===================================================
// Command line arguments
//==================================================

const char *options_summary = "HAKit proc test";

static const options_entry_t options_entries[] = {
	{ "debug",  'd', 0, OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "daemon", 'D', 0, OPTIONS_TYPE_NONE, &opt_daemon,  "Run in background as a daemon" },
	{ NULL }
};


static void proc_out_func(void *user_data, char *buf, int size)
{
	log_str("-- proc_out_func");
	log_debug_data((unsigned char *) buf, size);
}


static void proc_err_func(void *user_data, char *buf, int size)
{
	log_str("-- proc_err_func");
	log_debug_data((unsigned char *) buf, size);
}


static void proc_term_func(void *user_data, int status)
{
	log_str("-- proc_term_func %d", status);
	sys_quit();
}


int main(int argc, char *argv[])
{
	log_init(NAME);

	if (options_parse(options_entries, &argc, argv) != 0) {
		exit(1);
	}

	proc_start(argc-1, argv+1,
		   (proc_out_func_t) proc_out_func,
		   (proc_out_func_t) proc_err_func,
		   (proc_term_func_t) proc_term_func, NULL);

	sys_run();

	return 0;
}
