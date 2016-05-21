all: ls_router manager_send

ls_router: ls_main.cpp util.cpp
	g++ -g -std=c++11 -pthread -o ls_router ls_main.cpp util.cpp

manager_send: manager_send.c
	gcc -o manager_send manager_send.c

.PHONY: clean
clean:
	rm -f *.o vec_router ls_router manager_send
