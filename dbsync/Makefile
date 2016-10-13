#Change the following according to mysql installation path
# Minimal requriement: Mysql >= 5.6, Pgsql >= 9.3
mysql_install_dir=/u01/mysql
mysql_include_dir=$(mysql_install_dir)/include/
mysql_lib_dir=$(mysql_install_dir)/lib

pgsql_install_dir=/u01/pgsql

PGFILEDESC = "ali_recvlogical"
NAME = ali_recvlogical
MODULE_big = ali_recvlogical
MODULES = ali_recvlogical

OBJS = pg_logicaldecode.o pqformat.o stringinfo.o utils.o misc.o pgsync.o ini.o

PG_CPPFLAGS  = -DFRONTEND -I$(srcdir) -I$(libpq_srcdir) -I/u01/mysql/include/
PG_FLAGS  = -DFRONTEND -I$(srcdir) -I$(libpq_srcdir) -I/u01/mysql/include/

PG_CONFIG = $(pgsql_install_dir)/bin/pg_config
pgsql_lib_dir := $(shell $(PG_CONFIG) --libdir)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

RPATH_LDFLAGS='-Wl,-rpath,$$ORIGIN,-rpath,$(mysql_lib_dir),-rpath,$(pgsql_lib_dir)'
export RPATH_LDFLAGS

all: demo.o dbsync-pgsql2pgsql.o mysql2pgsql.o dbsync-mysql2pgsql.o readcfg.o
	$(CXX) $(CFLAGS) demo.o ali_recvlogical.so $(libpq_pgport) $(RPATH_LDFLAGS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o demo
	$(CXX) $(CFLAGS) readcfg.o ini.o dbsync-pgsql2pgsql.o ali_recvlogical.so $(libpq_pgport) $(RPATH_LDFLAGS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o pgsql2pgsql
	$(CXX) $(CFLAGS) readcfg.o ini.o mysql2pgsql.o dbsync-mysql2pgsql.o misc.o stringinfo.o $(libpq_pgport) $(RPATH_LDFLAGS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -L$(mysql_lib_dir) -lmysqlclient -o mysql2pgsql

clean:
	rm -rf *.o pgsql2pgsql mysql2pgsql demo
