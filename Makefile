all:
	gcc -s -Wall main.c -o libevent2-cli -levent -lpthread

debug:
	gcc -g -Wall main.c -o libevent2-cli -levent -lpthread

clean:
	rm -f libevent2-cli
