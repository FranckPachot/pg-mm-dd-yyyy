#include "postgres.h"

#include "access/gist.h"
#include "access/stratnum.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/date.h"
#include "utils/datetime.h"
#include "utils/errcodes.h"

PG_MODULE_MAGIC;

#define MMDDYYYY_LENGTH 10
#define MMDDYYYY_PATTERN_MONTH 0x01
#define MMDDYYYY_PATTERN_DAY 0x02
#define MMDDYYYY_PATTERN_YEAR 0x04
#define MMDDYYYY_MATCH_STRATEGY 1
#define MMDDYYYY_DISTANCE_STRATEGY 15
#define MONTH_DISTANCE_WEIGHT 32.0

typedef struct MmDdYyyy
{
    char value[MMDDYYYY_LENGTH];
} MmDdYyyy;

typedef struct MmDdYyyyPattern
{
    int16 year;
    uint8 month;
    uint8 day;
    uint8 mask;
} MmDdYyyyPattern;

typedef struct MmDdYyyyGistKey
{
    int16 year_min;
    int16 year_max;
    uint8 month_min;
    uint8 month_max;
    uint8 day_min;
    uint8 day_max;
} MmDdYyyyGistKey;

typedef struct MmDdYyyyPickSplitItem
{
    int primary;
    int secondary;
    int tertiary;
    OffsetNumber index;
    MmDdYyyyGistKey *key;
} MmDdYyyyPickSplitItem;

StaticAssertDecl(sizeof(MmDdYyyy) == MMDDYYYY_LENGTH,
                 "MmDdYyyy must contain only its displayed text");
StaticAssertDecl(sizeof(MmDdYyyyPattern) == 6,
                 "MmDdYyyyPattern SQL layout must remain stable");
StaticAssertDecl(sizeof(MmDdYyyyGistKey) == 8,
                 "MmDdYyyyGistKey SQL layout must remain stable");

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
parse_mmddyyyy(const char *value, int *month, int *day, int *year)
{
    if (strlen(value) != MMDDYYYY_LENGTH ||
        value[2] != '/' || value[5] != '/' ||
        !is_two_digits(value) || !is_two_digits(value + 3) ||
        !is_four_digits(value + 6))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_DATETIME_FORMAT),
                 errmsg("invalid input syntax for mmddyyyy: \"%s\"", value),
                 errhint("Use the canonical form MM/DD/YYYY, for example 09/15/2026.")));

    *month = read_two_digits(value);
    *day = read_two_digits(value + 3);
    *year = read_four_digits(value + 6);

    if (*month < 1 || *month > 12 || *year == 0 || *day == 0 ||
        *day > days_in_month(*year, *month))
        ereport(ERROR,
                (errcode(ERRCODE_DATETIME_FIELD_OVERFLOW),
                 errmsg("date out of range for mmddyyyy: \"%s\"", value),
                 errhint("Years must be between 0001 and 9999 and the day must exist in that month.")));
}

static void
unpack_mmddyyyy(const MmDdYyyy *value, int *month, int *day, int *year)
{
    *month = read_two_digits(value->value);
    *day = read_two_digits(value->value + 3);
    *year = read_four_digits(value->value + 6);
}

static int
compare_mmddyyyy(const MmDdYyyy *left, const MmDdYyyy *right)
{
    int left_month;
    int left_day;
    int left_year;
    int right_month;
    int right_day;
    int right_year;

    unpack_mmddyyyy(left, &left_month, &left_day, &left_year);
    unpack_mmddyyyy(right, &right_month, &right_day, &right_year);

    if (left_year != right_year)
        return left_year < right_year ? -1 : 1;
    if (left_month != right_month)
        return left_month < right_month ? -1 : 1;
    if (left_day != right_day)
        return left_day < right_day ? -1 : 1;
    return 0;
}

PG_FUNCTION_INFO_V1(mmddyyyy_in);
Datum
mmddyyyy_in(PG_FUNCTION_ARGS)
{
    char *input = PG_GETARG_CSTRING(0);
    MmDdYyyy *result;
    int month;
    int day;
    int year;

    parse_mmddyyyy(input, &month, &day, &year);
    result = palloc(sizeof(MmDdYyyy));
    memcpy(result->value, input, MMDDYYYY_LENGTH);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mmddyyyy_out);
Datum
mmddyyyy_out(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    char *result = palloc(MMDDYYYY_LENGTH + 1);

    memcpy(result, value->value, MMDDYYYY_LENGTH);
    result[MMDDYYYY_LENGTH] = '\0';

    PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(mmddyyyy_to_date);
Datum
mmddyyyy_to_date(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mmddyyyy(value, &month, &day, &year);
    PG_RETURN_DATEADT(date2j(year, month, day) - POSTGRES_EPOCH_JDATE);
}

PG_FUNCTION_INFO_V1(mmddyyyy_from_date);
Datum
mmddyyyy_from_date(PG_FUNCTION_ARGS)
{
    DateADT value = PG_GETARG_DATEADT(0);
    MmDdYyyy *result;
    char output[MMDDYYYY_LENGTH + 1];
    int month;
    int day;
    int year;

    if (DATE_NOT_FINITE(value))
        ereport(ERROR,
                (errcode(ERRCODE_DATETIME_FIELD_OVERFLOW),
                 errmsg("infinite dates cannot be represented as mmddyyyy")));

    j2date(value + POSTGRES_EPOCH_JDATE, &year, &month, &day);
    if (year < 1 || year > 9999)
        ereport(ERROR,
                (errcode(ERRCODE_DATETIME_FIELD_OVERFLOW),
                 errmsg("date is outside the mmddyyyy year range 0001 through 9999")));

    result = palloc(sizeof(MmDdYyyy));
    snprintf(output, sizeof(output), "%02d/%02d/%04d", month, day, year);
    memcpy(result->value, output, MMDDYYYY_LENGTH);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mmddyyyy_month);
Datum
mmddyyyy_month(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mmddyyyy(value, &month, &day, &year);
    PG_RETURN_INT32(month);
}

PG_FUNCTION_INFO_V1(mmddyyyy_day);
Datum
mmddyyyy_day(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mmddyyyy(value, &month, &day, &year);
    PG_RETURN_INT32(day);
}

PG_FUNCTION_INFO_V1(mmddyyyy_year);
Datum
mmddyyyy_year(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    int month;
    int day;
    int year;

    unpack_mmddyyyy(value, &month, &day, &year);
    PG_RETURN_INT32(year);
}

PG_FUNCTION_INFO_V1(mmddyyyy_cmp);
Datum
mmddyyyy_cmp(PG_FUNCTION_ARGS)
{
    MmDdYyyy *left = (MmDdYyyy *) PG_GETARG_POINTER(0);
    MmDdYyyy *right = (MmDdYyyy *) PG_GETARG_POINTER(1);

    PG_RETURN_INT32(compare_mmddyyyy(left, right));
}

#define MMDDYYYY_BOOL_OPERATOR(name, expression) \
    PG_FUNCTION_INFO_V1(name); \
    Datum \
    name(PG_FUNCTION_ARGS) \
    { \
        MmDdYyyy *left = (MmDdYyyy *) PG_GETARG_POINTER(0); \
        MmDdYyyy *right = (MmDdYyyy *) PG_GETARG_POINTER(1); \
        PG_RETURN_BOOL(expression); \
    }

MMDDYYYY_BOOL_OPERATOR(mmddyyyy_eq, compare_mmddyyyy(left, right) == 0)
MMDDYYYY_BOOL_OPERATOR(mmddyyyy_ne, compare_mmddyyyy(left, right) != 0)
MMDDYYYY_BOOL_OPERATOR(mmddyyyy_lt, compare_mmddyyyy(left, right) < 0)
MMDDYYYY_BOOL_OPERATOR(mmddyyyy_le, compare_mmddyyyy(left, right) <= 0)
MMDDYYYY_BOOL_OPERATOR(mmddyyyy_ge, compare_mmddyyyy(left, right) >= 0)
MMDDYYYY_BOOL_OPERATOR(mmddyyyy_gt, compare_mmddyyyy(left, right) > 0)

static void
invalid_pattern(const char *value)
{
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_DATETIME_FORMAT),
             errmsg("invalid input syntax for mmddyyyy_pattern: \"%s\"", value),
             errhint("Use MM/DD/YYYY with * for unknown components, for example 09/*/* or */15/*.")));
}

static MmDdYyyyPattern *
parse_mmddyyyy_pattern(const char *value)
{
    const char *first_separator = strchr(value, '/');
    const char *second_separator;
    int month_length;
    int day_length;
    int year_length;
    MmDdYyyyPattern *result;

    if (first_separator == NULL)
        invalid_pattern(value);
    second_separator = strchr(first_separator + 1, '/');
    if (second_separator == NULL || strchr(second_separator + 1, '/') != NULL)
        invalid_pattern(value);

    month_length = first_separator - value;
    day_length = second_separator - first_separator - 1;
    year_length = strlen(second_separator + 1);
    result = palloc0(sizeof(MmDdYyyyPattern));

    if (month_length == 1 && value[0] == '*')
    {
        result->month = 0;
    }
    else if (month_length == 2 && is_two_digits(value))
    {
        result->month = read_two_digits(value);
        result->mask |= MMDDYYYY_PATTERN_MONTH;
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
        result->mask |= MMDDYYYY_PATTERN_DAY;
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
        result->mask |= MMDDYYYY_PATTERN_YEAR;
    }
    else
        invalid_pattern(value);

    if ((result->mask & MMDDYYYY_PATTERN_MONTH) != 0 &&
        (result->month == 0 || result->month > 12))
        invalid_pattern(value);
    if ((result->mask & MMDDYYYY_PATTERN_DAY) != 0 &&
        (result->day == 0 || result->day > 31))
        invalid_pattern(value);
    if ((result->mask & MMDDYYYY_PATTERN_YEAR) != 0 && result->year == 0)
        invalid_pattern(value);
    if ((result->mask & (MMDDYYYY_PATTERN_MONTH | MMDDYYYY_PATTERN_DAY)) ==
        (MMDDYYYY_PATTERN_MONTH | MMDDYYYY_PATTERN_DAY))
    {
        int validation_year = (result->mask & MMDDYYYY_PATTERN_YEAR) != 0
                              ? result->year : 2000;

        if (result->day > days_in_month(validation_year, result->month))
            invalid_pattern(value);
    }

    return result;
}

PG_FUNCTION_INFO_V1(mmddyyyy_pattern_in);
Datum
mmddyyyy_pattern_in(PG_FUNCTION_ARGS)
{
    PG_RETURN_POINTER(parse_mmddyyyy_pattern(PG_GETARG_CSTRING(0)));
}

PG_FUNCTION_INFO_V1(mmddyyyy_pattern_out);
Datum
mmddyyyy_pattern_out(PG_FUNCTION_ARGS)
{
    MmDdYyyyPattern *pattern = (MmDdYyyyPattern *) PG_GETARG_POINTER(0);
    StringInfoData output;

    initStringInfo(&output);
    if ((pattern->mask & MMDDYYYY_PATTERN_MONTH) != 0)
        appendStringInfo(&output, "%02d", pattern->month);
    else
        appendStringInfoChar(&output, '*');
    appendStringInfoChar(&output, '/');
    if ((pattern->mask & MMDDYYYY_PATTERN_DAY) != 0)
        appendStringInfo(&output, "%02d", pattern->day);
    else
        appendStringInfoChar(&output, '*');
    appendStringInfoChar(&output, '/');
    if ((pattern->mask & MMDDYYYY_PATTERN_YEAR) != 0)
        appendStringInfo(&output, "%04d", pattern->year);
    else
        appendStringInfoChar(&output, '*');

    PG_RETURN_CSTRING(output.data);
}

PG_FUNCTION_INFO_V1(mmddyyyy_pattern_from_mmddyyyy);
Datum
mmddyyyy_pattern_from_mmddyyyy(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    MmDdYyyyPattern *result = palloc0(sizeof(MmDdYyyyPattern));
    int month;
    int day;
    int year;

    unpack_mmddyyyy(value, &month, &day, &year);
    result->month = month;
    result->day = day;
    result->year = year;
    result->mask = MMDDYYYY_PATTERN_MONTH | MMDDYYYY_PATTERN_DAY | MMDDYYYY_PATTERN_YEAR;

    PG_RETURN_POINTER(result);
}

static void
key_from_mmddyyyy(MmDdYyyyGistKey *key, const MmDdYyyy *value)
{
    int month;
    int day;
    int year;

    unpack_mmddyyyy(value, &month, &day, &year);
    key->month_min = key->month_max = month;
    key->day_min = key->day_max = day;
    key->year_min = key->year_max = year;
}

static void
expand_key(MmDdYyyyGistKey *target, const MmDdYyyyGistKey *addition)
{
    target->month_min = Min(target->month_min, addition->month_min);
    target->month_max = Max(target->month_max, addition->month_max);
    target->day_min = Min(target->day_min, addition->day_min);
    target->day_max = Max(target->day_max, addition->day_max);
    target->year_min = Min(target->year_min, addition->year_min);
    target->year_max = Max(target->year_max, addition->year_max);
}

static MmDdYyyyGistKey *
copy_key(const MmDdYyyyGistKey *source)
{
    MmDdYyyyGistKey *result = palloc(sizeof(MmDdYyyyGistKey));

    memcpy(result, source, sizeof(MmDdYyyyGistKey));
    return result;
}

static bool
key_matches_pattern(const MmDdYyyyGistKey *key, const MmDdYyyyPattern *pattern)
{
    if ((pattern->mask & MMDDYYYY_PATTERN_MONTH) != 0 &&
        (pattern->month < key->month_min || pattern->month > key->month_max))
        return false;
    if ((pattern->mask & MMDDYYYY_PATTERN_DAY) != 0 &&
        (pattern->day < key->day_min || pattern->day > key->day_max))
        return false;
    if ((pattern->mask & MMDDYYYY_PATTERN_YEAR) != 0 &&
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
key_distance(const MmDdYyyyGistKey *key, const MmDdYyyyPattern *pattern)
{
    double result = 0.0;

    if ((pattern->mask & MMDDYYYY_PATTERN_MONTH) != 0)
        result += month_gap(pattern->month, key->month_min, key->month_max) *
                  MONTH_DISTANCE_WEIGHT;
    if ((pattern->mask & MMDDYYYY_PATTERN_DAY) != 0)
        result += linear_gap(pattern->day, key->day_min, key->day_max);
    if ((pattern->mask & MMDDYYYY_PATTERN_YEAR) != 0)
    {
        int difference = linear_gap(pattern->year,
                                    key->year_min, key->year_max);

        result += (double) difference / ((double) difference + 1.0);
    }
    return result;
}

static double
key_span(const MmDdYyyyGistKey *key)
{
    int year_span = key->year_max - key->year_min;

    return (key->month_max - key->month_min) * MONTH_DISTANCE_WEIGHT +
           (key->day_max - key->day_min) +
           (double) year_span / ((double) year_span + 1.0);
}

PG_FUNCTION_INFO_V1(mmddyyyy_matches);
Datum
mmddyyyy_matches(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    MmDdYyyyPattern *pattern = (MmDdYyyyPattern *) PG_GETARG_POINTER(1);
    MmDdYyyyGistKey key;

    key_from_mmddyyyy(&key, value);
    PG_RETURN_BOOL(key_matches_pattern(&key, pattern));
}

PG_FUNCTION_INFO_V1(mmddyyyy_distance);
Datum
mmddyyyy_distance(PG_FUNCTION_ARGS)
{
    MmDdYyyy *value = (MmDdYyyy *) PG_GETARG_POINTER(0);
    MmDdYyyyPattern *pattern = (MmDdYyyyPattern *) PG_GETARG_POINTER(1);
    MmDdYyyyGistKey key;

    key_from_mmddyyyy(&key, value);
    PG_RETURN_FLOAT8(key_distance(&key, pattern));
}

PG_FUNCTION_INFO_V1(mmddyyyy_gkey_in);
Datum
mmddyyyy_gkey_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("mmddyyyy_gkey values are created only by the GiST operator class")));
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(mmddyyyy_gkey_out);
Datum
mmddyyyy_gkey_out(PG_FUNCTION_ARGS)
{
    MmDdYyyyGistKey *key = (MmDdYyyyGistKey *) PG_GETARG_POINTER(0);

    PG_RETURN_CSTRING(psprintf("([%d,%d],[%d,%d],[%d,%d])",
                              key->month_min, key->month_max,
                              key->day_min, key->day_max,
                              key->year_min, key->year_max));
}

PG_FUNCTION_INFO_V1(mmddyyyy_gist_consistent);
Datum
mmddyyyy_gist_consistent(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    MmDdYyyyPattern *pattern = (MmDdYyyyPattern *) PG_GETARG_POINTER(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    bool *recheck = (bool *) PG_GETARG_POINTER(4);
    MmDdYyyyGistKey *key = (MmDdYyyyGistKey *) DatumGetPointer(entry->key);

    if (strategy != MMDDYYYY_MATCH_STRATEGY)
        elog(ERROR, "unrecognized mmddyyyy GiST strategy number: %d", strategy);

    *recheck = false;
    PG_RETURN_BOOL(key_matches_pattern(key, pattern));
}

PG_FUNCTION_INFO_V1(mmddyyyy_gist_union);
Datum
mmddyyyy_gist_union(PG_FUNCTION_ARGS)
{
    GistEntryVector *entry_vector = (GistEntryVector *) PG_GETARG_POINTER(0);
    int *size = (int *) PG_GETARG_POINTER(1);
    MmDdYyyyGistKey *result;
    int index;

    result = copy_key((MmDdYyyyGistKey *) DatumGetPointer(entry_vector->vector[0].key));
    for (index = 1; index < entry_vector->n; index++)
        expand_key(result,
                   (MmDdYyyyGistKey *) DatumGetPointer(entry_vector->vector[index].key));

    *size = sizeof(MmDdYyyyGistKey);
    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mmddyyyy_gist_compress);
Datum
mmddyyyy_gist_compress(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);

    if (entry->leafkey)
    {
        MmDdYyyy *value = (MmDdYyyy *) DatumGetPointer(entry->key);
        MmDdYyyyGistKey *key = palloc(sizeof(MmDdYyyyGistKey));
        GISTENTRY *result = palloc(sizeof(GISTENTRY));

        key_from_mmddyyyy(key, value);
        gistentryinit(*result, PointerGetDatum(key),
                      entry->rel, entry->page, entry->offset, false);
        PG_RETURN_POINTER(result);
    }

    PG_RETURN_POINTER(entry);
}

PG_FUNCTION_INFO_V1(mmddyyyy_gist_penalty);
Datum
mmddyyyy_gist_penalty(PG_FUNCTION_ARGS)
{
    GISTENTRY *original_entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY *new_entry = (GISTENTRY *) PG_GETARG_POINTER(1);
    float *penalty = (float *) PG_GETARG_POINTER(2);
    MmDdYyyyGistKey expanded;
    MmDdYyyyGistKey *original = (MmDdYyyyGistKey *) DatumGetPointer(original_entry->key);
    MmDdYyyyGistKey *addition = (MmDdYyyyGistKey *) DatumGetPointer(new_entry->key);

    memcpy(&expanded, original, sizeof(MmDdYyyyGistKey));
    expand_key(&expanded, addition);
    *penalty = (float) (key_span(&expanded) - key_span(original));

    PG_RETURN_POINTER(penalty);
}

static int
picksplit_item_compare(const void *left, const void *right)
{
    const MmDdYyyyPickSplitItem *left_item = (const MmDdYyyyPickSplitItem *) left;
    const MmDdYyyyPickSplitItem *right_item = (const MmDdYyyyPickSplitItem *) right;

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

PG_FUNCTION_INFO_V1(mmddyyyy_gist_picksplit);
Datum
mmddyyyy_gist_picksplit(PG_FUNCTION_ARGS)
{
    GistEntryVector *entry_vector = (GistEntryVector *) PG_GETARG_POINTER(0);
    GIST_SPLITVEC *split_vector = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
    OffsetNumber max_offset = entry_vector->n - 1;
    MmDdYyyyPickSplitItem *items = palloc(max_offset * sizeof(MmDdYyyyPickSplitItem));
    int month_center_min = PG_INT32_MAX;
    int month_center_max = PG_INT32_MIN;
    int day_center_min = PG_INT32_MAX;
    int day_center_max = PG_INT32_MIN;
    int dimension;
    int first_right;
    int index;
    MmDdYyyyGistKey *left_key;
    MmDdYyyyGistKey *right_key;

    Assert(max_offset >= 2);
    for (index = FirstOffsetNumber; index <= max_offset; index++)
    {
        MmDdYyyyGistKey *key = (MmDdYyyyGistKey *)
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
        MmDdYyyyGistKey *key = items[index].key;
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
    qsort(items, max_offset, sizeof(MmDdYyyyPickSplitItem), picksplit_item_compare);

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

PG_FUNCTION_INFO_V1(mmddyyyy_gist_same);
Datum
mmddyyyy_gist_same(PG_FUNCTION_ARGS)
{
    MmDdYyyyGistKey *left = (MmDdYyyyGistKey *) PG_GETARG_POINTER(0);
    MmDdYyyyGistKey *right = (MmDdYyyyGistKey *) PG_GETARG_POINTER(1);
    bool *result = (bool *) PG_GETARG_POINTER(2);

    *result = memcmp(left, right, sizeof(MmDdYyyyGistKey)) == 0;
    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(mmddyyyy_gist_distance);
Datum
mmddyyyy_gist_distance(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    MmDdYyyyPattern *pattern = (MmDdYyyyPattern *) PG_GETARG_POINTER(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    bool *recheck = (bool *) PG_GETARG_POINTER(4);
    MmDdYyyyGistKey *key = (MmDdYyyyGistKey *) DatumGetPointer(entry->key);

    if (strategy != MMDDYYYY_DISTANCE_STRATEGY)
        elog(ERROR, "unrecognized mmddyyyy GiST distance strategy number: %d",
             strategy);

    *recheck = false;
    PG_RETURN_FLOAT8(key_distance(key, pattern));
}

PG_FUNCTION_INFO_V1(mmddyyyy_gist_fetch);
Datum
mmddyyyy_gist_fetch(PG_FUNCTION_ARGS)
{
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    MmDdYyyyGistKey *key = (MmDdYyyyGistKey *) DatumGetPointer(entry->key);
    MmDdYyyy *value = palloc(sizeof(MmDdYyyy));
    GISTENTRY *result = palloc(sizeof(GISTENTRY));
    char output[MMDDYYYY_LENGTH + 1];

    Assert(key->month_min == key->month_max);
    Assert(key->day_min == key->day_max);
    Assert(key->year_min == key->year_max);
    snprintf(output, sizeof(output), "%02d/%02d/%04d",
             key->month_min, key->day_min, key->year_min);
    memcpy(value->value, output, MMDDYYYY_LENGTH);
    gistentryinit(*result, PointerGetDatum(value),
                  entry->rel, entry->page, entry->offset, false);

    PG_RETURN_POINTER(result);
}