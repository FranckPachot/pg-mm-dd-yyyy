\set ON_ERROR_STOP 1
\pset pager off

\echo '\nStart with the production answer: text plus an expression index.'
DROP TABLE IF EXISTS mmddyyyy_text_demo;

CREATE OR REPLACE FUNCTION us_text_to_date(value text)
RETURNS date
LANGUAGE sql
IMMUTABLE STRICT PARALLEL SAFE
AS $$
        SELECT make_date(
                substring(value FROM 7 FOR 4)::integer,
                substring(value FROM 1 FOR 2)::integer,
                substring(value FROM 4 FOR 2)::integer
        )
$$;

CREATE TABLE mmddyyyy_text_demo (
        label text PRIMARY KEY,
        happened_on text NOT NULL
);

INSERT INTO mmddyyyy_text_demo VALUES
        ('the target',          '09/15/2026'),
        ('same month, old',     '09/01/1980'),
        ('same day, previous',  '08/15/2026'),
        ('same day, next',      '10/15/2026'),
        ('year boundary left',  '12/31/2026'),
        ('year boundary right', '01/01/2027'),
        ('leap day',            '02/29/2024');

CREATE INDEX mmddyyyy_text_chronology
ON mmddyyyy_text_demo (us_text_to_date(happened_on));
SET enable_seqscan = off;

EXPLAIN (COSTS OFF)
SELECT *
FROM mmddyyyy_text_demo
WHERE us_text_to_date(happened_on) >= date '2026-09-01'
    AND us_text_to_date(happened_on) <  date '2026-10-01';

SELECT label, happened_on
FROM mmddyyyy_text_demo
WHERE us_text_to_date(happened_on) >= date '2026-09-01'
    AND us_text_to_date(happened_on) <  date '2026-10-01'
ORDER BY us_text_to_date(happened_on);

\echo '\nNow continue for education: a native type and native operators.'
CREATE EXTENSION IF NOT EXISTS mmddyyyy;
DROP TABLE IF EXISTS mmddyyyy_demo;

CREATE TABLE mmddyyyy_demo (
    label text PRIMARY KEY,
    happened_on mmddyyyy NOT NULL
);

INSERT INTO mmddyyyy_demo VALUES
    ('the target',          '09/15/2026'),
    ('same month, old',     '09/01/1980'),
    ('same day, previous',  '08/15/2026'),
    ('same day, next',      '10/15/2026'),
    ('year boundary left',  '12/31/2026'),
    ('year boundary right', '01/01/2027'),
    ('leap day',            '02/29/2024');

\echo '\nThe native table value really is the fixed 10-byte spelling:'
SELECT label, happened_on, pg_column_size(happened_on) AS payload_bytes
FROM mmddyyyy_demo
WHERE label = 'the target';

\echo '\nA B-tree asks its data type operator class how to compare keys:'
SELECT '12/31/2025'::text < '01/01/2026'::text AS plain_text_says_before,
             '12/31/2025'::mmddyyyy < '01/01/2026'::mmddyyyy AS mmddyyyy_says_before;

SELECT opc.opcname AS default_btree_operator_class,
             proc.amproc::regprocedure AS comparison_support_function
FROM pg_opclass AS opc
JOIN pg_am AS access_method
    ON access_method.oid = opc.opcmethod
JOIN pg_amproc AS proc
    ON proc.amprocfamily = opc.opcfamily
 AND proc.amprocnum = 1
WHERE access_method.amname = 'btree'
    AND opc.opcdefault
    AND opc.opcintype = 'mmddyyyy'::regtype;

CREATE INDEX mmddyyyy_demo_btree ON mmddyyyy_demo (happened_on);
CREATE INDEX mmddyyyy_demo_gist ON mmddyyyy_demo USING gist (happened_on);
SET enable_seqscan = off;

\echo '\nB-tree means chronological order, despite month-first text:'
SELECT label, happened_on
FROM mmddyyyy_demo
ORDER BY happened_on;

\echo '\nGiST can search an independently specified month, day, or year:'
EXPLAIN (COSTS OFF)
SELECT * FROM mmddyyyy_demo WHERE happened_on <@ '09/*/*';

SELECT label, happened_on
FROM mmddyyyy_demo
WHERE happened_on <@ '09/*/*'
ORDER BY happened_on;

SELECT label, happened_on
FROM mmddyyyy_demo
WHERE happened_on <@ '*/15/*'
ORDER BY happened_on;

\echo '\nContainment is yes/no. <-> assigns a similarity distance to every date.'
\echo 'KNN means k-nearest-neighbor search: ORDER BY that distance and LIMIT k.'
\echo 'This policy ranks same-month dates before same-day dates in other months:'
EXPLAIN (COSTS OFF)
SELECT label, happened_on
FROM mmddyyyy_demo
ORDER BY happened_on <-> '09/15/2026'
LIMIT 5;

SELECT label,
       happened_on,
    round((happened_on <-> '09/15/2026')::numeric, 6) AS distance
FROM mmddyyyy_demo
ORDER BY happened_on <-> '09/15/2026'
LIMIT 5;

RESET enable_seqscan;