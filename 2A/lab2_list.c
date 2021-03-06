//NAME: Conner Yang
//EMAIL: conneryang@g.ucla.edu
//ID: 905417287


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "SortedList.h"

int opt_yield;

pthread_mutex_t mutex;
long lock;

    // Option-getters
char sync_type = 0;
int num_threads = 1;
int num_iterations = 1;

int* thread_ids = NULL;     // Identifies which thread it is in the thread_worker func
pthread_t* threads = NULL;

SortedList_t* listhead;

    // First dimension points is thread number
    // Second dimension is element number
SortedListElement_t** pool;     // 2-D array for easier navigation

char* keys;     // holds the keys of the elements of the linked list

void process_args(int argc, char * argv[])
{
        // Collect options
    int option;
    
    static struct option options[] = {
      {"iterations", required_argument, NULL, 'i'},
      {"threads", required_argument, NULL, 't'},
      {"yield", required_argument, NULL, 'y'},
      {"sync", required_argument, NULL, 's'},
      {0, 0, 0, 0}
    };
    
    while (1)
    {
        option = getopt_long(argc, argv, "i:t:y:s:", options, NULL);
        if (option == -1) break;
        switch (option)
        {
          case 'i':
                num_iterations = atoi(optarg);
                if (num_iterations < 0)
                {
                    fprintf(stderr, "The number of iterations must be positive\n");
                }
                break;
          case 't':
                num_threads = atoi(optarg);
                if (num_threads < 0)
                {
                    fprintf(stderr, "The number of threads must be positive\n");
                }
                break;
          case 'y':
                for (size_t i = 0; i < strlen(optarg); i++)
                {
                    switch (optarg[i])
                    {
                        case 'i':
                            opt_yield |= INSERT_YIELD;
                            break;
                        case 'd':
                            opt_yield |= DELETE_YIELD;
                            break;
                        case 'l':
                            opt_yield |= LOOKUP_YIELD;
                            break;
                        default:
                            fprintf(stderr, "Error: Correct usage is --sync=m, s, or c\n");
                            exit(1);
                    }
                }
                break;
          case 's':
                if (optarg[0] != 'm' && optarg[0] != 's')
                {
                    fprintf(stderr, "Error: Correct usage is --sync=m, s, or c\n");
                    exit(1);
                }
                sync_type = optarg[0];
                break;
          default:
                fprintf(stderr, "Correct options: --iterations= --threads=: %s\n", strerror(errno));
                exit(1);
        }
    }
}

void lock_func()
{
    if (sync_type == 'm') pthread_mutex_lock(&mutex);
    if (sync_type == 's')
      while (__sync_lock_test_and_set(&lock, 1))
        while (lock) continue;
}

void unlock_func()
{
    if (sync_type == 's') __sync_lock_release(&lock);
    else if (sync_type == 'm') pthread_mutex_unlock(&mutex);
}

void* process_list(void* threadID)
{
  int thread_id = *(int*) threadID;

    // Insert elements into linked list
  for (int i = 0; i < num_iterations; i++)
  {
    lock_func();
    SortedList_insert(listhead, (pool[thread_id]) + i);
    unlock_func();
  }

    
    // Checks length (in order to check for corruption)
  lock_func();
  if (SortedList_length(listhead) < 0)
  {
      fprintf(stderr, "Race condition during length function!\n");
      exit(2);
  }
  unlock_func();

    
    // Lookup each inserted element and delete
  for (int i = 0; i < num_iterations; i++)
  {
    lock_func();
    SortedListElement_t* e = SortedList_lookup(listhead, pool[thread_id][i].key);
      // Check for race conditions
    if (pool == NULL)
    {
        fprintf(stderr, "Race condition during lookup!\n");
        exit(2);
    }
      // Delete and check for race conditions
    if (SortedList_delete(e) == 1)
    {
        fprintf(stderr, "Race condition during delete!\n");
        exit(2);
    }
    unlock_func();
  }

  return NULL;
}

void print_out(long long total_runtime)
{
        // Get average time per operation
    int operations = 3 * num_threads * num_iterations;
    long long avg_time_per_op = total_runtime / operations;
    int num_lists = 1;
    
    char name[24];
    strcpy(name, "list-");
    if (!opt_yield)
        strcat(name, "none");
    else
    {
        if (opt_yield & INSERT_YIELD)
            strcat(name, "i");
        if (opt_yield & DELETE_YIELD)
            strcat(name, "d");
        if (opt_yield & LOOKUP_YIELD)
            strcat(name, "l");
    }
    if (sync_type == 'm')
        strcat(name, "-m");
    else if (sync_type == 's')
        strcat(name, "-s");
    else
        strcat(name, "-none");

    printf("%s,%d,%d,%d,%d,%lld,%lld\n", name, num_threads, num_iterations, num_lists, operations, total_runtime, avg_time_per_op);
}

void signalHandler(int signum)
{
    if (signum == SIGSEGV)
    {
        fprintf(stderr, "Segmentation fault occurred!\n");
        exit(2);
    }
}

void free_memory(void)
{
    if (threads != NULL)
        free(threads);
    if (thread_ids != NULL)
        free(thread_ids);
    if (listhead != NULL)
        free(listhead);

    for (int t = 0; t < num_threads; t++)
    {
        if (pool[t] != NULL)
            free(pool[t]);
    }
    
    if (pool != NULL)
        free(pool);
    if (keys != NULL)
        free(keys);
}

int main(int argc, char* argv[])
{
    process_args(argc, argv);

        // Initializes and sets up stage for list operations
    signal(SIGSEGV, signalHandler);

        // Create head pointer of the linked list
    listhead = (SortedList_t*) malloc(sizeof(SortedList_t));
    listhead->next = listhead->prev = listhead;
    listhead->key = NULL;       // Head pointer has a key of NULL

        // Create 2-D array of elements and 1-D array of respective keys
    pool = malloc(num_threads * sizeof(SortedListElement_t*));
    keys = malloc(num_threads * num_iterations * sizeof(char));
    
        // Initialize the above arrays
    for (int thread_num = 0; thread_num < num_threads; thread_num++)
    {
        pool[thread_num] = malloc(num_iterations * sizeof(SortedListElement_t));
        for (int i = 0; i < num_iterations; i++)
        {
            keys[thread_num * i + i] = (char)('A' + rand() % 26);  // Random key value
            pool[thread_num][i].key = keys + ((thread_num * i) + i);
        }
    }

        // Allocate memory for the threads and the array of thread_ids
    threads = malloc(num_threads * sizeof(pthread_t));
    thread_ids = malloc(num_threads * sizeof(int));

        // Initialize mutex lock if needed
    if (sync_type == 'm')
    {
        if (pthread_mutex_init(&mutex, NULL) != 0)
        {
            fprintf(stderr, "Error intiializing mutex lock\n");
            exit(1);
        }
    }
    atexit(free_memory);

    struct timespec start_runtime, end_runtime;
    if (clock_gettime(CLOCK_MONOTONIC, &start_runtime) == -1)
    {
        fprintf(stderr, "Error getting start time\n");
        exit(1);
    }
    
    for (int thread_num = 0; thread_num < num_threads; thread_num++)
    {
        thread_ids[thread_num] = thread_num;
        if (pthread_create(&threads[thread_num], NULL, process_list, &thread_ids[thread_num]) != 0)
        {
            fprintf(stderr, "Error creating threads\n");
            exit(1);
        }
    }
    
    for (int t = 0; t < num_threads; t++)
    {
        if (pthread_join(threads[t], NULL))
        {
            fprintf(stderr, "Error joining threads\n");
            exit(1);
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end_runtime) == -1)
    {
        fprintf(stderr, "Error getting end time\n");
        exit(1);
    }

        // Final check for corruption after threads execute
    if (SortedList_length(listhead) != 0)
    {
        fprintf(stderr, "Race condition while using length function final time!\n");
        exit(2);
    }

    long long total_runtime = 1000000000 * (end_runtime.tv_sec - start_runtime.tv_sec) + end_runtime.tv_nsec - start_runtime.tv_nsec;

    print_out(total_runtime);
    exit(0);
}
