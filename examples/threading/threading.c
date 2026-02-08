#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    thread_func_args->thread_complete_success = false; // Default to false

    // 1. Wait to obtain
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // 2. Obtain mutex
    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0) {
        ERROR_LOG("Failed to lock mutex");
        return thread_param;
    }

    // 3. Wait to release
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // 4. Release mutex
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0) {
        ERROR_LOG("Failed to unlock mutex");
        return thread_param;
    }

    // Success!
    thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
   // 1. Allocate memory for thread_data
    struct thread_data* data = (struct thread_data*)malloc(sizeof(struct thread_data));
    if (data == NULL) {
        ERROR_LOG("Malloc failed");
        return false;
    }

    // 2. Setup the structure
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    // 3. Create the thread
    int rc = pthread_create(thread, NULL, threadfunc, data);
    
    if (rc != 0) {
        ERROR_LOG("Thread creation failed with code %d", rc);
        free(data); // Don't leak memory if creation fails!
        return false;
    }

    return true;
}

