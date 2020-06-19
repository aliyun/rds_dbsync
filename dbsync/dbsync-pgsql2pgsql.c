
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
#include <unistd.h>

int
main(int argc, char **argv)
{
	char	*src = NULL;
	char	*desc = NULL;
	char	*local = NULL;
	void	*cfg = NULL;

	int		num_thread = 5;
	int 	res_getopt = 0;
	char 	*ini_file = "my.cfg";

	while ((res_getopt = getopt(argc, argv, "l:j:h")) != -1)
	{
		switch (res_getopt)
		{
			case 'l':
				ini_file = optarg;
				break;
			case 'j':
				fprintf(stderr, "ini_file=%s, optarg=%s\n", ini_file, optarg);
				num_thread = atoi(optarg);
				if(num_thread == 0) 
				{
					num_thread = 5;
				}
				break;
			case ':':
				fprintf(stderr, "No value specified for -%c\n", optopt);
				break;
			case 'h':
				fprintf(stderr, "Usage: -l <ini file> -j <thread number> -h\n");
				fprintf(stderr, "\n -l specifies a file like my.cfg;\n -j specifies number of threads to do the job;\n -h display this usage manual\n");
				return 0;
			case '?':
				fprintf(stderr, "Unsupported option: %c", optopt);
				break;
			default:
				fprintf(stderr, "Parameter parsing error: %c", res_getopt);
				return -1;
		}
	}

	cfg = init_config(ini_file);
	if (cfg == NULL)
	{
		fprintf(stderr, "read config file error, insufficient permissions or %s does not exist\n", ini_file);
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

	return db_sync_main(src, desc, local ,num_thread);
}

