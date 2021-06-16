#include "request.h"
#include "threadpool.h"
#include <stdlib.h>
#include "segel.h"

static void *thread_do(void* args);

 typedef struct args{
    threadpool_t* pool;
    int thread_id;
}pthread_args;

threadpool_t *threadpool_create(int num_of_threads, int size_of_queue,
	handling_policy policy)
{
    if(num_of_threads <= 0 ||  size_of_queue <= 0) {
        return NULL;
    }
    threadpool_t *pool = (threadpool_t *)malloc(sizeof(threadpool_t));

    if(pool == NULL) {
        goto err;
    }
    pool->num_of_threads = num_of_threads;
    pool->size_of_queue = size_of_queue;
	pool->policy = policy;
    pool->waiting_conn = pool->handeled_conn = 0;
    pool->threads = (mythread_t **)malloc(sizeof(mythread_t*) * num_of_threads);
    pool->queue_head = NULL;
    for (int i = 0; i < num_of_threads ; ++i) {
        pool->threads[i] = (mythread_t *)malloc(sizeof(mythread_t));
        pool->threads[i]->thread_id = i;
        pool->threads[i]->thread_count = pool->threads[i]->thread_dynamic =
                pool->threads[i]->thread_static = 0;
    }
    if((pthread_mutex_init(&(pool->lock), NULL) != 0) ||
       (pthread_cond_init(&(pool->notify_notempty), NULL) != 0) ||
	   (pthread_cond_init(&(pool->notify_notfull), NULL) != 0) ||
       (pool->threads == NULL)) {
        goto err;
    }
    /* Start worker threads */
    for(int i = 0; i < num_of_threads; i++) {
        pthread_args* arg = (pthread_args*)malloc(sizeof(pthread_args));
        arg->pool = pool;
        arg->thread_id = i;
        if(pthread_create(&(pool->threads[i]->pthread), NULL,
                          thread_do, (void*)arg) != 0) {
            return NULL;
        }
    }
    return pool;

 err:
    return NULL;
}

int listLen(conn_t* head){
    conn_t* temp = head;
    int count = 0;
    while(temp){
        count ++;
        temp = temp->next;
    }
    return count;
}

int randRemove(threadpool_t *pool){
    int index = rand() % pool->waiting_conn;
    conn_t* temp;
    if (index == 0){
        temp = pool->queue_head;
        pool->queue_head = pool->queue_head->next;
        free(temp);
    }
    else{ //index > 0
        conn_t* curr = pool->queue_head;
        conn_t* prev = NULL;
        for (int i = 0; i < index; ++i) {
            prev = curr;
            curr = curr->next;
        }
        prev->next = curr->next;
        free(curr);
    }
    pool->waiting_conn --;
    return 0;
}

int randomDrop(threadpool_t *pool)
{
    srand(time(0));
    int size = pool->waiting_conn;
    for (int i = 0; i < size / 4; i++) {
        if(randRemove(pool) == -1)
            return -1;
    }
    return 0;
}

int threadpool_add(threadpool_t *pool, int connfd) {
    if (pool == NULL) {
        return threadpool_invalid;
    }
    if (pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpool_lock_failure;
    }
    conn_t *conn = (conn_t *) malloc(sizeof(conn_t));
    conn->conn_fd = connfd;
    conn->next = NULL;
    if (gettimeofday(&(conn->req_arrival), NULL) != 0) {
        return threadpool_invalid;
    }
    if (pool->waiting_conn + pool->handeled_conn >= pool->size_of_queue) {
        switch (pool->policy) {
            case block:
                if (pool->waiting_conn + pool->handeled_conn >= pool->size_of_queue)
                {
                    pthread_cond_wait(&(pool->notify_notfull), &(pool->lock));
                }
                break;
            case drop_tail:
                Close(connfd);
                free(conn);
                if (pthread_mutex_unlock(&pool->lock) != 0) {
                    return threadpool_lock_failure;
                }
                return 0;
            case drop_head:
                if (pool->waiting_conn == 0){ //in case all jobs are working
                    Close(connfd);
                    free(conn);
                    if (pthread_mutex_unlock(&pool->lock) != 0) {
                        return threadpool_lock_failure;
                    }
                    return 0;
                }
                Close(pool->queue_head->conn_fd);
                conn_t* temp = pool->queue_head;
                pool->queue_head = pool->queue_head->next;
                free(temp);
                pool->waiting_conn--;
                break;
            case random_drop:
                if (pool->waiting_conn == 0){ //in case all jobs are working
                    Close(connfd);
                    free(conn);
                    if (pthread_mutex_unlock(&pool->lock) != 0) {
                        return threadpool_lock_failure;
                    }
                    return 0;
                }
                if (randomDrop(pool) == -1){
                    return threadpool_invalid;
                }
                break;
            default:
                return threadpool_invalid;
        }
    }

    if(pool->queue_head == NULL){ //queue empty
        pool->queue_head = conn;
    }
    else { // queue not empty
        conn_t* temp = pool->queue_head;
        while(temp->next) {
            temp = temp->next;
        }
        temp->next = conn;
    }
    pool->waiting_conn ++;

    if (pthread_cond_signal(&(pool->notify_notempty)) != 0) {
        return threadpool_lock_failure;
    }

    if (pthread_mutex_unlock(&pool->lock) != 0) {
        return threadpool_lock_failure;
    }
    return 0;
}

static void *thread_do(void* args)
{
    threadpool_t* pool = ((pthread_args*)args)->pool;
    conn_t *conn;
    int thread_id = ((pthread_args*)args)->thread_id;
    while(1) {
        pthread_mutex_lock(&(pool->lock));
        while(pool->waiting_conn == 0) {
            pthread_cond_wait(&(pool->notify_notempty), &(pool->lock));
        }
        //printf("list lens is %d, needs to be %d - before taking from queue \n", listLen(pool->queue_head),  pool->waiting_conn);
        conn = pool->queue_head;
        pool->queue_head = pool->queue_head->next;
        pool->waiting_conn--;
		pool->handeled_conn++;
        //printf("list lens is %d, needs to be %d - after taking from queue \n", listLen(pool->queue_head),  pool->waiting_conn);
        pthread_mutex_unlock(&(pool->lock));
        gettimeofday(&conn->req_pickup, NULL);
        requestHandle(conn, pool->threads[thread_id]);
        pthread_mutex_lock(&(pool->lock));
		pool->handeled_conn--;
		pthread_cond_signal(&(pool->notify_notfull));
		pthread_mutex_unlock(&(pool->lock));
		Close(conn->conn_fd);
        free(conn);
    }
    free(args);
    return NULL;
}
