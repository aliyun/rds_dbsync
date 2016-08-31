/* -------------------------------------------------------------------------
 *
 * pg_logicaldecode.c
 *		Replication
 *
 * Replication
 *
 * Copyright (C) 2012-2015, Alibaba Group
 *
 * IDENTIFICATION
 *		pg_logicaldecode.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres_fe.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pg_logicaldecode.h"
#include "pqexpbuffer.h"

#include <time.h>

#include "utils.h"
#include "misc.h"

#ifndef WIN32
#include <unistd.h>
#include <sys/time.h>
#endif

static int checktuple(ALI_PG_DECODE_MESSAGE *msg, int kind, Decode_TupleData *new_tuple, Decode_TupleData *old_tuple);
static void append_insert_colname(ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple);
static void quote_literal_local(Decoder_handler *hander, const char *rawstr, char *type, PQExpBuffer buffer);
static void append_insert_values(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple);
static void append_delete_where_statement(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple);
static void append_update_statement(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer);
static bool is_key_column(ALI_PG_DECODE_MESSAGE *msg, char *colname);
static bool append_values(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple, int i);
static void append_update_statement_key_not_change(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer);
static void append_update_statement_key_change(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer);
static void append_update_statement_full_row(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer);
static XLogRecPtr pg_lsn_in(char *lsn);

void
out_put_key_att(ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer)
{
	int i;
	if (msg->k_natt <= 0)
		return;

	appendPQExpBuffer(buffer, "key column");
	for (i = 0; i < msg->k_natt; i++)
	{
		appendPQExpBuffer(buffer, " %s", msg->k_attname[i]);
	}
	appendPQExpBuffer(buffer, "\n");
	
}

void
out_put_tuple(ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple)
{
	int natt;
	int	i;

	if (tuple->natt == 0)
	{
		fprintf(stderr, "tuple is null");
		return;
	}

	natt = msg->natt;
		
	if (tuple->natt != natt)
	{
		fprintf(stderr, "attnum %d not equal tuple attnum %d",  msg->natt, tuple->natt);
		return;
	}
	for (i = 0; i < natt; i++)
	{
		char	*value;

		if (tuple->isnull[i] && tuple->changed[i] && tuple->svalues[i] == NULL)
		{
			value = "{null}";
		}
		else if (tuple->isnull[i] && tuple->changed[i] == false && tuple->svalues[i] == NULL)
		{
			value = "{unchanged toast column}";
		}
		else
		{
			if (tuple->svalues[i] == NULL)
			{
				fprintf(stderr, "%s.%s column %s value is null unnormal",  msg->schemaname, msg->relname,
														msg->attname[i]);
				return;
			}
			value = tuple->svalues[i];
		}

        if (msg->attname[i])
        {
            appendPQExpBuffer(buffer, "%d %s[%s]'%s'\n", i, msg->attname[i], msg->atttype[i], value);
        }
        else
        {
            appendPQExpBuffer(buffer, "%d dropped\n", i);
        }
	}
}

static int
checktuple(ALI_PG_DECODE_MESSAGE *msg, int kind, Decode_TupleData *new_tuple, Decode_TupleData *old_tuple)
{
	if (kind == MSGKIND_DELETE && msg->has_key_or_old == false)
	{
		fprintf(stderr, "update or delete message does not contain old key or old tuple");
		return 1;
	}
	
	if ((kind == MSGKIND_INSERT || kind == MSGKIND_UPDATE) && new_tuple->natt == 0)
	{
		fprintf(stderr, "new tuple is null");
		return 1;
	}

	if (kind == MSGKIND_DELETE && old_tuple->natt == 0)
	{
		fprintf(stderr, "old tuple is null");
		return 1;
	}

	if ((kind == MSGKIND_INSERT || kind == MSGKIND_UPDATE) && new_tuple->natt != msg->natt)
	{
		fprintf(stderr, "attnum %d not equal tuple attnum %d",  msg->natt, new_tuple->natt);
		return 1;
	}

	return 0;
}

static void
append_insert_colname(ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple)
{
	int i;
	
	appendPQExpBuffer(buffer, "(");
	for (i = 0; i < tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}

		appendPQExpBuffer(buffer, "%s", msg->attname[i]);
		if(i != tuple->natt - 1)
		{
			appendPQExpBuffer(buffer, ",");
		}
	}
	appendPQExpBuffer(buffer, ") ");
}

static void
append_insert_values(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple)
{
	int i;
	bool	first = true;

	appendPQExpBuffer(buffer, "VALUES(");
	for (i = 0; i < tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(buffer, ", ");
		}
		
		if (tuple->isnull[i] && tuple->changed[i] && tuple->svalues[i] == NULL)
		{
			appendPQExpBuffer(buffer, "null");
		}
		else if (tuple->isnull[i] && tuple->changed[i] == false && tuple->svalues[i] == NULL)
		{
			appendPQExpBuffer(buffer, "null");
		}
		else
		{
			quote_literal_local(hander, tuple->svalues[i], msg->atttype[i], buffer);
		}
	}
	appendPQExpBuffer(buffer, ");");

}

static void
append_delete_where_statement(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple)
{
	int i;
	bool	first = true;

	appendPQExpBuffer(buffer, "WHERE ");
	for (i = 0; i < tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}
		else
		{
			bool is_key = false;

			if (msg->k_natt > 0)
			{
				is_key = is_key_column(msg, msg->attname[i]);
				if (is_key == false)
					continue;
			}
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(buffer, " AND ");
		}

		append_values(hander, msg, buffer, tuple, i);
	}
	appendPQExpBuffer(buffer, ";");

}

static void
append_update_statement(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer)
{
	Decode_TupleData *new_tuple = &(msg->newtuple);
	Decode_TupleData *old_tuple = &(msg->oldtuple);
	bool	full_row_mode = false;
	bool 	key_not_change = false;

	if (msg->k_natt == 0)
	{
		if(msg->has_key_or_old == false || old_tuple->natt == 0)
		{
			fprintf(stderr, "invalid tuple data");
			return;
		}
		else if(msg->natt == new_tuple->natt &&
			msg->natt == old_tuple->natt)
		{
			full_row_mode = true;
		}
		else
		{
			fprintf(stderr, "invalid tuple data");
			return;
		}
	}
	else if (old_tuple->natt == 0)
	{
		full_row_mode = false;
		key_not_change = true;
	}
	else
	{
		full_row_mode = false;
		key_not_change = false;
	}

	appendPQExpBuffer(buffer, "UPDATE %s.%s ", msg->schemaname, msg->relname);

	if(full_row_mode)
	{
		append_update_statement_full_row(hander, msg, buffer);
	}
	else if(key_not_change)
	{
		append_update_statement_key_not_change(hander, msg, buffer);
	}
	else
	{
		append_update_statement_key_change(hander, msg, buffer);
	}

	return;
}

static void
append_update_statement_key_change(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer)
{
	Decode_TupleData *new_tuple = &(msg->newtuple);
	Decode_TupleData *old_tuple = &(msg->oldtuple);
	int i;
	bool	first = true;
	PQExpBuffer where;
	PQExpBuffer set;

	where = createPQExpBuffer();
	set = createPQExpBuffer();

	appendPQExpBuffer(where, " WHERE ");
	for (i = 0; i < old_tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}
		else
		{
			bool is_key = false;

			if (msg->k_natt > 0)
			{
				is_key = is_key_column(msg, msg->attname[i]);
				if (is_key == false)
					continue;
			}
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(where, " AND ");
		}

		append_values(hander, msg, where, old_tuple, i);
	}
	appendPQExpBuffer(where, ";");

	first = true;
	appendPQExpBuffer(set, " SET ");
	for (i = 0; i < new_tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(set, " , ");
		}

		append_values(hander, msg, set, new_tuple, i);
	}

	appendPQExpBufferStr(buffer, set->data);
	appendPQExpBufferStr(buffer, where->data);

	destroyPQExpBuffer(set);
	destroyPQExpBuffer(where);

	return;
}

static void
append_update_statement_full_row(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer)
{
	Decode_TupleData *new_tuple = &(msg->newtuple);
	Decode_TupleData *old_tuple = &(msg->oldtuple);
	int i;
	bool	first = true;
	PQExpBuffer where;
	PQExpBuffer set;
	bool	*droped = NULL;
	bool	*change = NULL;
	bool	*old_isnull = NULL;
	bool	*uchange_toast = NULL;

	where = createPQExpBuffer();
	set = createPQExpBuffer();

	droped = (bool *)palloc0(sizeof(bool) * msg->natt);
	change = (bool *)palloc0(sizeof(bool) * msg->natt);
	old_isnull = (bool *)palloc0(sizeof(bool) * msg->natt);
	uchange_toast = (bool *)palloc0(sizeof(bool) * msg->natt);

	appendPQExpBuffer(where, " WHERE ");
	for (i = 0; i < old_tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			droped[i] = true;
			continue;
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(where, " AND ");
		}

		old_isnull[i] = append_values(hander, msg, where, old_tuple, i);
		if (old_tuple->isnull[i] && old_tuple->changed[i] == false && old_tuple->svalues[i] == NULL)
		{
			uchange_toast[i] = true;
		}
	}
	appendPQExpBuffer(where, ";");

	for (i = 0; i < new_tuple->natt; i++)
	{
		if (droped[i])
		{
			continue;
		}
		
		if (new_tuple->isnull[i] && new_tuple->changed[i] && new_tuple->svalues[i] == NULL)
		{
			if (old_isnull[i])
			{
				change[i] = false;
			}
			else if(uchange_toast[i])
			{
				change[i] = true;
			}
			else
			{
				change[i] = true;
			}
		}
		else if (new_tuple->isnull[i] && new_tuple->changed[i] == false && new_tuple->svalues[i] == NULL)
		{
			if (old_isnull[i])
			{
				change[i] = false;
			}
			else if(uchange_toast[i])
			{
				change[i] = false;
			}
			else
			{
				change[i] = false;
			}
		}
		else
		{
			if (old_isnull[i])
			{
				change[i] = true;
			}
			else if(uchange_toast[i])
			{
				change[i] = false;
			}
			else
			{
				if (new_tuple->svalues[i] == NULL || old_tuple->svalues[i] == NULL)
				{
					fprintf(stderr, "invalid old or new tuple data");
					change[i] = false;
				}
				else if (strcmp(new_tuple->svalues[i], old_tuple->svalues[i]) == 0)
				{
					change[i] = false;
				}
				else
				{
					change[i] = true;
				}
			}
		}
	}

	first = true;
	appendPQExpBuffer(set, " SET ");
	for (i = 0; i < new_tuple->natt; i++)
	{
		if (droped[i])
		{
			continue;
		}

		if (change[i])
		{
			if (first)
			{
				first = false;
			}
			else
			{
				appendPQExpBuffer(set, " , ");
			}
			append_values(hander, msg, set, new_tuple, i);
		}
	}

	appendPQExpBufferStr(buffer, set->data);
	appendPQExpBufferStr(buffer, where->data);

	destroyPQExpBuffer(set);
	destroyPQExpBuffer(where);

	return;
}


static void
append_update_statement_key_not_change(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer)
{
	Decode_TupleData *new_tuple = &(msg->newtuple);
	int i;
	bool	first = true;
	PQExpBuffer where;
	PQExpBuffer set;

	where = createPQExpBuffer();
	set = createPQExpBuffer();

	appendPQExpBuffer(where, " WHERE ");
	for (i = 0; i < new_tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}
		else
		{
			bool is_key = false;

			if (msg->k_natt > 0)
			{
				is_key = is_key_column(msg, msg->attname[i]);
				if (is_key == false)
					continue;
			}
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(where, " AND ");
		}

		append_values(hander, msg, where, new_tuple, i);
	}
	appendPQExpBuffer(where, ";");

	first = true;
	appendPQExpBuffer(set, " SET ");
	for (i = 0; i < new_tuple->natt; i++)
	{
		if (msg->attname[i] == NULL)
		{
			continue;
		}
		else
		{
			bool is_key = false;
			if (msg->k_natt > 0)
			{
				is_key = is_key_column(msg, msg->attname[i]);
				if (is_key == true)
					continue;
			}
		}

		if (first)
		{
			first = false;
		}
		else
		{
			appendPQExpBuffer(set, " , ");
		}

		append_values(hander, msg, set, new_tuple, i);
	}


	appendPQExpBufferStr(buffer, set->data);
	appendPQExpBufferStr(buffer, where->data);

	destroyPQExpBuffer(set);
	destroyPQExpBuffer(where);

	return;
}

int
out_put_tuple_to_sql(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer)
{
	int kind = msg->type;
	Decode_TupleData *new_tuple = NULL;
	Decode_TupleData *old_tuple = NULL;
	int rc = 0;

	new_tuple = &(msg->newtuple);
	old_tuple = &(msg->oldtuple);
	rc = checktuple(msg, kind, new_tuple, old_tuple);
	if (rc != 0)
		return 1;

	switch (kind)
	{
		case MSGKIND_BEGIN:
			{
				appendPQExpBuffer(buffer, "begin;");
			}
			break;

		case MSGKIND_COMMIT:
			{
				appendPQExpBuffer(buffer, "commit;");
			}
			break;

		case MSGKIND_INSERT:
			{
				appendPQExpBuffer(buffer, "INSERT INTO %s.%s ", msg->schemaname,msg->relname);
				append_insert_colname(msg, buffer, new_tuple);
				append_insert_values(hander, msg, buffer, new_tuple);
			}
			break;

		case MSGKIND_UPDATE:
			{
				append_update_statement(hander, msg, buffer);
			}
			break;

		case MSGKIND_DELETE:
			{
				appendPQExpBuffer(buffer, "DELETE FROM %s.%s ", msg->schemaname,msg->relname);
				append_delete_where_statement(hander, msg, buffer, old_tuple);
			}
			break;

		default:
			{
				fprintf(stderr, "unknown action of type %c", kind);
				exit(1);
			}
	}

	return 0;
}

void
out_put_decode_message(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, int outfd)
{
	int bytes_left = 0;
	int	bytes_written = 0;
	PQExpBuffer buffer;
	char	msgkind = MSGKIND_UNKNOWN;

	buffer = createPQExpBuffer();
	
	if (hander->verbose)
	{
		msgkind = msg->type;
		switch (msgkind)
		{
			case MSGKIND_BEGIN:
				{
					//appendPQExpBuffer(buffer, "BEGIN %u (at %s)\n", msg->xid, timestamptz_to_str(msg->tm));
				}
				break;
			case MSGKIND_COMMIT:
				{
					//appendPQExpBuffer(buffer, "COMMIT (at %s) lsn %X/%X\n\n", timestamptz_to_str(msg->tm),
					//				(uint32)(msg->lsn>>32), (uint32)msg->lsn);
				}
				break;
			case MSGKIND_INSERT:
				{
					appendPQExpBuffer(buffer, "INSERT TABLE %s.%s\n", msg->schemaname,msg->relname);
					appendPQExpBuffer(buffer, "new tuple:\n");
					out_put_tuple(msg, buffer, &msg->newtuple);
				}
				break;
			case MSGKIND_UPDATE:
				{
					appendPQExpBuffer(buffer, "UPDATE TABLE %s.%s\n", msg->schemaname,msg->relname);
					out_put_key_att(msg, buffer);
					appendPQExpBuffer(buffer, "new tuple:\n");
					out_put_tuple(msg, buffer, &msg->newtuple);

					if(msg->has_key_or_old == false)
					{
						appendPQExpBuffer(buffer, "null old or key tuple\n");
					}
					else if (msg->natt == msg->oldtuple.natt)
					{
						appendPQExpBuffer(buffer, "old tuple:\n");
						out_put_tuple(msg, buffer, &msg->oldtuple);
					}
					else
					{
						appendPQExpBuffer(buffer, "key tuple:\n");
						out_put_tuple(msg, buffer, &msg->oldtuple);
					}
					
				}
				break;
			case MSGKIND_DELETE:
				{
					appendPQExpBuffer(buffer, "DELETE TABLE %s.%s\n", msg->schemaname,msg->relname);
					out_put_key_att(msg, buffer);
					if(msg->has_key_or_old == false)
					{
						appendPQExpBuffer(buffer, "null old or key tuple\n");
					}
					else if (msg->natt == msg->oldtuple.natt)
					{
						appendPQExpBuffer(buffer, "old tuple:\n");
						out_put_tuple(msg, buffer, &msg->oldtuple);
					}
					else
					{
						appendPQExpBuffer(buffer, "key tuple:\n");
						out_put_tuple(msg, buffer, &msg->oldtuple);
					}
				}
				break;

			default:
				fprintf(stderr, "unknown action of type %c", msgkind);

		}
	}

	out_put_tuple_to_sql(hander, msg, buffer);

	bytes_left = buffer->len;
	bytes_written = 0;

	while (bytes_left)
	{
		int 		ret;
	
		ret = write(outfd,
					buffer->data + bytes_written,
					bytes_left);

		if (ret < 0)
		{
			fprintf(stderr,
			  _("could not write %u bytes to log file: %s\n"),
					bytes_left, strerror(errno));
		}
	
		bytes_written += ret;
		bytes_left -= ret;
	}
	
	if (write(outfd, "\n", 1) != 1)
	{
		fprintf(stderr,
			  _("could not write %u bytes to log file: %s\n"),
					bytes_left, strerror(errno));
		fprintf(stderr, "could not write %u bytes to log file: %s\n",
					bytes_left, strerror(errno));
	}
	
	destroyPQExpBuffer(buffer);
}

#define MAXDATELEN		128
#define USECS_PER_SEC	INT64CONST(1000000)

#define UNIX_EPOCH_JDATE		2440588 /* == date2j(1970, 1, 1) */
#define POSTGRES_EPOCH_JDATE	2451545 /* == date2j(2000, 1, 1) */
#define SECS_PER_DAY	86400

char *
timestamptz_to_str(TimestampTz dt)
{
	static char buf[MAXDATELEN + 1];
	char		ts[MAXDATELEN + 1];
	char		zone[MAXDATELEN + 1];
	time_t		result = (time_t) timestamptz_to_time_t(dt);
	struct tm  *ltime = localtime(&result);

	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", ltime);
	strftime(zone, sizeof(zone), "%Z", ltime);

#ifdef HAVE_INT64_TIMESTAMP
	sprintf(buf, "%s.%06d %s", ts, (int) (dt % USECS_PER_SEC), zone);
#else
	sprintf(buf, "%s.%.6f %s", ts, fabs(dt - floor(dt)), zone);
#endif

	return buf;
}

int64
timestamptz_to_time_t(TimestampTz t)
{
	int64	result;

#ifdef HAVE_INT64_TIMESTAMP
	result = (int64) (t / USECS_PER_SEC +
				 ((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY));
#else
	result = (int64) (t +
				 ((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY));
#endif
	return result;
}

/*
 * Connect to the server. Returns a valid PGconn pointer if connected,
 * or NULL on non-permanent error. On permanent error, the function will
 * call exit(1) directly.
 */
PGconn *
GetConnection(char *connection_string)
{
	PGconn	   *tmpconn;
	int			argcount = 7;	/* dbname, replication, fallback_app_name,
								 * host, user, port, password */
	int			i;
	const char **keywords;
	const char **values;
	const char *tmpparam;
	PQconninfoOption *conn_opts = NULL;
	PQconninfoOption *conn_opt;
	char	   *err_msg = NULL;
	char	*progname = "ali_logicaldecode";

	/*
	 * Merge the connection info inputs given in form of connection string,
	 * options and default values (dbname=replication, replication=true, etc.)
	 */
	i = 0;
	if (connection_string)
	{
		conn_opts = PQconninfoParse(connection_string, &err_msg);
		if (conn_opts == NULL)
		{
			fprintf(stderr, "%s: %s", progname, err_msg);
			exit(1);
		}

		for (conn_opt = conn_opts; conn_opt->keyword != NULL; conn_opt++)
		{
			if (conn_opt->val != NULL && conn_opt->val[0] != '\0')
				argcount++;
		}

		keywords = pg_malloc0((argcount + 1) * sizeof(*keywords));
		values = pg_malloc0((argcount + 1) * sizeof(*values));

		for (conn_opt = conn_opts; conn_opt->keyword != NULL; conn_opt++)
		{
			if (conn_opt->val != NULL && conn_opt->val[0] != '\0')
			{
				keywords[i] = conn_opt->keyword;
				values[i] = conn_opt->val;
				i++;
			}
		}
	}
	else
	{
		keywords = pg_malloc0((argcount + 1) * sizeof(*keywords));
		values = pg_malloc0((argcount + 1) * sizeof(*values));
	}

	keywords[i] = "replication";
	//values[i] = dbname == NULL ? "true" : "database";
	values[i] = "database";
	i++;
	keywords[i] = "fallback_application_name";
	values[i] = "ali_logicaldecode";
	i++;

	keywords[i] = NULL;
	values[i] = NULL;

	tmpconn = PQconnectdbParams(keywords, values, true);

	/*
	 * If there is too little memory even to allocate the PGconn object
	 * and PQconnectdbParams returns NULL, we call exit(1) directly.
	 */
	if (!tmpconn)
	{
		fprintf(stderr, _("%s: could not connect to server\n"),
				progname);
		exit(1);
	}

	if (PQstatus(tmpconn) != CONNECTION_OK)
	{
		fprintf(stderr, _("%s: could not connect to server: %s\n"),
				progname, PQerrorMessage(tmpconn));
		PQfinish(tmpconn);
		free(values);
		free(keywords);
		if (conn_opts)
			PQconninfoFree(conn_opts);
		return NULL;
	}

	/* Connection ok! */
	free(values);
	free(keywords);
	if (conn_opts)
		PQconninfoFree(conn_opts);

	/*
	 * Ensure we have the same value of integer timestamps as the server we
	 * are connecting to.
	 */
	tmpparam = PQparameterStatus(tmpconn, "integer_datetimes");
	if (!tmpparam)
	{
		fprintf(stderr,
		 _("%s: could not determine server setting for integer_datetimes\n"),
				progname);
		PQfinish(tmpconn);
		exit(1);
	}

#ifdef HAVE_INT64_TIMESTAMP
	if (strcmp(tmpparam, "on") != 0)
#else
	if (strcmp(tmpparam, "off") != 0)
#endif
	{
		fprintf(stderr,
			 _("%s: integer_datetimes compile flag does not match server\n"),
				progname);
		PQfinish(tmpconn);
		exit(1);
	}

	return tmpconn;
}

void
fe_sendint64(int64 i, char *buf)
{
	uint32		n32;

	/* High order half first, since we're doing MSB-first */
	n32 = (uint32) (i >> 32);
	n32 = htonl(n32);
	memcpy(&buf[0], &n32, 4);

	/* Now the low order half */
	n32 = (uint32) i;
	n32 = htonl(n32);
	memcpy(&buf[4], &n32, 4);
}

int64
feGetCurrentTimestamp(void)
{
	int64		result;
	struct timeval tp;

	gettimeofday(&tp, NULL);

	result = (int64) tp.tv_sec -
		((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY);

	result = (result * USECS_PER_SEC) + tp.tv_usec;

	return result;
}

bool
feTimestampDifferenceExceeds(int64 start_time,
							 int64 stop_time,
							 int msec)
{
	int64		diff = stop_time - start_time;

	return (diff >= msec * INT64CONST(1000));
}

void
feTimestampDifference(int64 start_time, int64 stop_time,
					  long *secs, int *microsecs)
{
	int64		diff = stop_time - start_time;

	if (diff <= 0)
	{
		*secs = 0;
		*microsecs = 0;
	}
	else
	{
		*secs = (long) (diff / USECS_PER_SEC);
		*microsecs = (int) (diff % USECS_PER_SEC);
	}
}

int64
fe_recvint64(char *buf)
{
	int64		result;
	uint32		h32;
	uint32		l32;

	memcpy(&h32, buf, 4);
	memcpy(&l32, buf + 4, 4);
	h32 = ntohl(h32);
	l32 = ntohl(l32);

	result = h32;
	result <<= 32;
	result |= l32;

	return result;
}

/*
 * Send a Standby Status Update message to server.
 */
bool
sendFeedback(Decoder_handler *hander, int64 now, bool force, bool replyRequested)
{
	char		replybuf[1 + 8 + 8 + 8 + 8 + 1];
	int			len = 0;

	/*
	 * we normally don't want to send superfluous feedbacks, but if it's
	 * because of a timeout we need to, otherwise wal_sender_timeout will kill
	 * us.
	 */
	if (!force &&
		hander->last_recvpos == hander->recvpos)
		return true;

	fprintf(stderr,"%s: confirming recv up to %X/%X, flush to %X/%X (slot %s)\n",
				hander->progname,
				(uint32) (hander->recvpos >> 32), (uint32) hander->recvpos,
				(uint32) (hander->flushpos >> 32), (uint32) hander->flushpos,
				hander->replication_slot);

	replybuf[len] = 'r';
	len += 1;
	fe_sendint64(hander->recvpos, &replybuf[len]);		/* write */
	len += 8;
	fe_sendint64(hander->flushpos, &replybuf[len]);		/* flush */
	len += 8;
	fe_sendint64(InvalidXLogRecPtr, &replybuf[len]);	/* apply */
	len += 8;
	fe_sendint64(now, &replybuf[len]);	/* sendTime */
	len += 8;
	replybuf[len] = replyRequested ? 1 : 0;		/* replyRequested */
	len += 1;

	hander->startpos = hander->recvpos;
	hander->last_recvpos= hander->recvpos;

	if (PQputCopyData(hander->conn, replybuf, len) <= 0 || PQflush(hander->conn))
	{
		fprintf(stderr, "%s: could not send feedback packet: %s", hander->progname, PQerrorMessage(hander->conn));
		return false;
	}

	return true;
}

int
initialize_connection(Decoder_handler *hander)
{
	PGresult *res = NULL;

	if (hander->connection_string == NULL)
	{
		fprintf(stderr, _("connection_string is null"));
		return 1;
	}

	/*
	 * don't really need this but it actually helps to get more precise error
	 * messages about authentication, required GUCs and such without starting
	 * to loop around connection attempts lateron.
	 */
	hander->conn = GetConnection(hander->connection_string);
	if (!hander->conn)
	{
		return 1;
	}

	/*
	 * Run IDENTIFY_SYSTEM so we can get the timeline and current xlog
	 * position.
	 */
	res = PQexec(hander->conn, "IDENTIFY_SYSTEM");
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, _("%s: could not send replication command \"%s\": %s"),
				hander->progname, "IDENTIFY_SYSTEM", PQerrorMessage(hander->conn));
		disconnect(hander);
		return 1;
	}

	if (PQntuples(res) != 1 || PQnfields(res) < 4)
	{
		fprintf(stderr,
				_("%s: could not identify system: got %d rows and %d fields, expected %d rows and %d or more fields\n"),
				hander->progname, PQntuples(res), PQnfields(res), 1, 4);
		disconnect(hander);
		return 1;
	}
	PQclear(res);

	return 0;
}

void
disconnect(Decoder_handler *hander)
{
	if (hander->conn != NULL)
		PQfinish(hander->conn);
}

int
check_handler_parameters(Decoder_handler *hander)
{
	/*
	 * Required arguments
	 */
	if (hander->replication_slot == NULL)
	{
		fprintf(stderr, _("%s: no slot specified\n"), hander->progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				hander->progname);
		return 1;
	}

	if (hander->do_start_slot && hander->outfile == NULL)
	{
		fprintf(stderr, _("%s: no target file specified\n"), hander->progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				hander->progname);
		return 1;
	}

	if (!hander->do_drop_slot && !hander->do_create_slot && !hander->do_start_slot)
	{
		fprintf(stderr, _("%s: at least one action needs to be specified\n"), hander->progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				hander->progname);
		return 1;
	}

	if (hander->do_drop_slot && (hander->do_create_slot || hander->do_start_slot))
	{
		fprintf(stderr, _("%s: cannot use --create-slot or --start together with --drop-slot\n"), hander->progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				hander->progname);
		return 1;
	}

	if (hander->startpos != InvalidXLogRecPtr && (hander->do_create_slot || hander->do_drop_slot))
	{
		fprintf(stderr, _("%s: cannot use --create-slot or --drop-slot together with --startpos\n"), hander->progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				hander->progname);
		return 1;
	}

	return 0;
}

int
drop_replication_slot(Decoder_handler *hander)
{
	PGresult *res = NULL;

	/*
	 * drop a replication slot
	 */
	if (hander->do_drop_slot)
	{
		char		query[256];

		if (hander->verbose)
			fprintf(stderr,
					_("%s: dropping replication slot \"%s\"\n"),
					hander->progname, hander->replication_slot);

		snprintf(query, sizeof(query), "DROP_REPLICATION_SLOT \"%s\"",
				 hander->replication_slot);
		res = PQexec(hander->conn, query);
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			fprintf(stderr, _("%s: could not send replication command \"%s\": %s"),
					hander->progname, query, PQerrorMessage(hander->conn));
			disconnect(hander);
			return 1;
		}

		if (PQntuples(res) != 0 || PQnfields(res) != 0)
		{
			fprintf(stderr,
					_("%s: could not drop replication slot \"%s\": got %d rows and %d fields, expected %d rows and %d fields\n"),
					hander->progname, hander->replication_slot, PQntuples(res), PQnfields(res), 0, 0);
			disconnect(hander);
			return 1;
		}

		PQclear(res);
		disconnect(hander);
	}
	else
	{
		return 1;
	}

	return 0;
}

char *
create_replication_slot(Decoder_handler *hander,XLogRecPtr *lsn, char *replication_slot)
{
	PGresult *res = NULL;
	uint32		hi,
				lo;
	char		   *snapshot;
	char			*slsn;

	if (replication_slot == NULL)
	{
		return NULL;
	}

	/*
	 * create a replication slot
	 */
	if (hander->do_create_slot)
	{
		char		query[256];

		if (hander->verbose)
			fprintf(stderr,
					_("%s: creating replication slot \"%s\"\n"),
					hander->progname, replication_slot);

		snprintf(query, sizeof(query), "CREATE_REPLICATION_SLOT \"%s\" LOGICAL \"ali_decoding\"",
				 replication_slot);

		res = PQexec(hander->conn, query);
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			fprintf(stderr, _("%s: could not send replication command \"%s\": %s"),
					hander->progname, query, PQerrorMessage(hander->conn));
			disconnect(hander);
			return NULL;
		}

		if (PQntuples(res) != 1 || PQnfields(res) != 4)
		{
			fprintf(stderr,
					_("%s: could not create replication slot \"%s\": got %d rows and %d fields, expected %d rows and %d fields\n"),
					hander->progname, replication_slot, PQntuples(res), PQnfields(res), 1, 4);
			disconnect(hander);
			return NULL;
		}

		if (sscanf(PQgetvalue(res, 0, 1), "%X/%X", &hi, &lo) != 2)
		{
			fprintf(stderr,
					_("%s: could not parse transaction log location \"%s\"\n"),
					hander->progname, PQgetvalue(res, 0, 1));
			disconnect(hander);
			return NULL;
		}
		hander->startpos = ((uint64) hi) << 32 | lo;

		hander->replication_slot = strdup(PQgetvalue(res, 0, 0));

		slsn = PQgetvalue(res, 0, 1);
		*lsn = pg_lsn_in(slsn);
		snapshot = pstrdup(PQgetvalue(res, 0, 2));
		
		PQclear(res);
	}
	else
	{
		return NULL;
	}

	return snapshot;
}

int
init_logfile(Decoder_handler *hander)
{
	/* open the output file, if not open yet */
	if (hander->outfd == -1 && hander->outfile != NULL)
	{
		if (strcmp(hander->outfile, "-") == 0)
			hander->outfd = fileno(stdout);
		else
			hander->outfd = open(hander->outfile, O_CREAT | O_APPEND | O_WRONLY | PG_BINARY,
						 S_IRUSR | S_IWUSR);
		if (hander->outfd == -1)
		{
			fprintf(stderr,
					_("%s: could not open log file \"%s\": %s\n"),
					hander->progname, hander->outfile, strerror(errno));
			return 1;
		}
	}
	else
	{
		fprintf(stderr,
					_("%s: init logfile \"%s\": faild\n"),
					hander->progname, hander->outfile);
		return 1;
	}

	return 0;
}

int
init_streaming(Decoder_handler *hander)
{
	PGresult   *res = NULL;
	PQExpBuffer query;

	query = createPQExpBuffer();

	/*
	 * Connect in replication mode to the server
	 */

	if (!hander->conn)
		/* Error message already written in GetConnection() */
		return 1;

	fprintf(stderr,
				_("%s: starting log streaming at %X/%X (slot %s)\n"),
				hander->progname, (uint32) (hander->startpos >> 32), (uint32) hander->startpos,
				hander->replication_slot);

	/* Initiate the replication stream at specified location */
	appendPQExpBuffer(query, "START_REPLICATION SLOT \"%s\" LOGICAL %X/%X (",
			 hander->replication_slot, (uint32) (hander->startpos >> 32), (uint32) hander->startpos);

	appendPQExpBuffer(query, "version '%u'", PG_VERSION_NUM);
	appendPQExpBuffer(query, ", encoding '%s'", "UTF8");
	appendPQExpBufferChar(query, ')');

	res = PQexec(hander->conn, query->data);
	if (PQresultStatus(res) != PGRES_COPY_BOTH)
	{
		fprintf(stderr, _("%s: could not send replication command \"%s\": %s"),
				hander->progname, query->data, PQresultErrorMessage(res));
		PQclear(res);
		destroyPQExpBuffer(query);
		return 1;
	}
	PQclear(res);

	if (hander->verbose)
		fprintf(stderr,
				_("%s: streaming initiated\n"),
				hander->progname);
	
	destroyPQExpBuffer(query);

	return 0;
}

/*
 * Start the log streaming
 */
ALI_PG_DECODE_MESSAGE *
exec_logical_decoder(Decoder_handler *hander, volatile bool *time_to_stop)
{
	PGresult   *res = NULL;
	int 		r;
	int64		now;
	int 		hdr_len;
	bool		in_redo = false;
	bool		rc = false;

redo:

	if (hander->copybuf != NULL)
	{
		PQfreemem(hander->copybuf);
		hander->copybuf = NULL;
	}

    if (*time_to_stop == true)
    {
        return NULL;
    }

	/*
	 * Potentially send a status message to the master
	 */
	now = feGetCurrentTimestamp();
	if (in_redo && 
		hander->standby_message_timeout > 0 &&
		feTimestampDifferenceExceeds(hander->last_status, now,
									 hander->standby_message_timeout))
	{
		/* Time to send feedback! */
		if (!sendFeedback(hander, now, true, false))
			goto error;

		hander->last_status = now;
	}

	r = PQgetCopyData(hander->conn, &hander->copybuf, 1);
	if (r == 0)
	{
		/*
		 * In async mode, and no data available. We block on reading but
		 * not more than the specified timeout, so that we can send a
		 * response back to the client.
		 */
		fd_set		input_mask;
		int64		message_target = 0;
		struct timeval timeout;
		struct timeval *timeoutptr = NULL;

		FD_ZERO(&input_mask);
		FD_SET(PQsocket(hander->conn), &input_mask);

		/* Compute when we need to wakeup to send a keepalive message. */
		if (hander->standby_message_timeout)
			message_target = hander->last_status + (hander->standby_message_timeout - 1) *
				((int64) 1000);

		/* Now compute when to wakeup. */
		if (message_target > 0)
		{
			int64		targettime;
			long		secs;
			int			usecs;

			targettime = message_target;

			feTimestampDifference(now,
								  targettime,
								  &secs,
								  &usecs);
			if (secs <= 0)
				timeout.tv_sec = 1; /* Always sleep at least 1 sec */
			else
				timeout.tv_sec = secs;
			timeout.tv_usec = usecs;
			timeoutptr = &timeout;
		}

		r = select(PQsocket(hander->conn) + 1, &input_mask, NULL, NULL, timeoutptr);
		if (r == 0 || (r < 0 && errno == EINTR))
		{
			/*
			 * Got a timeout or signal. Continue the loop and either
			 * deliver a status packet to the server or just go back into
			 * blocking.
			 */
			in_redo = true;
			goto redo;
		}
		else if (r < 0)
		{
			fprintf(stderr, _("%s: select() failed: %s\n"),
					hander->progname, strerror(errno));
			goto error;
		}

		/* Else there is actually data on the socket */
		if (PQconsumeInput(hander->conn) == 0)
		{
			fprintf(stderr,
					_("%s: could not receive data from WAL stream: %s"),
					hander->progname, PQerrorMessage(hander->conn));
			goto error;
		}
		in_redo = true;
		goto redo;
	}

	/* End of copy stream */
	if (r == -1)
	{
		fprintf(stderr,
					_("%s: End of copy stream"),
					hander->progname);
		res = PQgetResult(hander->conn);
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			fprintf(stderr,
					_("%s: unexpected termination of replication stream: %s"),
					hander->progname, PQresultErrorMessage(res));
			goto error;
		}
		PQclear(res);
		return NULL;
	}
	else if (r == -2)
	{
		fprintf(stderr, _("%s: could not read COPY data: %s"),
				hander->progname, PQerrorMessage(hander->conn));
		goto error;
	}
	else
	{
		StringInfoData s;
		char c;
		
		initStringInfo(&s);
		s.data = hander->copybuf;
		s.len = r;
		s.maxlen = -1;

		c = pq_getmsgbyte(&s);
	
		/* Check the message type. */
		if (c == 'k')
		{
			int			pos;
			bool		replyRequested;
			XLogRecPtr	walEnd;

			/*
			 * Parse the keepalive message, enclosed in the CopyData message.
			 * We just check if the server requested a reply, and ignore the
			 * rest.
			 */
			pos = 1;			/* skip msgtype 'k' */
			walEnd = fe_recvint64(&hander->copybuf[pos]);
			hander->recvpos = Max(walEnd, hander->recvpos);

			pos += 8;			/* read walEnd */

			pos += 8;			/* skip sendTime */

			if (r < pos + 1)
			{
				fprintf(stderr, _("%s: streaming header too small: %d\n"),
						hander->progname, r);
				goto error;
			}
			replyRequested = hander->copybuf[pos];

			/* If the server requested an immediate reply, send one. */
			if (replyRequested)
			{
				now = feGetCurrentTimestamp();
				fprintf(stderr, _("server requested an immediate reply %s\n"), timestamptz_to_str(now));
				if (!sendFeedback(hander, now, true, false))
					goto error;
				hander->last_status = now;
			}

			goto redo;
		}
		else if (c == 'w')
		{
		
			XLogRecPtr	last_received = InvalidXLogRecPtr;
			XLogRecPtr	start_lsn;
			XLogRecPtr	end_lsn;

			/*
			 * Read the header of the XLogData message, enclosed in the CopyData
			 * message. We only need the WAL location field (dataStart), the rest
			 * of the header is ignored.
			 */
			hdr_len = 1;			/* msgtype 'w' */
			hdr_len += 8;			/* dataStart */
			hdr_len += 8;			/* walEnd */
			hdr_len += 8;			/* sendTime */
			if (r < hdr_len + 1)
			{
				fprintf(stderr, _("%s: streaming header too small: %d\n"),
						hander->progname, r);
				goto error;
			}
		
			start_lsn = pq_getmsgint64(&s);
			end_lsn = pq_getmsgint64(&s);
			pq_getmsgint64(&s);
		
			if (last_received < start_lsn)
				last_received = start_lsn;
		
			if (last_received < end_lsn)
				last_received = end_lsn;

			hander->recvpos = last_received;
			memset(&hander->msg, 0, sizeof(ALI_PG_DECODE_MESSAGE));
			rc = bdr_process_remote_action(&s, &hander->msg);
			if (rc == false)
			{
				goto error;
			}
		}
		else
		{
			fprintf(stderr, _("%s: unrecognized streaming header: \"%c\"\n"),
					hander->progname, hander->copybuf[0]);
			goto error;
		}
	}

	now = feGetCurrentTimestamp();
	if (hander->standby_message_timeout > 0 &&
		feTimestampDifferenceExceeds(hander->last_status, now,
									 hander->standby_message_timeout))
	{
		/* Time to send feedback! */
		if (!sendFeedback(hander, now, true, false))
			goto error;

		hander->last_status = now;
	}
	
	return &hander->msg;

error:

	if (hander->copybuf != NULL)
	{
		PQfreemem(hander->copybuf);
		hander->copybuf = NULL;
	}
	PQfinish(hander->conn);
	hander->conn = NULL;

	return NULL;
}

void
pg_sleep(long microsec)
{
	if (microsec > 0)
	{
#ifndef WIN32
		struct timeval delay;

		delay.tv_sec = microsec / 1000000L;
		delay.tv_usec = microsec % 1000000L;
		(void) select(0, NULL, NULL, NULL, &delay);
#else
		SleepEx((microsec < 500 ? 1 : (microsec + 500) / 1000), FALSE);
#endif
	}
}

/*
 * quote_literal_cstr -
 *	  returns a properly quoted literal
 */
static void
quote_literal_local(Decoder_handler *hander, const char *rawstr, char *type, PQExpBuffer buffer)
{
	char	   *result;
	int			len;
	int			newlen;
	StringInfo	s = hander->buffer;
	bool need_process = true;

	if (strcmp(type, "smallint") == 0)
		need_process = false;
	else if (strcmp(type, "integer") == 0)
		need_process = false;
	else if (strcmp(type, "bigint") == 0)
		need_process = false;
	else if (strcmp(type, "oid") == 0)
		need_process = false;
	else if (strcmp(type, "real") == 0)
		need_process = false;
	else if (strcmp(type, "double precision") == 0)
		need_process = false;
	else if (strcmp(type, "numeric") == 0)
		need_process = false;

	if (need_process == false)
	{
		appendPQExpBuffer(buffer, "%s", rawstr);
		return;
	}

	if (strcmp(type, "timestamp without time zone") == 0)
		need_process = false;
	else if (strcmp(type, "timestamp with time zone") == 0)
		need_process = false;
	else if (strcmp(type, "time without time zone") == 0)
		need_process = false;
	else if (strcmp(type, "time with time zone") == 0)
		need_process = false;
	else if (strcmp(type, "money") == 0)
		need_process = false;
	else if (strcmp(type, "date") == 0)
		need_process = false;
	else if (strcmp(type, "interval") == 0)
		need_process = false;

	if (need_process == false)
	{
		appendPQExpBuffer(buffer, "'");
		appendPQExpBuffer(buffer, "%s", rawstr);
		appendPQExpBuffer(buffer, "'");
		return;
	}

	len = strlen(rawstr);
	resetStringInfo(s);
	appendStringInfoSpaces(s, len * 2 + 3);

	result = s->data;

	newlen = quote_literal_internal(result, rawstr, len);
	result[newlen] = '\0';

	appendPQExpBuffer(buffer, "%s", result);

	return;
}

Decoder_handler *
init_hander(void)
{
	Decoder_handler *hander = NULL;

	hander = (Decoder_handler *)malloc(sizeof(Decoder_handler));
	memset(hander, 0, sizeof(Decoder_handler));
	hander->verbose = 0;
	hander->outfd = -1;
	
	hander->recvpos = InvalidXLogRecPtr;
	hander->flushpos= InvalidXLogRecPtr;
	hander->startpos= InvalidXLogRecPtr;

	hander->standby_message_timeout = 5 * 1000;
	hander->last_status = -1;
	hander->progname = (char *)"pg_recvlogical";

	hander->outfile = (char *)"-";
	hander->replication_slot = (char *)"rds_logical_sync_slot";
	hander->do_create_slot = false;
	hander->do_start_slot = true;
	hander->do_drop_slot = false;

	hander->buffer = (StringInfoData *)malloc(sizeof(StringInfoData));
	initStringInfo(hander->buffer);

	return hander;
}

static bool
is_key_column(ALI_PG_DECODE_MESSAGE *msg, char *colname)
{
	int j = 0;
	bool	is_key = false;

	if (colname == NULL)
	{
		return false;
	}

	for(j = 0; j < msg->k_natt; j++)
	{
		if (strcmp(colname, msg->k_attname[j]) == 0)
		{
			is_key = true;
			break;
		}
	}

	return is_key;
}

static bool
append_values(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple, int i)
{
	bool isnull = false;

	if (tuple->isnull[i] && tuple->changed[i] && tuple->svalues[i] == NULL)
	{
		appendPQExpBuffer(buffer, "%s", msg->attname[i]);
		appendPQExpBuffer(buffer, "=");
		appendPQExpBuffer(buffer, "null");	
		isnull = true;
	}
	else
	{
		appendPQExpBuffer(buffer, "%s", msg->attname[i]);
		appendPQExpBuffer(buffer, "=");
		quote_literal_local(hander, tuple->svalues[i], msg->atttype[i], buffer);
	}

	return isnull;
}

/*----------------------------------------------------------
 * Formatting and conversion routines.
 *---------------------------------------------------------*/

static XLogRecPtr
pg_lsn_in(char *lsn)
{
#define MAXPG_LSNCOMPONENT	8

	char	   *str = lsn;
	int			len1,
				len2;
	uint32		id,
				off;
	XLogRecPtr	result = 0;

	/* Sanity check input format. */
	len1 = strspn(str, "0123456789abcdefABCDEF");
	if (len1 < 1 || len1 > MAXPG_LSNCOMPONENT || str[len1] != '/')
	{
		fprintf(stderr, "invalid input syntax for type pg_lsn: \"%s\"", str);

		return result;
	}
	len2 = strspn(str + len1 + 1, "0123456789abcdefABCDEF");
	if (len2 < 1 || len2 > MAXPG_LSNCOMPONENT || str[len1 + 1 + len2] != '\0')
	{
		fprintf(stderr,"invalid input syntax for type pg_lsn: \"%s\"", str);

		return result;
	}

	/* Decode result. */
	id = (uint32) strtoul(str, NULL, 16);
	off = (uint32) strtoul(str + len1 + 1, NULL, 16);
	result = ((uint64) id << 32) | off;

	return result;
}

