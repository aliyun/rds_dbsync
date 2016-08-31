
#include "postgres_fe.h"
#include "lib/stringinfo.h"

#include <sys/stat.h>

#include "common/fe_memutils.h"
#include "libpq-fe.h"
#include "libpq/pqsignal.h"
#include "pqexpbuffer.h"
#include "libpq/pqformat.h"

#include "pg_logicaldecode.h"
#include "pgsync.h"
#include "ini.h"

int
main(int argc, char **argv)
{
	char	*src = NULL;
	char	*desc = NULL;
	char	*local = NULL;
	void	*cfg = NULL;

	cfg = init_config("my.cfg");
	if (cfg == NULL)
	{
		fprintf(stderr, "read config file error, insufficient permissions or my.cfg does not exist");
		return 1;
	}

	get_config(cfg, "src.pgsql", "connect_string", &src);
	get_config(cfg, "local.pgsql", "connect_string", &local);
	get_config(cfg, "desc.pgsql", "connect_string", &desc);

	if (src == NULL || desc == NULL || local == NULL)
	{
		fprintf(stderr, "parameter error, the necessary parameter is empty");
		return 1;
	}

	return db_sync_main(src, desc, local ,5);
}

