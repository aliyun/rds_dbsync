/*-------------------------------------------------------------------------
 *
 * pg_recvlogical.c - receive data from a logical decoding slot in a streaming
 *					  fashion and write it to a local file.
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  src/bin/pg_basebackup/pg_recvlogical.c
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/stringinfo.h"

#include "access/xlog_internal.h"
#include "common/fe_memutils.h"
#include "getopt_long.h"
#include "libpq-fe.h"
#include "libpq/pqsignal.h"
#include "pqexpbuffer.h"
#include "libpq/pqformat.h"

#include "pg_logicaldecode.h"
#include "catalog/catversion.h"


/* Time to sleep between reconnection attempts */
#define RECONNECT_SLEEP_TIME 5

static int	standby_message_timeout = 10 * 1000;		/* 10 sec = default */

static volatile bool time_to_abort = false;

/*
 * Unfortunately we can't do sensible signal handling on windows...
 */
#ifndef WIN32

/*
 * When sigint is called, just tell the system to exit at the next possible
 * moment.
 */
static void
sigint_handler(int signum)
{
	time_to_abort = true;
}

#endif


int
main(int argc, char **argv)
{
	Decoder_handler *hander;
	int		rc = 0;
	bool	init = false;

	hander = init_hander();
	hander->connection_string = (char *)"host=192.168.1.1 port=3001 dbname=test user=test password=123456";
	XLogRecPtr lsn;

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
	init_streaming(hander);
	init = true;

#ifndef WIN32
		signal(SIGINT, sigint_handler);
#endif

	if (hander->do_drop_slot)
	{
		rc = drop_replication_slot(hander);
		return 0;
	}

	if (hander->do_create_slot)
	{
		if(create_replication_slot(hander, &lsn, (char *)"test") == NULL)
		{
			exit(1);
		}
	}

	if (!hander->do_start_slot)
	{
		disconnect(hander);
		exit(0);
	}

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
			out_put_decode_message(hander, msg, hander->outfd);
			hander->flushpos = hander->recvpos;
		}
		else
		{
			//printf("%s: disconnected; waiting %d seconds to try again\n",hander->progname, RECONNECT_SLEEP_TIME);
			pg_sleep(RECONNECT_SLEEP_TIME * 1000000);
			init = false;
		}
	}
}

