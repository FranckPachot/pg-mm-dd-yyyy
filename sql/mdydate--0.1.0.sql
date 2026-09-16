CREATE TYPE mdydate;

CREATE FUNCTION mdydate_in(cstring)
RETURNS mdydate
AS 'MODULE_PATHNAME', 'mdydate_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_out(mdydate)
RETURNS cstring
AS 'MODULE_PATHNAME', 'mdydate_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE mdydate (
    INPUT = mdydate_in,
    OUTPUT = mdydate_out,
    INTERNALLENGTH = 10,
    ALIGNMENT = char,
    STORAGE = plain
);

COMMENT ON TYPE mdydate IS
'A calendar date stored literally as the 10 ASCII bytes MM/DD/YYYY.';

CREATE FUNCTION mdydate_to_date(mdydate)
RETURNS date
AS 'MODULE_PATHNAME', 'mdydate_to_date'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_from_date(date)
RETURNS mdydate
AS 'MODULE_PATHNAME', 'mdydate_from_date'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (mdydate AS date)
WITH FUNCTION mdydate_to_date(mdydate)
AS ASSIGNMENT;

CREATE CAST (date AS mdydate)
WITH FUNCTION mdydate_from_date(date)
AS ASSIGNMENT;

CREATE FUNCTION mdydate_month(mdydate)
RETURNS integer
AS 'MODULE_PATHNAME', 'mdydate_month'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_day(mdydate)
RETURNS integer
AS 'MODULE_PATHNAME', 'mdydate_day'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_year(mdydate)
RETURNS integer
AS 'MODULE_PATHNAME', 'mdydate_year'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_cmp(mdydate, mdydate)
RETURNS integer
AS 'MODULE_PATHNAME', 'mdydate_cmp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_eq(mdydate, mdydate)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_eq'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_ne(mdydate, mdydate)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_ne'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_lt(mdydate, mdydate)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_lt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_le(mdydate, mdydate)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_le'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_ge(mdydate, mdydate)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_ge'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gt(mdydate, mdydate)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_gt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR = (
    LEFTARG = mdydate,
    RIGHTARG = mdydate,
    FUNCTION = mdydate_eq,
    COMMUTATOR = =,
    NEGATOR = <>,
    RESTRICT = eqsel,
    JOIN = eqjoinsel,
    MERGES
);

CREATE OPERATOR <> (
    LEFTARG = mdydate,
    RIGHTARG = mdydate,
    FUNCTION = mdydate_ne,
    COMMUTATOR = <>,
    NEGATOR = =,
    RESTRICT = neqsel,
    JOIN = neqjoinsel
);

CREATE OPERATOR < (
    LEFTARG = mdydate,
    RIGHTARG = mdydate,
    FUNCTION = mdydate_lt,
    COMMUTATOR = >,
    NEGATOR = >=,
    RESTRICT = scalarltsel,
    JOIN = scalarltjoinsel
);

CREATE OPERATOR <= (
    LEFTARG = mdydate,
    RIGHTARG = mdydate,
    FUNCTION = mdydate_le,
    COMMUTATOR = >=,
    NEGATOR = >,
    RESTRICT = scalarlesel,
    JOIN = scalarlejoinsel
);

CREATE OPERATOR >= (
    LEFTARG = mdydate,
    RIGHTARG = mdydate,
    FUNCTION = mdydate_ge,
    COMMUTATOR = <=,
    NEGATOR = <,
    RESTRICT = scalargesel,
    JOIN = scalargejoinsel
);

CREATE OPERATOR > (
    LEFTARG = mdydate,
    RIGHTARG = mdydate,
    FUNCTION = mdydate_gt,
    COMMUTATOR = <,
    NEGATOR = <=,
    RESTRICT = scalargtsel,
    JOIN = scalargtjoinsel
);

CREATE OPERATOR CLASS mdydate_btree_ops
DEFAULT FOR TYPE mdydate USING btree AS
    OPERATOR 1 <,
    OPERATOR 2 <=,
    OPERATOR 3 =,
    OPERATOR 4 >=,
    OPERATOR 5 >,
    FUNCTION 1 mdydate_cmp(mdydate, mdydate);

CREATE TYPE mdypattern;

CREATE FUNCTION mdypattern_in(cstring)
RETURNS mdypattern
AS 'MODULE_PATHNAME', 'mdypattern_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdypattern_out(mdypattern)
RETURNS cstring
AS 'MODULE_PATHNAME', 'mdypattern_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE mdypattern (
    INPUT = mdypattern_in,
    OUTPUT = mdypattern_out,
    INTERNALLENGTH = 6,
    ALIGNMENT = int2,
    STORAGE = plain
);

COMMENT ON TYPE mdypattern IS
'An MM/DD/YYYY query pattern in which any component may be *.';

CREATE FUNCTION mdypattern_from_mdydate(mdydate)
RETURNS mdypattern
AS 'MODULE_PATHNAME', 'mdypattern_from_mdydate'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (mdydate AS mdypattern)
WITH FUNCTION mdypattern_from_mdydate(mdydate)
AS IMPLICIT;

CREATE FUNCTION mdydate_matches(mdydate, mdypattern)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_matches'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR <@ (
    LEFTARG = mdydate,
    RIGHTARG = mdypattern,
    FUNCTION = mdydate_matches
);

COMMENT ON OPERATOR <@ (mdydate, mdypattern) IS
'True when the date matches every specified component of the pattern.';

CREATE FUNCTION mdydate_distance(mdydate, mdypattern)
RETURNS double precision
AS 'MODULE_PATHNAME', 'mdydate_distance'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR <-> (
    LEFTARG = mdydate,
    RIGHTARG = mdypattern,
    FUNCTION = mdydate_distance
);

COMMENT ON OPERATOR <-> (mdydate, mdypattern) IS
'Calendar similarity distance: cyclic month first, then day, then year.';

CREATE TYPE mdydate_gkey;

CREATE FUNCTION mdydate_gkey_in(cstring)
RETURNS mdydate_gkey
AS 'MODULE_PATHNAME', 'mdydate_gkey_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gkey_out(mdydate_gkey)
RETURNS cstring
AS 'MODULE_PATHNAME', 'mdydate_gkey_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE mdydate_gkey (
    INPUT = mdydate_gkey_in,
    OUTPUT = mdydate_gkey_out,
    INTERNALLENGTH = 8,
    ALIGNMENT = int2,
    STORAGE = plain
);

COMMENT ON TYPE mdydate_gkey IS
'Internal GiST bounding box: month range, day range, and year range.';

CREATE FUNCTION mdydate_gist_consistent(internal, mdydate, smallint, oid, internal)
RETURNS boolean
AS 'MODULE_PATHNAME', 'mdydate_gist_consistent'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_union(internal, internal)
RETURNS mdydate_gkey
AS 'MODULE_PATHNAME', 'mdydate_gist_union'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_compress(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mdydate_gist_compress'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_penalty(internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mdydate_gist_penalty'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_picksplit(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mdydate_gist_picksplit'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_same(mdydate_gkey, mdydate_gkey, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mdydate_gist_same'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_distance(internal, mdydate, smallint, oid, internal)
RETURNS double precision
AS 'MODULE_PATHNAME', 'mdydate_gist_distance'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION mdydate_gist_fetch(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'mdydate_gist_fetch'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR CLASS mdydate_gist_ops
DEFAULT FOR TYPE mdydate USING gist AS
    STORAGE mdydate_gkey,
    OPERATOR 1 <@ (mdydate, mdypattern),
    OPERATOR 15 <-> (mdydate, mdypattern) FOR ORDER BY pg_catalog.float_ops,
    FUNCTION 1 mdydate_gist_consistent(internal, mdydate, smallint, oid, internal),
    FUNCTION 2 mdydate_gist_union(internal, internal),
    FUNCTION 3 mdydate_gist_compress(internal),
    FUNCTION 5 mdydate_gist_penalty(internal, internal, internal),
    FUNCTION 6 mdydate_gist_picksplit(internal, internal),
    FUNCTION 7 mdydate_gist_same(mdydate_gkey, mdydate_gkey, internal),
    FUNCTION 8 mdydate_gist_distance(internal, mdydate, smallint, oid, internal),
    FUNCTION 9 mdydate_gist_fetch(internal);