#include "segel.h"
#include "threadpool.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too



handling_policy parse_policy(char *arg, handling_policy* policy){
	*policy = invalid;
	if (strcmp(arg,"block") == 0)
	{
		*policy = block;
	}if (strcmp(arg,"dt") == 0)
	{
		*policy = drop_tail;
	}if (strcmp(arg,"dh") == 0)
	{
		*policy = drop_head;
	}if (strcmp(arg,"random") == 0)
	{
		*policy = random_drop;
	}
	return *policy;
}

void getargs(int *port, int argc, char *argv[],
		int* num_of_threads,int* size_of_queue, handling_policy* policy)
{
    if (argc < 4 || num_of_threads ==NULL || size_of_queue == NULL ||
			policy == NULL || parse_policy(argv[4], policy) == invalid ) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		exit(1);
    }
    *port = atoi(argv[1]);
	*num_of_threads = atoi(argv[2]);
    *size_of_queue = atoi(argv[3]);
}


int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen, num_of_threads, size_of_queue;
    struct sockaddr_in clientaddr;
	handling_policy policy;
    getargs(&port, argc, argv, &num_of_threads, &size_of_queue, &policy);
    threadpool_t* thpool = threadpool_create(num_of_threads,size_of_queue,policy);
    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        if (threadpool_add(thpool,connfd) != 0){
            printf("error");
            return -1;
        }
    }

}


    


 
