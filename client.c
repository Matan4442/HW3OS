/*
 * client.c: A very, very primitive HTTP client.
 * 
 * To run, try: 
 *      ./client www.cs.technion.ac.il 80 /
 *
 * Sends one HTTP request to the specified HTTP server.
 * Prints out the HTTP response.
 *
 * HW3: For testing your server, you will want to modify this client.  
 * For example:
 * 
 * You may want to make this multi-threaded so that you can 
 * send many requests simultaneously to the server.
 *
 * You may also want to be able to request different URIs; 
 * you may want to get more URIs from the command line 
 * or read the list from a file. 
 *
 * When we test your server, we will be using modifications to this client.
 *
 */

#include "segel.h"
#include <pthread.h>

/*
 * Send an HTTP request for the specified file 
 */
void clientSend(int fd, char *filename)
{
  char buf[MAXLINE];
  char hostname[MAXLINE];

  Gethostname(hostname, MAXLINE);

  /* Form and send the HTTP request */
  sprintf(buf, "GET %s HTTP/1.1\n", filename);
  sprintf(buf, "%shost: %s\n\r\n", buf, hostname);
  Rio_writen(fd, buf, strlen(buf));
}
  
/*
 * Read the HTTP response and print it out
 */
void clientPrint(int fd)
{
  rio_t rio;
  char buf[MAXBUF];  
  int length = 0;
  int n;
  
  Rio_readinitb(&rio, fd);

  /* Read and display the HTTP Header */
  n = Rio_readlineb(&rio, buf, MAXBUF);
  while (strcmp(buf, "\r\n") && (n > 0)) {
    printf("Header: %s", buf);
    n = Rio_readlineb(&rio, buf, MAXBUF);

    /* If you want to look for certain HTTP tags... */
    if (sscanf(buf, "Content-Length: %d ", &length) == 1) {
      printf("Length = %d\n", length);
    }
  }

  /* Read and display the HTTP Body */
  n = Rio_readlineb(&rio, buf, MAXBUF);
  while (n > 0) {
    printf("%s", buf);
    n = Rio_readlineb(&rio, buf, MAXBUF);
  }
}

#define THREAD_NUM 5

typedef struct Task {
    int clientfd;
    char *filename;
} Task;

Task taskQueue[256];
int taskCount = 0;

pthread_mutex_t CmutexQueue;
pthread_cond_t CcondQueue;

void executeTask(Task *task) {
    clientSend(task->clientfd, task->filename);
    clientPrint(task->clientfd);
    Close(task->clientfd);
}

void submitTask(Task task) {
    pthread_mutex_lock(&CmutexQueue);
    taskQueue[taskCount] = task;
    taskCount++;
    pthread_mutex_unlock(&CmutexQueue);
    pthread_cond_signal(&CcondQueue);
}

void *startThread(void *args) {
    while (1) {
        Task task;

        pthread_mutex_lock(&CmutexQueue);
        while (taskCount == 0) {
            pthread_cond_wait(&CcondQueue, &CmutexQueue);
        }

        task = taskQueue[0];
        int i;
        for (i = 0; i < taskCount - 1; i++) {
            taskQueue[i] = taskQueue[i + 1];
        }
        taskCount--;
        pthread_mutex_unlock(&CmutexQueue);
        executeTask(&task);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    char *host, *file_name;
    int port;
    int client_fd;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <filename>\n", argv[0]);
        exit(1);
    }

    host = argv[1];
    port = atoi(argv[2]);
    file_name = argv[3];

    pthread_t th[THREAD_NUM];
    pthread_mutex_init(&CmutexQueue, NULL);
    pthread_cond_init(&CcondQueue, NULL);
    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_create(&th[i], NULL, &startThread, NULL) != 0) {
            perror("Failed to create the thread");
        }
    }

    for (int i = 0; i < 1; i++) {
        /* Open a single connection to the specified host and port */
        if (i != 39){
            client_fd = Open_clientfd(host, port);
            Task t = {
                    .clientfd = client_fd,
                    .filename = file_name};
            submitTask(t);
        }
        else
        {
            client_fd = Open_clientfd(host, port);
            Task t = {
                    .clientfd = client_fd,
                    .filename = "test.html"};
            submitTask(t);
        }
    }

    for (int i = 0; i < THREAD_NUM; i++) {
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join the thread");
        }
    }
    pthread_mutex_destroy(&CmutexQueue);
    pthread_cond_destroy(&CcondQueue);
    exit(0);
}

