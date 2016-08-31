

#ifndef PG_SYNC_H
#define PG_SYNC_H

#include "postgres_fe.h"

#include "lib/stringinfo.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq/pqformat.h"
#include "pqexpbuffer.h"

#include "misc.h"

#ifdef __cplusplus
extern		"C"
{
#endif

#ifndef WIN32
#include <sys/time.h>

typedef struct timeval TimevalStruct;

#define GETTIMEOFDAY(T) gettimeofday(T, NULL)
#define DIFF_MSEC(T, U, res) \
do { \
	res = ((((int) ((T)->tv_sec - (U)->tv_sec)) * 1000000.0 + \
	  ((int) ((T)->tv_usec - (U)->tv_usec))) / 1000.0); \
} while(0)

#else

#include <sys/types.h>
#include <sys/timeb.h>
#include <windows.h>

typedef LARGE_INTEGER TimevalStruct;
#define GETTIMEOFDAY(T) QueryPerformanceCounter(T)
#define DIFF_MSEC(T, U, res) \
do { \
	LARGE_INTEGER frq; 						\
											\
	QueryPerformanceFrequency(&frq); 		\
	res = (double)(((T)->QuadPart - (U)->QuadPart)/(double)frq.QuadPart); \
	res *= 1000; \
} while(0)

#endif

#ifdef WIN32
static int64 atoll(const char *nptr);
#endif


#include "mysql.h"

typedef struct ThreadArg
{
	int			id;
	long		count;
	bool		all_ok;
	PGconn		*from;
	PGconn		*to;

	struct Thread_hd *hd;
}ThreadArg;

typedef struct mysql_conn_info
{
	char	*host;
	int		port;
	char	*user;
	char	*passwd;
	char	*encoding;
	char	*db;
	char	*encodingdir;
	char	**tabnames;
	char **queries;
}mysql_conn_info;

typedef struct Thread_hd
{
	int			nth;
	struct ThreadArg *th;
	
	const char *snapshot;
	char		*src;
	int			src_version;
	char		*slot_name;

	mysql_conn_info	*mysql_src;

	char		*desc;
	int			desc_version;
	bool		desc_is_greenplum;
	char		*local;

	int			ntask;
	struct Task_hd		*task;
	struct Task_hd		*l_task;
	pthread_mutex_t	t_lock;

	int			ntask_com;
	struct Task_hd		*task_com;
	pthread_mutex_t	t_lock_com;
}Thread_hd;

typedef struct Task_hd
{
	int			id;
	char	   *schemaname;		/* the schema name, or NULL */
	char	   *relname;		/* the relation/sequence name */
	char    *query;
	long		count;
	bool		complete;

	struct Task_hd *next;
}Task_hd;


#define EXTENSION_NAME "rds_logical_sync"

extern int db_sync_main(char *src, char *desc, char *local, int nthread);


extern int mysql2pgsql_sync_main(char *desc, int nthread, mysql_conn_info *hd);


#ifdef __cplusplus
}
#endif

#endif 

