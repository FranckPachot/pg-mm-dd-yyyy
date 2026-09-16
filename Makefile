MODULE_big = mdydate
OBJS = src/mdydate.o

EXTENSION = mdydate
DATA = sql/mdydate--0.1.0.sql
PGFILEDESC = "mdydate - text-backed calendar type with educational indexes"

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)