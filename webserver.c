

/*
This is the main program for the simple webserver.
Your job is to modify it to take additional command line
arguments to specify the number of threads and scheduling mode,
and then, using a pthread monitor, add support for a thread pool.
All of your changes should be made to this file, with the possible
exception that you may add items to struct request in request.h
*/

#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include<sys/stat.h>
#include "tcp.h"
#include "request.h"


#define FCFS 1
#define SFF 2

int mode = 1;


//intimaximum thread
int max_thread = 1;


//use of mutex -> to avoid race condition
//by using lock and unlock befor and after reading and writing

typedef struct rwl{
        pthread_mutex_t *mut;            //mutex for avoiding race condition
        int writers;                            //Number of Writers writing the data
        int readers;                              // number of Readersreading the data
        int waiting;                              // number of Writers waiting for their turn to write
        pthread_cond_t *writeOK;          //when reader completed reading
        pthread_cond_t *readOK;            //When writer completed writing
        int read_wait;
        int write_wait;
} rwl;


//To store the list of request
typedef struct Node{
        int file_size;
        struct request* req;    //to store current request data structure
        struct Node* next;  //to store the next request data structure
} Node;

//To implement queue
typedef struct Queue{
        Node *head;
        Node *front;
        Node *rear;
        int size ;
}Queue;

//Intialize the queue globally so that all thread can use it.
//use mutex lock inside thread  to avoid race condition and deadlock

Queue *queue;

/*declaration of function*/
void read_lock(rwl *lock);
void read_unlock(rwl *lock);
void write_lock(rwl *lock);
void write_unlock(rwl *lock);
void request_writer(struct request *req);
struct request *dequeue_request();
void * request_reader(void * arg);
rwl *mut_lock_int();
void create_queue();
struct request *dequeue_request_SFF();
void *request(void *arg);




int main( int argc, char *argv[] )
{
        if(argc == 2){

        }
        else if(argc<4) {
                fprintf(stderr,"use: %s <port>, <total thread> and <MODE> like <MODE> = FCFS or SFF\n",argv[0]);
                return 1;
        }

        //move to webdocs directory
        if(chdir("webdocs")!=0) {
                fprintf(stderr,"couldn't change to webdocs directory: %s\n",strerror(errno));
                return 1;
        }

        //typecast the string to integer
        int port = atoi(argv[1]);
        if(argc == 4){
                max_thread = atoi(argv[2]);

                if(strcmp(argv[3], "FCFS")== 0)
                {
                        mode = FCFS;
                }else if(strcmp(argv[3], "SFF")== 0)
                {
                        mode = SFF;
                }else{
                        printf("Enter a valid mode either FCFS or SFF\n");
                        exit(0);
                }
        }


        struct tcp *master = tcp_listen(port);
        if(!master) {
                fprintf(stderr,"couldn't listen on port %d: %s\n",port,strerror(errno));
                return 1;
        }
        rwl *mutex_lock;

        if(argc == 4){
                printf("webserver: waiting for requests..\n");

                printf("No error upto mut_lock_init\n");
                 //Intialize the lock
                 mutex_lock = mut_lock_int();
                 printf("No error just after mut_lock_init\n");

                 printf("fine upto create_queue\n");
                 //create queue
                 create_queue();
                 printf("fine after create_queue\n");

                 //create thread of pool
                pthread_t thread_pool[max_thread];
                //create all thread
                for(int i = 0 ; i < max_thread ; i++)
                {
                        //On success full creation of thread it will return 0 else return rrror number
                        if(pthread_create(&thread_pool[i], NULL, &request_reader, (void *)mutex_lock) != 0)
                        {
                                perror("Error wile creating thread : ");
                                exit(0);
                        }
                }

                printf("no error upto creating pool of thread\n");
        }
        while(1) {
                printf("hello");
                struct tcp *conn = tcp_accept(master,time(0)+300);

                if(argc == 4){
                        if(conn) {
                                printf("webserver: got new connection.\n");

                                printf("no error upto request_create\n");

                                struct request *req = request_create(conn);

                                printf("No error upto request_create\n");
                                if(req) {
                                        printf("webserver: got request for %s\n",req->filename);

                                        write_lock(mutex_lock); //to ensure that other thread donot use the queue
                                              //while the thread is writing for request
                                        printf("no error before calling request_writer\n");
                                        request_writer(req);
                                        printf("no error upto calling request_writer\n");
                                        write_unlock(mutex_lock); //unlock after writing request to queue successfully

                                } else {
                                        tcp_close(conn);
                                }
                        } else {
                                printf("webserver: shutting down because idle too long\n");
                                break;
                        }
                }else{
                        pthread_t th;
                        if(pthread_create(&th, NULL, &request, (void *)conn) != 0)
                        {
                                perror("Error wile creating thread : ");
                                exit(0);
                        }

                }
        }

        return 0;
}

void create_queue()
{
        queue = (Queue *) malloc(sizeof(Queue));
        queue->head = NULL;
        queue->front = NULL;
        queue->rear = NULL;
        queue->size = 0;


}


struct request *dequeue_request_SFF()
{
        struct request *req;
        int min;
        Node *pre;
        Node *pre2;
        Node *curr;
        Node *min_node;
        if(queue->head != NULL)
        {
                curr = queue->head;
                pre = queue->head;
                pre2 = queue->head;
                min = curr->file_size;
                min_node = curr;
                curr=curr->next;

                while(curr != NULL)
                {
                        if(curr->file_size < min)
                        {
                                pre = pre2;
                                min_node = curr;
                                min = curr->file_size;

                        }
                        pre2 = curr;
                        curr=curr->next;

                }
                         req = min_node->req;
                         if(min_node == queue->head)
                         {
                                 queue->head = min_node->next;
                         }else{
                                 pre->next = min_node->next;
                                 if(min_node->next == NULL)
                                 	queue->rear = pre;
                         }
                         free(min_node);


                         queue->size -= 1;

        }else{
                req = NULL;
        }

        return req;
}



struct request *dequeue_request()
{
        struct request *req;
        if(queue->head != NULL)
        {
                req = queue->front->req;

                queue->front = queue->front->next;
                free(queue->head);
                queue->head = queue->front;
                queue->size -= 1;

        }else{
                req = NULL;
        }

        return req;
}

void request_writer(struct request *req)
{
        struct stat *stats;
        Node *temp = (Node *)malloc(sizeof(Node));
        if(queue->head == NULL)
        {

                temp->req =  req;
                temp->next = NULL;


                queue->head = temp;
                queue->front = temp;
                queue->rear = temp;
                queue->size += 1;

                if(stat(req->filename, stats)== 0)
                {
                        queue->head->file_size = stats->st_size;
                }else{
                        perror("stat : unable to get file information");
                }
        }else{
                temp->req = req;
                temp->next = NULL;

                queue->rear->next = temp;
                queue->rear = temp;
                queue->size += 1;

                if(stat(req->filename, stats) == 0)
                {
                        temp->file_size = stats->st_size;
                }else{
                        perror("stat : unable to get file information");
                }

        }
}

rwl* mut_lock_int() {
                rwl *lock;
        //allocate the memory
        lock = (rwl*) malloc (sizeof (rwl));
        //If memory is not allocated retuen NULL
        if(lock == NULL)
                return (NULL);

        lock->mut = (pthread_mutex_t*) malloc (sizeof (pthread_mutex_t));
        if(lock->mut == NULL) {
         free(lock);
         return (NULL);
          }

        lock->writeOK = (pthread_cond_t*) malloc (sizeof (pthread_cond_t));
        if(lock->writeOK == NULL) {
                free (lock->mut);
                free (lock);
                return (NULL);
        }

        lock->readOK = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
        if(lock->readOK == NULL) {
         free (lock->mut);
         free (lock->writeOK);
         free (lock); return (NULL);
        }
        lock->readers=0;
        lock->writers=0;
        lock->read_wait=0;
        lock->write_wait=0;

        return lock;
}

//Function to process request
void* request_reader(void* args)
{
        struct request *req;
        rwl *m_lock = (rwl *)args;
        while(1)
        {
                read_lock(m_lock); //lock
                if(mode == FCFS)
                        req = dequeue_request();

                if(mode == SFF)
                        req = dequeue_request_SFF();

                read_unlock(m_lock);//unlock

                request_handle(req);
                printf("webserver: done sending %s\n",req->filename);
                request_delete(req);
        }
}

void *request(void *args)
{
        struct tcp *conn = (struct tcp *)args;
         if(conn) {
                                printf("webserver: got new connection.\n");

                                printf("no error upto request_create\n");

                                struct request *req = request_create(conn);

                                printf("No error upto request_create\n");
                                if(req) {
                                        printf("webserver: got request for %s\n",req->filename);

                                         request_handle(req);
                                         printf("webserver: done sending %s\n",req->filename);
                                         request_delete(req);


                                } else {
                                        tcp_close(conn);
                                }
                        } else {
                                printf("webserver: shutting down because idle too long\n");

                        }

}

void read_lock(rwl* lock) {
        pthread_mutex_lock(lock->mut);
        while(lock->writers || lock->readers || queue->size <= 0)
                pthread_cond_wait(lock->readOK, lock->mut);
        lock->readers++;
        pthread_mutex_unlock(lock->mut);
}

void read_unlock(rwl* lock) {
        pthread_mutex_lock(lock->mut);
        lock->readers--;
        pthread_cond_signal(lock->writeOK);
        pthread_cond_signal(lock->readOK);
        pthread_mutex_unlock(lock->mut);
}

void write_lock(rwl* lock) {
        pthread_mutex_lock(lock->mut);
        lock->write_wait++;

        while(lock->readers || queue->size == 10)
                pthread_cond_wait(lock->writeOK, lock->mut);

        lock->write_wait--;
        lock->writers++;
        pthread_mutex_unlock(lock->mut);
}

void write_unlock(rwl* lock) {
        pthread_mutex_lock(lock->mut);
        lock->writers--;
        pthread_cond_signal(lock->readOK);
        pthread_mutex_unlock(lock->mut);
}

