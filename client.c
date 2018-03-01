void client_close(client_t *c)
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

void client_free(client_t *c)
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
