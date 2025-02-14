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

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* args = (struct thread_data*) thread_param;


    usleep(args->wait_to_obtain_ms_data * 1000);
    pthread_mutex_lock(args->mutexLock);
    usleep(args->wait_to_release_ms_data * 1000);
    pthread_mutex_unlock(args->mutexLock);

    args->thread_complete_success = true;

    return thread_param;

}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

/**
* Start a thread which sleeps @param wait_to_obtain_ms number of milliseconds, then obtains the
* mutex in @param mutex, then holds for @param wait_to_release_ms milliseconds, then releases.
* The start_thread_obtaining_mutex function should only start the thread and should not block
* for the thread to complete.
* The start_thread_obtaining_mutex function should use dynamic memory allocation for thread_data
* structure passed into the thread.  The number of threads active should be limited only by the
* amount of available memory.
* The thread started should return a pointer to the thread_data structure when it exits, which can be used
* to free memory as well as to check thread_complete_success for successful exit.
* If a thread was started succesfully @param thread should be filled with the pthread_create thread ID
* coresponding to the thread which was started.
* @return true if the thread could be started, false if a failure occurred.
*/
    
    // Dynamically allocate the thread_data structure
    struct thread_data *data = malloc(sizeof(struct thread_data));
    if (data == NULL) {
        ERROR_LOG("Failed to allocate memory for thread_data\n");
        return false;
    }

    // Set up the structure
    data->mutexLock = mutex;
    data->wait_to_obtain_ms_data = wait_to_obtain_ms;
    data->wait_to_release_ms_data = wait_to_release_ms;
    data->thread_complete_success = false;

    // Create the thread; the new thread will free the allocated memory after use
    int result = pthread_create(thread, NULL, threadfunc, data);
    if (result != 0) {
        ERROR_LOG("pthread_create failed\n");
        free(data);  // Clean up memory since thread was not created
        return false;
    }

    return true;
}

