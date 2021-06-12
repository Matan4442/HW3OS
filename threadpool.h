
#ifndef HW3OS_THREADPOOL_H
#define HW3OS_THREADPOOL_H
#include <pthread.h>
#include <sys/time.h>

typedef enum {
    invalid = -1,
    block = 0,
    drop_tail = 1,
    drop_head = 2,
    random_drop = 3,
} handling_policy;

typedef enum {
    threadpool_invalid        = -1,
    threadpool_lock_failure   = -2,
    threadpool_thread_failure = -3
} threadpool_error_t;

typedef struct conn_t {
    int conn_fd;
    struct timeval req_arrival;
    struct timeval req_pickup;
    struct conn_t* next;
}conn_t;

typedef struct mythread_t {
    pthread_t pthread;
    int thread_id;
    int thread_count;
    int thread_static;
    int thread_dynamic;
}mythread_t;

typedef struct threadpool_t {
    pthread_mutex_t lock;
    pthread_cond_t notify_notempty;
    pthread_cond_t notify_notfull;
    mythread_t **threads;
    int num_of_threads;
    int size_of_queue;
    conn_t* queue_head;
    int waiting_conn;
    int handeled_conn;
    handling_policy policy;
} threadpool_t;

threadpool_t *threadpool_create(int num_of_threads, int size_of_queue,
                                handling_policy policy);

int threadpool_add(threadpool_t *pool, int connfd);


#endif //HW3OS_THREADPOOL_H
