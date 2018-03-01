/*
 *
 *
 *
 *
 */

typedef struct client_s
{
	int fd;
	struct event_base *evbase;
	struct bufferevent *bev;
	struct evbuffer *output;

	struct client_s *prev;
	struct client_s *next;
} client_t;

void client_close(client_t *c);
void client_free(client_t *c);
