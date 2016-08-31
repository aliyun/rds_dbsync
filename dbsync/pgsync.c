/*
 * pg_sync.c
 *
 */


#include "postgres_fe.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pg_logicaldecode.h"
#include "pqexpbuffer.h"
#include "pgsync.h"
#include "nodes/pg_list.h"
#include "libpq/pqsignal.h"

#include <time.h>

#ifndef WIN32
#include <zlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#endif



static void *copy_table_data(void *arg);
static char *get_synchronized_snapshot(PGconn *conn);
static bool is_slot_exists(PGconn *conn, char *slotname);
static void *logical_decoding_receive_thread(void *arg);
static void get_task_status(PGconn *conn, char **full_start, char **full_end, char **decoder_start, char **apply_id);
static void update_task_status(PGconn *conn, bool full_start, bool full_end, bool decoder_start, int64 apply_id);
static void *logical_decoding_apply_thread(void *arg);
static int64 get_apply_status(PGconn *conn);
static void sigint_handler(int signum);

static volatile bool time_to_abort = false;


#define ERROR_DUPLICATE_KEY		23505

#define SQL_TYPE_BEGIN				0
#define SQL_TYPE_COMMIT				1
#define SQL_TYPE_FIRST_STATMENT		2
#define SQL_TYPE_OTHER_STATMENT		3

#define RECONNECT_SLEEP_TIME 5

#define ALL_DB_TABLE_SQL "select n.nspname, c.relname from pg_class c, pg_namespace n where n.oid = c.relnamespace and c.relkind = 'r' and n.nspname not in ('pg_catalog','tiger','tiger_data','topology','postgis','information_schema') order by c.relpages desc;"
#define GET_NAPSHOT "SELECT pg_export_snapshot()"

#define TASK_ID "1"

#ifndef WIN32
static void
sigint_handler(int signum)
{
	time_to_abort = true;
}
#else
static int64 atoll(const char *nptr)
{
	return atol(nptr);
}
#endif


/*
 * COPY single table over wire.
 */
static void *
copy_table_data(void *arg)
{
	ThreadArg *args = (ThreadArg *)arg;
	Thread_hd *hd = args->hd;
	PGresult   *res1;
	PGresult   *res2;
	int			bytes;
	char	   *copybuf;
	StringInfoData	query;
	char *nspname;
	char *relname;
	Task_hd 	*curr = NULL;
	TimevalStruct before,
					after; 
	double		elapsed_msec = 0;

	PGconn *origin_conn = args->from;
	PGconn *target_conn = args->to;

	origin_conn = pglogical_connect(hd->src, EXTENSION_NAME "_copy");
	if (origin_conn == NULL)
	{
		fprintf(stderr, "init src conn failed: %s", PQerrorMessage(origin_conn));
		return NULL;
	}
	
	target_conn = pglogical_connect(hd->desc, EXTENSION_NAME "_copy");
	if (target_conn == NULL)
	{
		fprintf(stderr, "init desc conn failed: %s", PQerrorMessage(target_conn));
		return NULL;
	}
	
	initStringInfo(&query);
	while(1)
	{
		int			nlist = 0;

		GETTIMEOFDAY(&before);
		pthread_mutex_lock(&hd->t_lock);
		nlist = hd->ntask;
		if (nlist == 1)
		{
		  curr = hd->l_task;
		  hd->l_task = NULL;
		  hd->ntask = 0;
		}
		else if (nlist > 1)
		{
		  Task_hd	*tmp = hd->l_task->next;
		  curr = hd->l_task;
		  hd->l_task = tmp;
		  hd->ntask--;
		}
		else
		{
			curr = NULL;
		}

		pthread_mutex_unlock(&hd->t_lock);

		if(curr == NULL)
		{
			break;
		}

		start_copy_origin_tx(origin_conn, hd->snapshot, hd->src_version);
		start_copy_target_tx(target_conn, hd->desc_version, hd->desc_is_greenplum);

		nspname = curr->schemaname;
		relname = curr->relname;

		/* Build COPY TO query. */
		appendStringInfo(&query, "COPY %s.%s TO stdout",
						 PQescapeIdentifier(origin_conn, nspname,
											strlen(nspname)),
						 PQescapeIdentifier(origin_conn, relname,
											strlen(relname)));

		/* Execute COPY TO. */
		res1 = PQexec(origin_conn, query.data);
		if (PQresultStatus(res1) != PGRES_COPY_OUT)
		{
			fprintf(stderr,"table copy failed Query '%s': %s", 
					query.data, PQerrorMessage(origin_conn));
			goto exit;
		}

		/* Build COPY FROM query. */
		resetStringInfo(&query);
		appendStringInfo(&query, "COPY %s.%s FROM stdin",
						 PQescapeIdentifier(target_conn, nspname,
											strlen(nspname)),
						 PQescapeIdentifier(target_conn, relname,
											strlen(relname)));

		/* Execute COPY FROM. */
		res2 = PQexec(target_conn, query.data);
		if (PQresultStatus(res2) != PGRES_COPY_IN)
		{
			fprintf(stderr,"table copy failed Query '%s': %s", 
				query.data, PQerrorMessage(target_conn));
			goto exit;
		}

		while ((bytes = PQgetCopyData(origin_conn, &copybuf, false)) > 0)
		{
			if (PQputCopyData(target_conn, copybuf, bytes) != 1)
			{
				fprintf(stderr,"writing to target table failed destination connection reported: %s",
							 PQerrorMessage(target_conn));
				goto exit;
			}
			args->count++;
			curr->count++;
			PQfreemem(copybuf);
		}

		if (bytes != -1)
		{
			fprintf(stderr,"reading from origin table failed source connection returned %d: %s",
						bytes, PQerrorMessage(origin_conn));
			goto exit;
		}

		/* Send local finish */
		if (PQputCopyEnd(target_conn, NULL) != 1)
		{
			fprintf(stderr,"sending copy-completion to destination connection failed destination connection reported: %s",
						 PQerrorMessage(target_conn));
			goto exit;
		}

		PQclear(res2);
		res2 = PQgetResult(target_conn);
		if (PQresultStatus(res2) != PGRES_COMMAND_OK)
		{
			fprintf(stderr, "COPY failed for table \"%s\": %s",
								 relname, PQerrorMessage(target_conn));
			goto exit;
		}

		finish_copy_origin_tx(origin_conn);
		finish_copy_target_tx(target_conn);
		curr->complete = true;
		PQclear(res1);
		PQclear(res2);
		resetStringInfo(&query);

		GETTIMEOFDAY(&after);
		DIFF_MSEC(&after, &before, elapsed_msec);
		fprintf(stderr,"thread %d migrate task %d table %s.%s %ld rows complete, time cost %.3f ms\n",
						 args->id, curr->id, nspname, relname, curr->count, elapsed_msec);
	}
	
	args->all_ok = true;

exit:

	PQfinish(origin_conn);
	PQfinish(target_conn);
	ThreadExit(0);
	return NULL;
}


int 
db_sync_main(char *src, char *desc, char *local, int nthread)
{
	int 		i = 0;
	Thread_hd th_hd;
	Thread			*thread = NULL;
	PGresult		*res = NULL;
	PGconn		*origin_conn_repl;
	PGconn		*desc_conn;
	PGconn		*local_conn;
	char		*snapshot = NULL;
	XLogRecPtr	lsn = 0;
	long		s_count = 0;
	long		t_count = 0;
	bool		have_err = false;
	TimevalStruct before,
					after; 
	double		elapsed_msec = 0;
	Decoder_handler *hander = NULL;
	int	src_version = 0;
	struct Thread *decoder = NULL;
	bool	replication_sync = false;
	bool	need_full_sync = false;
	char	*full_start = NULL;
	char	*full_end = NULL;
	char	*decoder_start = NULL;
	char	*apply_id = NULL;
	int		ntask = 0;

#ifndef WIN32
		signal(SIGINT, sigint_handler);
#endif

	GETTIMEOFDAY(&before);

	memset(&th_hd, 0, sizeof(Thread_hd));
	th_hd.nth = nthread;
	th_hd.src = src;
	th_hd.desc = desc;
	th_hd.local = local;

	origin_conn_repl = pglogical_connect(src, EXTENSION_NAME "_main");
	if (origin_conn_repl == NULL)
	{
		fprintf(stderr, "conn to src faild: %s", PQerrorMessage(origin_conn_repl));
		return 1;
	}

	desc_conn = pglogical_connect(desc, EXTENSION_NAME "_main");
	if (desc_conn == NULL)
	{
		fprintf(stderr, "init desc conn failed: %s", PQerrorMessage(desc_conn));
		return 1;
	}
	th_hd.desc_version = PQserverVersion(desc_conn);
	th_hd.desc_is_greenplum = is_greenplum(desc_conn);
	PQfinish(desc_conn);

	local_conn = pglogical_connect(local, EXTENSION_NAME "_main");
	if (local_conn == NULL)
	{
		fprintf(stderr, "init local conn failed: %s", PQerrorMessage(local_conn));
		return 1;
	}
	ExecuteSqlStatement(local_conn, "CREATE TABLE IF NOT EXISTS sync_sqls(id bigserial, sql text)");
	ExecuteSqlStatement(local_conn, "CREATE TABLE IF NOT EXISTS db_sync_status(id bigserial primary key, full_s_start timestamp DEFAULT NULL, full_s_end timestamp DEFAULT NULL, decoder_start timestamp DEFAULT NULL, apply_id bigint DEFAULT NULL)");
	ExecuteSqlStatement(local_conn, "insert into db_sync_status (id) values (" TASK_ID ");");
	get_task_status(local_conn, &full_start, &full_end, &decoder_start, &apply_id);

	if (full_start && full_end == NULL)
	{
		fprintf(stderr, "full sync start %s, but not finish.truncate all data and restart dbsync\n", full_start);
		return 1;
	}
	else if(full_start == NULL && full_end == NULL)
	{
		need_full_sync = true;
		fprintf(stderr, "new dbsync task");
	}
	else if(full_start && full_end)
	{
		fprintf(stderr, "full sync start %s, end %s restart decoder sync\n", full_start, full_end);
		need_full_sync = false;
	}

	if (decoder_start)
	{
		fprintf(stderr, "decoder sync start %s\n", decoder_start);
	}
	
	if (apply_id)
	{
		fprintf(stderr, "decoder apply id %s\n", apply_id);
	}

	src_version = PQserverVersion(origin_conn_repl);
	is_greenplum(origin_conn_repl);
	th_hd.src_version = src_version;
	if (src_version >= 90400)
	{
		replication_sync = true;
		if (!is_slot_exists(origin_conn_repl, EXTENSION_NAME "_slot"))
		{
			int rc = 0;

			hander = init_hander();
			hander->connection_string = src;
			init_logfile(hander);
			rc = initialize_connection(hander);
			if(rc != 0)
			{
				fprintf(stderr, "create replication conn failed\n");
				return 1;
			}
			hander->do_create_slot = true;
			snapshot = create_replication_slot(hander, &lsn, EXTENSION_NAME "_slot");
			if (snapshot == NULL)
			{
				fprintf(stderr, "create replication slot failed\n");
				return 1;
			}

			th_hd.slot_name = hander->replication_slot;
		}
		else
		{
			fprintf(stderr, "decoder slot %s exist\n", EXTENSION_NAME "_slot");
			th_hd.slot_name = EXTENSION_NAME "_slot";
		}
	}

	if (need_full_sync)
	{
		const char	   *setup_query =
			"BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ, READ ONLY;\n";
		PQExpBuffer	query;
		
		query = createPQExpBuffer();
		appendPQExpBuffer(query, "%s", setup_query);

		if (snapshot)
		{
			appendPQExpBuffer(query, "SET TRANSACTION SNAPSHOT '%s';\n", snapshot);
		}

		res = PQexec(origin_conn_repl, query->data);
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			fprintf(stderr, "init open a tran failed: %s", PQresultErrorMessage(res));
			return 1;
		}
		resetPQExpBuffer(query);

		if (snapshot == NULL)
		{
			if (src_version >= 90200)
			{
				snapshot = get_synchronized_snapshot(origin_conn_repl);
				th_hd.snapshot = snapshot;
			}
		}

		appendPQExpBuffer(query, ALL_DB_TABLE_SQL);
		res = PQexec(origin_conn_repl, query->data);
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			fprintf(stderr, "init sql run failed: %s", PQresultErrorMessage(res));
			return 1;
		}

		ntask = PQntuples(res);
		th_hd.ntask = ntask;
		if (th_hd.ntask >= 1)
		{
			th_hd.task = (Task_hd *)palloc0(sizeof(Task_hd) * th_hd.ntask);
		}

		for (i = 0; i < th_hd.ntask; i++)
		{
			th_hd.task[i].id = i;
			th_hd.task[i].schemaname = pstrdup(PQgetvalue(res, i, 0));
			th_hd.task[i].relname = pstrdup(PQgetvalue(res, i, 1));
			th_hd.task[i].count = 0;
			th_hd.task[i].complete = false;
			if (i != th_hd.ntask - 1)
			{
				th_hd.task[i].next = &th_hd.task[i+1];
			}
		}

		th_hd.l_task = &(th_hd.task[0]);
		PQclear(res);
		destroyPQExpBuffer(query);

		th_hd.th = (ThreadArg *)malloc(sizeof(ThreadArg) * th_hd.nth);
		for (i = 0; i < th_hd.nth; i++)
		{
			th_hd.th[i].id = i;
			th_hd.th[i].count = 0;
			th_hd.th[i].all_ok = false;

			th_hd.th[i].hd = &th_hd;
		}
		pthread_mutex_init(&th_hd.t_lock, NULL);

		fprintf(stderr, "starting full sync");
		if (snapshot)
		{
			fprintf(stderr, " with snapshot %s", snapshot);
		}
		fprintf(stderr, "\n");

		thread = (Thread *)palloc0(sizeof(Thread) * th_hd.nth);
		for (i = 0; i < th_hd.nth; i++)
		{
			ThreadCreate(&thread[i], copy_table_data, &th_hd.th[i]);
		}

		update_task_status(local_conn, true, false, false, -1);
	}
	
	if (replication_sync)
	{
		decoder = (Thread *)palloc0(sizeof(Thread) * 2);
		fprintf(stderr, "starting logical decoding sync thread\n");
		ThreadCreate(&decoder[0], logical_decoding_receive_thread, &th_hd);
		update_task_status(local_conn, false, false, true, -1);
	}
		
	if (need_full_sync)
	{
		WaitThreadEnd(th_hd.nth, thread);
		update_task_status(local_conn, false, true, false, -1);

		GETTIMEOFDAY(&after);
		DIFF_MSEC(&after, &before, elapsed_msec);

		for (i = 0; i < th_hd.nth; i++)
		{
			if(th_hd.th[i].all_ok)
			{
				s_count += th_hd.th[i].count;
			}
			else
			{
				have_err = true;
			}
		}

		for (i = 0; i < ntask; i++)
		{
			t_count += th_hd.task[i].count;
		}

		fprintf(stderr, "job migrate row %ld task row %ld \n", s_count, t_count);
		fprintf(stderr, "full sync time cost %.3f ms\n", elapsed_msec);
		if (have_err)
		{
			fprintf(stderr, "migration process with errors\n");
		}
	}

	if (replication_sync)
	{
		ThreadCreate(&decoder[1], logical_decoding_apply_thread, &th_hd);
		fprintf(stderr, "starting decoder apply thread\n");
		WaitThreadEnd(2, decoder);
	}
	
	PQfinish(origin_conn_repl);
	PQfinish(local_conn);

	return 0;
}


static char *
get_synchronized_snapshot(PGconn *conn)
{
	char	   *query = "SELECT pg_export_snapshot()";
	char	   *result;
	PGresult   *res;

	res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, "init sql run failed: %s", PQresultErrorMessage(res));
		return NULL;
	}
	result = pstrdup(PQgetvalue(res, 0, 0));
	PQclear(res);

	return result;
}

static bool
is_slot_exists(PGconn *conn, char *slotname)
{
	PGresult   *res;
	int			ntups;
	bool	exist = false;
	PQExpBuffer query;

	query = createPQExpBuffer();
	appendPQExpBuffer(query, "select slot_name from pg_replication_slots where slot_name = '%s';",
							  slotname);

	res = PQexec(conn, query->data);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		destroyPQExpBuffer(query);
		return false;
	}

	/* Expecting a single result only */
	ntups = PQntuples(res);
	if (ntups == 1)
	{
		exist = true;
	}

	PQclear(res);
	destroyPQExpBuffer(query);

	return exist;
}

static void
get_task_status(PGconn *conn, char **full_start, char **full_end, char **decoder_start, char **apply_id)
{
	PGresult   *res;
	char *query = "SELECT full_s_start , full_s_end, decoder_start, apply_id FROM db_sync_status where id =" TASK_ID;

	res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		return;
	}

	if (PQntuples(res) != 1)
	{
		PQclear(res);
		return;
	}

	if (!PQgetisnull(res, 0, 0))
	{
		*full_start = pstrdup(PQgetvalue(res, 0, 0));
	}
	
	if (!PQgetisnull(res, 0, 1))
	{
		*full_end = pstrdup(PQgetvalue(res, 0, 1));
	}
	
	if (!PQgetisnull(res, 0, 2))
	{
		*decoder_start = pstrdup(PQgetvalue(res, 0, 2));
	}

	if (!PQgetisnull(res, 0, 3))
	{
		*apply_id = pstrdup(PQgetvalue(res, 0, 3));
	}

	PQclear(res);

	return;
}

static void
update_task_status(PGconn *conn, bool full_start, bool full_end, bool decoder_start, int64 apply_id)
{
	PQExpBuffer query;

	query = createPQExpBuffer();

	if (full_start)
	{
		appendPQExpBuffer(query, "UPDATE db_sync_status SET full_s_start = now() WHERE id = %s",
							  TASK_ID);
		ExecuteSqlStatement(conn, query->data);
	}
	
	if (full_end)
	{
		appendPQExpBuffer(query, "UPDATE db_sync_status SET full_s_end = now() WHERE id = %s",
							  TASK_ID);
		ExecuteSqlStatement(conn, query->data);
	}
	
	if (decoder_start)
	{
		appendPQExpBuffer(query, "UPDATE db_sync_status SET decoder_start = now() WHERE id = %s",
							  TASK_ID);
		ExecuteSqlStatement(conn, query->data);
	}
	
	if (apply_id >= 0)
	{
		appendPQExpBuffer(query, "UPDATE db_sync_status SET apply_id = " INT64_FORMAT " WHERE id = %s",
							  apply_id, TASK_ID);
		ExecuteSqlStatement(conn, query->data);
	}

	destroyPQExpBuffer(query);

	return;
}

static void *
logical_decoding_receive_thread(void *arg)
{
	Thread_hd *hd = (Thread_hd *)arg;
	Decoder_handler *hander;
	int		rc = 0;
	bool	init = false;
	PGconn *local_conn;
	PQExpBuffer buffer;
    char    *stmtname = "insert_sqls";
    Oid     type[1];
	const char *paramValues[1];
	PGresult *res = NULL;

    type[0] = 25;
	buffer = createPQExpBuffer();

	local_conn = pglogical_connect(hd->local, EXTENSION_NAME "_decoding");
	if (local_conn == NULL)
	{
		fprintf(stderr, "init src conn failed: %s", PQerrorMessage(local_conn));
		goto exit;
	}
	setup_connection(local_conn, 90400, false);

	res = PQprepare(local_conn, stmtname, "INSERT INTO sync_sqls (sql) VALUES($1)", 1, type);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		fprintf(stderr, "create PQprepare failed: %s", PQerrorMessage(local_conn));
		PQfinish(local_conn);
		goto exit;
	}
	PQclear(res);

	hander = init_hander();
	hander->connection_string = hd->src;
	init_logfile(hander);
	rc = check_handler_parameters(hander);
	if(rc != 0)
	{
		exit(1);
	}

	rc = initialize_connection(hander);
	if(rc != 0)
	{
		exit(1);
	}

	hander->replication_slot = hd->slot_name;
	init_streaming(hander);
	init = true;

	while (true)
	{
		ALI_PG_DECODE_MESSAGE *msg = NULL;

		if (time_to_abort)
		{
			if (hander->copybuf != NULL)
			{
				PQfreemem(hander->copybuf);
				hander->copybuf = NULL;
			}
			if (hander->conn)
			{
				PQfinish(hander->conn);
				hander->conn = NULL;
			}
			if (local_conn)
			{
				PQdescribePrepared(local_conn, stmtname);
				PQfinish(local_conn);
			}
			break;
		}

		if (!init)
		{
			initialize_connection(hander);
			init_streaming(hander);
			init = true;
		}

		msg = exec_logical_decoder(hander, &time_to_abort);
		if (msg != NULL)
		{
			out_put_tuple_to_sql(hander, msg, buffer);
			if(msg->type == MSGKIND_BEGIN)
			{
				res = PQexec(local_conn, "BEGIN");
				if (PQresultStatus(res) != PGRES_COMMAND_OK)
				{
					fprintf(stderr, "decoding receive thread begin a local trans failed: %s", PQerrorMessage(local_conn));
					goto exit;
				}
				PQclear(res);
			}

			paramValues[0] = buffer->data;
			res = PQexecPrepared(local_conn, stmtname, 1, paramValues, NULL, NULL, 1);
			if (PQresultStatus(res) != PGRES_COMMAND_OK)
			{
				fprintf(stderr, "exec prepare INSERT INTO sync_sqls failed: %s", PQerrorMessage(local_conn));
				time_to_abort = true;
				goto exit;
			}
			PQclear(res);

			hander->flushpos = hander->recvpos;
			if(msg->type == MSGKIND_COMMIT)
			{
				res = PQexec(local_conn, "END");
				if (PQresultStatus(res) != PGRES_COMMAND_OK)
				{
					fprintf(stderr, "decoding receive thread commit a local trans failed: %s", PQerrorMessage(local_conn));
					goto exit;
				}
				PQclear(res);
			}

			resetPQExpBuffer(buffer);
		}
		else
		{
			fprintf(stderr, "decoding receive no record, sleep and reconnect");
			pg_sleep(RECONNECT_SLEEP_TIME * 1000000);
			init = false;
		}
	}


exit:

	destroyPQExpBuffer(buffer);

	ThreadExit(0);
	return NULL;
}

static void *
logical_decoding_apply_thread(void *arg)
{
	Thread_hd *hd = (Thread_hd *)arg;
	PGconn *local_conn = NULL;
	PGconn *local_conn_u = NULL;
	PGconn *apply_conn = NULL;
    Oid     type[1];
	PGresult *resreader = NULL;
	PGresult *applyres = NULL;
	int		pgversion;
	bool	is_gp = false;
	int64	apply_id = 0;

    type[0] = 25;

	local_conn = pglogical_connect(hd->local, EXTENSION_NAME "apply_reader");
	if (local_conn == NULL)
	{
		fprintf(stderr, "decoding applyer init src conn failed: %s", PQerrorMessage(local_conn));
		goto exit;
	}
	setup_connection(local_conn, 90400, false);
	apply_id = get_apply_status(local_conn);
	if (apply_id == -1)
	{
		goto exit;
	}

	local_conn_u = pglogical_connect(hd->local, EXTENSION_NAME "apply_update_status");
	if (local_conn_u == NULL)
	{
		fprintf(stderr, "decoding applyer init src conn failed: %s", PQerrorMessage(local_conn_u));
		goto exit;
	}
	setup_connection(local_conn_u, 90400, false);

	apply_conn = pglogical_connect(hd->desc, EXTENSION_NAME "_decoding_apply");
	if (apply_conn == NULL)
	{
		fprintf(stderr, "decoding_apply init desc conn failed: %s", PQerrorMessage(apply_conn));
		goto exit;
	}
	pgversion = PQserverVersion(apply_conn);
	is_gp = is_greenplum(apply_conn);
	setup_connection(apply_conn, pgversion, is_gp);

	while (!time_to_abort)
	{
		const char *paramValues[1];
		char	*ssql;
		char	tmp[16];
		int		n_commit = 0;
		int		sqltype = SQL_TYPE_BEGIN;

		sprintf(tmp, INT64_FORMAT, apply_id);
		paramValues[0] = tmp;

        resreader = PQexec(local_conn, "BEGIN");
        if (PQresultStatus(resreader) != PGRES_COMMAND_OK)
        {
                fprintf(stderr, "BEGIN command failed: %s\n", PQerrorMessage(local_conn));
                PQclear(resreader);
                goto exit;
        }
		PQclear(resreader);

        resreader = PQexecParams(local_conn,
           "DECLARE ali_decoder_cursor CURSOR FOR select id, sql from sync_sqls where id > $1 order by id",
           1,
           NULL,
           paramValues,
           NULL,
           NULL,
           1);

        if (PQresultStatus(resreader) != PGRES_COMMAND_OK)
        {
			fprintf(stderr, "DECLARE CURSOR command failed: %s\n", PQerrorMessage(local_conn));
			PQclear(resreader);
			goto exit;
        }
        PQclear(resreader);

		while(!time_to_abort)
		{
			resreader = PQexec(local_conn, "FETCH FROM ali_decoder_cursor");
			if (PQresultStatus(resreader) != PGRES_TUPLES_OK)
			{
					fprintf(stderr, "FETCH ALL command didn't return tuples properly: %s\n", PQerrorMessage(local_conn));
					PQclear(resreader);
			}

			if (PQntuples(resreader) == 0)
			{
				PQclear(resreader);
				resreader = PQexec(local_conn, "CLOSE ali_decoder_cursor");
				PQclear(resreader);
				resreader = PQexec(local_conn, "END");
				PQclear(resreader);

				if (n_commit != 0)
				{
					n_commit = 0;
					update_task_status(local_conn_u, false, false, false, apply_id);
				}

				pg_sleep(1000000);
				break;
			}

			ssql = PQgetvalue(resreader, 0, 1);
			if(strcmp(ssql,"begin;") == 0)
			{
				sqltype = SQL_TYPE_BEGIN;
			}
			else if (strcmp(ssql,"commit;") == 0)
			{
				sqltype = SQL_TYPE_COMMIT;
			}
			else if(sqltype == SQL_TYPE_BEGIN)
			{
				sqltype = SQL_TYPE_FIRST_STATMENT;
			}
			else
			{
				sqltype = SQL_TYPE_OTHER_STATMENT;
			}

			applyres = PQexec(apply_conn, ssql);
			if (PQresultStatus(applyres) != PGRES_COMMAND_OK)
			{
				char	*sqlstate = PQresultErrorField(applyres, PG_DIAG_SQLSTATE);
				int		errcode = 0;
				fprintf(stderr, "exec apply id %s, sql %s failed: %s\n", PQgetvalue(resreader, 0, 0), ssql, PQerrorMessage(apply_conn));
				errcode = atoi(sqlstate);
				if (errcode == ERROR_DUPLICATE_KEY && sqltype == SQL_TYPE_FIRST_STATMENT)
				{
					PQclear(applyres);
					applyres = PQexec(apply_conn, "END");
					if (PQresultStatus(applyres) != PGRES_COMMAND_OK)
					{
						goto exit;
					}
					PQclear(applyres);
					applyres = PQexec(apply_conn, "BEGIN");
					if (PQresultStatus(applyres) != PGRES_COMMAND_OK)
					{
						goto exit;
					}
					sqltype = SQL_TYPE_BEGIN;
				}
				else
				{
					PQclear(resreader);
					PQclear(applyres);
					goto exit;
				}
			}

			if (sqltype == SQL_TYPE_COMMIT)
			{
				n_commit++;
				apply_id = atoll(PQgetvalue(resreader, 0, 0));
				if(n_commit == 5)
				{
					n_commit = 0;
					update_task_status(local_conn_u, false, false, false, apply_id);
				}
			}
			PQclear(resreader);
			PQclear(applyres);
		}
	}

exit:

	if (local_conn)
	{
		PQfinish(local_conn);
	}

	if (local_conn_u)
	{
		PQfinish(local_conn_u);
	}

	if (apply_conn)
	{
		PQfinish(apply_conn);
	}

	time_to_abort = true;

	ThreadExit(0);
	return NULL;
}

static int64
get_apply_status(PGconn *conn)
{
	PGresult   *res;
	char *query = "SELECT apply_id FROM db_sync_status where id =" TASK_ID;
	int64	rc = 0;

	res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		PQclear(res);
		return -1;
	}

	if (PQntuples(res) != 1)
	{
		PQclear(res);
		return -1;
	}

	if (!PQgetisnull(res, 0, 0))
	{
		char	*tmp = PQgetvalue(res, 0, 0);
		rc = atoll(tmp);
	}

	PQclear(res);

	return rc;
}

