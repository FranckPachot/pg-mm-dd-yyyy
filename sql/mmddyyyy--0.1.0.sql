CREATE TYPE mmddyyyy;

CREATE FUNCTION mmddyyyy_in(cstring)
RETURNS mmddyyyy
AS 'MODULE_PATHNAME', 'mmddyyyy_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_out(mmddyyyy)
RETURNS cstring
AS 'MODULE_PATHNAME', 'mmddyyyy_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE mmddyyyy (
    INPUT = mmddyyyy_in,
    OUTPUT = mmddyyyy_out,
    INTERNALLENGTH = 10,
    ALIGNMENT = char,
    STORAGE = plain
);

COMMENT ON TYPE mmddyyyy IS
'A calendar date stored literally as the 10 ASCII bytes MM/DD/YYYY.';

CREATE FUNCTION mmddyyyy_to_date(mmddyyyy)
RETURNS date
AS 'MODULE_PATHNAME', 'mmddyyyy_to_date'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_from_date(date)
RETURNS mmddyyyy
AS 'MODULE_PATHNAME', 'mmddyyyy_from_date'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (mmddyyyy AS date)
WITH FUNCTION mmddyyyy_to_date(mmddyyyy)
AS ASSIGNMENT;

CREATE CAST (date AS mmddyyyy)
WITH FUNCTION mmddyyyy_from_date(date)
AS ASSIGNMENT;

CREATE FUNCTION mmddyyyy_month(mmddyyyy)
RETURNS integer
AS 'MODULE_PATHNAME', 'mmddyyyy_month'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_day(mmddyyyy)
RETURNS integer
AS 'MODULE_PATHNAME', 'mmddyyyy_day'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_year(mmddyyyy)
RETURNS integer
AS 'MODULE_PATHNAME', 'mmddyyyy_year'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_cmp(mmddyyyy, mmddyyyy)
RETURNS integer
AS 'MODULE_PATHNAME', 'mmddyyyy_cmp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_eq(mmddyyyy, mmddyyyy)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_eq'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_ne(mmddyyyy, mmddyyyy)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_ne'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_lt(mmddyyyy, mmddyyyy)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_lt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_le(mmddyyyy, mmddyyyy)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_le'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_ge(mmddyyyy, mmddyyyy)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_ge'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gt(mmddyyyy, mmddyyyy)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_gt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR = (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy,
    FUNCTION = mmddyyyy_eq,
    COMMUTATOR = =,
    NEGATOR = <>,
    RESTRICT = eqsel,
    JOIN = eqjoinsel,
    MERGES
);

CREATE OPERATOR <> (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy,
    FUNCTION = mmddyyyy_ne,
    COMMUTATOR = <>,
    NEGATOR = =,
    RESTRICT = neqsel,
    JOIN = neqjoinsel
);

CREATE OPERATOR < (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy,
    FUNCTION = mmddyyyy_lt,
    COMMUTATOR = >,
    NEGATOR = >=,
    RESTRICT = scalarltsel,
    JOIN = scalarltjoinsel
);

CREATE OPERATOR <= (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy,
    FUNCTION = mmddyyyy_le,
    COMMUTATOR = >=,
    NEGATOR = >,
    RESTRICT = scalarlesel,
    JOIN = scalarlejoinsel
);

CREATE OPERATOR >= (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy,
    FUNCTION = mmddyyyy_ge,
    COMMUTATOR = <=,
    NEGATOR = <,
    RESTRICT = scalargesel,
    JOIN = scalargejoinsel
);

CREATE OPERATOR > (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy,
    FUNCTION = mmddyyyy_gt,
    COMMUTATOR = <,
    NEGATOR = <=,
    RESTRICT = scalargtsel,
    JOIN = scalargtjoinsel
);

CREATE OPERATOR CLASS mmddyyyy_btree_ops
DEFAULT FOR TYPE mmddyyyy USING btree AS
    OPERATOR 1 <,
    OPERATOR 2 <=,
    OPERATOR 3 =,
    OPERATOR 4 >=,
    OPERATOR 5 >,
    FUNCTION 1 mmddyyyy_cmp(mmddyyyy, mmddyyyy);

CREATE TYPE mmddyyyy_pattern;

CREATE FUNCTION mmddyyyy_pattern_in(cstring)
RETURNS mmddyyyy_pattern
AS 'MODULE_PATHNAME', 'mmddyyyy_pattern_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_pattern_out(mmddyyyy_pattern)
RETURNS cstring
AS 'MODULE_PATHNAME', 'mmddyyyy_pattern_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE mmddyyyy_pattern (
    INPUT = mmddyyyy_pattern_in,
    OUTPUT = mmddyyyy_pattern_out,
    INTERNALLENGTH = 6,
    ALIGNMENT = int2,
    STORAGE = plain
);

COMMENT ON TYPE mmddyyyy_pattern IS
'An MM/DD/YYYY query pattern in which any component may be *.';

CREATE FUNCTION mmddyyyy_pattern_from_mmddyyyy(mmddyyyy)
RETURNS mmddyyyy_pattern
AS 'MODULE_PATHNAME', 'mmddyyyy_pattern_from_mmddyyyy'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (mmddyyyy AS mmddyyyy_pattern)
WITH FUNCTION mmddyyyy_pattern_from_mmddyyyy(mmddyyyy)
AS IMPLICIT;

CREATE FUNCTION mmddyyyy_matches(mmddyyyy, mmddyyyy_pattern)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_matches'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR <@ (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy_pattern,
    FUNCTION = mmddyyyy_matches
);

COMMENT ON OPERATOR <@ (mmddyyyy, mmddyyyy_pattern) IS
'True when the date matches every specified component of the pattern.';

CREATE FUNCTION mmddyyyy_distance(mmddyyyy, mmddyyyy_pattern)
RETURNS double precision
AS 'MODULE_PATHNAME', 'mmddyyyy_distance'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR <-> (
    LEFTARG = mmddyyyy,
    RIGHTARG = mmddyyyy_pattern,
    FUNCTION = mmddyyyy_distance
);

COMMENT ON OPERATOR <-> (mmddyyyy, mmddyyyy_pattern) IS
'Calendar similarity distance: cyclic month first, then day, then year.';

CREATE TYPE mmddyyyy_gkey;

CREATE FUNCTION mmddyyyy_gkey_in(cstring)
RETURNS mmddyyyy_gkey
AS 'MODULE_PATHNAME', 'mmddyyyy_gkey_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gkey_out(mmddyyyy_gkey)
RETURNS cstring
AS 'MODULE_PATHNAME', 'mmddyyyy_gkey_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE mmddyyyy_gkey (
    INPUT = mmddyyyy_gkey_in,
    OUTPUT = mmddyyyy_gkey_out,
    INTERNALLENGTH = 8,
    ALIGNMENT = int2,
    STORAGE = plain
);

COMMENT ON TYPE mmddyyyy_gkey IS
'Internal GiST bounding box: month range, day range, and year range.';

CREATE FUNCTION mmddyyyy_gist_consistent(internal, mmddyyyy, smallint, oid, internal)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_consistent'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_union(internal, internal)
RETURNS mmddyyyy_gkey
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_union'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_compress(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_compress'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_penalty(internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_penalty'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_picksplit(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_picksplit'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_same(mmddyyyy_gkey, mmddyyyy_gkey, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_same'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_distance(internal, mmddyyyy, smallint, oid, internal)
RETURNS double precision
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_distance'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mmddyyyy_gist_fetch(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mmddyyyy_gist_fetch'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR CLASS mmddyyyy_gist_ops
DEFAULT FOR TYPE mmddyyyy USING gist AS
    STORAGE mmddyyyy_gkey,
    OPERATOR 1 <@ (mmddyyyy, mmddyyyy_pattern),
    OPERATOR 15 <-> (mmddyyyy, mmddyyyy_pattern) FOR ORDER BY pg_catalog.float_ops,
    FUNCTION 1 mmddyyyy_gist_consistent(internal, mmddyyyy, smallint, oid, internal),
    FUNCTION 2 mmddyyyy_gist_union(internal, internal),
    FUNCTION 3 mmddyyyy_gist_compress(internal),
    FUNCTION 5 mmddyyyy_gist_penalty(internal, internal, internal),
    FUNCTION 6 mmddyyyy_gist_picksplit(internal, internal),
    FUNCTION 7 mmddyyyy_gist_same(mmddyyyy_gkey, mmddyyyy_gkey, internal),
    FUNCTION 8 mmddyyyy_gist_distance(internal, mmddyyyy, smallint, oid, internal),
    FUNCTION 9 mmddyyyy_gist_fetch(internal);