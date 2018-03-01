static pthread_t *s_workers = NULL;
static uint8_t *s_nworkers = 0;

int wpool_initialize_workers(uint8_t nworkers)
{
	uint8_t i;

	s_workers = (pthread_t *) malloc(nworkers * sizeof(pthread_t));

	if ( s_workers == NULL )
	{
		fprintf(stderr, "Cannot allocate thread structures\n");
		return -1;
	}

	for ( i = 0; i < nworkers; ++i )
	{
		if ( pthread_create(&s_workers[i], NULL, &worker_loop, NULL) != 0 )
		{
			fprintf(stderr, "Failed creating thread %i\n", i);
		}
	}

	fprintf(stderr, "Worker pool has been initialized\n");

	return 0;
}

int wpool_shutdown_workers()
{
	uint8_t i;

	if ( s_workers == NULL )
		return -1;

	
}
