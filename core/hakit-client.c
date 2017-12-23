/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>

#include "options.h"
#include "log.h"
#include "sys.h"
#include "tcpio.h"

#include "hakit_version.h"


//===================================================
// Command line arguments
//===================================================

const char *options_summary = "HAKit hkcp client " HAKIT_VERSION " (" ARCH ")";

static char *opt_certs = NULL;

static const options_entry_t options_entries[] = {
	{ "debug",   'd', 0, OPTIONS_TYPE_INT,    &opt_debug,   "Set debug level", "N" },
#ifdef WITH_SSL
	{ "certs",      'e', 0, OPTIONS_TYPE_STRING, &opt_certs,  "TLS/SSL certificate directory", "DIR" },
#endif
	{ NULL }
};


//===================================================
// Program body
//===================================================

tcp_sock_t tcp_sock;
buf_t tcp_buf;
io_channel_t stdin_chan;

static void tcp_recv_line(char *str)
{
        printf("%s\n", str);
}


static void tcp_recv(char *rbuf, int rsize)
{
	int i;

	log_debug(2, "tcp_recv rsize=%d", rsize);

	if (rsize <= 0) {
		return;
	}

	i = 0;
	while (i < rsize) {
		int i0 = i;

		/* Search end-of-line delimiter */
		while ((i < rsize) && (rbuf[i] != '\n')) {
			i++;
		}

		// If newline character reached, process this line
		if (i < rsize) {
			char *str = &rbuf[i0];
			int len = i - i0;

			rbuf[i++] = '\0';

			if (tcp_buf.len > 0) {
				buf_append(&tcp_buf, (unsigned char *) str, len);
				str = (char *) tcp_buf.base;
				len = tcp_buf.len;
				tcp_buf.len = 0;
			}

			if (len > 0) {
				tcp_recv_line(str);
			}
		}
		else {
			buf_append(&tcp_buf, (unsigned char *) &rbuf[i0], i-i0);
		}	
	}
}


static void tcp_event(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	log_debug(2, "tcp_event [%d]", tcp_sock->chan.fd);

	switch (io) {
	case TCP_IO_CONNECT:
		log_debug(2, "  CONNECT %s", rbuf);
		break;
	case TCP_IO_DATA:
		log_debug(2, "  DATA %d", rsize);
                log_debug_data((unsigned char *) rbuf, rsize);
		tcp_recv(rbuf, rsize);
		break;
	case TCP_IO_HUP:
		log_debug(2, "  HUP");
                sys_quit();
		break;
	default:
		log_debug(2, "  PANIC: unknown event caught");
		break;
	}
}


static int stdin_recv(void *user_data, char *buf, int len)
{
        if (buf == NULL) {
                sys_quit();
                return 0;
        }

        tcp_sock_write(&tcp_sock, buf, len);

        return 1;
}


int main(int argc, char *argv[])
{
        char *host;
        int port = 5678;

	if (options_parse(options_entries, &argc, argv) != 0) {
		return 1;
	}

        if (argc < 2) {
                fprintf(stderr, "Usage: ...\n");
		return 1;
        }

        host = argv[1];
        log_debug(3, "argc=%d", argc);
        if (argc > 2) {
                port = atoi(argv[2]);
        }

	/* Init log management */
	log_init("hakit-client");
	log_str(options_summary);

        /* Enable per-line output buffering */
        setlinebuf(stdout);

	/* Init system runtime */
	sys_init();

	/* Connect to HKCP node */
        buf_init(&tcp_buf);
	log_str("Connecting to %s:%d", host, port);
	if (tcp_sock_connect(&tcp_sock, host, port, opt_certs, tcp_event, NULL) < 0) {
		return 2;
	}

        /* Setup stdin handling */
        io_channel_setup(&stdin_chan, fileno(stdin), (io_func_t) stdin_recv, NULL);

	sys_run();

	return 0;
}
