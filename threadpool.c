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
    pool->queue_head = pool->queue_tail = NULL;
    for (int i = 0; i < num_of_threads; ++i) {
        pool->threads[i] = (mythread_t *)malloc(sizeof(mythread_t));
        pool->threads[i]->stat_thread_id = i;
        pool->threads[i]->stat_thread_count = pool->threads[i]->stat_thread_dynamic =
                pool->threads[i]->stat_thread_static = 0;
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
            threadpool_destroy(pool);
            return NULL;
        }
    }
    return pool;

 err:
    if(pool) {
        threadpool_free(pool);
    }
    return NULL;
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
        while(index-1 > 0) {
            curr = curr->next;
            index --;
        }
        temp = curr->next;
        if (curr->next == NULL) return -1;
        curr->next = curr->next->next;
        free(temp);
    }
    pool->waiting_conn --;
    return 0;
}

int randomDrop(threadpool_t *pool)
{
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
    if (gettimeofday(&(conn->stat_req_arrival), NULL) != 0) {
        return threadpool_invalid;
    }
    if (pool->waiting_conn + pool->handeled_conn >= pool->size_of_queue) {
        switch (pool->policy) {
            case block:
                while (pool->waiting_conn + pool->handeled_conn >= pool->size_of_queue)
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
    if(pool->queue_tail == NULL){ //queue empty
        pool->queue_head = pool->queue_tail = conn;
    }
    else { // queue not empty
        pool->queue_tail->next = conn;
        pool->queue_tail = conn;
    }
    pool->waiting_conn ++;
    if (pthread_cond_signal(&(pool->notify_notempty)) != 0) {
        return threadpool_lock_failure;
    }
    if (pthread_mutex_unlock(&pool->lock) != 0) {
        return threadpool_lock_failure;
    }
    printf("added client req to pool\n");
    return 0;
}

int threadpool_destroy(threadpool_t *pool)
{
    int i, err = 0;
    if(pool == NULL) {
        return threadpool_invalid;
    }
    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpool_lock_failure;
    }
	if((pthread_cond_broadcast(&(pool->notify_notempty)) != 0) ||
		(pthread_cond_broadcast(&(pool->notify_notfull)) != 0) ||
	   (pthread_mutex_unlock(&(pool->lock)) != 0)) {
		err = threadpool_lock_failure;
	}
	for(i = 0; i < pool->num_of_threads; i++) {
		if(pthread_join(pool->threads[i]->pthread, NULL) != 0) {
			err = threadpool_thread_failure;
		}
	}
    if(!err) {
        threadpool_free(pool);
    }
    return err;
}

int threadpool_free(threadpool_t *pool)
{
    if(pool == NULL) {
        return -1;
    }
    /* Did we manage to allocate ? */
    if(pool->threads) {
        free(pool->threads);
        /* Because we allocate pool->threads after initializing the
           mutex and condition variable, we're sure they're
           initialized. Let's lock the mutex just in case. */
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify_notempty));
    }
    free(pool);    
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
        conn = pool->queue_head;
        pool->queue_head = pool->queue_head->next;
        pool->waiting_conn--;
		pool->handeled_conn++;
        pthread_mutex_unlock(&(pool->lock));
        gettimeofday(&conn->stat_req_dispatch, NULL);
        requestHandle(conn,pool->threads[thread_id]);
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
