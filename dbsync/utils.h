

#ifndef PG_UTILS_H
#define PG_UTILS_H

#include "postgres_fe.h"

#include "lib/stringinfo.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

//#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pqexpbuffer.h"



extern void pq_copymsgbytes(StringInfo msg, char *buf, int datalen);
extern void fe_sendint64(int64 i, char *buf);
extern PGconn *GetConnection(char *connection_string);
extern int64 feGetCurrentTimestamp(void);
extern bool feTimestampDifferenceExceeds(int64 start_time,
							 int64 stop_time,
							 int msec);
extern void feTimestampDifference(int64 start_time, int64 stop_time,
					  long *secs, int *microsecs);
extern int64 fe_recvint64(char *buf);
extern int getopt(int nargc, char *const * nargv, const char *ostr);


#endif 


