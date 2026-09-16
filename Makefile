MODULE_big = mmddyyyy
OBJS = src/mmddyyyy.o

EXTENSION = mmddyyyy
DATA = sql/mmddyyyy--0.1.0.sql
PGFILEDESC = "mmddyyyy - text-backed calendar type with educational indexes"

# Correctness is the priority for this teaching extension, so compile loudly.
# -Wall -Wextra surface the exact class of bugs (uninitialised reads,
# signed/unsigned mixups in the month/day/year arithmetic) that would silently
# corrupt an index, and -Werror refuses to install anything that trips them.
#
# -Wextra also enables -Wunused-parameter, which is unavoidable noise here: the
# fixed PG_FUNCTION_ARGS calling convention passes fcinfo that many functions
# ignore, and PostgreSQL's own headers trip it too. We disable just that one
# check and keep everything else strict.
PG_CFLAGS += -Wall -Wextra -Wno-unused-parameter -Werror

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)