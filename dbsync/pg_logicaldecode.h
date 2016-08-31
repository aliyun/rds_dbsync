

#ifndef PG_LOGICALDECODE_H
#define PG_LOGICALDECODE_H

#include "postgres_fe.h"

#include "lib/stringinfo.h"
#include "lib/stringinfo.h"
#include "common/fe_memutils.h"

#include "libpq-fe.h"

#include "access/transam.h"
#include "libpq/pqformat.h"
#include "pqexpbuffer.h"
#include "misc.h"

#ifdef __cplusplus
extern		"C"
{
#endif

/*
 * Don't include libpq here, msvc infrastructure requires linking to libpq
 * otherwise.
 */
struct pg_conn;

#ifdef HAVE_INT64_TIMESTAMP
typedef int64 timestamp;
typedef int64 TimestampTz;
#else
typedef double timestamp;
typedef double TimestampTz;
#endif

#define MaxTupleAttributeNumber 1664	/* 8 * 208 */

#define		  MSGKIND_BEGIN			'B'		
#define		  MSGKIND_COMMIT		'C'		
#define		  MSGKIND_INSERT		'I'		
#define		  MSGKIND_UPDATE		'U'		
#define		  MSGKIND_DELETE		'D'	
#define		  MSGKIND_DDL			'L'	
#define		  MSGKIND_UNKNOWN		'K'	

typedef struct Decode_TupleData
{
	int			natt;
	bool		isnull[MaxTupleAttributeNumber];
	bool		changed[MaxTupleAttributeNumber];
	char		*svalues[MaxTupleAttributeNumber];
} Decode_TupleData;

typedef struct ALI_PG_DECODE_MESSAGE
{
	char			type;
	
	XLogRecPtr		lsn;
	TimestampTz		tm;
	
	TransactionId	xid;		/* begin */
	TimestampTz		end_lsn;	/* commit */

	char	*relname;
	char	*schemaname;

	int			natt;
	char		*attname[MaxTupleAttributeNumber];
	char		*atttype[MaxTupleAttributeNumber];
	
	int			k_natt;
	char		*k_attname[MaxTupleAttributeNumber];

	bool		has_key_or_old;
	Decode_TupleData newtuple;
	Decode_TupleData oldtuple;

} ALI_PG_DECODE_MESSAGE;

typedef struct Decoder_handler
{
	bool do_create_slot;
	bool do_start_slot;
	bool do_drop_slot;

	PGconn *conn;
	char	*connection_string;

	char	*progname;
	char	*replication_slot;
	
	int		outfd;
	char	*outfile;

	XLogRecPtr startpos;
		
	XLogRecPtr recvpos;
	XLogRecPtr flushpos;
	XLogRecPtr last_recvpos;

	ALI_PG_DECODE_MESSAGE msg;
	
	int verbose;

	char	   *copybuf;
	int			standby_message_timeout;
	int64		last_status;

	StringInfo buffer;
} Decoder_handler;


/* Error level codes */
#define DEBUG5		10			/* Debugging messages, in categories of
								 * decreasing detail. */
#define DEBUG4		11
#define DEBUG3		12
#define DEBUG2		13
#define DEBUG1		14			/* used by GUC debug_* variables */
#define LOG			15			/* Server operational messages; sent only to
								 * server log by default. */
#define COMMERROR	16			/* Client communication problems; same as LOG
								 * for server reporting, but never sent to
								 * client. */
#define INFO		17			/* Messages specifically requested by user (eg
								 * VACUUM VERBOSE output); always sent to
								 * client regardless of client_min_messages,
								 * but by default not sent to server log. */
#define NOTICE		18			/* Helpful messages to users about query
								 * operation; sent to client and server log by
								 * default. */
#define WARNING		19			/* Warnings.  NOTICE is for expected messages
								 * like implicit sequence creation by SERIAL.
								 * WARNING is for unexpected messages. */
#define ERROR		20			/* user error - abort transaction; return to
								 * known state */


#ifdef HAVE_FUNCNAME__FUNC
#define PG_FUNCNAME_MACRO	__func__
#else
#ifdef HAVE_FUNCNAME__FUNCTION
#define PG_FUNCNAME_MACRO	__FUNCTION__
#else
#define PG_FUNCNAME_MACRO	NULL
#endif
#endif

extern bool bdr_process_remote_action(StringInfo s, ALI_PG_DECODE_MESSAGE *msg);

/*
#define elog  \
	elog_start(__FILE__, __LINE__, PG_FUNCNAME_MACRO), \
	elog_finish

extern void elog_start(const char *filename, int lineno, const char *funcname);
extern void
elog_finish(int elevel, const char *fmt,...)
__attribute__((format(PG_PRINTF_ATTRIBUTE, 2, 3)));
*/


extern char *timestamptz_to_str(TimestampTz dt);
extern int64 timestamptz_to_time_t(TimestampTz t);
extern void out_put_tuple(ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer, Decode_TupleData *tuple);
extern int out_put_tuple_to_sql(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer);
extern void out_put_key_att(ALI_PG_DECODE_MESSAGE *msg, PQExpBuffer buffer);
extern void out_put_decode_message(Decoder_handler *hander, ALI_PG_DECODE_MESSAGE *msg, int outfd);
extern bool sendFeedback(Decoder_handler *hander, int64 now, bool force, bool replyRequested);
extern int initialize_connection(Decoder_handler *hander);
extern void disconnect(Decoder_handler *hander);
extern int check_handler_parameters(Decoder_handler *hander);
extern char *create_replication_slot(Decoder_handler *hander,XLogRecPtr *lsn, char *replication_slot);
extern int drop_replication_slot(Decoder_handler *hander);
extern int init_logfile(Decoder_handler *hander);
extern int init_streaming(Decoder_handler *hander);
extern ALI_PG_DECODE_MESSAGE *exec_logical_decoder(Decoder_handler *hander, volatile bool *time_to_stop);
extern void pg_sleep(long microsec);
extern Decoder_handler *init_hander(void);

#ifdef __cplusplus
}
#endif



#endif   /* PG_LOGICALDECODE_H */


