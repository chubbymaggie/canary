/*
 * Developed by Qingkai Shi
 * Copy Right by Prism Research Group, HKUST and State Key Lab for Novel Software Tech., Nanjing University.  
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <set>

#include <boost/unordered_map.hpp>

#include "SignalRoutine.h"
#include "../Log.h"

/*
 * Define local log keys
 */
static pthread_key_t lrlog_key;
static pthread_key_t rlog_key;
static pthread_key_t wlog_key;
static pthread_key_t address_birthday_map_key;

/*
 * Define global log variables, each shared var has one
 */
static std::vector<g_llog_t *> llogs; // <g_llog_t *> size is not fixed, and should be determined at runtime
static g_flog_t flog;
static g_mlog_t mlog;

/*
 * write versions, each shared var has a version
 */
static unsigned* write_versions;

/*
 * locks for synchronization, each shared var has one
 */
static pthread_mutex_t* locks;
static pthread_mutex_t fork_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_init_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * mutex_t-> mutex_id hashmap
 * pthread_t -> thread_id hashmap
 */
static unsigned mutex_id = 0;
static boost::unordered_map<pthread_mutex_t *, unsigned> mutex_ht;
static boost::unordered_map<pthread_t, unsigned> thread_ht;

static l_rlog_t* rlogs[CANARY_THREADS_MAX];
static l_lrlog_t* lrlogs[CANARY_THREADS_MAX];
static l_wlog_t* wlogs[CANARY_THREADS_MAX];
static l_addmap_t* addlogs[CANARY_THREADS_MAX];
static unsigned onexternals[CANARY_THREADS_MAX];

/*
 * set of active threads, not the original tid
 * 
 * static std::set<unsigned> active_threads;
 * 
 * cannot use it!!
 */

static bool start = false;
static bool forking = false;
static bool main_started = false;

/*
 * number of shared variables
 */
static unsigned num_shared_vars = 0;
static unsigned num_local_vars = 0;

#ifdef NO_TIME_CMD
static struct timeval tpstart, tpend;
#endif

// seq record ready

static inline bool sready(pthread_t self, unsigned * tid) {
    bool contains = thread_ht.count(self);
    while (!contains && forking) {
        sched_yield();
        contains = thread_ht.count(self);
    }

    if (contains) {
        unsigned idtemp = thread_ht[self];
        *tid = idtemp;
        return onexternals[idtemp] == 0;
    }

    return false;
}

// concurrency record ready

static inline bool cready(pthread_t self, unsigned * tid) {
    return start && sready(self, tid);
}

static inline void lock(unsigned svId) {
    pthread_mutex_lock(&locks[svId]);
}

static inline void unlock(unsigned svId) {
    pthread_mutex_unlock(&locks[svId]);
}

static inline void forkLock() {
    pthread_mutex_lock(&fork_lock);
}

static inline void forkUnlock() {
    pthread_mutex_unlock(&fork_lock);
}

static inline void mutexInitLock() {
    pthread_mutex_lock(&mutex_init_lock);
}

static inline void mutexInitUnlock() {
    pthread_mutex_unlock(&mutex_init_lock);
}

/* we do not dump logs here, because it is time consuming
 * we store them in a global map, dump them at last
 * 
 * in practice, we can dump them here to avoid memory leak
 */
void close_read_log(void* log) {
}

void close_local_read_log(void* log) {
}

void close_write_log(void* log) {
}

void close_map_log(void* log) {
}

extern "C" {

    void OnInit(unsigned svsNum, unsigned lvsNum) {
        printf("[INFO] OnInit-Record (canary record)\n");
        num_shared_vars = svsNum;
        num_local_vars = lvsNum;
        initializeSigRoutine();

        pthread_key_create(&rlog_key, close_read_log);
        pthread_key_create(&lrlog_key, close_local_read_log);
        pthread_key_create(&wlog_key, close_write_log);
        pthread_key_create(&address_birthday_map_key, close_map_log);

        memset(addlogs, 0, sizeof (void*)*CANARY_THREADS_MAX);
        memset(wlogs, 0, sizeof (void*)*CANARY_THREADS_MAX);
        memset(rlogs, 0, sizeof (void*)*CANARY_THREADS_MAX);
        memset(lrlogs, 0, sizeof (void*)*CANARY_THREADS_MAX);
        memset(onexternals, 0, sizeof (unsigned)*CANARY_THREADS_MAX);

        write_versions = new unsigned[svsNum];
        memset(write_versions, 0, sizeof (unsigned) * svsNum);
        locks = new pthread_mutex_t[svsNum];

        for (unsigned i = 0; i < svsNum; i++) {
            pthread_mutex_init(&locks[i], NULL);
        }

        // main thread.
        pthread_t tid = pthread_self();
        thread_ht[tid] = thread_ht.size();

        main_started = true;

#ifdef NO_TIME_CMD
        gettimeofday(&tpstart, NULL);
#endif
    }

    void OnExit() {
#ifdef NO_TIME_CMD
        gettimeofday(&tpend, NULL);
        double timeuse = 1000000 * (tpend.tv_sec - tpstart.tv_sec) + tpend.tv_usec - tpstart.tv_usec;
        timeuse /= 1000;
        printf("[INFO] Wall time is %lf ms\n", timeuse);
#endif
        start = false;

        printf("[INFO] OnExit-Record (canary record)\n");

        // dump, delete/free is not needed, because program will exit.
        {//flog
#ifdef DEBUG
            printf("dumping flog...\n");
#endif
            FILE * fout = fopen("fork.dat", "w");
            flog.dump(fout);
            fclose(fout);
        }

        {//mlog, llogs
#ifdef DEBUG
            printf("dumping mlog, llog...\n");
#endif
            FILE * fout = fopen("mutex.dat", "w");
            mlog.dump(fout);

            unsigned size = llogs.size();
#ifdef LDEBUG
            fprintf(fout, "Size: %u \n", size);
#else
            fwrite(&size, sizeof (unsigned), 1, fout);
#endif
            for (unsigned i = 0; i < size; i++) {
                g_llog_t * llog = llogs[i];
                llog->dump(fout);
            }
            fclose(fout);
        }

        {//rlog
#ifdef DEBUG
            printf("dumping rlog...\n");
#endif
            FILE * fout = fopen("read.dat", "w");
            for (unsigned tid = 0; tid < thread_ht.size(); tid++) {
                l_rlog_t* rlog = rlogs[tid];
                if (rlog == NULL) continue;
#ifdef LDEBUG
                fprintf(fout, "Thread %u \n", tid);
#else
                fwrite(&tid, sizeof (unsigned), 1, fout);
#endif
                for (unsigned i = 0; i < num_shared_vars; i++) {
                    rlog[i].VAL_LOG.dump(fout);
                    rlog[i].VER_LOG.dump(fout);
                }
            }
            fclose(fout);
        }
        {//lrlog
#ifdef DEBUG
            printf("dumping lrlog...\n");
#endif
            FILE * fout = fopen("lread.dat", "w");
            for (unsigned tid = 0; tid < thread_ht.size(); tid++) {
                l_lrlog_t* rlog = lrlogs[tid];
                if (rlog == NULL) continue;
#ifdef LDEBUG
                fprintf(fout, "Thread %u \n", tid);
#else
                fwrite(&tid, sizeof (unsigned), 1, fout);
#endif
                for (unsigned i = 0; i < num_local_vars; i++) {
                    rlog[i].dump(fout);
                }
            }
            fclose(fout);
        }
        {//wlog
#ifdef DEBUG
            printf("dumping wlog...\n");
#endif
            FILE * fout = fopen("write.dat", "w");
            for (unsigned tid = 0; tid < thread_ht.size(); tid++) {
                l_wlog_t* wlog = wlogs[tid];
                if (wlog == NULL) continue;
#ifdef LDEBUG
                fprintf(fout, "Thread %u \n", tid);
#else
                fwrite(&tid, sizeof (unsigned), 1, fout);
#endif
                for (unsigned i = 0; i < num_shared_vars; i++) {
                    wlog[i].dump(fout);
                }
            }
            fclose(fout);
        }
        {//address birthday map
#ifdef DEBUG
            printf("dumping addressmap...\n");
#endif
            FILE * fout = fopen("addressmap.dat", "w");

            for (unsigned tid = 0; tid < thread_ht.size(); tid++) {
                l_addmap_t * addmap = addlogs[tid];
                if (addmap == NULL) continue;

                unsigned size = addmap->adds.size();
#ifdef LDEBUG
                fprintf(fout, "Size = %u, Thread = %u\n", size, tid);
#else
                fwrite(&size, sizeof (unsigned), 1, fout);
                fwrite(&tid, sizeof (unsigned), 1, fout);
#endif
                for (unsigned i = 0; i < size; i++) {
                    mem_t * m = addmap->adds.at(i);
#ifdef LDEBUG
                    fprintf(fout, "(%p, %u, %d); ", m->address, m->range, m->type);
#else
                    fwrite(m, sizeof (mem_t), 1, fout);
#endif
                }
#ifdef LDEBUG
                fprintf(fout, "\n");
#endif
            }
            fclose(fout);
        }
        printf("[INFO] Threads num: %d\n", thread_ht.size());

        // zip
        system("if [ -f canary.zip ]; then rm canary.zip; fi");
        system("zip -9 canary.zip ./fork.dat ./mutex.dat ./addressmap.dat ./write.dat ./read.dat ./lread.dat");
        system("rm -f ./fork.dat ./mutex.dat ./addressmap.dat ./write.dat ./read.dat ./lread.dat");
        system("echo -n \"[INFO] Log size (Byte): \"; echo -n `du -sb canary.zip` | cut -d\" \" -f 1;");
    }

    unsigned OnPreExternalCall() {
        if (!main_started)
            return INVALID_THREAD_ID;

        unsigned tid = INVALID_THREAD_ID;
        sready(pthread_self(), &tid);
        if (tid != INVALID_THREAD_ID)
            onexternals[tid]++;

        return tid;
    }

    void OnExternalCall(unsigned tid, long value, int lvid) {
        if (tid == INVALID_THREAD_ID)
            return;
        unsigned oe = onexternals[tid];
        onexternals[tid] = oe - 1;

        if (oe == 1 && lvid != -1) {
            l_lrlog_t * rlog = (l_lrlog_t*) pthread_getspecific(lrlog_key);
            if (rlog == NULL) {
                rlog = new l_lrlog_t[num_local_vars];
                pthread_setspecific(lrlog_key, rlog);

                lrlogs[tid] = rlog;
            }

            rlog[lvid].logValue(value);
        }
    }

    void OnAddressInit(void* value, size_t size, size_t n, int type) {
        unsigned tid = 0; // should be zero
        if (!thread_ht.empty() && !sready(pthread_self(), &tid)) {
            return;
        }

#ifdef DEBUG
        printf("OnAddressInit\n");
#endif

        l_addmap_t * mlog = (l_addmap_t*) pthread_getspecific(address_birthday_map_key);
        if (mlog == NULL) {
            mlog = new l_addmap_t;
            mlog->stack_tag = false;
            pthread_setspecific(address_birthday_map_key, mlog);
            addlogs[tid] = mlog;
        }

        if (type == 1000) { // a stack address
            if (mlog->stack_tag) {
                return;
            } else {
                mlog->stack_tag = true;
            }
        }

        mem_t * m = new mem_t;
        m->address = value;
        m->range = size*n;
        m->type = type;
        mlog->adds.push_back(m);
    }

    void OnLocal(long value, int id) {
        unsigned tid = INVALID_THREAD_ID;
        if (!sready(pthread_self(), &tid)) {
            return;
        }

#ifdef DEBUG
        printf("OnLocal %d, %d, %ld\n", id, tid, value);
#endif
        l_lrlog_t * rlog = (l_lrlog_t*) pthread_getspecific(lrlog_key);
        if (rlog == NULL) {
            rlog = new l_lrlog_t[num_local_vars];
            pthread_setspecific(lrlog_key, rlog);

            lrlogs[tid] = rlog;
        }

        rlog[id].logValue(value);
    }

    void OnLoad(int svId, int lvId, long value, int debug) {
        if (!start) {
            if (lvId != -1) {
                OnLocal(value, lvId);
            }
            return;
        }

        unsigned tid = INVALID_THREAD_ID;
        if (!sready(pthread_self(), &tid)) {
            return;
        }

#ifdef DEBUG
        printf("OnLoad === 1\n");
#endif
        // using thread_key to tell whether log has been established
        // if so, use it, otherwise, new one
        l_rlog_t * rlog = (l_rlog_t*) pthread_getspecific(rlog_key);
        if (rlog == NULL) {
            rlog = new l_rlog_t[num_shared_vars];
            pthread_setspecific(rlog_key, rlog);

            rlogs[tid] = rlog;
        }

#ifdef DEBUG
        printf("OnLoad === 2\n");
#endif

        rlog[svId].VAL_LOG.logValue(value);
        rlog[svId].VER_LOG.logValue(write_versions[svId]);
    }

    unsigned OnPreStore(int svId, int debug) {
        unsigned tid = INVALID_THREAD_ID;
        if (!cready(pthread_self(), &tid)) {
            return 0;
        }

        lock(svId);
#ifdef DEBUG
        printf("OnPreStore: %d [%d]\n", svId, debug);
#endif
        unsigned version = write_versions[svId];
        write_versions[svId] = version + 1;

        return version;
    }

    void OnStore(int svId, unsigned version, int debug) {
        unsigned tid = INVALID_THREAD_ID;
        if (!cready(pthread_self(), &tid)) {
            return;
        }
#ifdef DEBUG
        printf("OnStore\n");
#endif
        unlock(svId);

        l_wlog_t * wlog = (l_wlog_t*) pthread_getspecific(wlog_key);
        if (wlog == NULL) {
            wlog = new l_wlog_t[num_shared_vars];
            pthread_setspecific(wlog_key, wlog);

            wlogs[tid] = wlog;
        }
        wlog[svId].logValue(version);
    }

    void OnLock(pthread_mutex_t* mutex_ptr) {
        unsigned tid = INVALID_THREAD_ID;
        if (!cready(pthread_self(), &tid)) {
            return;
        }

        // if map does not contain it, it must be a bug
        // because lock and init have a race.
        if (!mutex_ht.count(mutex_ptr)) {
            fprintf(stderr, "[ERROR] Program bug- mutex is not initialized before lock()!\n");
            OnExit();
            exit(1);
        }

        // here, the same address mutex_ptr is impossible to init a new lock
        // because the lock has been locked
        // therefore mutex_ht[mutex_ptr] is safe
        g_llog_t* llog = llogs[mutex_ht[mutex_ptr]];
        llog->logValue(pthread_self()); // exchange to _tid at last

#ifdef DEBUG
        printf("OnLock --> t%d\n", thread_ht[pthread_self()]);
#endif
    }

    void OnWait(pthread_cond_t* cond_ptr, pthread_mutex_t* mutex_ptr) {
        unsigned tid = INVALID_THREAD_ID;
        if (!cready(pthread_self(), &tid)) {
            return;
        }
#ifdef DEBUG
        printf("OnWait\n");
#endif

        if (!mutex_ht.count(mutex_ptr)) {
            fprintf(stderr, "[ERROR] Program bug- mutex is not initialized before wait()!\n");
            OnExit();
            exit(1);
        }
        g_llog_t* llog = llogs[mutex_ht[mutex_ptr]];
        llog->logValue(tid);
    }

    unsigned OnPreMutexInit(bool race) {
        unsigned tid = INVALID_THREAD_ID;
        if (sready(pthread_self(), &tid) && race) {
            mutexInitLock();
        }
        return tid;
    }

    void OnMutexInit(pthread_mutex_t* mutex_ptr, bool race, unsigned tid) {
        if (tid == INVALID_THREAD_ID) {
            return;
        }

        // if mutex_ht contains mutex_ptr, it means that the original one may be
        // destroyed; No matther mutex_ht contains it or not, it is a new lock
        mutex_ht[mutex_ptr] = mutex_id++;
        g_llog_t * llog = new g_llog_t;
        llogs.push_back(llog);


        if (race) {
            mlog.logValue(tid);

            mutexInitUnlock();
        }
    }

    unsigned OnPreFork(bool race) {
        unsigned tid = INVALID_THREAD_ID;
        if (!sready(pthread_self(), &tid)) {
            return tid;
        }

        if (!start)
            start = true;

#ifdef DEBUG
        printf("OnPreFork\n");
#endif
        if (race) forkLock();
        if (thread_ht.size() >= CANARY_THREADS_MAX) {
            fprintf(stderr, "[ERROR] Program bug- too many threads!!\n");
            OnExit();
            exit(1);
        }

        forking = true; // I am forking new thread.

        return tid;
    }

    void OnFork(pthread_t* forked_tid_ptr, bool race, unsigned tid) {
        if (tid == INVALID_THREAD_ID) {
            return;
        }
#ifdef DEBUG
        printf("OnFork\n");
#endif

        pthread_t ftid = *(forked_tid_ptr);
        if (!thread_ht.count(ftid)) {
            thread_ht[ftid] = thread_ht.size();
        }

        forking = false; // Forking finishes.

        if (race) {
            flog.logValue(tid);

            forkUnlock();
        }
    }
}

/* ************************************************************************
 * Signal Process
 * ************************************************************************/

void sigroutine(int dunno) {
    printSigInformation(dunno);

    OnExit();
    exit(dunno);
}
