/*
 * mysql2pgsql.c
 *
 */

#include "postgres_fe.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pqexpbuffer.h"
#include "pgsync.h"
#include "nodes/pg_list.h"
#include "libpq/pqsignal.h"
#include "catalog/pg_type.h"

#include <time.h>

#ifndef WIN32
#include <zlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#endif

#include "mysql.h"
#include "utils.h"
#include <unistd.h> 

static volatile bool time_to_abort = false;
bool get_ddl_only = false;

#define STMT_SHOW_TABLES "show full tables in `%s` where table_type='BASE TABLE'"

#define STMT_SELECT      "select * from `%s`.`%s`"

static MYSQL *connect_to_mysql(mysql_conn_info* hd);
static void *mysql2pgsql_copy_data(void *arg);
static Oid *fetch_colmum_info(char *tabname, MYSQL_RES *my_res, bool is_target_gp);
static void quote_literal_local_withoid(StringInfo s, const char *rawstr, Oid type, PQExpBuffer buffer);
static int setup_connection_from_mysql(PGconn *conn);
static void sigint_handler(int signum);

#ifndef WIN32
static void
sigint_handler(int signum)
{
	time_to_abort = true;
}
#endif

static Oid *
fetch_colmum_info(char *tabname, MYSQL_RES *my_res, bool is_target_gp)
{
	MYSQL_FIELD *field;
	int		col_num = 0;
	Oid		*col_type = NULL;
	int		i = 0;
	PQExpBuffer ddl;
	bool	first = true;

	ddl = createPQExpBuffer();
	appendPQExpBufferStr(ddl, "Reference DDL to create the target table:\n");
	appendPQExpBuffer(ddl, "CREATE TABLE %s%s (", 
					is_target_gp ? "" : "IF NOT EXISTS ", tabname);
	
	col_num = mysql_num_fields(my_res);
	col_type = palloc0(sizeof(Oid) * col_num);
    for (i = 0; i < col_num; i++)
    {
		int type;

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBufferStr(ddl, ", ");
		}

		field = mysql_fetch_field(my_res);
    	type = field->type;
        switch(type)
		{
			case MYSQL_TYPE_VARCHAR:
			case MYSQL_TYPE_VAR_STRING:
			case MYSQL_TYPE_STRING:
			case MYSQL_TYPE_BIT:
			case MYSQL_TYPE_BLOB:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "text");
				col_type[i] = TEXTOID;
				break;

			case MYSQL_TYPE_TIMESTAMP:
			case MYSQL_TYPE_DATE:
			case MYSQL_TYPE_TIME:
			case MYSQL_TYPE_DATETIME:
			case MYSQL_TYPE_YEAR:
			case MYSQL_TYPE_NEWDATE:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "timestamp");
				col_type[i] = TIMESTAMPOID;
				break;

			case MYSQL_TYPE_SHORT:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "int2");
				col_type[i] = INT2OID;
				break;

			case MYSQL_TYPE_TINY:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "int4");
				col_type[i] = INT4OID;
				break;

			case MYSQL_TYPE_LONG:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "int4");
				col_type[i] = INT4OID;
				break;

			case MYSQL_TYPE_LONGLONG:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "int8");
				col_type[i] = INT8OID;
				break;
	
			case MYSQL_TYPE_FLOAT:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "float4");
				col_type[i] = FLOAT4OID;
				break;
				
			case MYSQL_TYPE_DOUBLE:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "float8");
				col_type[i] = FLOAT8OID;
				break;

			case MYSQL_TYPE_DECIMAL:
			case MYSQL_TYPE_NEWDECIMAL:
				appendPQExpBuffer(ddl, "%s %s", field->org_name, "numeric");
				col_type[i] = NUMERICOID;
				break;

			default:
				fprintf(stderr, "unsupported col %s type %d\n", field->org_name, type);
				return NULL;
		}
    }
	
	appendPQExpBuffer(ddl, ")%s;\n\n", (is_target_gp ? " with (APPENDONLY=true, ORIENTATION=column, CHECKSUM=true, OIDS=false) DISTRIBUTED BY (<distribution key>)" : ""));

	fprintf(stderr, "%s", ddl->data);
	
	destroyPQExpBuffer(ddl);

	return col_type;
}


static MYSQL *
connect_to_mysql(mysql_conn_info* hd)
{
	int ret  = -1;
	bool m_reConn = true;

	//my_init();
	//mysql_thread_init();
	MYSQL *m_mysqlConnection = mysql_init(NULL);
	if (hd->encoding != NULL)
	{
		ret = mysql_options(m_mysqlConnection, MYSQL_SET_CHARSET_NAME, hd->encoding);
		if (ret != 0)
		{
			fprintf(stderr, "set CHARSET_NAME to %s error: %s", hd->encoding, mysql_error(m_mysqlConnection));
			return NULL;
		}
	}
	if (hd->encodingdir != NULL)
	{
		ret = mysql_options(m_mysqlConnection, MYSQL_SET_CHARSET_DIR, hd->encodingdir);
		 if (ret != 0)
		{
			fprintf(stderr, "set CHARSET_DIR to %s error: %s", hd->encodingdir, mysql_error(m_mysqlConnection));
			return NULL;
		}
	}

	if (1)
	{
		int opt_local_infile = 1; 
		mysql_options(m_mysqlConnection, MYSQL_OPT_LOCAL_INFILE, (char*) &opt_local_infile);
	}

   // printf("set reconnect %s", m_reConn ? "true" : "false");
	ret = mysql_options(m_mysqlConnection, MYSQL_OPT_RECONNECT, &m_reConn);
	 if (ret != 0)
	{
		fprintf(stderr, "set OPT_RECONNECT error: %s", mysql_error(m_mysqlConnection));
		return NULL;
	}

	if (! mysql_real_connect(m_mysqlConnection,
				hd->host,
				hd->user,
				hd->passwd,
				hd->db,
				hd->port,
				NULL,
				CLIENT_MULTI_STATEMENTS|CLIENT_MULTI_RESULTS))
	{
		fprintf(stderr, "connect error: %s", mysql_error(m_mysqlConnection));

		return NULL;
	}

	ret = mysql_query(m_mysqlConnection, "set unique_checks = 0;");
	if (ret != 0)
	{
		fprintf(stderr, "set unique_checks = 0 error: %s", mysql_error(m_mysqlConnection));
		return NULL;
	}

 //	hd->conn_hd = m_mysqlConnection;

	return m_mysqlConnection;
}

/*
 * Entry point for mysql2pgsql
 */
int 
mysql2pgsql_sync_main(char *desc, int nthread, mysql_conn_info *hd)
{
	int 		i = 0;
	Thread_hd th_hd;
	Thread			*thread = NULL;
	PGresult		*res = NULL;
	PGconn		*desc_conn;
	long		s_count = 0;
	long		t_count = 0;
	bool		have_err = false;
	TimevalStruct before,
					after; 
	double		elapsed_msec = 0;
	int		ntask = 0;
	MYSQL	*conn_src = NULL;
	MYSQL_RES	*my_res = NULL;
	char **p = NULL;

#ifndef WIN32
	signal(SIGINT, sigint_handler);
#endif

	GETTIMEOFDAY(&before);

	memset(&th_hd, 0, sizeof(Thread_hd));
	th_hd.nth = nthread;
	th_hd.desc = desc;
	th_hd.mysql_src = hd;

	conn_src = connect_to_mysql(hd);
	if (conn_src == NULL)
	{
		fprintf(stderr, "init src conn failed");
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

	if (hd->tabnames == NULL)
	{
		PQExpBuffer	query;
		MYSQL_ROW row;
		int ret = -1;

		query = createPQExpBuffer();

		appendPQExpBuffer(query, STMT_SHOW_TABLES, hd->db);
		ret = mysql_real_query(conn_src, query->data, strlen(query->data));
		if (ret != 0)
		{
			fprintf(stderr, "init desc conn failed: %s", mysql_error(conn_src));
			return 1;
		}
		
		my_res = mysql_store_result(conn_src);
		if (my_res == NULL)
		{
			fprintf(stderr, "get src db table failed: %s", mysql_error(conn_src));
			return 1;
		}
	
		ntask = mysql_num_rows(my_res);
		th_hd.ntask = ntask;
		if (th_hd.ntask >= 1)
		{
			th_hd.task = (Task_hd *)palloc0(sizeof(Task_hd) * th_hd.ntask);
		}

		/*
		  * The linked-array th_hd.task serves as a task queue for all the worker threads to pick up tasks and consume
		  */
		for (i = 0; i < th_hd.ntask; i++)
		{
			row = mysql_fetch_row(my_res);
			th_hd.task[i].id = i;
			th_hd.task[i].schemaname = NULL;
			th_hd.task[i].relname = pstrdup(row[0]);
			th_hd.task[i].count = 0;
			th_hd.task[i].complete = false;

			/* Set the former entry's link to this entry. Last entry's next feild would remain NULL */
			if (i != 0)
			{
				th_hd.task[i-1].next = &th_hd.task[i+1];
			}
		}
		mysql_free_result(my_res);
		PQclear(res);
		destroyPQExpBuffer(query);
	}
	else
	{
		for (i = 0, p = hd->tabnames; *p != NULL; p++, i++)
		{
		}

		ntask = i;
		th_hd.ntask = ntask;
		th_hd.task = (Task_hd *)palloc0(sizeof(Task_hd) * th_hd.ntask);

		for (i = 0, p = hd->tabnames; *p != NULL; p++, i++)
		{
			th_hd.task[i].id = i;
			th_hd.task[i].schemaname = NULL;
			th_hd.task[i].relname = *p;
			th_hd.task[i].query = hd->queries[i];

			th_hd.task[i].count = 0;
			th_hd.task[i].complete = false;
			/* Set the former entry's link to this entry. Last entry's next feild would remain NULL */
			if (i != 0)
			{
				th_hd.task[i-1].next = &th_hd.task[i];
			}		
		}
	}

	
	th_hd.l_task = &(th_hd.task[0]);

	th_hd.th = (ThreadArg *)palloc0(sizeof(ThreadArg) * th_hd.nth);
	for (i = 0; i < th_hd.nth; i++)
	{
		th_hd.th[i].id = i;
		th_hd.th[i].count = 0;
		th_hd.th[i].all_ok = false;

		th_hd.th[i].hd = &th_hd;
	}
	pthread_mutex_init(&th_hd.t_lock, NULL);

	fprintf(stderr, "starting full sync\n");

	thread = (Thread *)palloc0(sizeof(Thread) * th_hd.nth);
	for (i = 0; i < th_hd.nth; i++)
	{
		ThreadCreate(&thread[i], mysql2pgsql_copy_data, &th_hd.th[i]);
	}

	WaitThreadEnd(th_hd.nth, thread);

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

	fprintf(stderr, "number of rows migrated: %ld (number of source tables' rows: %ld) \n", s_count, t_count);
	fprintf(stderr, "full sync time cost %.3f ms\n", elapsed_msec);
	if (have_err)
	{
		fprintf(stderr, "errors occured during migration\n");
	}

	return 0;
}

static void *
mysql2pgsql_copy_data(void *arg)
{
	ThreadArg *args = (ThreadArg *)arg;
	Thread_hd *hd = args->hd;
	MYSQL_RES	*my_res;
	PGresult   *res2;
	PQExpBuffer	query;
	char *nspname;
	char *relname;
	Task_hd 	*curr = NULL;
	TimevalStruct before,
					after; 
	double		elapsed_msec = 0;
	MYSQL *origin_conn = NULL;
	PGconn *target_conn = NULL;
	int		ret = -1;
	MYSQL_ROW	row;
	Oid		*column_oids = NULL;
	StringInfoData s_tmp;
	int		i = 0;
	int		target_version;
	bool	isgp = false;

	initStringInfo(&s_tmp);
	origin_conn = connect_to_mysql(hd->mysql_src);
	if (origin_conn == NULL)
	{
		fprintf(stderr, "init src conn failed");
		return NULL;
	}
	
	target_conn = pglogical_connect(hd->desc, EXTENSION_NAME "_copy");
	if (target_conn == NULL)
	{
		fprintf(stderr, "init desc conn failed: %s", PQerrorMessage(target_conn));
		return NULL;
	}
	target_version = PQserverVersion(target_conn);
	isgp = is_greenplum(target_conn);
	setup_connection(target_conn, target_version, isgp);
	setup_connection_from_mysql(target_conn);

	query = createPQExpBuffer();

	if (get_ddl_only)
		fprintf(stderr, "\nReference commands to create target tables %s: \n***************\n\n",
				isgp ? "(Please choose a distribution key and replace it with <distribution key> for each table)" : "");
	while(1)
	{
		int			nlist = 0;
		int			n_col = 0;

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
	
		if (!get_ddl_only)
			start_copy_target_tx(target_conn, hd->desc_version, hd->desc_is_greenplum);

		nspname = hd->mysql_src->db;
		relname = curr->relname;

		//fprintf(stderr, "relname %s, query %s \n", relname, curr->query ? curr->query : "");

		if (curr->query || *curr->query == '\0')
			appendPQExpBufferStr(query, curr->query);
		else
			appendPQExpBuffer(query, STMT_SELECT,
							 nspname,
							 relname);

		fprintf(stderr, "Query to get source data for target table %s: %s \n", relname, query->data);
		ret = mysql_query(origin_conn, query->data);
		if (ret != 0)
		{
			fprintf(stderr, "run query error: %s\n", mysql_error(origin_conn));
			goto exit;
		}
		my_res = mysql_use_result(origin_conn);
		column_oids = fetch_colmum_info(relname, my_res, isgp);
		if (column_oids == NULL)
		{
			fprintf(stderr, "get table %s column type error", relname);
			goto exit;
		}

		if (get_ddl_only)
		{
			curr->complete = true;
			mysql_free_result(my_res);
			resetPQExpBuffer(query);
			continue;
		}
		
		n_col = mysql_num_fields(my_res);

		resetPQExpBuffer(query);
		appendPQExpBuffer(query, "COPY %s FROM stdin DELIMITERS '|' with csv QUOTE ''''",
						 PQescapeIdentifier(target_conn, relname,
											strlen(relname)));

		res2 = PQexec(target_conn, query->data);
		if (PQresultStatus(res2) != PGRES_COPY_IN)
		{
			fprintf(stderr,"table copy failed Query '%s': %s", 
				query->data, PQerrorMessage(target_conn));
			goto exit;
		}

		while ((row = mysql_fetch_row(my_res)) != NULL)
		{
			unsigned long *lengths;
			bool	first = true;

			resetPQExpBuffer(query);
			lengths = mysql_fetch_lengths(my_res);
			for (i = 0; i < n_col; i++)
			{
				if (first)
				{
					first = false;
				}
				else
				{
					appendPQExpBufferStr(query, "|");
				}
			
				if(lengths[i] != 0)
				{
					quote_literal_local_withoid(&s_tmp, row[i], column_oids[i], query);
				}
			}
			appendPQExpBufferStr(query, "\n");

			if (PQputCopyData(target_conn, query->data, query->len) != 1)
			{
				fprintf(stderr,"writing to target table failed destination connection reported: %s",
							 PQerrorMessage(target_conn));
				goto exit;
			}
				
			args->count++;
			curr->count++;

			if (time_to_abort)
			{
				fprintf(stderr, "receive shutdown sigint\n");
				goto exit;
			}
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

		finish_copy_target_tx(target_conn);
		curr->complete = true;
		PQclear(res2);
		resetPQExpBuffer(query);
		mysql_free_result(my_res);

		GETTIMEOFDAY(&after);
		DIFF_MSEC(&after, &before, elapsed_msec);
		fprintf(stderr,"thread %d migrate task %d table %s.%s %ld rows complete, time cost %.3f ms\n",
						 args->id, curr->id, nspname, relname, curr->count, elapsed_msec);
	}
	
	args->all_ok = true;

	if (get_ddl_only)
		fprintf(stderr, "***************\n\n");

exit:

	mysql_close(origin_conn);
	PQfinish(target_conn);
	ThreadExit(0);
	return NULL;
}

static void
quote_literal_local_withoid(StringInfo s, const char *rawstr, Oid type, PQExpBuffer buffer)
{
	char	   *result;
	int			len;
	int			newlen;
	bool need_process = true;

	if (INT2OID == type)
		need_process = false;
	else if (INT4OID == type)
		need_process = false;
	else if (INT8OID == type)
		need_process = false;
	else if (FLOAT4OID == type)
		need_process = false;
	else if (FLOAT8OID == type)
		need_process = false;
	else if (NUMERICOID == type)
		need_process = false;
	else if (TIMESTAMPOID == type)
	{
		if (strcmp(rawstr, "0000-00-00") == 0 ||
			strcmp(rawstr, "00:00:00") == 0 ||
			strcmp(rawstr, "0000-00-00 00:00:00") == 0 ||
			strcmp(rawstr, "0000") == 0)
		{
			return;
		}
	
		need_process = false;
	}

	if (need_process == false)
	{
		appendPQExpBuffer(buffer, "%s", rawstr);
		return;
	}

	/*if (TIMESTAMPOID == type)
		need_process = false;

	if (need_process == false)
	{
		appendPQExpBuffer(buffer, "'");
		appendPQExpBuffer(buffer, "%s", rawstr);
		appendPQExpBuffer(buffer, "'");
		return;
	}*/

	len = strlen(rawstr);
	resetStringInfo(s);
	appendStringInfoSpaces(s, len * 2 + 3);

	result = s->data;

	newlen = quote_literal_internal(result, rawstr, len);
	result[newlen] = '\0';

	appendPQExpBufferStr(buffer, result);

	return;
}

static int
setup_connection_from_mysql(PGconn *conn)
{
	ExecuteSqlStatement(conn, "SET standard_conforming_strings TO off");
	ExecuteSqlStatement(conn, "SET backslash_quote TO on");

	return 0;
}

