/*
 * Copyright (C) 2003 Robert Lougher <rob@lougher.demon.co.uk>.
 *
 * This file is part of JamVM.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "jam.h"
#include "thread.h"
#include "lock.h"

#ifdef TRACETHREAD
#define TRACE(x) printf x
#else
#define TRACE(x)
#endif

static int java_stack_size;

/* Thread create/destroy lock and condvar */
static pthread_mutex_t lock;
static pthread_cond_t cv;

/* lock and condvar used by main thread to wait for
 * all non-daemon threads to die */
static pthread_mutex_t exit_lock;
static pthread_cond_t exit_cv;

/* Monitor for sleeping threads to do a timed-wait against */
static Monitor sleep_mon;

/* Thread specific key holding thread's Thread pntr */
static pthread_key_t threadKey;

/* Attributes for spawned threads */
static pthread_attr_t attributes;

/* The main thread info - head of the thread list */
static Thread main, dead_thread;

/* Main thread ExecEnv */
static ExecEnv main_ee;

/* Various field offsets into java.lang.Thread -
 * cached at startup and used in thread creation */
static int vmData_offset;
static int daemon_offset;
static int group_offset;
static int priority_offset;
static int name_offset;

/* Method table indexes of Thread.run method and
 * ThreadGroup.removeThread - cached at startup */
static int run_mtbl_idx;
static int rmveThrd_mtbl_idx;

/* Cached java.lang.Thread class */
static Class *thread_class;

/* Count of non-daemon threads still running in VM */
static int non_daemon_thrds = 0;

/* Bitmap - used for generating unique thread ID's */
#define MAP_INC 32
static unsigned int *tidBitmap;
static int tidBitmapSize = 0;

/* Mark a threadID value as no longer used */
#define freeThreadID(n) tidBitmap[n>>5] &= ~(1<<(n&0x1f))

/* Generate a new thread ID - assumes the thread queue
 * lock is held */

static int genThreadID() {
    int i = 0;

retry:
    for(; i < tidBitmapSize; i++) {
        if(tidBitmap[i] != 0xffffffff) {
            int n = ffs(~tidBitmap[i]);
            tidBitmap[i] |= 1 << (n-1);
            return (i<<5) + n;
	}
    }

    tidBitmap = (unsigned int *)realloc(tidBitmap, (tidBitmapSize + MAP_INC) * sizeof(unsigned int));
    memset(tidBitmap + tidBitmapSize, 0, MAP_INC * sizeof(unsigned int));
    tidBitmapSize += MAP_INC;
    goto retry;
}

int threadIsAlive(Thread *thread) {
    return thread->state != 0;
}

int threadInterrupted(Thread *thread) {
    int r = thread->interrupted;
    thread->interrupted = FALSE;
    return r;
}

int threadIsInterrupted(Thread *thread) {
    return thread->interrupted;
}

void threadSleep(Thread *thread, long long ms, int ns) {
    monitorLock(&sleep_mon, thread);
    monitorWait(&sleep_mon, thread, ms, ns);
    monitorUnlock(&sleep_mon, thread);
}

void threadYield(Thread *thread) {
    pthread_yield();
}

void threadInterrupt(Thread *thread) {
    Monitor *mon = thread->wait_mon;

    thread->interrupted = TRUE;

    if(mon) {
        Thread *self = threadSelf();
        char owner = (mon->owner == self);
        if(!owner) {
            int i;

	    /* Another thread may be holding the monitor -
	     * if we can't get it after a couple attempts give-up
	     * to avoid deadlock */
            for(i = 0; i < 5; i++) {
                if(!pthread_mutex_trylock(&mon->lock))
                    goto got_lock;
                pthread_yield();
                if(thread->wait_mon != mon)
                    return;
            }
            return;
        }
got_lock:
        if((thread->wait_mon == mon) && thread->interrupted && !thread->interrupting &&
                  ((mon->notifying + mon->interrupting) < mon->waiting)) {
            thread->interrupting = TRUE;
            mon->interrupting++;
            pthread_cond_broadcast(&mon->cv);
        }
        if(!owner)
            pthread_mutex_unlock(&mon->lock);
    }
}

void *getStackTop(Thread *thread) {
    return thread->stack_top;
}

void *getStackBase(Thread *thread) {
    return thread->stack_base;
}

Thread *threadSelf0(Object *jThread) {
    return (Thread*)(INST_DATA(jThread)[vmData_offset]);
}

Thread *threadSelf() {
    return (Thread*)pthread_getspecific(threadKey);
}

void setThreadSelf(Thread *thread) {
   pthread_setspecific(threadKey, thread);
}

ExecEnv *getExecEnv() {
    return threadSelf()->ee;
}

void initialiseJavaStack(ExecEnv *ee) {
   char *stack = malloc(java_stack_size);
   MethodBlock *mb = (MethodBlock *) stack;
   Frame *top = (Frame *) (mb+1);

   mb->max_stack = 0;
   top->mb = mb;
   top->ostack = (u4*)(top+1);
   top->prev = 0;

   ee->stack = stack;
   ee->last_frame = top;
   ee->stack_end = stack + java_stack_size-1024;
}

void *threadStart(void *arg) {
    Thread *thread = (Thread *)arg;
    ExecEnv *ee = thread->ee;
    Object *jThread = ee->thread;
    ClassBlock *cb = CLASS_CB(jThread->class);
    MethodBlock *run = cb->method_table[run_mtbl_idx];
    Object *group, *excep;

    TRACE(("Thread 0x%x id: %d started\n", thread, thread->id));

    initialiseJavaStack(ee);
    setThreadSelf(thread);

    /* Need to disable suspension as we'll most likely
     * be waiting on lock when we're added to the thread
     * list, and now liable for suspension */

    thread->stack_base = &group;
    disableSuspend0(thread, &group);

    pthread_mutex_lock(&lock);
    thread->id = genThreadID();

    thread->state = STARTED;
    pthread_cond_broadcast(&cv);

    while(thread->state != RUNNING)
        pthread_cond_wait(&cv, &lock);
    pthread_mutex_unlock(&lock);

    /* Execute the thread's run() method... */
    enableSuspend(thread);
    executeMethod(jThread, run);

    /* Call thread group's uncaughtException if exception
     * is of type java.lang.Throwable */

    group = (Object *)INST_DATA(jThread)[group_offset];
    if(excep = exceptionOccured()) {
        Class *throwable;
	MethodBlock *uncaught_exp;
       
	clearException();
	throwable = findSystemClass0("java/lang/Throwable");
        if(throwable && isInstanceOf(throwable, excep->class)
                     && (uncaught_exp = lookupMethod(group->class, "uncaughtException",
			                              "(Ljava/lang/Thread;Ljava/lang/Throwable;)V")))
            executeMethod(group, uncaught_exp, jThread, excep);
	else {
            setException(excep);
            printException();
	}
    }

    /* remove thread from thread group */
    executeMethod(group, (CLASS_CB(group->class))->method_table[rmveThrd_mtbl_idx], jThread);

    objectLock(jThread);
    objectNotifyAll(jThread);
    thread->state = 0;
    objectUnlock(jThread);

    disableSuspend0(thread, &group);
    pthread_mutex_lock(&lock);

    /* remove from thread list... */

    if((thread->prev->next = thread->next))
        thread->next->prev = thread->prev;

    if(!INST_DATA(jThread)[daemon_offset])
        non_daemon_thrds--;

    freeThreadID(thread->id);

    pthread_mutex_unlock(&lock);
    enableSuspend(thread);

    INST_DATA(jThread)[vmData_offset] = (u4)&dead_thread;
    free(thread);
    free(ee->stack);
    free(ee);

    if(non_daemon_thrds == 0) {
        /* No need to bother with disabling suspension
	 * around lock, as we're no longer on thread list */
        pthread_mutex_lock(&exit_lock);
        pthread_cond_signal(&exit_cv);
        pthread_mutex_unlock(&exit_lock);
    }

    TRACE(("Thread 0x%x id: %d exited\n", thread, thread->id));
}

void createJavaThread(Object *jThread) {
    ExecEnv *ee;
    Thread *thread;
    Thread *self = threadSelf();

    disableSuspend(self);

    pthread_mutex_lock(&lock);
    if(INST_DATA(jThread)[vmData_offset]) {
        pthread_mutex_unlock(&lock);
        enableSuspend(self);
        signalException("java/lang/IllegalThreadStateException", "thread already started");
	return;
    }

    ee = (ExecEnv*)malloc(sizeof(ExecEnv));
    thread = (Thread*)malloc(sizeof(Thread));
    memset(ee, 0, sizeof(ExecEnv));
    memset(thread, 0, sizeof(Thread));

    thread->ee = ee;
    ee->thread = jThread;
    INST_DATA(jThread)[vmData_offset] = (u4)thread;
    pthread_mutex_unlock(&lock);

    if(pthread_create(&thread->tid, &attributes, threadStart, thread)) {
        INST_DATA(jThread)[vmData_offset] = 0;
        free(ee);
	free(thread);
        enableSuspend(self);
	signalException("java/lang/OutOfMemoryError", "can't create thread");
	return;
    }

    pthread_mutex_lock(&lock);
    while(thread->state != STARTED)
        pthread_cond_wait(&cv, &lock);

    /* add to thread list... */

    if((thread->next = main.next))
        main.next->prev = thread;
    thread->prev = &main;
    main.next = thread;

    if(!INST_DATA(jThread)[daemon_offset])
        non_daemon_thrds++;

    thread->state = RUNNING;
    pthread_cond_broadcast(&cv);

    pthread_mutex_unlock(&lock);
    enableSuspend(self);
}

Thread *attachThread(char *name, char is_daemon, void *stack_base) {
    ExecEnv *ee;
    Thread *thread;

    ee = (ExecEnv*)malloc(sizeof(ExecEnv));
    thread = (Thread*)malloc(sizeof(Thread));
    memset(ee, 0, sizeof(ExecEnv));
    memset(thread, 0, sizeof(Thread));

    thread->tid = pthread_self();
    thread->state = RUNNING;
    thread->stack_base = stack_base;
    thread->ee = ee;

    initialiseJavaStack(ee);
    setThreadSelf(thread);

    ee->thread = allocObject(thread_class);

    INST_DATA(ee->thread)[daemon_offset] = FALSE;
    INST_DATA(ee->thread)[name_offset] = (u4)Cstr2String(name);
    INST_DATA(ee->thread)[group_offset] = INST_DATA(main_ee.thread)[group_offset];
    INST_DATA(ee->thread)[priority_offset] = 5;
    INST_DATA(ee->thread)[vmData_offset] = (u4)thread;

    /* add to thread list... */

    pthread_mutex_lock(&lock);
    if((thread->next = main.next))
        main.next->prev = thread;
    thread->prev = &main;
    main.next = thread;

    if(!is_daemon)
        non_daemon_thrds++;

    thread->id = genThreadID();
    pthread_mutex_unlock(&lock);

    TRACE(("Thread 0x%x id: %d attached\n", thread, thread->id));
    return thread;
}

static void *shell(void *args) {
    void *start = ((void**)args)[1];
    Thread *self = attachThread(((char**)args)[0], TRUE, &self);

    free(args);
    (*(void(*)(Thread*))start)(self);
}

void createVMThread(char *name, void (*start)(Thread*)) {
    void **args = malloc(2 * sizeof(void*));
    pthread_t tid;

    args[0] = name;
    args[1] = start;
    pthread_create(&tid, &attributes, shell, args);
}

void mainThreadWaitToExitVM() {
    Thread *self = threadSelf();
    TRACE(("Waiting for %d non-daemon threads to exit\n", non_daemon_thrds));

    disableSuspend(self);
    pthread_mutex_lock(&exit_lock);

    self->state = WAITING;
    while(non_daemon_thrds)
        pthread_cond_wait(&exit_cv, &exit_lock);

    pthread_mutex_unlock(&exit_lock);
    enableSuspend(self);
}

void suspendAllThreads(Thread *self) {
    Thread *thread;

    TRACE(("Thread 0x%x id: %d is suspending all threads\n", self, self->id));
    pthread_mutex_lock(&lock);

    for(thread = &main; thread != NULL; thread = thread->next) {
        if(thread == self)
            continue;
	thread->suspend = TRUE;
	if(!thread->blocking)
	    pthread_kill(thread->tid, SIGUSR1);
    }

    for(thread = &main; thread != NULL; thread = thread->next) {
        if(thread == self)
            continue;
	while(!thread->blocking && thread->state != SUSPENDED)
            pthread_yield();
    }

    TRACE(("All threads suspended...\n"));
    pthread_mutex_unlock(&lock);
}

void resumeAllThreads(Thread *self) {
    Thread *thread;

    TRACE(("Thread 0x%x id: %d is resuming all threads\n", self, self->id));
    pthread_mutex_lock(&lock);

    for(thread = &main; thread != NULL; thread = thread->next) {
        if(thread == self)
            continue;
	thread->suspend = FALSE;
	if(!thread->blocking)
	    pthread_kill(thread->tid, SIGUSR1);
    }

    for(thread = &main; thread != NULL; thread = thread->next) {
	while(thread->state == SUSPENDED)
            pthread_yield();
    }

    TRACE(("All threads resumed...\n"));
    pthread_mutex_unlock(&lock);
}

static void suspendLoop(Thread *thread) {
    char old_state = thread->state;
    sigset_t mask;
    sigjmp_buf env;

    sigsetjmp(env, FALSE);

    thread->stack_top = &env;
    thread->state = SUSPENDED;

    sigfillset(&mask);
    sigdelset(&mask, SIGUSR1);
    sigdelset(&mask, SIGTERM);

    while(thread->suspend)
        sigsuspend(&mask);

    thread->state = old_state;
}

static void suspendHandler(int sig) {
    Thread *thread = threadSelf();
    suspendLoop(thread);
}

void disableSuspend0(Thread *thread, void *stack_top) {
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    thread->stack_top = stack_top;
    thread->blocking = TRUE;
}

void enableSuspend(Thread *thread) {
    sigset_t mask;

    sigemptyset(&mask);

    thread->blocking = FALSE;

    if(thread->suspend)
        suspendLoop(thread);

    sigaddset(&mask, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
}

void *dumpThreadsLoop(void *arg) {
    Thread *thread, dummy;
    sigset_t mask;
    int sig;

    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);

    for(;;) {
	sigwait(&mask, &sig);

	if(sig == SIGINT)
            exit(0);

	suspendAllThreads(&dummy);
        printf("Thread Dump\n-----------\n\n");
        for(thread = &main; thread != NULL; thread = thread->next) {
            char *name = String2Cstr((Object*)(INST_DATA(thread->ee->thread)[name_offset]));
            printf("Thread: %s 0x%x tid: %d state: %d\n", name, thread, thread->tid, thread->state);
	    free(name);
        }
	resumeAllThreads(&dummy);
    }
}

static void initialiseSignals() {
    struct sigaction act;
    sigset_t mask;
    pthread_t tid;

    act.sa_handler = suspendHandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGUSR1, &act, NULL);

    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    pthread_create(&tid, &attributes, dumpThreadsLoop, NULL);
}

/* garbage collection support */

extern void scanThread(Thread *thread);

void scanThreads() {
    Thread *thread;

    for(thread = &main; thread != NULL; thread = thread->next)
        scanThread(thread);
}

int systemIdle(Thread *self) {
    Thread *thread;

    for(thread = &main; thread != NULL; thread = thread->next)
        if(thread != self && thread->state < WAITING)
            return FALSE;

    return TRUE;
}

void initialiseMainThread(int stack_size) {
    Class *thrdGrp_class;
    FieldBlock *vmData;
    MethodBlock *run, *remove_thread, uncaught_exp;
    FieldBlock *daemon, *name, *group, *priority, *root;

    java_stack_size = stack_size;

    pthread_key_create(&threadKey, NULL);

    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cv, NULL);

    pthread_mutex_init(&exit_lock, NULL);
    pthread_cond_init(&exit_cv, NULL);

    monitorInit(&sleep_mon);

    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    main.stack_base = &thrdGrp_class;

    main.tid = pthread_self();
    main.id = genThreadID();
    main.state = RUNNING;
    main.ee = &main_ee;

    initialiseJavaStack(&main_ee);
    setThreadSelf(&main);

    /* As we're initialising, VM will abort if Thread can't be found */
    thread_class = findSystemClass0("java/lang/Thread");

    vmData = findField(thread_class, "vmData", "I");
    daemon = findField(thread_class, "daemon", "Z");
    name = findField(thread_class, "name", "Ljava/lang/String;");
    group = findField(thread_class, "group", "Ljava/lang/ThreadGroup;");
    priority = findField(thread_class, "priority", "I");

    run = findMethod(thread_class, "run", "()V");

    /* findField and findMethod do not throw an exception... */
    if((vmData == NULL) || (run == NULL) || (daemon == NULL) || (name == NULL) ||
           (group == NULL) || (priority == NULL))
        goto error;

    vmData_offset = vmData->offset;
    daemon_offset = daemon->offset;
    group_offset = group->offset;
    priority_offset = priority->offset;
    name_offset = name->offset;
    run_mtbl_idx = run->method_table_index;

    main_ee.thread = allocObject(thread_class);

    thrdGrp_class = findSystemClass("java/lang/ThreadGroup");
    if(exceptionOccured()) {
        printException();
	exit(1);
    }

    root = findField(thrdGrp_class, "root", "Ljava/lang/ThreadGroup;");
    remove_thread = findMethod(thrdGrp_class, "removeThread", "(Ljava/lang/Thread;)V");

    /* findField and findMethod do not throw an exception... */
    if((root == NULL) || (remove_thread == NULL))
        goto error;

    rmveThrd_mtbl_idx = remove_thread->method_table_index;

    INST_DATA(main_ee.thread)[daemon_offset] = FALSE;
    INST_DATA(main_ee.thread)[name_offset] = (u4)Cstr2String("main");
    INST_DATA(main_ee.thread)[group_offset] = root->static_value;
    INST_DATA(main_ee.thread)[priority_offset] = 5;

    INST_DATA(main_ee.thread)[vmData_offset] = (u4)&main;

    initialiseSignals();

    return;

error:
    printf("Error initialising VM (initialiseMainThread)\n");
    exit(0);
}

