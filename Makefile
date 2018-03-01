all:
	gcc -s -Wall main.c -o libevent2-cli -levent -lpthread -levent_pthreads

debug:
	gcc -g -Wall main.c -o libevent2-cli -levent -lpthread -levent_pthreads

clean:
	rm -f libevent2-cli
