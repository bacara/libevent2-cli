/*
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

static struct evconnlistener *s_listener;

/***** ***** ***** ***** ***** ***** ***** *****
 *           Commands-related stuff
 ***** ***** ***** ***** ***** ***** ***** *****/

typedef int8_t (*cmd_handler_t)(struct bufferevent *bev, int argc, char *argv[]);

typedef struct {
	const char    *cmd;
	const char    *help;
	cmd_handler_t handler;
} cmd_t;

int8_t cmd_help(struct bufferevent *bev, int argc, char *argv[]);
int8_t cmd_info(struct bufferevent *bev, int argc, char *argv[]);
int8_t cmd_quit(struct bufferevent *bev, int argc, char *argv[]);
int8_t cmd_kill(struct bufferevent *bev, int argc, char *argv[]);

static cmd_t s_commands[] = {
	{ "help", "Display help",      &cmd_help },
	{ "info", "Display some info", &cmd_info },
	{ "quit", "Quit interface",    &cmd_quit },
	{ "kill", "Kill console",      &cmd_kill },
};

#define CMD_COUNT (sizeof(s_commands) / sizeof(cmd_t))

/***** ***** ***** ***** ***** ***** ***** *****
 *           Command functions
 ***** ***** ***** ***** ***** ***** ***** *****/

int8_t cmd_help(struct bufferevent *bev, int argc, char *argv[])
{
	struct evbuffer *output = bufferevent_get_output(bev);
	uint8_t i = 0;

	if ( argc < 2 )
	{
		evbuffer_add_printf(output, "%s\n", s_commands[0].help);
		return 0;
	}

	for ( i = 0; i < CMD_COUNT; ++i )
	{
		if ( strcmp(argv[1], s_commands[i].cmd) == 0 )
		{
			evbuffer_add_printf(output, "%s\n", s_commands[i].help);
			return 0;
		}
	}

	evbuffer_add_printf(output, "help: unknown command: %s\n", argv[1]);
	return -1;
}

int8_t cmd_info(struct bufferevent *bev, int argc, char *argv[])
{
	struct evbuffer *output = bufferevent_get_output(bev);

	evbuffer_add_printf(output, "\
Here are some information. You might find them useful or not (surely not), but\
 the fact is we kept our promise. You have information by typing the \"info\"\
command in here.\n");

	return 0;
}

int8_t cmd_quit(struct bufferevent *bev, int argc, char *argv[])
{
	bufferevent_free(bev);
}

int8_t cmd_kill(struct bufferevent *bev, int argc, char *argv[])
{
	struct event_base *base = bufferevent_get_base(bev);

	bufferevent_free(bev);
	event_base_loopexit(base, 0);
}

/***** ***** ***** ***** ***** ***** ***** *****
 *            Event callbacks
 ***** ***** ***** ***** ***** ***** ***** *****/

static void command_cb(struct bufferevent *bev, void *arg)
{
	struct evbuffer *input  = bufferevent_get_input(bev);
	struct evbuffer *output = bufferevent_get_output(bev);

	size_t len;

	char   *line  = NULL;
	char   *token = NULL;
	char   **argv = NULL;
	char   commandline[256];

	unsigned int i    = 0;
	unsigned int argc = 0;

	/* Read a line from the evbuffer */
	line = evbuffer_readln(input, &len, EVBUFFER_EOL_ANY);
	if ( len > sizeof(commandline) )
	{
		evbuffer_add_printf(output, "line is too long (max: 256 characters)\n");
		return;
	}

	/* Copy line to a more convenient buffer */
	strncpy(commandline, line, sizeof(commandline));

	/* Tokenize the commandline */
	argc = 0;
	token = strtok(commandline, " \t");
	do
	{
		argv        = (char **) realloc(argv, sizeof(char *) * (argc + 1));
		argv[argc]  = token;
		argc       += 1;
	} while ( (token = strtok(NULL, " \t")) != NULL );

	if ( argv[0] != NULL )
	{
		for ( i = 0; i < CMD_COUNT; ++i )
		{
			if ( strcmp(argv[0], s_commands[i].cmd) == 0 )
			{
				if ( s_commands[i].handler(bev, argc, argv) != 0 )
				{
					/* Error handling */
				}
				break;
			}
		}

		if ( i == CMD_COUNT )
			evbuffer_add_printf(output, "%s: unknown command\n", argv[0]);

		free(argv);
	}

	free(line);
}

static void error_cb(struct bufferevent *bev, short events, void *arg)
{
	if ( events & BEV_EVENT_ERROR )
		perror("Error from bufferevent");
	if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) )
		bufferevent_free(bev);
}

static void accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int len, void *ptr)
{
	struct event_base *base = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);

	bufferevent_setcb(bev, command_cb, NULL, error_cb, NULL);
	bufferevent_enable(bev, EV_READ);
}

static void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
	struct event_base *base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();

	fprintf(stderr, "Got an error %d (%s) on the listener\n", err, evutil_socket_error_to_string(err));

	event_base_loopexit(base, NULL);
}

int main(int argc, char *argv[])
{
	struct event_base *evbase;
	struct sockaddr_in addr;

	/* Initialize event base */
	evbase = event_base_new();
	if ( evbase == NULL )
	{
		fprintf(stderr, "Cannot initialized event base\n");
		return 1;
	}

	/* Initialize socket address */
	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1337);
	addr.sin_addr.s_addr = INADDR_ANY;

	/* Set up a connection listener */
	s_listener = evconnlistener_new_bind(
		evbase,
		accept_conn_cb,
		NULL,
		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
		1,
		(const struct sockaddr *) &addr,
		sizeof(addr));

	if ( s_listener == NULL )
	{
		fprintf(stderr, "Cannot initialize evconnlistener\n");
		return;
	}
	evconnlistener_set_error_cb(s_listener, accept_error_cb);

	/* Run event loop
	 * NOTE: If no flags are needed, use event_base_dispatch().
	 */
	switch ( event_base_loop(evbase, EVLOOP_NO_EXIT_ON_EMPTY) )
	{
		case -1:
			fprintf(stderr, "Event base loop exit: error happened in libevent backend\n");
			break;
		case 0:
			fprintf(stderr, "Event base loop exit: all went right\n");
			break;
		case 1:
			fprintf(stderr, "Event base loop exit: no more pending or active events\n");
			break;
		default:
			fprintf(stderr, "Event base loop exit: program should not go here\n");
			break;
	}


	evconnlistener_free(s_listener);
	event_base_free(evbase);

	return 0;
}
