# contrib/ali_decoding/Makefile
MODULE_big = ali_decoding
MODULES = ali_decoding
OBJS    = ali_decoding.o 

DATA = ali_decoding--0.0.1.sql ali_decoding--unpackaged--0.0.1.sql

EXTENSION = ali_decoding

NAME = ali_decoding

#subdir = contrib/ali_decoding
#top_builddir = ../..
#include $(top_builddir)/src/Makefile.global
#include $(top_srcdir)/contrib/contrib-global.mk

PG_CONFIG = pg_config
pgsql_lib_dir := $(shell $(PG_CONFIG) --libdir)
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

