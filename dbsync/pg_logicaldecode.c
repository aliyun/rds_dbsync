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



static bool process_remote_begin(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);
static bool process_remote_commit(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);
static bool process_remote_insert(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);
static bool process_remote_update(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);
static bool process_remote_delete(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);
static bool read_tuple_parts(StringInfo s, Decode_TupleData *tup);
static bool process_read_colunm_info(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);



/*
 * Read a remote action type and process the action record.
 *
 * May set got_SIGTERM to stop processing before next record.
 */
bool
bdr_process_remote_action(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	char action = pq_getmsgbyte(s);
	bool	rc = false;

	switch (action)
	{
			/* BEGIN */
		case 'B':
			rc = process_remote_begin(s,msg);
			break;
			/* COMMIT */
		case 'C':
			rc = process_remote_commit(s,msg);
			break;
			/* INSERT */
		case 'I':
			rc = process_remote_insert(s,msg);
			break;
			/* UPDATE */
		case 'U':
			rc = process_remote_update(s,msg);
			break;
			/* DELETE */
		case 'D':
			rc = process_remote_delete(s,msg);
			break;
		default:
		{
			fprintf(stderr, "unknown action of type %c", action);
			return false;
		}
	}

	return true;
}

static bool
process_remote_begin(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	XLogRecPtr		origlsn;
	TimestampTz		committime;
	TransactionId	remote_xid;
	int				flags = 0;

	flags = pq_getmsgint(s, 4);

	origlsn = pq_getmsgint64(s);
	Assert(origlsn != InvalidXLogRecPtr);
	committime = pq_getmsgint64(s);
	remote_xid = pq_getmsgint(s, 4);

	msg->type = MSGKIND_BEGIN;
	msg->lsn = origlsn;
	msg->tm	= committime;
	msg->xid = remote_xid;

	return true;
}

/*
 * Process a commit message from the output plugin, advance replication
 * identifiers, commit the local transaction, and determine whether replay
 * should continue.
 *
 * Returns true if apply should continue with the next record, false if replay
 * should stop after this record.
 */
static bool
process_remote_commit(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	XLogRecPtr		commit_lsn;
	TimestampTz		committime;
	TimestampTz		end_lsn;
	int				flags;

	flags = pq_getmsgint(s, 4);

	if (flags != 0)
	{
		fprintf(stderr, "Commit flags are currently unused, but flags was set to %i", flags);
		return false;
	}

	/* order of access to fields after flags is important */
	commit_lsn = pq_getmsgint64(s);
	end_lsn = pq_getmsgint64(s);
	committime = pq_getmsgint64(s);

	msg->type = MSGKIND_COMMIT;
	msg->lsn = commit_lsn;
	msg->tm = committime;
	msg->end_lsn = end_lsn;

	return true;
}

static bool
process_remote_insert(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	char		action;
	int			relnamelen;
	int			nspnamelen;
	char	*schemaname;
	char	*relname;

	msg->type = MSGKIND_INSERT;

	nspnamelen = pq_getmsgint(s, 2);
	schemaname = (char *) pq_getmsgbytes(s, nspnamelen);

	relnamelen = pq_getmsgint(s, 2);
	relname = (char *) pq_getmsgbytes(s, relnamelen);

	msg->relname = relname;
	msg->schemaname = schemaname;

	action = pq_getmsgbyte(s);
	if (action != 'N' && action != 'C')
	{
		fprintf(stderr, "expected new tuple but got %d",
			 action);
		return false;
	}
	
	if (action == 'C')
	{
		process_read_colunm_info(s, msg);
		action = pq_getmsgbyte(s);
	}
	
	if (action != 'N')
	{
		fprintf(stderr, "expected new tuple but got %d",
			 action);
		return false;
	}

	return read_tuple_parts(s, &msg->newtuple);
}

static bool
process_remote_update(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	char		action;
	bool		pkey_sent;
	int			relnamelen;
	int			nspnamelen;
	char	*schemaname;
	char	*relname;
	
	msg->type = MSGKIND_UPDATE;

	nspnamelen = pq_getmsgint(s, 2);
	schemaname = (char *) pq_getmsgbytes(s, nspnamelen);

	relnamelen = pq_getmsgint(s, 2);
	relname = (char *) pq_getmsgbytes(s, relnamelen);

	msg->relname = relname;
	msg->schemaname = schemaname;

	action = pq_getmsgbyte(s);

	/* old key present, identifying key changed */
	if (action != 'K' && action != 'N' && action != 'C')
	{
		fprintf(stderr, "expected action 'N' or 'K', got %c",
			 action);
		return false;
	}

	if (action == 'C')
	{
		process_read_colunm_info(s, msg);
		action = pq_getmsgbyte(s);
	}
	
	if (action != 'K' && action != 'N')
	{
		fprintf(stderr, "expected action 'N' or 'K', got %c",
			 action);
		return false;
	}

	if (action == 'K')
	{
		pkey_sent = true;
		msg->has_key_or_old = true;
		read_tuple_parts(s, &msg->oldtuple);
		action = pq_getmsgbyte(s);
	}
	else
		pkey_sent = false;

	/* check for new  tuple */
	if (action != 'N')
	{
		fprintf(stderr, "expected action 'N', got %c",
			 action);
		return false;
	}

	/* read new tuple */
	return read_tuple_parts(s, &msg->newtuple);
}

static bool
process_read_colunm_info(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	int natt;
	int	i;
	char	action;
	
	natt = pq_getmsgint(s, 2);

	msg->natt = natt;
	for (i = 0; i < natt; i++)
	{
		char *tmp;
		int	len;

		len = pq_getmsgint(s, 2);
        if(len == 0)
        {
            continue;
        }

		tmp = (char *) pq_getmsgbytes(s, len);

		msg->attname[i] = tmp;

		{
			len = pq_getmsgint(s, 2);
			tmp = (char *) pq_getmsgbytes(s, len);
			msg->atttype[i] = tmp;
		}
	}

	action = pq_getmsgbyte(s);

	if (action != 'M' && action != 'P')
	{
		fprintf(stderr, "expected new tuple but got %d",
			 action);
		return false;
	}
	
	if (action == 'P')
	{
		return true;
	}

	natt = pq_getmsgint(s, 2);

	msg->k_natt = natt;
	for (i = 0; i < natt; i++)
	{
		char	*tmp;
		int		len;

		len = pq_getmsgint(s, 2);
		tmp = (char *) pq_getmsgbytes(s, len);

		msg->k_attname[i] = tmp;
	}

	return true;
}


static bool
process_remote_delete(StringInfo s, ALI_PG_DECODE_MESSAGE *msg)
{
	char		action;
	int			relnamelen;
	int			nspnamelen;
	char	*schemaname;
	char	*relname;

	msg->type = MSGKIND_DELETE; 

	nspnamelen = pq_getmsgint(s, 2);
	schemaname = (char *) pq_getmsgbytes(s, nspnamelen);

	relnamelen = pq_getmsgint(s, 2);
	relname = (char *) pq_getmsgbytes(s, relnamelen);

	msg->relname = relname;
	msg->schemaname = schemaname;

	action = pq_getmsgbyte(s);

	if (action != 'K' && action != 'E' && action != 'C')
	{
		fprintf(stderr, "expected action K or E got %c", action);
		return false;
	}

	if (action == 'C')
	{
		process_read_colunm_info(s, msg);
		action = pq_getmsgbyte(s);
	}
	
	if (action != 'K' && action != 'E')
	{
		fprintf(stderr, "expected action K or E got %c", action);
		return false;
	}

	if (action == 'E')
	{
		fprintf(stderr, "got delete without pkey\n");
		return true;
	}
	
	msg->has_key_or_old = true;
	return read_tuple_parts(s, &msg->oldtuple);

}

static bool
read_tuple_parts(StringInfo s, Decode_TupleData *tup)
{
	int			i;
	int			rnatts;
	char		action;

	action = pq_getmsgbyte(s);

	if (action != 'T')
	{
		fprintf(stderr, "expected TUPLE, got %c", action);
		return false;
	}

	memset(tup->isnull, 1, sizeof(tup->isnull));
	memset(tup->changed, 1, sizeof(tup->changed));

	rnatts = pq_getmsgint(s, 4);

	tup->natt = rnatts;
	
	/* FIXME: unaligned data accesses */
	for (i = 0; i < rnatts; i++)
	{
		char		kind = pq_getmsgbyte(s);
		//const char *data;
		int			len;

		switch (kind)
		{
			case 'n':
				tup->svalues[i] = NULL;
				break;
			case 'u':
				tup->isnull[i] = true;
				tup->changed[i] = false;
				tup->svalues[i] = NULL; 

				break;
/*
			case 'b':
				tup->isnull[i] = false;
				len = pq_getmsgint(s, 4);

				data = pq_getmsgbytes(s, len);

				tup->svalues[i] = palloc0(len + 1);
				memcpy(tup->svalues[i], data, len);
				
				break;
			case 's':
				{
					StringInfoData buf;

					tup->isnull[i] = false;
					len = pq_getmsgint(s, 4);

					initStringInfo(&buf);
					buf.data = (char *) pq_getmsgbytes(s, len);
					buf.len = len;
					
					tup->svalues[i] = palloc0(len + 1);
					memcpy(tup->svalues[i], buf.data, len);

					break;
				}
*/			
			case 't':
				{
					tup->isnull[i] = false;
					len = pq_getmsgint(s, 4);

					tup->svalues[i] = (char *) pq_getmsgbytes(s, len);
					//tup->svalues[i] = palloc0(len + 1);
					//memcpy(tup->svalues[i], data, len);
				}
				break;
			default:
			{
				fprintf(stderr, "unknown column type '%c'", kind);
				return false;
			}
		}

	}

	return true;
}

