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
} client_t;

void client_close(client_t *c);
void client_free(client_t *c);
