/*-------------------------------------------------------------------------
 *
 * ali_decoding.c
 *		  ali logical decoding output plugin
 *
 * 
 *
 * IDENTIFICATION
 *		  contrib/ali_decoding/ali_decoding.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/sysattr.h"

#include "catalog/pg_class.h"
#include "catalog/pg_type.h"

#include "nodes/parsenodes.h"

#include "replication/output_plugin.h"
#include "replication/logical.h"

#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

#include "libpq/pqformat.h"
#include "access/tuptoaster.h"
#include "mb/pg_wchar.h"
#include "utils/guc.h"


PG_MODULE_MAGIC;

/* These must be available to pg_dlsym() */
extern void _PG_init(void);
extern void _PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct
{
	MemoryContext context;

	bool allow_binary_protocol;
	bool allow_sendrecv_protocol;
	bool int_datetime_mismatch;

	uint32 client_version;

	size_t client_sizeof_int;
	size_t client_sizeof_long;
	size_t client_sizeof_datum;
	size_t client_maxalign;
	bool client_bigendian;
	bool client_float4_byval;
	bool client_float8_byval;
	bool client_int_datetime;

	char *client_encoding;

	bool output_key_info;
	bool output_column_info;
	bool output_type_as_name;
} Ali_OutputData;

static void pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt,
				  bool is_init);
static void pg_decode_shutdown(LogicalDecodingContext *ctx);
static void pg_decode_begin_txn(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
static void write_rel(StringInfo out, Relation rel, Ali_OutputData *data, int action);
static void write_tuple(Ali_OutputData *data, StringInfo out, Relation rel,
			HeapTuple tuple);
static void write_colum_info(StringInfo out, Relation rel, Ali_OutputData *data, int action);
static void parse_notnull(DefElem *elem, const char *paramtype);
static void parse_uint32(DefElem *elem, uint32 *res);

void
_PG_init(void)
{
	/* other plugins can perform things here */
}

/* specify output plugin callbacks */
void
_PG_output_plugin_init(OutputPluginCallbacks *cb)
{
	AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);

	cb->startup_cb = pg_decode_startup;
	cb->begin_cb = pg_decode_begin_txn;
	cb->change_cb = pg_decode_change;
	cb->commit_cb = pg_decode_commit_txn;
	cb->shutdown_cb = pg_decode_shutdown;
}


/* initialize this plugin */
static void
pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt,
				  bool is_init)
{
	ListCell   *option;
	Ali_OutputData *data;

	data = palloc0(sizeof(Ali_OutputData));
	data->context = AllocSetContextCreate(ctx->context,
										  "ali decode conversion context",
										  ALLOCSET_DEFAULT_MINSIZE,
										  ALLOCSET_DEFAULT_INITSIZE,
										  ALLOCSET_DEFAULT_MAXSIZE);

	ctx->output_plugin_private = data;

	opt->output_type = OUTPUT_PLUGIN_BINARY_OUTPUT;

	foreach(option, ctx->output_plugin_options)
	{
		DefElem    *elem = lfirst(option);

		Assert(elem->arg == NULL || IsA(elem->arg, String));
		
		if (strcmp(elem->defname, "version") == 0)
			parse_uint32(elem, &data->client_version);
		else if (strcmp(elem->defname, "encoding") == 0)
			data->client_encoding = pstrdup(strVal(elem->arg));
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("option \"%s\" = \"%s\" is unknown",
							elem->defname,
							elem->arg ? strVal(elem->arg) : "(null)")));
		}
	}

	if (!is_init)
	{
		/* Set defaults values */
		if(data->client_encoding == NULL)
		{
			data->client_encoding = pstrdup(GetDatabaseEncodingName());
		}

		/* fix me */
		data->allow_binary_protocol = false;
		data->allow_sendrecv_protocol = false;
		data->int_datetime_mismatch = false;

		data->output_key_info = true;
		data->output_column_info = true;
		data->output_type_as_name = true;

		if (strcmp(data->client_encoding, GetDatabaseEncodingName()) != 0)
			elog(ERROR, "mismatching encodings are not yet supported");

		if (extra_float_digits < 3)
			(void) set_config_option("extra_float_digits", "3",
								 PGC_USERSET, PGC_S_SESSION,
								 GUC_ACTION_SAVE, true, 0, false);
	}

	return;
}

/* cleanup this plugin's resources */
static void
pg_decode_shutdown(LogicalDecodingContext *ctx)
{
	Ali_OutputData *data = ctx->output_plugin_private;

	if(data->client_encoding != NULL)
	{
		pfree(data->client_encoding);
		data->client_encoding = NULL;
	}

	/* cleanup our own resources via memory context reset */
	MemoryContextDelete(data->context);

	pfree(ctx->output_plugin_private);
	ctx->output_plugin_private = NULL;
}

/*
 * BEGIN callback
 */
static void
pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	int flags = 0;
	MemoryContext old;
	Ali_OutputData *data;

	data = ctx->output_plugin_private;
	old = MemoryContextSwitchTo(data->context);

	AssertVariableIsOfType(&pg_decode_begin_txn, LogicalDecodeBeginCB);

	OutputPluginPrepareWrite(ctx, true);
	pq_sendbyte(ctx->out, 'B');		/* BEGIN */

	/* send the flags field its self */
	pq_sendint(ctx->out, flags, 4);

	/* fixed fields */
	pq_sendint64(ctx->out, txn->final_lsn);
	pq_sendint64(ctx->out, txn->commit_time);
	pq_sendint(ctx->out, txn->xid, 4);

	OutputPluginWrite(ctx, true);
	MemoryContextSwitchTo(old);

	return;
}

/*
 * COMMIT callback
 *
 * Send the LSN at the time of the commit, the commit time, and the end LSN.
 *
 * The presence of additional records is controlled by a flag field, with
 * records that're present appearing strictly in the order they're listed
 * here. There is no sub-record header or other structure beyond the flags
 * field.
 *
 * If you change this, you'll need to change process_remote_commit(...)
 * too. Make sure to keep any flags in sync.
 */
static void
pg_decode_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	int flags = 0;
	MemoryContext old;
	Ali_OutputData *data;

	data = ctx->output_plugin_private;
	old = MemoryContextSwitchTo(data->context);

	OutputPluginPrepareWrite(ctx, true);
	pq_sendbyte(ctx->out, 'C');		/* sending COMMIT */

	/* send the flags field its self */
	pq_sendint(ctx->out, flags, 4);

	/* Send fixed fields */
	pq_sendint64(ctx->out, commit_lsn);
	pq_sendint64(ctx->out, txn->end_lsn);
	pq_sendint64(ctx->out, txn->commit_time);

	OutputPluginWrite(ctx, true);
	MemoryContextSwitchTo(old);

	return;
}

void
pg_decode_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	Ali_OutputData *data;
	MemoryContext old;

	data = ctx->output_plugin_private;

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	OutputPluginPrepareWrite(ctx, true);

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			pq_sendbyte(ctx->out, 'I');		/* action INSERT */
			write_rel(ctx->out, relation, data, change->action);
			pq_sendbyte(ctx->out, 'N');		/* new tuple follows */
			write_tuple(data, ctx->out, relation, &change->data.tp.newtuple->tuple);
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			pq_sendbyte(ctx->out, 'U');		/* action UPDATE */
			write_rel(ctx->out, relation, data, change->action);
			if (change->data.tp.oldtuple != NULL)
			{
				pq_sendbyte(ctx->out, 'K');	/* old key follows */
				write_tuple(data, ctx->out, relation,
							&change->data.tp.oldtuple->tuple);
			}
			pq_sendbyte(ctx->out, 'N');		/* new tuple follows */
			write_tuple(data, ctx->out, relation,
						&change->data.tp.newtuple->tuple);
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			pq_sendbyte(ctx->out, 'D');		/* action DELETE */
			write_rel(ctx->out, relation, data, change->action);
			if (change->data.tp.oldtuple != NULL)
			{
				pq_sendbyte(ctx->out, 'K');	/* old key follows */
				write_tuple(data, ctx->out, relation,
							&change->data.tp.oldtuple->tuple);
			}
			else
				pq_sendbyte(ctx->out, 'E');	/* empty */
			break;
		default:
			Assert(false);
	}
	OutputPluginWrite(ctx, true);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

}

/*
 * Write schema.relation to the output stream.
 */
static void
write_rel(StringInfo out, Relation rel, Ali_OutputData *data, int action)
{
	const char *nspname;
	int64		nspnamelen;
	const char *relname;
	int64		relnamelen;

	nspname = get_namespace_name(rel->rd_rel->relnamespace);
	if (nspname == NULL)
		elog(ERROR, "cache lookup failed for namespace %u",
			 rel->rd_rel->relnamespace);
	nspnamelen = strlen(nspname) + 1;

	relname = NameStr(rel->rd_rel->relname);
	relnamelen = strlen(relname) + 1;

	pq_sendint(out, nspnamelen, 2);		/* schema name length */
	appendBinaryStringInfo(out, nspname, nspnamelen);

	pq_sendint(out, relnamelen, 2);		/* table name length */
	appendBinaryStringInfo(out, relname, relnamelen);

	if (data->output_column_info == true)
	{
		write_colum_info(out, rel, data, action);
	}
}

/*
 * Write a tuple to the outputstream, in the most efficient format possible.
 */
static void
write_tuple(Ali_OutputData *data, StringInfo out, Relation rel,
			HeapTuple tuple)
{
	TupleDesc	desc;
	Datum		values[MaxTupleAttributeNumber];
	bool		isnull[MaxTupleAttributeNumber];
	int			i;

	desc = RelationGetDescr(rel);

	pq_sendbyte(out, 'T');			/* tuple follows */

	pq_sendint(out, desc->natts, 4);		/* number of attributes */

	/* try to allocate enough memory from the get go */
	enlargeStringInfo(out, tuple->t_len +
					  desc->natts * ( 1 + 4));

	/*
	 * XXX: should this prove to be a relevant bottleneck, it might be
	 * interesting to inline heap_deform_tuple() here, we don't actually need
	 * the information in the form we get from it.
	 */
	heap_deform_tuple(tuple, desc, values, isnull);

	for (i = 0; i < desc->natts; i++)
	{
		HeapTuple	typtup;
		Form_pg_type typclass;
		char   	   *outputstr = NULL;
		int			len = 0;

		Form_pg_attribute att = desc->attrs[i];

		if (isnull[i] || att->attisdropped)
		{
			pq_sendbyte(out, 'n');	/* null column */
			continue;
		}
		else if (att->attlen == -1 && VARATT_IS_EXTERNAL_ONDISK(values[i]))
		{
			pq_sendbyte(out, 'u');	/* unchanged toast column */
			continue;
		}

		typtup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(att->atttypid));
		if (!HeapTupleIsValid(typtup))
			elog(ERROR, "cache lookup failed for type %u", att->atttypid);
		typclass = (Form_pg_type) GETSTRUCT(typtup);

		pq_sendbyte(out, 't');	/* 'text' data follows */

		outputstr =
		    OidOutputFunctionCall(typclass->typoutput, values[i]);
		len = strlen(outputstr) + 1;
		pq_sendint(out, len, 4); /* length */
		appendBinaryStringInfo(out, outputstr, len); /* data */
		pfree(outputstr);

		ReleaseSysCache(typtup);
	}
}

static void
write_colum_info(StringInfo out, Relation rel, Ali_OutputData *data, int action)
{
	TupleDesc	desc;
	int			i;

	desc = RelationGetDescr(rel);

	pq_sendbyte(out, 'C');			/* tuple follows */

	pq_sendint(out, desc->natts, 2);		/* number of attributes */

	for (i = 0; i < desc->natts; i++)
	{
		int		attlen;
		const char	*attname = NULL;
		int		typelen;
		char	*typname = NULL;

		Form_pg_attribute att = desc->attrs[i];

		if (att->attisdropped)
		{
			pq_sendint(out, 0, 2);
			continue;
		}

		if (att->attnum < 0)
		{
			pq_sendint(out, 0, 2);
			continue;
		}

		attname = quote_identifier(NameStr(att->attname));
		attlen = strlen(attname) + 1;
		
		typname = format_type_be(att->atttypid);
		typelen = strlen(typname) + 1;

		pq_sendint(out, attlen, 2);
		appendBinaryStringInfo(out, attname, attlen);

		if (data->output_type_as_name)
		{
			pq_sendint(out, typelen, 2);
			appendBinaryStringInfo(out, typname, typelen);
		}

	}

	if ((action == REORDER_BUFFER_CHANGE_UPDATE ||
		action == REORDER_BUFFER_CHANGE_DELETE) &&
		data->output_key_info == true)
	{
		Oid idxoid;
		Relation idxrel;
		TupleDesc idx_desc;
		int		idxnatt;
		List	*latt = NULL;
		ListCell *cell = NULL;

		if (rel->rd_indexvalid == 0)
			RelationGetIndexList(rel);
		idxoid = rel->rd_replidindex;
		if (!OidIsValid(idxoid))
		{
			pq_sendbyte(out, 'P');
			return;
		}

		idxrel = RelationIdGetRelation(idxoid);
		idx_desc = RelationGetDescr(idxrel);
		for (idxnatt = 0; idxnatt < idx_desc->natts; idxnatt++)
		{
			int 		attno = idxrel->rd_index->indkey.values[idxnatt];
			char		*attname;
				
			if (attno < 0)
			{
				if (attno == ObjectIdAttributeNumber)
					continue;
				elog(ERROR, "system column in index");
			}

			attname = get_relid_attribute_name(RelationGetRelid(rel), attno);	
			latt = lappend(latt, makeString(attname));
		}

		RelationClose(idxrel);

		pq_sendbyte(out, 'M');
		idxnatt = list_length(latt);
		pq_sendint(out, idxnatt, 2);
		foreach(cell, latt)
		{
			char	*col = strVal(lfirst(cell));
			int		len = strlen(col) + 1;
			
			pq_sendint(out, len, 2);
			appendBinaryStringInfo(out, col, len);
		}
		
		list_free_deep(latt);
	}
	else
	{
		pq_sendbyte(out, 'P');
	}

	return;
}

static void
parse_uint32(DefElem *elem, uint32 *res)
{
    parse_notnull(elem, "uint32");
    errno = 0;
    *res = strtoul(strVal(elem->arg), NULL, 0);

    if (errno != 0)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("could not parse uint32 value \"%s\" for parameter \"%s\": %m",
                        strVal(elem->arg), elem->defname)));
}

static void 
parse_notnull(DefElem *elem, const char *paramtype)
{
    if (elem->arg == NULL || strVal(elem->arg) == NULL)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s parameter \"%s\" had no value",
                 paramtype, elem->defname)));
}
