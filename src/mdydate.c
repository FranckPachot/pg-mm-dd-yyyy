#include "postgres.h"

#include "access/gist.h"
#include "access/stratnum.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/date.h"
#include "utils/datetime.h"
#include "utils/errcodes.h"

PG_MODULE_MAGIC;

#define MDYDATE_LENGTH 10
#define MDYPATTERN_MONTH 0x01
#define MDYPATTERN_DAY 0x02
#define MDYPATTERN_YEAR 0x04
#define MDYDATE_MATCH_STRATEGY 1
#define MDYDATE_DISTANCE_STRATEGY 15
#define MONTH_DISTANCE_WEIGHT 32.0

typedef struct MdyDate
{
    char value[MDYDATE_LENGTH];
} MdyDate;

typedef struct MdyPattern
{
    int16 year;
    uint8 month;
    uint8 day;
    uint8 mask;
} MdyPattern;

typedef struct MdyGistKey
{
    int16 year_min;
    int16 year_max;
    uint8 month_min;
    uint8 month_max;
    uint8 day_min;
    uint8 day_max;
} MdyGistKey;

typedef struct MdyPickSplitItem
{
    int primary;
    int secondary;
    int tertiary;
    OffsetNumber index;
    MdyGistKey *key;
} MdyPickSplitItem;

StaticAssertDecl(sizeof(MdyDate) == MDYDATE_LENGTH,
                 "MdyDate must contain only its displayed text");
StaticAssertDecl(sizeof(MdyPattern) == 6,
                 "MdyPattern SQL layout must remain stable");
StaticAssertDecl(sizeof(MdyGistKey) == 8,
                 "MdyGistKey SQL layout must remain stable");

static bool
is_two_digits(const char *value)
{
    return value[0] >= '0' && value[0] <= '9' &&
           value[1] >= '0' && value[1] <= '9';
}

static bool
is_four_digits(const char *value)
{
    return is_two_digits(value) && is_two_digits(value + 2);
}

static int
read_two_digits(const char *value)
{
    return (value[0] - '0') * 10 + (value[1] - '0');
}

static int
read_four_digits(const char *value)
{
    return read_two_digits(value) * 100 + read_two_digits(value + 2);
}

static bool
is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int
days_in_month(int year, int month)
{
    static const int days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    Assert(month >= 1 && month <= 12);
    if (month == 2 && is_leap_year(year))
        return 29;

    return days[month - 1];
}

static void
parse_mdydate(const char *value, int *month, int *day, int *year)
{
    if (strlen(value) != MDYDATE_LENGTH ||
        value[2] != '/' || value[5] != '/' ||
        !is_two_digits(value) || !is_two_digits(value + 3) ||
        !is_four_digits(value + 6))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_DATETIME_FORMAT),
                 errmsg("invalid input syntax for mdydate: \"%s\"", value),
                 errhint("Use the canonical form MM/DD/YYYY, for example 09/15/2026.")));

    *month = read_two_digits(value);
    *day = read_two_digits(value + 3);
    *year = read_four_digits(value + 6);

    if (*month < 1 || *month > 12 || *year == 0 || *day == 0 ||
        *day > days_in_month(*year, *month))
        ereport(ERROR,
                (errcode(ERRCODE_DATETIME_FIELD_OVERFLOW),
                 errmsg("date out of range for mdydate: \"%s\"", value),
                 errhint("Years must be between 0001 and 9999 and the day must exist in that month.")));
}

static void
unpack_mdydate(const MdyDate *value, int *month, int *day, int *year)
{
    *month = read_two_digits(value->value);
    *day = read_two_digits(value->value + 3);
    *year = read_four_digits(value->value + 6);
}

static int
compare_mdydate(const MdyDate *left, const MdyDate *right)
{
    int left_month;
    int left_day;
    int left_year;
    int right_month;
    int right_day;
    int right_year;

    unpack_mdydate(left, &left_month, &left_day, &left_year);
    unpack_mdydate(right, &right_month, &right_day, &right_year);

    if (left_year != right_year)
        return left_year < right_year ? -1 : 1;
    if (left_month != right_month)
        return left_month < right_month ? -1 : 1;
    if (left_day != right_day)
        return left_day < right_day ? -1 : 1;
    return 0;
}

PG_FUNCTION_INFO_V1(mdydate_in);
Datum
mdydate_in(PG_FUNCTION_ARGS)
{
    char *input = PG_GETARG_CSTRING(0);
    MdyDate *result;
    int month;
    int day;
    int year;

    parse_mdydate(input, &month, &day, &year);
    result = palloc(sizeof(MdyDate));
    memcpy(result->value, input, MDYDATE_LENGTH);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mdydate_out);
Datum
mdydate_out(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    char *result = palloc(MDYDATE_LENGTH + 1);

    memcpy(result, value->value, MDYDATE_LENGTH);
    result[MDYDATE_LENGTH] = '\0';

    PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(mdydate_to_date);
Datum
mdydate_to_date(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mdydate(value, &month, &day, &year);
    PG_RETURN_DATEADT(date2j(year, month, day) - POSTGRES_EPOCH_JDATE);
}

PG_FUNCTION_INFO_V1(mdydate_from_date);
Datum
mdydate_from_date(PG_FUNCTION_ARGS)
{
    DateADT value = PG_GETARG_DATEADT(0);
    MdyDate *result;
    char output[MDYDATE_LENGTH + 1];
    int month;
    int day;
    int year;

    if (DATE_NOT_FINITE(value))
        ereport(ERROR,
                (errcode(ERRCODE_DATETIME_FIELD_OVERFLOW),
                 errmsg("infinite dates cannot be represented as mdydate")));

    j2date(value + POSTGRES_EPOCH_JDATE, &year, &month, &day);
    if (year < 1 || year > 9999)
        ereport(ERROR,
                (errcode(ERRCODE_DATETIME_FIELD_OVERFLOW),
                 errmsg("date is outside the mdydate year range 0001 through 9999")));

    result = palloc(sizeof(MdyDate));
    snprintf(output, sizeof(output), "%02d/%02d/%04d", month, day, year);
    memcpy(result->value, output, MDYDATE_LENGTH);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mdydate_month);
Datum
mdydate_month(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mdydate(value, &month, &day, &year);
    PG_RETURN_INT32(month);
}

PG_FUNCTION_INFO_V1(mdydate_day);
Datum
mdydate_day(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mdydate(value, &month, &day, &year);
    PG_RETURN_INT32(day);
}

PG_FUNCTION_INFO_V1(mdydate_year);
Datum
mdydate_year(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mdydate(value, &month, &day, &year);
    PG_RETURN_INT32(year);
}

PG_FUNCTION_INFO_V1(mdydate_cmp);
Datum
mdydate_cmp(PG_FUNCTION_ARGS)
{
    MdyDate *left = (MdyDate *) PG_GETARG_POINTER(0);
    MdyDate *right = (MdyDate *) PG_GETARG_POINTER(1);

    PG_RETURN_INT32(compare_mdydate(left, right));
}

#define MDYDATE_BOOL_OPERATOR(name, expression) \
    PG_FUNCTION_INFO_V1(name); \
    Datum \
    name(PG_FUNCTION_ARGS) \
    { \
        MdyDate *left = (MdyDate *) PG_GETARG_POINTER(0); \
        MdyDate *right = (MdyDate *) PG_GETARG_POINTER(1); \
        PG_RETURN_BOOL(expression); \
    }

MDYDATE_BOOL_OPERATOR(mdydate_eq, compare_mdydate(left, right) == 0)
MDYDATE_BOOL_OPERATOR(mdydate_ne, compare_mdydate(left, right) != 0)
MDYDATE_BOOL_OPERATOR(mdydate_lt, compare_mdydate(left, right) < 0)
MDYDATE_BOOL_OPERATOR(mdydate_le, compare_mdydate(left, right) <= 0)
MDYDATE_BOOL_OPERATOR(mdydate_ge, compare_mdydate(left, right) >= 0)
MDYDATE_BOOL_OPERATOR(mdydate_gt, compare_mdydate(left, right) > 0)

static void
invalid_pattern(const char *value)
{
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_DATETIME_FORMAT),
             errmsg("invalid input syntax for mdypattern: \"%s\"", value),
             errhint("Use MM/DD/YYYY with * for unknown components, for example 09/*/* or */15/*.")));
}

static MdyPattern *
parse_mdypattern(const char *value)
{
    const char *first_separator = strchr(value, '/');
    const char *second_separator;
    int month_length;
    int day_length;
    int year_length;
    MdyPattern *result;

    if (first_separator == NULL)
        invalid_pattern(value);
    second_separator = strchr(first_separator + 1, '/');
    if (second_separator == NULL || strchr(second_separator + 1, '/') != NULL)
        invalid_pattern(value);

    month_length = first_separator - value;
    day_length = second_separator - first_separator - 1;
    year_length = strlen(second_separator + 1);
    result = palloc0(sizeof(MdyPattern));

    if (month_length == 1 && value[0] == '*')
    {
        result->month = 0;
    }
    else if (month_length == 2 && is_two_digits(value))
    {
        result->month = read_two_digits(value);
        result->mask |= MDYPATTERN_MONTH;
    }
    else
        invalid_pattern(value);

    if (day_length == 1 && first_separator[1] == '*')
    {
        result->day = 0;
    }
    else if (day_length == 2 && is_two_digits(first_separator + 1))
    {
        result->day = read_two_digits(first_separator + 1);
        result->mask |= MDYPATTERN_DAY;
    }
    else
        invalid_pattern(value);

    if (year_length == 1 && second_separator[1] == '*')
    {
        result->year = 0;
    }
    else if (year_length == 4 && is_four_digits(second_separator + 1))
    {
        result->year = read_four_digits(second_separator + 1);
        result->mask |= MDYPATTERN_YEAR;
    }
    else
        invalid_pattern(value);

    if ((result->mask & MDYPATTERN_MONTH) != 0 &&
        (result->month == 0 || result->month > 12))
        invalid_pattern(value);
    if ((result->mask & MDYPATTERN_DAY) != 0 &&
        (result->day == 0 || result->day > 31))
        invalid_pattern(value);
    if ((result->mask & MDYPATTERN_YEAR) != 0 && result->year == 0)
        invalid_pattern(value);
    if ((result->mask & (MDYPATTERN_MONTH | MDYPATTERN_DAY)) ==
        (MDYPATTERN_MONTH | MDYPATTERN_DAY))
    {
        int validation_year = (result->mask & MDYPATTERN_YEAR) != 0
                              ? result->year : 2000;

        if (result->day > days_in_month(validation_year, result->month))
            invalid_pattern(value);
    }

    return result;
}

PG_FUNCTION_INFO_V1(mdypattern_in);
Datum
mdypattern_in(PG_FUNCTION_ARGS)
{
    PG_RETURN_POINTER(parse_mdypattern(PG_GETARG_CSTRING(0)));
}

PG_FUNCTION_INFO_V1(mdypattern_out);
Datum
mdypattern_out(PG_FUNCTION_ARGS)
{
    MdyPattern *pattern = (MdyPattern *) PG_GETARG_POINTER(0);
    StringInfoData output;

    initStringInfo(&output);
    if ((pattern->mask & MDYPATTERN_MONTH) != 0)
        appendStringInfo(&output, "%02d", pattern->month);
    else
        appendStringInfoChar(&output, '*');
    appendStringInfoChar(&output, '/');
    if ((pattern->mask & MDYPATTERN_DAY) != 0)
        appendStringInfo(&output, "%02d", pattern->day);
    else
        appendStringInfoChar(&output, '*');
    appendStringInfoChar(&output, '/');
    if ((pattern->mask & MDYPATTERN_YEAR) != 0)
        appendStringInfo(&output, "%04d", pattern->year);
    else
        appendStringInfoChar(&output, '*');

    PG_RETURN_CSTRING(output.data);
}

PG_FUNCTION_INFO_V1(mdypattern_from_mdydate);
Datum
mdypattern_from_mdydate(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    MdyPattern *result = palloc0(sizeof(MdyPattern));
    int month;
    int day;
    int year;

    unpack_mdydate(value, &month, &day, &year);
    result->month = month;
    result->day = day;
    result->year = year;
    result->mask = MDYPATTERN_MONTH | MDYPATTERN_DAY | MDYPATTERN_YEAR;

    PG_RETURN_POINTER(result);
}

static void
key_from_mdydate(MdyGistKey *key, const MdyDate *value)
{
    int month;
    int day;
    int year;

    unpack_mdydate(value, &month, &day, &year);
    key->month_min = key->month_max = month;
    key->day_min = key->day_max = day;
    key->year_min = key->year_max = year;
}

static void
expand_key(MdyGistKey *target, const MdyGistKey *addition)
{
    target->month_min = Min(target->month_min, addition->month_min);
    target->month_max = Max(target->month_max, addition->month_max);
    target->day_min = Min(target->day_min, addition->day_min);
    target->day_max = Max(target->day_max, addition->day_max);
    target->year_min = Min(target->year_min, addition->year_min);
    target->year_max = Max(target->year_max, addition->year_max);
}

static MdyGistKey *
copy_key(const MdyGistKey *source)
{
    MdyGistKey *result = palloc(sizeof(MdyGistKey));

    memcpy(result, source, sizeof(MdyGistKey));
    return result;
}

static bool
key_matches_pattern(const MdyGistKey *key, const MdyPattern *pattern)
{
    if ((pattern->mask & MDYPATTERN_MONTH) != 0 &&
        (pattern->month < key->month_min || pattern->month > key->month_max))
        return false;
    if ((pattern->mask & MDYPATTERN_DAY) != 0 &&
        (pattern->day < key->day_min || pattern->day > key->day_max))
        return false;
    if ((pattern->mask & MDYPATTERN_YEAR) != 0 &&
        (pattern->year < key->year_min || pattern->year > key->year_max))
        return false;
    return true;
}

static int
linear_gap(int value, int lower, int upper)
{
    if (value < lower)
        return lower - value;
    if (value > upper)
        return value - upper;
    return 0;
}

static int
month_gap(int month, int lower, int upper)
{
    int candidate;
    int result = 6;

    for (candidate = lower; candidate <= upper; candidate++)
    {
        int difference = abs(month - candidate);

        result = Min(result, Min(difference, 12 - difference));
    }
    return result;
}

static double
key_distance(const MdyGistKey *key, const MdyPattern *pattern)
{
    double result = 0.0;

    if ((pattern->mask & MDYPATTERN_MONTH) != 0)
        result += month_gap(pattern->month, key->month_min, key->month_max) *
                  MONTH_DISTANCE_WEIGHT;
    if ((pattern->mask & MDYPATTERN_DAY) != 0)
        result += linear_gap(pattern->day, key->day_min, key->day_max);
    if ((pattern->mask & MDYPATTERN_YEAR) != 0)
    {
        int difference = linear_gap(pattern->year,
                                    key->year_min, key->year_max);

        result += (double) difference / ((double) difference + 1.0);
    }
    return result;
}

static double
key_span(const MdyGistKey *key)
{
    int year_span = key->year_max - key->year_min;

    return (key->month_max - key->month_min) * MONTH_DISTANCE_WEIGHT +
           (key->day_max - key->day_min) +
           (double) year_span / ((double) year_span + 1.0);
}

PG_FUNCTION_INFO_V1(mdydate_matches);
Datum
mdydate_matches(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    MdyPattern *pattern = (MdyPattern *) PG_GETARG_POINTER(1);
    MdyGistKey key;

    key_from_mdydate(&key, value);
    PG_RETURN_BOOL(key_matches_pattern(&key, pattern));
}

PG_FUNCTION_INFO_V1(mdydate_distance);
Datum
mdydate_distance(PG_FUNCTION_ARGS)
{
    MdyDate *value = (MdyDate *) PG_GETARG_POINTER(0);
    MdyPattern *pattern = (MdyPattern *) PG_GETARG_POINTER(1);
    MdyGistKey key;

    key_from_mdydate(&key, value);
    PG_RETURN_FLOAT8(key_distance(&key, pattern));
}

PG_FUNCTION_INFO_V1(mdydate_gkey_in);
Datum
mdydate_gkey_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("mdydate_gkey values are created only by the GiST operator class")));
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(mdydate_gkey_out);
Datum
mdydate_gkey_out(PG_FUNCTION_ARGS)
{
    MdyGistKey *key = (MdyGistKey *) PG_GETARG_POINTER(0);

    PG_RETURN_CSTRING(psprintf("([%d,%d],[%d,%d],[%d,%d])",
                              key->month_min, key->month_max,
                              key->day_min, key->day_max,
                              key->year_min, key->year_max));
}

PG_FUNCTION_INFO_V1(mdydate_gist_consistent);
Datum
mdydate_gist_consistent(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    MdyPattern *pattern = (MdyPattern *) PG_GETARG_POINTER(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    bool *recheck = (bool *) PG_GETARG_POINTER(4);
    MdyGistKey *key = (MdyGistKey *) DatumGetPointer(entry->key);

    if (strategy != MDYDATE_MATCH_STRATEGY)
        elog(ERROR, "unrecognized mdydate GiST strategy number: %d", strategy);

    *recheck = false;
    PG_RETURN_BOOL(key_matches_pattern(key, pattern));
}

PG_FUNCTION_INFO_V1(mdydate_gist_union);
Datum
mdydate_gist_union(PG_FUNCTION_ARGS)
{
    GistEntryVector *entry_vector = (GistEntryVector *) PG_GETARG_POINTER(0);
    int *size = (int *) PG_GETARG_POINTER(1);
    MdyGistKey *result;
    int index;

    result = copy_key((MdyGistKey *) DatumGetPointer(entry_vector->vector[0].key));
    for (index = 1; index < entry_vector->n; index++)
        expand_key(result,
                   (MdyGistKey *) DatumGetPointer(entry_vector->vector[index].key));

    *size = sizeof(MdyGistKey);
    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mdydate_gist_compress);
Datum
mdydate_gist_compress(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);

    if (entry->leafkey)
    {
        MdyDate *value = (MdyDate *) DatumGetPointer(entry->key);
        MdyGistKey *key = palloc(sizeof(MdyGistKey));
        GISTENTRY *result = palloc(sizeof(GISTENTRY));

        key_from_mdydate(key, value);
        gistentryinit(*result, PointerGetDatum(key),
                      entry->rel, entry->page, entry->offset, false);
        PG_RETURN_POINTER(result);
    }

    PG_RETURN_POINTER(entry);
}

PG_FUNCTION_INFO_V1(mdydate_gist_penalty);
Datum
mdydate_gist_penalty(PG_FUNCTION_ARGS)
{
    GISTENTRY *original_entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY *new_entry = (GISTENTRY *) PG_GETARG_POINTER(1);
    float *penalty = (float *) PG_GETARG_POINTER(2);
    MdyGistKey expanded;
    MdyGistKey *original = (MdyGistKey *) DatumGetPointer(original_entry->key);
    MdyGistKey *addition = (MdyGistKey *) DatumGetPointer(new_entry->key);

    memcpy(&expanded, original, sizeof(MdyGistKey));
    expand_key(&expanded, addition);
    *penalty = (float) (key_span(&expanded) - key_span(original));

    PG_RETURN_POINTER(penalty);
}

static int
picksplit_item_compare(const void *left, const void *right)
{
    const MdyPickSplitItem *left_item = (const MdyPickSplitItem *) left;
    const MdyPickSplitItem *right_item = (const MdyPickSplitItem *) right;

    if (left_item->primary != right_item->primary)
        return left_item->primary < right_item->primary ? -1 : 1;
    if (left_item->secondary != right_item->secondary)
        return left_item->secondary < right_item->secondary ? -1 : 1;
    if (left_item->tertiary != right_item->tertiary)
        return left_item->tertiary < right_item->tertiary ? -1 : 1;
    if (left_item->index != right_item->index)
        return left_item->index < right_item->index ? -1 : 1;
    return 0;
}

PG_FUNCTION_INFO_V1(mdydate_gist_picksplit);
Datum
mdydate_gist_picksplit(PG_FUNCTION_ARGS)
{
    GistEntryVector *entry_vector = (GistEntryVector *) PG_GETARG_POINTER(0);
    GIST_SPLITVEC *split_vector = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
    OffsetNumber max_offset = entry_vector->n - 1;
    MdyPickSplitItem *items = palloc(max_offset * sizeof(MdyPickSplitItem));
    int month_center_min = PG_INT32_MAX;
    int month_center_max = PG_INT32_MIN;
    int day_center_min = PG_INT32_MAX;
    int day_center_max = PG_INT32_MIN;
    int dimension;
    int first_right;
    int index;
    MdyGistKey *left_key;
    MdyGistKey *right_key;

    Assert(max_offset >= 2);
    for (index = FirstOffsetNumber; index <= max_offset; index++)
    {
        MdyGistKey *key = (MdyGistKey *)
                          DatumGetPointer(entry_vector->vector[index].key);
        int month_center = key->month_min + key->month_max;
        int day_center = key->day_min + key->day_max;

        month_center_min = Min(month_center_min, month_center);
        month_center_max = Max(month_center_max, month_center);
        day_center_min = Min(day_center_min, day_center);
        day_center_max = Max(day_center_max, day_center);
        items[index - 1].index = index;
        items[index - 1].key = key;
    }

    dimension = month_center_min != month_center_max ? 0 :
                day_center_min != day_center_max ? 1 : 2;
    for (index = 0; index < max_offset; index++)
    {
        MdyGistKey *key = items[index].key;
        int month_center = key->month_min + key->month_max;
        int day_center = key->day_min + key->day_max;
        int year_center = key->year_min + key->year_max;

        if (dimension == 0)
        {
            items[index].primary = month_center;
            items[index].secondary = day_center;
            items[index].tertiary = year_center;
        }
        else if (dimension == 1)
        {
            items[index].primary = day_center;
            items[index].secondary = month_center;
            items[index].tertiary = year_center;
        }
        else
        {
            items[index].primary = year_center;
            items[index].secondary = month_center;
            items[index].tertiary = day_center;
        }
    }
    qsort(items, max_offset, sizeof(MdyPickSplitItem), picksplit_item_compare);

    first_right = max_offset / 2;
    split_vector->spl_left = palloc(max_offset * sizeof(OffsetNumber));
    split_vector->spl_right = palloc(max_offset * sizeof(OffsetNumber));
    split_vector->spl_nleft = 0;
    split_vector->spl_nright = 0;

    left_key = copy_key(items[0].key);
    for (index = 0; index < first_right; index++)
    {
        split_vector->spl_left[split_vector->spl_nleft++] = items[index].index;
        if (index > 0)
            expand_key(left_key, items[index].key);
    }

    right_key = copy_key(items[first_right].key);
    for (index = first_right; index < max_offset; index++)
    {
        split_vector->spl_right[split_vector->spl_nright++] = items[index].index;
        if (index > first_right)
            expand_key(right_key, items[index].key);
    }

    split_vector->spl_ldatum = PointerGetDatum(left_key);
    split_vector->spl_rdatum = PointerGetDatum(right_key);

    PG_RETURN_POINTER(split_vector);
}

PG_FUNCTION_INFO_V1(mdydate_gist_same);
Datum
mdydate_gist_same(PG_FUNCTION_ARGS)
{
    MdyGistKey *left = (MdyGistKey *) PG_GETARG_POINTER(0);
    MdyGistKey *right = (MdyGistKey *) PG_GETARG_POINTER(1);
    bool *result = (bool *) PG_GETARG_POINTER(2);

    *result = memcmp(left, right, sizeof(MdyGistKey)) == 0;
    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mdydate_gist_distance);
Datum
mdydate_gist_distance(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    MdyPattern *pattern = (MdyPattern *) PG_GETARG_POINTER(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    bool *recheck = (bool *) PG_GETARG_POINTER(4);
    MdyGistKey *key = (MdyGistKey *) DatumGetPointer(entry->key);

    if (strategy != MDYDATE_DISTANCE_STRATEGY)
        elog(ERROR, "unrecognized mdydate GiST distance strategy number: %d",
             strategy);

    *recheck = false;
    PG_RETURN_FLOAT8(key_distance(key, pattern));
}

PG_FUNCTION_INFO_V1(mdydate_gist_fetch);
Datum
mdydate_gist_fetch(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    MdyGistKey *key = (MdyGistKey *) DatumGetPointer(entry->key);
    MdyDate *value = palloc(sizeof(MdyDate));
    GISTENTRY *result = palloc(sizeof(GISTENTRY));
    char output[MDYDATE_LENGTH + 1];

    Assert(key->month_min == key->month_max);
    Assert(key->day_min == key->day_max);
    Assert(key->year_min == key->year_max);
    snprintf(output, sizeof(output), "%02d/%02d/%04d",
             key->month_min, key->day_min, key->year_min);
    memcpy(value->value, output, MDYDATE_LENGTH);
    gistentryinit(*result, PointerGetDatum(value),
                  entry->rel, entry->page, entry->offset, false);

    PG_RETURN_POINTER(result);
}