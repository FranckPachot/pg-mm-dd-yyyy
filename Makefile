MODULE_big = mmddyyyy
OBJS = src/mmddyyyy.o

EXTENSION = mmddyyyy
DATA = sql/mmddyyyy--0.1.0.sql
PGFILEDESC = "mmddyyyy - text-backed calendar type with educational indexes"

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)