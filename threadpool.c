#include "request.h"
#include "threadpool.h"
#include <stdlib.h>
#include "segel.h"
int finished = 0;
int inserted = 0;
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

int randRemove(threadpool_t *pool){
    int index = rand() % pool->waiting_conn;
    conn_t* temp;
    if (index == 0){
        temp = pool->queue_head;
        //printf("closing  %d\n",  temp->conn_fd);
        Close(temp->conn_fd);
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
        //printf("closing  %d\n",  curr->conn_fd);
        Close(curr->conn_fd);
        free(curr);
    }
    pool->waiting_conn --;
    return 0;
}

int randomDrop(threadpool_t *pool)
{
    srand(time(NULL));
    int size = ceil((double)pool->waiting_conn/4)+1e-9;
    for (int i = 0; i < size; i++) {
        //printf("dropped req number %d\n", ++finished);
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
    struct timeval curr_time;
    if (gettimeofday(&curr_time, NULL) != 0) {
        return threadpool_invalid;
    }
    int status = 0;
    //printf("inserting %d\n",  ++inserted);
    if (pool->waiting_conn + pool->handeled_conn >= pool->size_of_queue) {
        if (pool->policy == block) {
            if (pool->waiting_conn + pool->handeled_conn >= pool->size_of_queue) {
                pthread_cond_wait(&(pool->notify_notfull), &(pool->lock));
            }
            status = 0;
        } else if (pool->policy == drop_head && pool->waiting_conn > 0) {
            //printf("closing %d", pool->queue_head->conn_fd);
            //printf("dropped req number %d\n", ++finished);
            Close(pool->queue_head->conn_fd);
            conn_t *temp = pool->queue_head;
            pool->queue_head = pool->queue_head->next;
            free(temp);
            pool->waiting_conn--;
            status = 0;
        } else if (pool->policy == drop_tail ||
            ((pool->policy == drop_head ||pool->policy == random_drop) && pool->waiting_conn == 0)) {
            //printf("dropped req number %d\n", ++finished);
            //printf("closing %d\n", connfd);
            Close(connfd);
            status = 1;
        } else {
            if (randomDrop(pool) == -1) {
                return threadpool_invalid;
            }
            status = 0;
        }
    }
    if (status == 0){
        conn_t *conn = (conn_t *) malloc(sizeof(conn_t));
        conn->conn_fd = connfd;
        conn->req_arrival = curr_time;
        conn->next = NULL;
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
    }
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
        //printf("closing %d\n", conn->conn_fd);
        //printf("finished req num %d\n", ++finished);
		Close(conn->conn_fd);
        free(conn);
    }
    free(args);
    return NULL;
}
