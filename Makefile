all:
	gcc -s -Wall main.c -o libevent2-cli -levent

debug:
	gcc -g -Wall main.c -o libevent2-cli -levent

clean:
	rm -f libevent2-cli
