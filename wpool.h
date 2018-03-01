/*
 *
 *
 *
 *
 */


pthread_cond_t	wpool_cond	= PTHREAD_COND_INITIALIZER;
pthread_mutex_t wpool_mutex = PTHREAD_MUTEX_INITIALIZER;

int wpool_initialize_workers(uint8_t nworkers);
