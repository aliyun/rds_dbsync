

#ifndef PG_MISC_H
#define PG_MISC_H

#include "postgres_fe.h"

#include "lib/stringinfo.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pqexpbuffer.h"

#ifdef WIN32
typedef CRITICAL_SECTION pthread_mutex_t;
typedef HANDLE	ThreadHandle;
typedef DWORD	ThreadId;
typedef unsigned		thid_t;

typedef struct Thread
{
	ThreadHandle		os_handle;
	thid_t				thid;
}Thread;

typedef CRITICAL_SECTION pthread_mutex_t;
typedef DWORD		 pthread_t;
#define pthread_mutex_lock(A)	 (EnterCriticalSection(A),0)
#define pthread_mutex_trylock(A) win_pthread_mutex_trylock((A))
#define pthread_mutex_unlock(A)  (LeaveCriticalSection(A), 0)
#define pthread_mutex_init(A,B)  (InitializeCriticalSection(A),0)
#define pthread_mutex_lock(A)	 (EnterCriticalSection(A),0)
#define pthread_mutex_trylock(A) win_pthread_mutex_trylock((A))
#define pthread_mutex_unlock(A)  (LeaveCriticalSection(A), 0)
#define pthread_mutex_destroy(A) (DeleteCriticalSection(A), 0)


#else
typedef pthread_t	ThreadHandle;
typedef pthread_t	ThreadId;
typedef pthread_t	thid_t;

typedef struct Thread
{
	pthread_t	 os_handle;
} Thread;

#define SIGALRM				14
#endif 

#define MaxAllocSize	((Size) 0x3fffffff)		/* 1 gigabyte - 1 */

extern bool WaitThreadEnd(int n, Thread *th);
extern void ThreadExit(int code);
extern int ThreadCreate(Thread *th, void *(*start)(void *arg), void *arg);

extern PGconn *pglogical_connect(const char *connstring, const char *connname);
extern bool is_greenplum(PGconn *conn);
extern size_t quote_literal_internal(char *dst, const char *src, size_t len);
extern int start_copy_origin_tx(PGconn *conn, const char *snapshot, int pg_version);
extern int finish_copy_origin_tx(PGconn *conn);
extern int start_copy_target_tx(PGconn *conn, int pg_version, bool is_greenplum);
extern int finish_copy_target_tx(PGconn *conn);
extern int ExecuteSqlStatement(PGconn	   *conn, const char *query);
extern int setup_connection(PGconn *conn, int remoteVersion, bool is_greenplum);

#endif 


