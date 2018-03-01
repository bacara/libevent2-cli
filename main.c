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
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>

static struct evconnlistener *s_listener;
static struct event_base *s_evbase;

/***** ***** ***** ***** ***** ***** ***** *****
 *                Utilities
 ***** ***** ***** ***** ***** ***** ***** *****/

#define LL_ADD(item, list) { \
	item->prev = NULL; \
	item->next = list; \
	list = item; \
}

#define LL_REMOVE(item, list) { \
	if (item->prev != NULL) item->prev->next = item->next; \
	if (item->next != NULL) item->next->prev = item->prev; \
	if (list == item) list = item->next; \
	item->prev = item->next = NULL; \
}

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
command in here.\n\
\n\
This is coming from thread 0x%x !\n", (int) pthread_self());

	return 0;
}

int8_t cmd_quit(struct bufferevent *bev, int argc, char *argv[])
{
	bufferevent_free(bev);

	return 0;
}

int8_t cmd_kill(struct bufferevent *bev, int argc, char *argv[])
{
	struct event_base *base = bufferevent_get_base(bev);

	/* bufferevent_free(bev); */
	/* event_base_loopexit(base, 0); */

	event_base_loopexit(s_evbase, NULL);

	return 0;
}

/***** ***** ***** ***** ***** ***** ***** *****
 *                Client queue
 ***** ***** ***** ***** ***** ***** ***** *****/

typedef struct client_s
{
	int fd;
	struct event_base *evbase;
	struct bufferevent *bev;
	struct evbuffer *output;

	struct client_s *prev;
	struct client_s *next;
} client_t;

static void client_close(client_t *client)
{
	if ( client == NULL )
		return;

	if ( client->fd >= 0 )
	{
		if ( close(client->fd) )
			/* TODO: Handle errors from close() */ ;
		client->fd = -1;
	}
}

static void client_free(client_t *client)
{
	if ( client == NULL )
		return;

	/*
	 * Free event buffers and base
	 */
	if ( client->output != NULL )
	{
		evbuffer_free(client->output);
		client->output = NULL;
	}
	if ( client->bev != NULL )
	{
		bufferevent_free(client->bev);
		client->bev = NULL;
	}
	if ( client->evbase != NULL )
	{
		event_base_free(client->evbase);
		client->evbase = NULL;
	}

	/* Free application-specific data */

	/* Free client structure */
	free(client);
}

/***** ***** ***** ***** ***** ***** ***** *****
 *                Client queue
 ***** ***** ***** ***** ***** ***** ***** *****/

static struct
{
	client_t *waiting_clients;

	pthread_mutex_t clientqueue_mutex;
	pthread_cond_t  clientqueue_cond;
} s_clientqueue;

#define CLIENTQUEUE_LOCK()   pthread_mutex_lock(&s_clientqueue.clientqueue_mutex);
#define CLIENTQUEUE_UNLOCK() pthread_mutex_unlock(&s_clientqueue.clientqueue_mutex);

/***** ***** ***** ***** ***** ***** ***** *****
 *                Workers
 ***** ***** ***** ***** ***** ***** ***** *****/

#define NWORKERS 4

static pthread_t s_workers[NWORKERS];
static bool s_worker_continue = true;

static void *worker_routine(void *arg)
{
	pthread_t *me = (pthread_t *) arg;
	client_t *client = NULL;

	while (1)
	{
		CLIENTQUEUE_LOCK ();
		while ( s_clientqueue.waiting_clients == NULL )
			pthread_cond_wait(&s_clientqueue.clientqueue_cond, &s_clientqueue.clientqueue_mutex);

		client = s_clientqueue.waiting_clients;
		if ( client != NULL )
			LL_REMOVE (client, s_clientqueue.waiting_clients);
		CLIENTQUEUE_UNLOCK ();

		if ( !s_worker_continue )
			break;

		if ( client == NULL )
			continue;

		event_base_dispatch(client->evbase);
		client_close(client);
		client_free(client);
	}

	return NULL;
}

static int8_t workers_init()
{
	uint8_t i;

	pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t  blank_cond  = PTHREAD_COND_INITIALIZER;

	bzero(&s_clientqueue, sizeof(s_clientqueue));
	memcpy(&s_clientqueue.clientqueue_mutex, &blank_mutex, sizeof(pthread_mutex_t));
	memcpy(&s_clientqueue.clientqueue_cond,  &blank_cond,  sizeof(pthread_cond_t));

	for ( i = 0; i < NWORKERS; ++i )
	{
		if ( pthread_create(&s_workers[i], NULL, worker_routine, (void *) &s_workers[i]) )
		{
			fprintf(stderr, "Failed to start all threads\n");
			return -1;
		}
	}

	return 0;
}

static int8_t workers_shutdown()
{
	s_worker_continue = false;

	CLIENTQUEUE_LOCK ();
	pthread_cond_broadcast(&s_clientqueue.clientqueue_cond);
	CLIENTQUEUE_UNLOCK ();

	return 0;
}

/***** ***** ***** ***** ***** ***** ***** *****
 *            Event callbacks
 ***** ***** ***** ***** ***** ***** ***** *****/

static void send_prompt(struct evbuffer *output)
{
	evbuffer_add_printf(output, "> ");
}

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

		send_prompt(output);

		CLIENTQUEUE_LOCK ();
		pthread_cond_broadcast(&s_clientqueue.clientqueue_cond);
		CLIENTQUEUE_UNLOCK ();

		free(argv);
	}

	free(line);
}

static void write_callback(struct bufferevent *bev, void *arg)
{
	fprintf(stderr, "call from write callback\n");
}

static void error_cb(struct bufferevent *bev, short events, void *arg)
{
	if ( events & BEV_EVENT_ERROR )
		perror("Error from bufferevent");
	if ( events & (BEV_EVENT_EOF | BEV_EVENT_ERROR) )
		bufferevent_free(bev);
}

static void on_accept_callback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int len, void *ptr)
{
	struct bufferevent *bev;

	client_t *client;

	/* int c_fd = accept(fd, NULL, NULL); */

	/* if ( c_fd == -1 ) */
	/* { */
	/* 	perror("Error accepting client:"); */
	/* 	return; */
	/* } */

	fprintf(stderr, "on accept callback\n");

	if ( (client = malloc(sizeof(client_t))) == NULL )
	{
		fprintf(stderr, "Client structure allocation failed\n");
		return;
	}
	bzero(client, sizeof(client_t));
	client->fd = fd;

	if ( (client->evbase = event_base_new()) == NULL )
	{
		fprintf(stderr, "Client event base init failed\n");
		return;
	}

	bev = bufferevent_socket_new(client->evbase, fd, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(bev, command_cb, write_callback, error_cb, NULL);
	bufferevent_enable(bev, EV_READ);

	struct evbuffer *output = bufferevent_get_output(bev);
	send_prompt(output);

	CLIENTQUEUE_LOCK ();
	LL_ADD (client, s_clientqueue.waiting_clients);
	pthread_cond_broadcast(&s_clientqueue.clientqueue_cond);
	CLIENTQUEUE_UNLOCK ();
}

static void on_accept_error_callback(struct evconnlistener *listener, void *ctx)
{
	struct event_base *base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();

	fprintf(stderr, "Got an error %d (%s) on the listener\n", err, evutil_socket_error_to_string(err));

	event_base_loopexit(base, NULL);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;

	/*
	 * Event base initialization
	 *
	 * NOTE: Do NOT use event_init() anymore, as it is now deprecated and
	 *       according to documentation "totally unsafe for multithreaded use".
	 */

	struct event_config *cfg;

	cfg = event_config_new();
	event_config_avoid_method(cfg, "epoll");
	event_config_avoid_method(cfg, "poll");

	if ( evthread_use_pthreads() == -1 )
	{
		fprintf(stderr, "cannot utilize evthread_pthread\n");
		return 1;
	}

	s_evbase = event_base_new_with_config(cfg);
	if ( s_evbase == NULL )
	{
		fprintf(stderr, "Cannot initialized event base\n");
		return 1;
	}

	/*
	 * Event connection listener initialization
	 */

	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1337);
	addr.sin_addr.s_addr = INADDR_ANY;

	/* Set up a connection listener */
	s_listener = evconnlistener_new_bind(
		s_evbase,
		on_accept_callback,
		NULL,
		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_THREADSAFE,
		-1,
		(const struct sockaddr *) &addr,
		sizeof(addr));

	if ( s_listener == NULL )
	{
		fprintf(stderr, "Cannot initialize evconnlistener\n");
		return 1;
	}
	evconnlistener_set_error_cb(s_listener, on_accept_error_callback);

	workers_init();

	fprintf(stderr, "just before loop()\n");

	/* Run event loop
	 * NOTE: If no flags are needed, use event_base_dispatch().
	 */
	switch ( event_base_loop(s_evbase, EVLOOP_NO_EXIT_ON_EMPTY) )
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

	workers_shutdown();
	evconnlistener_free(s_listener);
	event_base_free(s_evbase);

	return 0;
}
