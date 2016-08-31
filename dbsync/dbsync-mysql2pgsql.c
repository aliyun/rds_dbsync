
#include "postgres_fe.h"
#include "libpq-fe.h"

#include "pgsync.h"
#include "ini.h"
#include "mysql.h"
#include <unistd.h>

extern bool get_ddl_only; 

static int load_table_list_file(const char *filename, char*** p_tables, char*** p_queries);


int
main(int argc, char **argv)
{
	char	*desc = NULL;
	mysql_conn_info src = {0};
	int		num_thread = 5; //Default to 5
	void	*cfg = NULL;
	char	*sport = NULL;
	//char *tabname = NULL;
	int res_getopt = 0;
	char *table_list_file = NULL;
	char **tables = NULL, **queries = NULL;

	cfg = init_config("my.cfg");
	if (cfg == NULL)
	{
		fprintf(stderr, "read config file error, insufficient permissions or my.cfg does not exist");
		return 1;
	}

	memset(&src, 0, sizeof(mysql_conn_info));
	get_config(cfg, "src.mysql", "host", &src.host);
	get_config(cfg, "src.mysql", "port", &sport);
	get_config(cfg, "src.mysql", "user", &src.user);
	get_config(cfg, "src.mysql", "password", &src.passwd);
	get_config(cfg, "src.mysql", "db", &src.db);
	get_config(cfg, "src.mysql", "encodingdir", &src.encodingdir);
	get_config(cfg, "src.mysql", "encoding", &src.encoding);
	get_config(cfg, "desc.pgsql", "connect_string", &desc);

	if (src.host == NULL || sport == NULL ||
		src.user == NULL || src.passwd == NULL ||
		src.db == NULL || src.encodingdir == NULL ||
		src.encoding == NULL || desc == NULL)
	{
		fprintf(stderr, "parameter error, the necessary parameter is empty");
		return 1;
	}

	src.port = atoi(sport);

	while ((res_getopt = getopt(argc, argv, ":l:j:d")) != -1)
	{
		switch (res_getopt)
		{
			case 'l':
				table_list_file = optarg;
				break;
			case 'j':
				num_thread = atoi(optarg);
				break;
			case ':':
				fprintf(stderr, "No value specified for -%c", optopt);
				break;
			case 'd':
				get_ddl_only = true;
				break;
			case '?':
				fprintf(stderr, "Unsupported option: %c", optopt);	
				break;
			default:
				fprintf(stderr, "Parameter parsing error: %c", res_getopt);
				
		}
	}
	
	if(table_list_file!= NULL)
	{
		if (load_table_list_file(table_list_file, &tables, &queries))
		{
			fprintf(stderr, "Error occurs while loading table list file %s \n", table_list_file);
			return -1;
		}

		src.tabnames = (char **) tables;
		src.queries = (char**) queries;
	}

	/* Only one thread is needed when just generating DDL */
	if (get_ddl_only)
		num_thread = 1;

	return mysql2pgsql_sync_main(desc , num_thread, &src);
}


int load_table_list_file(const char *filename, char*** p_tables, char*** p_queries) {
	FILE *fp = NULL;
	int n, sz, num_lines = 0;
	char *table_list = NULL;
	char **table_array = NULL;
	char **query_array = NULL;
	char *p = NULL;
	char *tail = NULL;
	char *table_begin = NULL;
	char *table_end = NULL;
	char *query_begin = NULL;
	int cur_table = 0;

	/* Open file */
	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Error opening file %s", filename);
		goto fail;
	}

	/* Get file size */
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	if(sz < 0) {
		fprintf(stderr, "Error ftell file %s", filename);
		goto fail;
	}
	rewind(fp);

	/* Load file content into memory, null terminate, init end var */
	table_list = (char*) palloc0(sz + 1);
	if (!table_list)
	{
		fprintf(stderr, "Error malloc mem for file %s", filename);
		goto fail;
	}
	
	table_list[sz] = '\0';
	p = table_list;
	tail = table_list + sz;
	n = fread(table_list, 1, sz, fp);
	if (n != sz) {
		fprintf(stderr, "Error reading file %s", filename);
		goto fail;
	}

	/* Count lines of the file */
	while(p < tail)
	{
		switch (*p)
		{
			case '\0':
				fprintf(stderr, "Unexpected terminator encountered in file %s \n", filename);
				goto fail;
			case '\n':
				num_lines++;
				break;

			default:
				break;		
		}
		p++;
	}

	/* Add the last line */
	num_lines++;
	
	/* Get memory for table array, with the last element being NULL */
	table_array = (char **) palloc0((num_lines + 1) * sizeof(char*));
	query_array = (char **) palloc0((num_lines + 1) * sizeof(char*));

	/* Parse data */
	p = table_list;
	table_begin = table_list;
	cur_table = 0;
	while(p <= tail)
	{
		if (*p == '\n' || p == tail)
		{
			/* Get the table name without leanding and trailing blanks
			  * E.g. following line will generate a table name "tab 1"
			  *     |    tab 1   :   select * from tab | 
			  */
			while (*table_begin == ' ' || *table_begin == '\t')
				table_begin++;
			
			table_end = table_begin;
			while (*table_end != ':' && table_end != p)
				table_end++;

			query_begin = table_end + 1;
			table_end--;
			while  (table_end >= table_begin &&
				(*table_end == ' ' || *table_end == '\t'))
				table_end--;

			if (table_end < table_begin)
				table_begin = NULL;
			else
				*(table_end+1) = '\0';

			while ((*query_begin == ' ' || *query_begin == '\t') && query_begin < p)
				query_begin++;

			if (query_begin >= p)
				query_begin = NULL;
			else
				*p = '\0';

			if (table_begin)
			{
				table_array[cur_table] = table_begin;
				query_array[cur_table] = query_begin;
				cur_table++;
				fprintf(stderr, "Adding table: %s\n", table_begin);
			}
			
			table_begin = p + 1;
		}
		
		p++;
	}

	/* Clean up and return */
	fclose(fp);
	*p_tables = table_array;
	*p_queries = query_array;
	return 0;

fail:
	if (fp) 
		fclose(fp);
	if (table_list)
		free(table_list);
	if (table_array)
		free(table_array);
	if (query_array)
		free(query_array);
	
	return -1;
}

