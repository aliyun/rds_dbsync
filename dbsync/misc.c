
#include "postgres_fe.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pqexpbuffer.h"

#include "misc.h"

#include <time.h>


#ifndef WIN32
#include <pthread.h>
#include <sys/time.h>
#endif

bool
WaitThreadEnd(int n, Thread *th)
{
	ThreadHandle *hanlde = NULL;
	int i;

	hanlde = (ThreadHandle *)malloc(sizeof(ThreadHandle) * n);
	for(i = 0; i < n; i++)
	{
		hanlde[i]=th[i].os_handle;
	}

#ifdef WIN32
	WaitForMultipleObjects(n, hanlde, TRUE, INFINITE);
#else
	for(i = 0; i < n; i++)
		pthread_join(hanlde[i], NULL);
#endif

	free(hanlde);

	return true;
}

int
ThreadCreate(Thread *th,
				  void *(*start)(void *arg),
				  void *arg)
{
	int		rc = -1;
#ifdef WIN32
	th->os_handle = (HANDLE)_beginthreadex(NULL,
		0,								
		(unsigned(__stdcall*)(void*)) start,
		arg,			
		0,				
		&th->thid);		

	/* error for returned value 0 */
	if (th->os_handle == (HANDLE) 0)
		th->os_handle = INVALID_HANDLE_VALUE;
	else
		rc = 1;
#else
	rc = pthread_create(&th->os_handle,
		NULL,
		start,
		arg);
#endif
	return rc;
}

void
ThreadExit(int code)
{
#ifdef WIN32
    _endthreadex((unsigned) code);
#else
	pthread_exit((void *)NULL);
	return;
#endif
}


PGconn *
pglogical_connect(const char *connstring, const char *connname)
{
	PGconn		   *conn;
	StringInfoData	dsn;

	initStringInfo(&dsn);
	appendStringInfo(&dsn,
					"%s fallback_application_name='%s'",
					connstring, connname);

	conn = PQconnectdb(dsn.data);
	if (PQstatus(conn) != CONNECTION_OK)
	{
		fprintf(stderr,"could not connect to the postgresql server: %s dsn was: %s",
						PQerrorMessage(conn), dsn.data);
		return NULL;
	}

	return conn;
}

bool
is_greenplum(PGconn *conn)
{
	char	   *query = "select version from  version()";
	bool	is_greenplum = false;
	char	*result;
	PGresult   *res;

	res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, "init sql run failed: %s", PQresultErrorMessage(res));
		return false;
	}
	result = PQgetvalue(res, 0, 0);
	if (strstr(result, "Greenplum") != NULL)
	{
		is_greenplum = true;
	}
	
	PQclear(res);

	return is_greenplum;
}

size_t
quote_literal_internal(char *dst, const char *src, size_t len)
{
	const char *s;
	char	   *savedst = dst;

	for (s = src; s < src + len; s++)
	{
		if (*s == '\\')
		{
			*dst++ = ESCAPE_STRING_SYNTAX;
			break;
		}
	}

	*dst++ = '\'';
	while (len-- > 0)
	{
		if (SQL_STR_DOUBLE(*src, true))
			*dst++ = *src;
		*dst++ = *src++;
	}
	*dst++ = '\'';

	return dst - savedst;
}

int
start_copy_origin_tx(PGconn *conn, const char *snapshot, int pg_version)
{
	PGresult	   *res;
	const char	   *setup_query =
		"BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ, READ ONLY;\n";
	StringInfoData	query;

	initStringInfo(&query);
	appendStringInfoString(&query, setup_query);

	if (snapshot)
		appendStringInfo(&query, "SET TRANSACTION SNAPSHOT '%s';\n", snapshot);

	res = PQexec(conn, query.data);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "BEGIN on origin node failed: %s",
				PQresultErrorMessage(res));
		return 1;
	}

	PQclear(res);
	
	setup_connection(conn, pg_version, false);

	return 0;
}

int
start_copy_target_tx(PGconn *conn, int pg_version, bool is_greenplum)
{
	PGresult	   *res;
	const char	   *setup_query =
		"BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;\n";

	res = PQexec(conn, setup_query);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "BEGIN on target node failed: %s",
				PQresultErrorMessage(res));
		return 1;
	}

	PQclear(res);

	setup_connection(conn, pg_version, is_greenplum);

	return 0;
}

int
finish_copy_origin_tx(PGconn *conn)
{
	PGresult   *res;

	/* Close the  transaction and connection on origin node. */
	res = PQexec(conn, "ROLLBACK");
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "ROLLBACK on origin node failed: %s",
				PQresultErrorMessage(res));
		return 1;
	}

	PQclear(res);
	//PQfinish(conn);
	return 0;
}

int
finish_copy_target_tx(PGconn *conn)
{
	PGresult   *res;

	/* Close the transaction and connection on target node. */
	res = PQexec(conn, "COMMIT");
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "COMMIT on target node failed: %s",
				PQresultErrorMessage(res));
		return 1;
	}

	PQclear(res);
	//PQfinish(conn);
	return 0;
}

int
setup_connection(PGconn *conn, int remoteVersion, bool is_greenplum)
{
	char *dumpencoding = "utf8";

	/*
	 * Set the client encoding if requested. If dumpencoding == NULL then
	 * either it hasn't been requested or we're a cloned connection and then
	 * this has already been set in CloneArchive according to the original
	 * connection encoding.
	 */
	if (PQsetClientEncoding(conn, dumpencoding) < 0)
	{
		fprintf(stderr, "invalid client encoding \"%s\" specified\n",
					dumpencoding);
		return 1;
	}

	/*
	 * Get the active encoding and the standard_conforming_strings setting, so
	 * we know how to escape strings.
	 */
	//AH->encoding = PQclientEncoding(conn);

	//std_strings = PQparameterStatus(conn, "standard_conforming_strings");
	//AH->std_strings = (std_strings && strcmp(std_strings, "on") == 0);

	/* Set the datestyle to ISO to ensure the dump's portability */
	ExecuteSqlStatement(conn, "SET DATESTYLE = ISO");

	/* Likewise, avoid using sql_standard intervalstyle */
	if (remoteVersion >= 80400)
		ExecuteSqlStatement(conn, "SET INTERVALSTYLE = POSTGRES");

	/*
	 * If supported, set extra_float_digits so that we can dump float data
	 * exactly (given correctly implemented float I/O code, anyway)
	 */
	if (remoteVersion >= 90000)
		ExecuteSqlStatement(conn, "SET extra_float_digits TO 3");
	else if (remoteVersion >= 70400)
		ExecuteSqlStatement(conn, "SET extra_float_digits TO 2");

	/*
	 * If synchronized scanning is supported, disable it, to prevent
	 * unpredictable changes in row ordering across a dump and reload.
	 */
	if (remoteVersion >= 80300 && !is_greenplum)
		ExecuteSqlStatement(conn, "SET synchronize_seqscans TO off");

	/*
	 * Disable timeouts if supported.
	 */
	if (remoteVersion >= 70300)
		ExecuteSqlStatement(conn, "SET statement_timeout = 0");
	if (remoteVersion >= 90300)
		ExecuteSqlStatement(conn, "SET lock_timeout = 0");

	return 0;
}

int
ExecuteSqlStatement(PGconn *conn, const char *query)
{
	PGresult   *res;
	int		rc = 0;

	res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "set %s failed: %s",
						query, PQerrorMessage(conn));
		rc = 1;
	}
	PQclear(res);

	return rc;
}

#ifndef FRONTEND
#error "This file is not expected to be compiled for backend code"
#endif

void *
pg_malloc(size_t size)
{
	void	   *tmp;

	/* Avoid unportable behavior of malloc(0) */
	if (size == 0)
		size = 1;
	tmp = malloc(size);
	if (!tmp)
	{
		fprintf(stderr, _("out of memory\n"));
		exit(EXIT_FAILURE);
	}
	return tmp;
}

void *
pg_malloc0(size_t size)
{
	void	   *tmp;

	tmp = pg_malloc(size);
	MemSet(tmp, 0, size);
	return tmp;
}

void *
palloc(Size size)
{
	return pg_malloc(size);
}

void *
palloc0(Size size)
{
	return pg_malloc0(size);
}

char *
pstrdup(const char *in)
{
	return pg_strdup(in);
}

void *
repalloc(void *pointer, Size size)
{
	return pg_realloc(pointer, size);
}

void *
pg_realloc(void *ptr, size_t size)
{
	void	   *tmp;

	/* Avoid unportable behavior of realloc(NULL, 0) */
	if (ptr == NULL && size == 0)
		size = 1;
	tmp = realloc(ptr, size);
	if (!tmp)
	{
		fprintf(stderr, _("out of memory\n"));
		exit(EXIT_FAILURE);
	}
	return tmp;
}

char *
pg_strdup(const char *in)
{
	char	   *tmp;

	if (!in)
	{
		fprintf(stderr,
				_("cannot duplicate null pointer (internal error)\n"));
		exit(EXIT_FAILURE);
	}
	tmp = strdup(in);
	if (!tmp)
	{
		fprintf(stderr, _("out of memory\n"));
		exit(EXIT_FAILURE);
	}
	return tmp;
}

void
pfree(void *pointer)
{
	pg_free(pointer);
}

void
pg_free(void *ptr)
{
	if (ptr != NULL)
		free(ptr);
}

char *
psprintf(const char *fmt,...)
{
	size_t		len = 128;		/* initial assumption about buffer size */

	for (;;)
	{
		char	   *result;
		va_list		args;
		size_t		newlen;

		/*
		 * Allocate result buffer.  Note that in frontend this maps to malloc
		 * with exit-on-error.
		 */
		result = (char *) palloc(len);

		/* Try to format the data. */
		va_start(args, fmt);
		newlen = pvsnprintf(result, len, fmt, args);
		va_end(args);

		if (newlen < len)
			return result;		/* success */

		/* Release buffer and loop around to try again with larger len. */
		pfree(result);
		len = newlen;
	}
}

/*
 * pvsnprintf
 *
 * Attempt to format text data under the control of fmt (an sprintf-style
 * format string) and insert it into buf (which has length len, len > 0).
 *
 * If successful, return the number of bytes emitted, not counting the
 * trailing zero byte.  This will always be strictly less than len.
 *
 * If there's not enough space in buf, return an estimate of the buffer size
 * needed to succeed (this *must* be more than the given len, else callers
 * might loop infinitely).
 *
 * Other error cases do not return, but exit via elog(ERROR) or exit().
 * Hence, this shouldn't be used inside libpq.
 *
 * This function exists mainly to centralize our workarounds for
 * non-C99-compliant vsnprintf implementations.  Generally, any call that
 * pays any attention to the return value should go through here rather
 * than calling snprintf or vsnprintf directly.
 *
 * Note that the semantics of the return value are not exactly C99's.
 * First, we don't promise that the estimated buffer size is exactly right;
 * callers must be prepared to loop multiple times to get the right size.
 * Second, we return the recommended buffer size, not one less than that;
 * this lets overflow concerns be handled here rather than in the callers.
 */
size_t
pvsnprintf(char *buf, size_t len, const char *fmt, va_list args)
{
	int			nprinted;

	Assert(len > 0);

	errno = 0;

	/*
	 * Assert check here is to catch buggy vsnprintf that overruns the
	 * specified buffer length.  Solaris 7 in 64-bit mode is an example of a
	 * platform with such a bug.
	 */
#ifdef USE_ASSERT_CHECKING
	buf[len - 1] = '\0';
#endif

	nprinted = vsnprintf(buf, len, fmt, args);

	Assert(buf[len - 1] == '\0');

	/*
	 * If vsnprintf reports an error other than ENOMEM, fail.  The possible
	 * causes of this are not user-facing errors, so elog should be enough.
	 */
	if (nprinted < 0 && errno != 0 && errno != ENOMEM)
	{
#ifndef FRONTEND
		elog(ERROR, "vsnprintf failed: %m");
#else
		fprintf(stderr, "vsnprintf failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
#endif
	}

	/*
	 * Note: some versions of vsnprintf return the number of chars actually
	 * stored, not the total space needed as C99 specifies.  And at least one
	 * returns -1 on failure.  Be conservative about believing whether the
	 * print worked.
	 */
	if (nprinted >= 0 && (size_t) nprinted < len - 1)
	{
		/* Success.  Note nprinted does not include trailing null. */
		return (size_t) nprinted;
	}

	if (nprinted >= 0 && (size_t) nprinted > len)
	{
		/*
		 * This appears to be a C99-compliant vsnprintf, so believe its
		 * estimate of the required space.  (If it's wrong, the logic will
		 * still work, but we may loop multiple times.)  Note that the space
		 * needed should be only nprinted+1 bytes, but we'd better allocate
		 * one more than that so that the test above will succeed next time.
		 *
		 * In the corner case where the required space just barely overflows,
		 * fall through so that we'll error out below (possibly after
		 * looping).
		 */
		if ((size_t) nprinted <= MaxAllocSize - 2)
			return nprinted + 2;
	}

	/*
	 * Buffer overrun, and we don't know how much space is needed.  Estimate
	 * twice the previous buffer size, but not more than MaxAllocSize; if we
	 * are already at MaxAllocSize, choke.  Note we use this palloc-oriented
	 * overflow limit even when in frontend.
	 */
	if (len >= MaxAllocSize)
	{
#ifndef FRONTEND
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("out of memory")));
#else
		fprintf(stderr, _("out of memory\n"));
		exit(EXIT_FAILURE);
#endif
	}

	if (len >= MaxAllocSize / 2)
		return MaxAllocSize;

	return len * 2;
}


