#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <glib.h>
#include <ticables.h>

/*
 * XXX: I'm debating whether to add networking capabilities (server and client
 * modes) or to leave them out and have the user use nc for networking.
 */

/*
some options from nc that might be added to tinc:

-l                      listen mode, for inbound connects
-n                      numeric-only IP addresses, no DNS
-o file                 hex dump of traffic
-p port                 local port number
-s addr                 local source address
-u                      UDP mode
-v                      verbose [use twice to be more verbose]
-w secs                 timeout for connects and final net reads
*/

/*
 * Usage:
 *
 * stdin/stdout:
 * tinc
 *
 * connect to host:
 * tinc host port
 *
 * listen for connection:
 * tinc -l port
 *
 * other options:
 * -k          keep alive after first connection
 * -t TIMEOUT  timeout for reading from/writing to TI (in milliseconds)
 */

/*
 * TODO: implement NETWORK_CLIENT and NETWORK_SERVER modes
 * TODO: add options to set cable model and port
 */

static const char *argv0;
static int timeout = -1; // use default timeout
static int keep_alive = 0;
static const char *host;
static long port = 0;

enum network_mode {
	NETWORK_NONE,
	NETWORK_CLIENT,
	NETWORK_SERVER,
};

static enum network_mode network_mode = NETWORK_NONE;
static int infd = -1, outfd = -1;
static CableModel cableModel = CABLE_NUL;
static CablePort cablePort = PORT_0;
static int signalled = 0;
static int verbosity = 0;

static void usage(int status)
{
	fprintf(stderr,
	        "Usage: %s [OPTIONS] [ -l PORT | HOST PORT ]\n"
	        "Options:\n"
	        "      -h          print this help message and exit\n"
	        "      -L          print license information and exit\n"
	        "      -V          print version information and exit\n"
	        "      -k          keep listening for additional connections\n"
	        "                  after each connection closes\n"
	        "      -c CABLE    set cable model\n"
	        "      -p PORT     set cable port\n"
	        "      -t TIMEOUT  set cable read/write timeout (in milliseconds)\n"
	        "      -v          display verbose output\n"
	        "                  use this option twice for more verbosity\n",

	        argv0);
	// TODO: print option descriptions
	exit(status);
}

static void license()
{
	fprintf(stderr, "license goes here\n");
}

static void version()
{
	fprintf(stderr, "version goes here\n");
}

static void get_options(int argc, char *argv[])
{
	int opt;

	argv0 = argv[0];

	while ((opt = getopt(argc, argv, "VLvc:p:hl:kt:")) != -1) {
		switch (opt) {
		case 'L':
			license();
			exit(EXIT_SUCCESS);
			break;
		case 'V':
			version();
			exit(EXIT_SUCCESS);
			break;
		case 'l':
			network_mode = NETWORK_SERVER;
			port = atol(optarg);
			break;
		case 'k':
			keep_alive = 1;
			break;
		case 't':
			timeout = atoi(optarg);
			if (timeout <= 0) {
				fprintf(stderr,
				        "%s: timeout must be greater than 0!\n",
					argv0);
				exit(EXIT_FAILURE);
			}
			break;
		case 'c':
			cableModel = ticables_string_to_model(optarg);
			if (cableModel == CABLE_NUL) {
				fprintf(stderr, "%s: unknown cable '%s'\n",
				        argv0, optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'p':
			cablePort = atoi(optarg);
			if (cablePort < 1 || 4 < cablePort) {
				fprintf(stderr,
				        "%s: port must be between 1 and 4!\n",
				        argv0);
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			if (++verbosity > 2)
				verbosity = 2;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
			break;

		default: /* '?' */
			usage(EXIT_FAILURE);
			break;
		}
	}

	if ((optind < argc && network_mode == NETWORK_SERVER) ||
	    optind + 2 < argc) {
		fprintf(stderr, "%s: too many arguments\n", argv[0]);
		usage(EXIT_FAILURE);
	} else if (optind < argc && optind + 2 > argc) {
		fprintf(stderr, "%s: not enough arguments\n", argv[0]);
		usage(EXIT_FAILURE);
	} else if (optind + 2 == argc) {
		// two arguments following options
		host = argv[optind];
		port = atoi(argv[optind+1]);
		network_mode = NETWORK_CLIENT;
	}

	// check options
	if (network_mode != NETWORK_NONE &&
	    (port <= 0 || 65535L < port)) {
		fprintf(stderr, "%s: invalid port %ld!\n", argv0, port);
	}

	if (network_mode != NETWORK_NONE) {
		fprintf(stderr, "%s: network modes not yet supported\n",
		        argv0);
		exit(EXIT_FAILURE);
	}
}

/*
connect:
if listen mode
	listen and accept a connection
	outfd = infd = socketfd
else if connect mode
	connect to remote host
	outfd = infd = socketfd
else
	outfd = 1
	infd = 0
*/
static void client_open()
{
	switch (network_mode) {
	case NETWORK_CLIENT:
		// TODO connect to host:port
	case NETWORK_SERVER:
		// TODO listen on port and accept a connection
	case NETWORK_NONE:
		infd = STDIN_FILENO;
		outfd = STDOUT_FILENO;
		break;
	}
}

static void client_close()
{
	if (network_mode != NETWORK_NONE) {
		// close only network file descriptors
		if (infd != outfd) {
			close(infd);
		}
		close(outfd);
	}
	infd = outfd = -1;
}

static void print_lc_error(int errnum)
{
	char *msg;
	ticables_error_get(errnum, &msg);
	fprintf(stderr, "%s: ticables error %d: %s\n", argv0, errnum, msg);
	g_free(msg);
}

/*
 * read from client (block for a short time)
 */
static ssize_t client_read(char *buf, size_t count)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(infd, &set);
	struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };
	if (select(infd + 1, &set, NULL, NULL, &tv) > 0) {
		return read(infd, buf, count);
	}
	errno = EAGAIN;
	return -1;
}

static ssize_t client_write(const char *buf, size_t count)
{
	ssize_t n;
	size_t left = count;
	while (left) {
		n = write(outfd, buf, left);
		if (n <= 0) {
			if (left == count) {
				return -1;
			} else {
				break;
			}
		}
		buf += n;
		left -= n;
	}
	return count - left;
}

static int transfer_data(CableHandle *handle)
{
	int ret = -1;
	int err;

	err = ticables_cable_open(handle);
	if (err) {
		goto err_open;
	}

	if (timeout > 0) {
		ticables_options_set_timeout(handle, (timeout + 99) / 100);
	}

	for (;;) {
		if (signalled) goto err;
		// read from client
		uint8_t buf[BUFSIZ];
		ssize_t n = client_read((char *)buf, sizeof(buf));
		
		// read from TI and write to client
		for (;;) {
			CableStatus status;
			err = ticables_cable_check(handle, &status);
			if (err) {
				goto err;
			}
			//fprintf(stderr, "<%d>", (int)status);
			if (status != STATUS_RX) {
				break;
			}
			uint8_t data;
			//fprintf(stderr, "get...\n");
			err = ticables_cable_get(handle, &data);
			if (err) {
				goto err;
			}
			if (client_write((const char *)&data, 1) != 1) {
				// write error
				perror("write error");
				goto err;
			}
		}

		// write to TI
		if (n == 0) {
			// EOF
			//fprintf(stderr, "<EOF>");
			// make sure data is flushed to calc (why is a small sleep necessary here?)
			//usleep(timeout * 100000L);
			usleep(100000);
			break;
		} else if (n > 0) {
			//fprintf(stderr, "send...\n");
			err = ticables_cable_send(handle, buf, n);
			if (err) {
				// send error
				goto err;
			}
		} else if (errno != EAGAIN) {
			perror("read error");
			// error
			goto err;
		}
	}
	ret = 0; // no error, regular exit (EOF)
err:
	ticables_cable_close(handle);
err_open:
	if (err) {
		print_lc_error(err);
	}
	return ret;

}

static void sighandler(int signum)
{
	signalled = signum;
}

/*
 * XXX: I'm not sure if this is the proper way to handle glib logging
 */
static void gloghandler(const gchar *log_domain, GLogLevelFlags log_level,
                        const gchar *message, gpointer user_data)
{
	const char *level = "";
	GLogLevelFlags minlevel = G_LOG_LEVEL_WARNING;
	switch (log_level) {
		case G_LOG_LEVEL_DEBUG: level = "DEBUG"; break;
		case G_LOG_LEVEL_INFO: level = "INFO"; break;
		case G_LOG_LEVEL_MESSAGE: level = "MESSAGE"; break;
		case G_LOG_LEVEL_WARNING: level = "WARNING"; break;
		case G_LOG_LEVEL_CRITICAL: level = "CRITICAL"; break;
		case G_LOG_LEVEL_ERROR: level = "ERROR"; break;
		default: level = "UNKNOWN"; break;
	}
	switch (verbosity) {
	case 0: minlevel = G_LOG_LEVEL_WARNING; break;
	case 1: minlevel = G_LOG_LEVEL_INFO; break;
	case 2: minlevel = G_LOG_LEVEL_DEBUG; break;
	}
	if (log_level <= minlevel) {
		fprintf(stderr, "%s-%s: %s\n", log_domain, level, message);
	}
	if (log_level <= G_LOG_LEVEL_ERROR) {
		abort();
	}
}

int main(int argc, char *argv[])
{
	int err;
	get_options(argc, argv);

	// catch some signals so we can cleanup gracefully
	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);
	signal(SIGABRT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);
	signal(SIGALRM, sighandler);
	signal(SIGUSR1, sighandler);
	signal(SIGUSR2 , sighandler);

	g_log_set_default_handler(gloghandler, NULL);

	ticables_library_init();
	CableHandle *handle = ticables_handle_new(cableModel, cablePort);
	if (!handle) {
		fprintf(stderr, "%s: cannot create ticables handle!\n",
		        argv[0]);
		return EXIT_FAILURE;
	}
	ticables_handle_show(handle);

	do {
		client_open();
		err = transfer_data(handle);
		client_close();
	} while (keep_alive && !err);

	ticables_handle_del(handle);
	ticables_library_exit();

	if (signalled) {
		fprintf(stderr, "exiting on signal %d\n", signalled);
		signal(signalled, SIG_DFL);
		raise(signalled);
	}

	return 0;
}
