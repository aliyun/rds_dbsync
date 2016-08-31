PGFILEDESC = "ali_recvlogical"

NAME = ali_recvlogical

MODULE_big = ali_recvlogical
MODULES = ali_recvlogical

#EXTENSION = ali_recvlogical
OBJS = pg_logicaldecode.o pqformat.o stringinfo.o utils.o misc.o pgsync.o ini.o

PG_CPPFLAGS  = -DFRONTEND -I$(srcdir) -I$(libpq_srcdir) -I/u01/mysql/include/
PG_FLAGS  = -DFRONTEND -I$(srcdir) -I$(libpq_srcdir) -I/u01/mysql/include/

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)


all: all-lib demo.o dbsync-pgsql2pgsql.o mysql2pgsql.o dbsync-mysql2pgsql.o readcfg.o
	$(CXX) $(CFLAGS) demo.o ali_recvlogical.so $(libpq_pgport) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o demo
	$(CXX) $(CFLAGS) readcfg.o ini.o dbsync-pgsql2pgsql.o ali_recvlogical.so $(libpq_pgport) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o pgsql2pgsql
	$(CXX) $(CFLAGS) readcfg.o ini.o mysql2pgsql.o dbsync-mysql2pgsql.o misc.o stringinfo.o $(libpq_pgport) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -L/u01/mysql/lib -lmysqlclient -o mysql2pgsql


clean:
	rm -rf *.o pgsql2pgsql mysql2pgsql demo
