CC = g++
CC_FLAGS = -W -Wall -Werror -I./
EXEC = my_http
OBJS = main.o my_http.o

$(EXEC): $(OBJS)
	$(CC) $(CC_FLAGS) $(OBJS) -o $@

main.o: main.cpp
	$(CC) $(CC_FLAGS) -c -o $@ main.cpp

my_http.o: http_server_2.cpp http_server_2.hpp
	$(CC) $(CC_FLAGS) -c -o $@ http_server_2.cpp

all: $(EXEC)

clean:
	rm -rf $(OBJS) $(EXEC)
