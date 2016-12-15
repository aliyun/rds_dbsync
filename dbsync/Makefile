#Change the following according to mysql installation path
# Minimal requriement: Mysql >= 5.6, Pgsql >= 9.3
mysql_install_dir=/usr
mysql_include_dir=$(mysql_install_dir)/include/mysql
mysql_lib_dir=$(mysql_install_dir)/lib64/mysql

pgsql_install_dir=/usr/pgsql-9.6

PGFILEDESC = "ali_recvlogical"
NAME = ali_recvlogical
MODULE_big = ali_recvlogical
MODULES = ali_recvlogical

OBJS = pg_logicaldecode.o pqformat.o stringinfo.o utils.o misc.o pgsync.o ini.o

PG_CPPFLAGS  = -DFRONTEND -I$(srcdir) -I$(libpq_srcdir) -I$(mysql_include_dir)
PG_FLAGS  = -DFRONTEND -I$(srcdir) -I$(libpq_srcdir) -I$(mysql_include_dir) 

PG_CONFIG = $(pgsql_install_dir)/bin/pg_config
pgsql_lib_dir := $(shell $(PG_CONFIG) --libdir)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

RPATH_LDFLAGS='-Wl,-rpath,$$ORIGIN,-rpath,$$ORIGIN/lib,-rpath,$$ORIGIN/../lib,-rpath,$(mysql_lib_dir),-rpath,$(pgsql_lib_dir)'
export RPATH_LDFLAGS

LIBS = -lpthread

all: demo.o dbsync-pgsql2pgsql.o mysql2pgsql.o dbsync-mysql2pgsql.o readcfg.o
	$(CXX) $(CFLAGS) demo.o $(OBJS) $(libpq_pgport) $(RPATH_LDFLAGS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o demo 
	$(CXX) $(CFLAGS) readcfg.o dbsync-pgsql2pgsql.o $(OBJS) $(libpq_pgport) $(RPATH_LDFLAGS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o pgsql2pgsql
	$(CXX) $(CFLAGS) readcfg.o ini.o mysql2pgsql.o dbsync-mysql2pgsql.o misc.o stringinfo.o $(libpq_pgport) $(RPATH_LDFLAGS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -L$(mysql_lib_dir) -lmysqlclient -o mysql2pgsql

clean:
	rm -rf *.o pgsql2pgsql mysql2pgsql demo ali_recvlogical.so

package:
	mkdir -p install
	mkdir -p install/bin
	mkdir -p install/lib
	cp -fr pgsql2pgsql install/bin
	cp -fr demo install/bin
	cp -fr ali_recvlogical.so install/lib
	cp -fr mysql2pgsql install/bin
	cp -fr $(mysql_install_dir)/lib64/mysql/libmysqlclient.so* install/lib
	cp -fr $(pgsql_lib_dir)/libpq.so* install/lib
