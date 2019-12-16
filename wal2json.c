/*-------------------------------------------------------------------------
 *
 * wal2json.c
 * 		JSON output plugin for changeset extraction
 *
 * Copyright (c) 2013-2019, Euler Taveira de Oliveira
 *
 * IDENTIFICATION
 *		contrib/wal2json/wal2json.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_type.h"

#include "replication/logical.h"

#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/json.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/pg_lsn.h"
#include "utils/rel.h"
#include "utils/syscache.h"

#define	WAL2JSON_FORMAT_VERSION			2
#define	WAL2JSON_FORMAT_MIN_VERSION		1

PG_MODULE_MAGIC;


/*
 * Define a similar macro for older versions of Postgres
 */
#if (PG_VERSION_NUM >= 90600 && PG_VERSION_NUM < 90605) \
  || (PG_VERSION_NUM >= 90500 && PG_VERSION_NUM < 90509)  || (PG_VERSION_NUM >= 90400 && PG_VERSION_NUM < 90414)

#define TupleDescAttr(TUPLEDESC, IDX) TUPLEDESC->attrs[IDX]

#endif

extern void		_PG_init(void);
extern void	PGDLLEXPORT	_PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct
{
	MemoryContext context;
	bool		include_transaction;	/* BEGIN and COMMIT objects (v2) */
	bool		include_xids;		/* include transaction ids */
	bool		include_timestamp;	/* include transaction timestamp */
	bool		include_schemas;	/* qualify tables */
	bool		include_types;		/* include data types */
	bool		include_type_oids;	/* include data type oids */
	bool		include_typmod;		/* include typmod in types */
	bool		include_not_null;	/* include not-null constraints */
    bool        use_key_value_hash; /* Output in column:value format */

	bool		pretty_print;		/* pretty-print JSON? */
	bool		write_in_chunks;	/* write in chunks? */

	List		*filter_tables;		/* filter out tables */
	List		*add_tables;		/* add only these tables */
	List		*filter_msg_prefixes;	/* filter by message prefixes */
	List		*add_msg_prefixes;	/* add only messages with these prefixes */

	int			format_version;		/* support different formats */

	/*
	 * LSN pointing to the end of commit record + 1 (txn->end_lsn)
	 * It is useful for tools that wants a position to restart from.
	 */
	bool		include_lsn;		/* include LSNs */

	uint64		nr_changes;			/* # of passes in pg_decode_change() */
									/* FIXME replace with txn->nentries */

	/* pretty print */
	char		ht[2];				/* horizontal tab, if pretty print */
	char		nl[2];				/* new line, if pretty print */
	char		sp[2];				/* space, if pretty print */
} JsonDecodingData;

typedef enum
{
	PGOUTPUTJSON_CHANGE,
	PGOUTPUTJSON_IDENTITY
} PGOutputJsonKind;

typedef struct SelectTable
{
	char	*schemaname;
	char	*tablename;
	bool	allschemas;				/* true means any schema */
	bool	alltables;				/* true means any table */
} SelectTable;

/* These must be available to pg_dlsym() */
static void pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt, bool is_init);
static void pg_decode_shutdown(LogicalDecodingContext *ctx);
static void pg_decode_begin_txn(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
#if	PG_VERSION_NUM >= 90600
static void pg_decode_message(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif

static bool parse_table_identifier(List *qualified_tables, char separator, List **select_tables);
static bool string_to_SelectTable(char *rawstring, char separator, List **select_tables);
static bool split_string_to_list(char *rawstring, char separator, List **sl);

/* version 1 */
static void pg_decode_begin_txn_v1(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn_v1(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change_v1(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
#if	PG_VERSION_NUM >= 90600
static void pg_decode_message_v1(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif

/* version 2 */
static void pg_decode_begin_txn_v2(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn_v2(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_write_value(LogicalDecodingContext *ctx, Datum value, bool isnull, Oid typid);
static void pg_decode_write_tuple(LogicalDecodingContext *ctx, Relation relation, HeapTuple tuple, PGOutputJsonKind kind);
static void pg_decode_write_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn, Relation relation, ReorderBufferChange *change);
static void pg_decode_change_v2(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
#if	PG_VERSION_NUM >= 90600
static void pg_decode_message_v2(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif

void
_PG_init(void)
{
}

/* Specify output plugin callbacks */
void
_PG_output_plugin_init(OutputPluginCallbacks *cb)
{
	AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);

	cb->startup_cb = pg_decode_startup;
	cb->begin_cb = pg_decode_begin_txn;
	cb->change_cb = pg_decode_change;
	cb->commit_cb = pg_decode_commit_txn;
	cb->shutdown_cb = pg_decode_shutdown;
#if	PG_VERSION_NUM >= 90600
	cb->message_cb = pg_decode_message;
#endif
}

/* Initialize this plugin */
static void
pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt, bool is_init)
{
	ListCell	*option;
	JsonDecodingData *data;
	SelectTable	*t;

	data = palloc0(sizeof(JsonDecodingData));
	data->context = AllocSetContextCreate(TopMemoryContext,
										"wal2json output context",
#if PG_VERSION_NUM >= 90600
										ALLOCSET_DEFAULT_SIZES
#else
										ALLOCSET_DEFAULT_MINSIZE,
										ALLOCSET_DEFAULT_INITSIZE,
										ALLOCSET_DEFAULT_MAXSIZE
#endif
                                        );
	data->include_transaction = true;
	data->include_xids = false;
	data->include_timestamp = false;
	data->include_schemas = true;
	data->include_types = true;
	data->include_type_oids = false;
	data->include_typmod = true;
	data->pretty_print = false;
	data->write_in_chunks = false;
	data->include_lsn = false;
	data->include_not_null = false;
	data->filter_tables = NIL;
	data->filter_msg_prefixes = NIL;
	data->add_msg_prefixes = NIL;

	data->format_version = WAL2JSON_FORMAT_VERSION;

    data->use_key_value_hash = false;

	/* pretty print */
	strcpy(data->ht, "");
	strcpy(data->nl, "");
	strcpy(data->sp, "");

	/* add all tables in all schemas by default */
	t = palloc0(sizeof(SelectTable));
	t->allschemas = true;
	t->alltables = true;
	data->add_tables = lappend(data->add_tables, t);

	data->nr_changes = 0;

	ctx->output_plugin_private = data;

	opt->output_type = OUTPUT_PLUGIN_TEXTUAL_OUTPUT;

	foreach(option, ctx->output_plugin_options)
	{
		DefElem *elem = lfirst(option);

		Assert(elem->arg == NULL || IsA(elem->arg, String));

		if (strcmp(elem->defname, "include-transaction") == 0)
		{
			/* if option value is NULL then assume that value is true */
			if (elem->arg == NULL)
				data->include_transaction = true;
			else if (!parse_bool(strVal(elem->arg), &data->include_transaction))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-xids") == 0)
		{
			/* If option does not provide a value, it means its value is true */
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-xids argument is null");
				data->include_xids = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_xids))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-timestamp") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-timestamp argument is null");
				data->include_timestamp = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_timestamp))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-schemas") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-schemas argument is null");
				data->include_schemas = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_schemas))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-types") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-types argument is null");
				data->include_types = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_types))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-type-oids") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-type-oids argument is null");
				data->include_type_oids = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_type_oids))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-typmod") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-typmod argument is null");
				data->include_typmod = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_typmod))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-not-null") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-not-null argument is null");
				data->include_not_null = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_not_null))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
        else if (strcmp(elem->defname, "use-key-value-hash") == 0)
        {
            if (elem->arg == NULL)
            {
                elog(DEBUG1, "use-key-value-hash is null");
                data->use_key_value_hash = false;
            }
            else if (!parse_bool(strVal(elem->arg), &data->use_key_value_hash))
            {
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("could not parse value for \"%s\" for parameter \"%s\"",
                             strVal(elem->arg), elem->defname)));
            }
        }
		else if (strcmp(elem->defname, "pretty-print") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "pretty-print argument is null");
				data->pretty_print = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->pretty_print))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));

			if (data->pretty_print)
			{
				strncpy(data->ht, "\t", 1);
				strncpy(data->nl, "\n", 1);
				strncpy(data->sp, " ", 1);
			}
		}
		else if (strcmp(elem->defname, "write-in-chunks") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "write-in-chunks argument is null");
				data->write_in_chunks = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->write_in_chunks))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-lsn") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-lsn argument is null");
				data->include_lsn = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_lsn))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-unchanged-toast") == 0)
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_NAME),
					 errmsg("parameter \"%s\" was deprecated", elem->defname)));
		}
		else if (strcmp(elem->defname, "filter-tables") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "filter-tables argument is null");
				data->filter_tables = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!string_to_SelectTable(rawstr, ',', &data->filter_tables))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "add-tables") == 0)
		{
			char	*rawstr;

			/*
			 * If this parameter is specified, remove 'all tables in all
			 * schemas' value from list.
			 */
			list_free_deep(data->add_tables);
			data->add_tables = NIL;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "add-tables argument is null");
				data->add_tables = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!string_to_SelectTable(rawstr, ',', &data->add_tables))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "filter-msg-prefixes") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "filter-msg-prefixes argument is null");
				data->filter_msg_prefixes = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!split_string_to_list(rawstr, ',', &data->filter_msg_prefixes))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "add-msg-prefixes") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "add-msg-prefixes argument is null");
				data->add_msg_prefixes = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!split_string_to_list(rawstr, ',', &data->add_msg_prefixes))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "format-version") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "format-version argument is null");
				data->format_version = WAL2JSON_FORMAT_VERSION;
			}
			else if (!parse_int(strVal(elem->arg), &data->format_version, 0, NULL))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));

			if (data->format_version > WAL2JSON_FORMAT_VERSION)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("client sent format_version=%d but we only support format %d or lower",
						 data->format_version, WAL2JSON_FORMAT_VERSION)));

			if (data->format_version < WAL2JSON_FORMAT_MIN_VERSION)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("client sent format_version=%d but we only support format %d or higher",
						 data->format_version, WAL2JSON_FORMAT_MIN_VERSION)));
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("option \"%s\" = \"%s\" is unknown",
						elem->defname,
						elem->arg ? strVal(elem->arg) : "(null)")));
		}
	}

	elog(DEBUG2, "format version: %d", data->format_version);
}

/* cleanup this plugin's resources */
static void
pg_decode_shutdown(LogicalDecodingContext *ctx)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* cleanup our own resources via memory context reset */
	MemoryContextDelete(data->context);
}

/* BEGIN callback */
static void
pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	if (data->format_version == 2)
		pg_decode_begin_txn_v2(ctx, txn);
	else if (data->format_version == 1)
		pg_decode_begin_txn_v1(ctx, txn);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_begin_txn_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	data->nr_changes = 0;

	/* Transaction starts */
	OutputPluginPrepareWrite(ctx, true);

	appendStringInfo(ctx->out, "{%s", data->nl);

	if (data->include_xids)
		appendStringInfo(ctx->out, "%s\"xid\":%s%u,%s", data->ht, data->sp, txn->xid, data->nl);

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, txn->end_lsn));

		appendStringInfo(ctx->out, "%s\"nextlsn\":%s\"%s\",%s", data->ht, data->sp, lsn_str, data->nl);

		pfree(lsn_str);
	}

	if (data->include_timestamp)
		appendStringInfo(ctx->out, "%s\"timestamp\":%s\"%s\",%s", data->ht, data->sp, timestamptz_to_str(txn->commit_time), data->nl);

	appendStringInfo(ctx->out, "%s\"change\":%s[", data->ht, data->sp);

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
}

static void
pg_decode_begin_txn_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* don't include BEGIN object */
	if (!data->include_transaction)
		return;

	OutputPluginPrepareWrite(ctx, true);
	appendStringInfoString(ctx->out, "{\"action\":\"B\"");
	if (data->include_xids)
		appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);
	if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, txn->final_lsn));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	appendStringInfoChar(ctx->out, '}');
	OutputPluginWrite(ctx, true);
}

/* COMMIT callback */
static void
pg_decode_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	if (data->format_version == 2)
		pg_decode_commit_txn_v2(ctx, txn, commit_lsn);
	else if (data->format_version == 1)
		pg_decode_commit_txn_v1(ctx, txn, commit_lsn);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_commit_txn_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	if (txn->has_catalog_changes)
		elog(DEBUG2, "txn has catalog changes: yes");
	else
		elog(DEBUG2, "txn has catalog changes: no");
	elog(DEBUG2, "my change counter: %lu ; # of changes: %lu ; # of changes in memory: %lu", data->nr_changes, txn->nentries, txn->nentries_mem);
	elog(DEBUG2, "# of subxacts: %d", txn->nsubtxns);

	/* Transaction ends */
	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks)
		appendStringInfo(ctx->out, "%s", data->nl);

	appendStringInfo(ctx->out, "%s]%s}", data->ht, data->nl);

	OutputPluginWrite(ctx, true);
}

static void
pg_decode_commit_txn_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* don't include COMMIT object */
	if (!data->include_transaction)
		return;

	OutputPluginPrepareWrite(ctx, true);
	appendStringInfoString(ctx->out, "{\"action\":\"C\"");
	if (data->include_xids)
		appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);
	if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, commit_lsn));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	appendStringInfoChar(ctx->out, '}');
	OutputPluginWrite(ctx, true);
}


/*
 * Accumulate tuple information and stores it at the end
 *
 * replident: is this tuple a replica identity?
 * hasreplident: does this tuple has an associated replica identity?
 */
static void
tuple_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, TupleDesc indexdesc, bool replident, bool hasreplident)
{
	JsonDecodingData	*data;
	int					natt;

	StringInfoData		colnames;
	StringInfoData		coltypes;
	StringInfoData		coltypeoids;
	StringInfoData		colnotnulls;
	StringInfoData		colvalues;
	char				comma[3] = "";

	data = ctx->output_plugin_private;

	initStringInfo(&colnames);
	initStringInfo(&coltypes);
	if (data->include_type_oids)
		initStringInfo(&coltypeoids);
	if (data->include_not_null)
		initStringInfo(&colnotnulls);
	initStringInfo(&colvalues);

	/*
	 * If replident is true, it will output info about replica identity. In this
	 * case, there are special JSON objects for it. Otherwise, it will print new
	 * tuple data.
	 */
	if (replident)
	{
		appendStringInfo(&colnames, "%s%s%s\"oldkeys\":%s{%s", data->ht, data->ht, data->ht, data->sp, data->nl);
		appendStringInfo(&colnames, "%s%s%s%s\"keynames\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&coltypes, "%s%s%s%s\"keytypes\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "%s%s%s%s\"keytypeoids\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&colvalues, "%s%s%s%s\"keyvalues\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
	}
	else
	{
		appendStringInfo(&colnames, "%s%s%s\"columnnames\":%s[", data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&coltypes, "%s%s%s\"columntypes\":%s[", data->ht, data->ht, data->ht, data->sp);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "%s%s%s\"columntypeoids\":%s[", data->ht, data->ht, data->ht, data->sp);
		if (data->include_not_null)
			appendStringInfo(&colnotnulls, "%s%s%s\"columnoptionals\":%s[", data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&colvalues, "%s%s%s\"columnvalues\":%s[", data->ht, data->ht, data->ht, data->sp);
	}

	/* Print column information (name, type, value) */
	for (natt = 0; natt < tupdesc->natts; natt++)
	{
		Form_pg_attribute	attr;		/* the attribute itself */
		Oid					typid;		/* type of current attribute */
		HeapTuple			type_tuple;	/* information about a type */
		Oid					typoutput;	/* output function */
		bool				typisvarlena;
		Datum				origval;	/* possibly toasted Datum */
		Datum				val;		/* definitely detoasted Datum */
		char				*outputstr = NULL;
		bool				isnull;		/* column is null? */

		/*
		 * Commit d34a74dd064af959acd9040446925d9d53dff15b introduced
		 * TupleDescAttr() in back branches. If the version supports
		 * this macro, use it. Version 10 and later already support it.
		 */
#if (PG_VERSION_NUM >= 90600 && PG_VERSION_NUM < 90605) || (PG_VERSION_NUM >= 90500 && PG_VERSION_NUM < 90509) || (PG_VERSION_NUM >= 90400 && PG_VERSION_NUM < 90414)
		attr = tupdesc->attrs[natt];
#else
		attr = TupleDescAttr(tupdesc, natt);
#endif

		elog(DEBUG1, "attribute \"%s\" (%d/%d)", NameStr(attr->attname), natt, tupdesc->natts);

		/* Do not print dropped or system columns */
		if (attr->attisdropped || attr->attnum < 0)
			continue;

		/* Search indexed columns in whole heap tuple */
		if (indexdesc != NULL)
		{
			int		j;
			bool	found_col = false;

			for (j = 0; j < indexdesc->natts; j++)
			{
				Form_pg_attribute	iattr;

				/* See explanation a few lines above. */
#if (PG_VERSION_NUM >= 90600 && PG_VERSION_NUM < 90605) || (PG_VERSION_NUM >= 90500 && PG_VERSION_NUM < 90509) || (PG_VERSION_NUM >= 90400 && PG_VERSION_NUM < 90414)
				iattr = indexdesc->attrs[j];
#else
				iattr = TupleDescAttr(indexdesc, j);
#endif

				if (strcmp(NameStr(attr->attname), NameStr(iattr->attname)) == 0)
					found_col = true;

			}

			/* Print only indexed columns */
			if (!found_col)
				continue;
		}

		/* Get Datum from tuple */
		origval = heap_getattr(tuple, natt + 1, tupdesc, &isnull);

		/* Skip nulls iif printing key/identity */
		if (isnull && replident)
			continue;

		typid = attr->atttypid;

		/* Figure out type name */
		type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(type_tuple))
			elog(ERROR, "cache lookup failed for type %u", typid);

		/* Get information needed for printing values of a type */
		getTypeOutputInfo(typid, &typoutput, &typisvarlena);

		/* XXX Unchanged TOAST Datum does not need to be output */
		if (!isnull && typisvarlena && VARATT_IS_EXTERNAL_ONDISK(origval))
		{
			elog(DEBUG1, "column \"%s\" has an unchanged TOAST", NameStr(attr->attname));
			continue;
		}

		/* Accumulate each column info */
		appendStringInfo(&colnames, "%s", comma);
		escape_json(&colnames, NameStr(attr->attname));

		if (data->include_types)
		{
			if (data->include_typmod)
			{
				char	*type_str;

				type_str = TextDatumGetCString(DirectFunctionCall2(format_type, attr->atttypid, attr->atttypmod));
				appendStringInfo(&coltypes, "%s", comma);
				escape_json(&coltypes, type_str);
				pfree(type_str);
			}
			else
			{
				Form_pg_type type_form = (Form_pg_type) GETSTRUCT(type_tuple);
				appendStringInfo(&coltypes, "%s", comma);
				escape_json(&coltypes, NameStr(type_form->typname));
			}

			/* oldkeys doesn't print not-null constraints */
			if (!replident && data->include_not_null)
			{
				if (attr->attnotnull)
					appendStringInfo(&colnotnulls, "%sfalse", comma);
				else
					appendStringInfo(&colnotnulls, "%strue", comma);
			}
		}

		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "%s%u", comma, typid);

		ReleaseSysCache(type_tuple);

		if (isnull)
		{
			appendStringInfo(&colvalues, "%snull", comma);
		}
		else
		{
			if (typisvarlena)
				val = PointerGetDatum(PG_DETOAST_DATUM(origval));
			else
				val = origval;

			/* Finally got the value */
			outputstr = OidOutputFunctionCall(typoutput, val);

			/*
			 * Data types are printed with quotes unless they are number, true,
			 * false, null, an array or an object.
			 *
			 * The NaN and Infinity are not valid JSON symbols. Hence,
			 * regardless of sign they are represented as the string null.
			 */
			switch (typid)
			{
				case INT2OID:
				case INT4OID:
				case INT8OID:
				case OIDOID:
				case FLOAT4OID:
				case FLOAT8OID:
				case NUMERICOID:
					if (pg_strncasecmp(outputstr, "NaN", 3) == 0 ||
							pg_strncasecmp(outputstr, "Infinity", 8) == 0 ||
							pg_strncasecmp(outputstr, "-Infinity", 9) == 0)
					{
						appendStringInfo(&colvalues, "%snull", comma);
						elog(DEBUG1, "attribute \"%s\" is special: %s", NameStr(attr->attname), outputstr);
					}
					else if (strspn(outputstr, "0123456789+-eE.") == strlen(outputstr))
						appendStringInfo(&colvalues, "%s%s", comma, outputstr);
					else
						elog(ERROR, "%s is not a number", outputstr);
					break;
				case BOOLOID:
					if (strcmp(outputstr, "t") == 0)
						appendStringInfo(&colvalues, "%strue", comma);
					else
						appendStringInfo(&colvalues, "%sfalse", comma);
					break;
				case BYTEAOID:
					appendStringInfo(&colvalues, "%s", comma);
					/* string is "\x54617069727573", start after "\x" */
					escape_json(&colvalues, (outputstr + 2));
					break;
				default:
					appendStringInfo(&colvalues, "%s", comma);
					escape_json(&colvalues, outputstr);
					break;
			}
		}

		/* The first column does not have comma */
		if (strcmp(comma, "") == 0)
			snprintf(comma, 3, ",%s", data->sp);
	}

	/* Column info ends */
	if (replident)
	{
		appendStringInfo(&colnames, "],%s", data->nl);
		if (data->include_types)
			appendStringInfo(&coltypes, "],%s", data->nl);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "],%s", data->nl);
		appendStringInfo(&colvalues, "]%s", data->nl);
		appendStringInfo(&colvalues, "%s%s%s}%s", data->ht, data->ht, data->ht, data->nl);
	}
	else
	{
		appendStringInfo(&colnames, "],%s", data->nl);
		if (data->include_types)
			appendStringInfo(&coltypes, "],%s", data->nl);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "],%s", data->nl);
		if (data->include_not_null)
			appendStringInfo(&colnotnulls, "],%s", data->nl);
		if (hasreplident)
			appendStringInfo(&colvalues, "],%s", data->nl);
		else
			appendStringInfo(&colvalues, "]%s", data->nl);
	}

	/* Print data */
	appendStringInfoString(ctx->out, colnames.data);
	if (data->include_types)
		appendStringInfoString(ctx->out, coltypes.data);
	if (data->include_type_oids)
		appendStringInfoString(ctx->out, coltypeoids.data);
	if (data->include_not_null)
		appendStringInfoString(ctx->out, colnotnulls.data);
	appendStringInfoString(ctx->out, colvalues.data);

	pfree(colnames.data);
	pfree(coltypes.data);
	if (data->include_type_oids)
		pfree(coltypeoids.data);
	if (data->include_not_null)
		pfree(colnotnulls.data);
	pfree(colvalues.data);
}

/* Parse tuple information into a hashmap
 *
 * replident: is this tuple a replica identity?
 * hasreplident: does this tuple has an associated replica identity?
 */
static void
tuple_to_hashmap(LogicalDecodingContext *ctx, TupleDesc tupdesc,
                 HeapTuple tuple, TupleDesc indexdesc, bool replident,
                 bool hasreplident)
{

    JsonDecodingData      *data;
    int                   natt;
    StringInfoData        coldata;
    StringInfoData        coltypedata;
    StringInfoData        coltypeoids;
    StringInfoData        colnotnulls;

    char                  comma[3] = "";

    data = ctx->output_plugin_private;
    initStringInfo(&coldata);
    initStringInfo(&coltypedata);

    if (data->include_type_oids)
        initStringInfo(&coltypeoids);
    if(data->include_not_null)
        initStringInfo(&colnotnulls);

    /* If replident is true, output replica identify information */
    if (replident)
    {
        appendStringInfo(
            &coldata, "%s%s%s\"oldkeys\":%s{",
            data->ht, data->ht, data->ht, data->sp);
        appendStringInfo(
            &coltypedata, "%s%s%s\"keytypes\":%s{",
            data->ht, data->ht, data->ht, data->sp);

        if (data->include_type_oids)
            appendStringInfo(
                &coltypeoids, "%s%s%s\"keytypeoids\":%s{",
                data->ht, data->ht, data->ht, data->sp);
    }
    else
    {
        appendStringInfo(
            &coldata, "%s%s%s\"changes\":%s{",
            data->ht, data->ht, data->ht, data->sp);
        appendStringInfo(
            &coltypedata, "%s%s%s\"columntypes\":%s{",
            data->ht, data->ht, data->ht, data->sp);

        if (data->include_type_oids)
            appendStringInfo(
                &coltypeoids, "%s%s%s\"coltypeoids\":%s{",
                data->ht, data->ht, data->ht, data->sp);

        if (data->include_not_null)
            appendStringInfo(
                &colnotnulls, "%s%s%s\"columnoptionals\":%s{",
                data->ht, data->ht, data->ht, data->sp);
    }

    /* Render column information */
    for (natt = 0; natt < tupdesc->natts; natt++)
    {
        Form_pg_attribute	attr;		/* the attribute itself */
        Oid				    typid;		/* type of current attribute */
        HeapTuple			type_tuple;	/* information about a type */
        Oid			    	typoutput;	/* output function */
        bool				typisvarlena;
        Datum				origval;	/* possibly toasted Datum */
        Datum				val;		/* definitely detoasted Datum */
        char				*outputstr = NULL;
        bool				isnull;		/* column is null? */

		attr = TupleDescAttr(tupdesc, natt);

		elog(DEBUG1, "attribute \"%s\" (%d/%d)", NameStr(attr->attname), natt, tupdesc->natts);

		/* Do not print dropped or system columns */
		if (attr->attisdropped || attr->attnum < 0)
			continue;

        /* Search indexed columns in whole heap tuple */
        if (indexdesc != NULL)
        {
            int  j;
            bool found_col = false;

            for (j = 0; j < indexdesc->natts; j++)
            {
                Form_pg_attribute   iattr;

                iattr = TupleDescAttr(indexdesc, j);

                if (strcmp(NameStr(attr->attname), NameStr(iattr->attname)) == 0)
                {
                    found_col = true;
                }
            }
            if (!found_col)
            {
                continue;
            }
        }

        /* Get Datum from the tuple */
        origval = heap_getattr(tuple, natt + 1, tupdesc, &isnull);
        if (isnull && replident)
            continue;

        typid = attr->atttypid;

        /* Determine the type name */
        type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
        if (!HeapTupleIsValid(type_tuple))
        {
            elog(ERROR, "cache lookup failed for type %u", typid);
        }

        /* Get information needed for printing values of a type */
		getTypeOutputInfo(typid, &typoutput, &typisvarlena);

        /* If the TOAST is unchanged, do not output */
        if (!isnull && typisvarlena && VARATT_IS_EXTERNAL_ONDISK(origval))
        {
            elog(DEBUG1, "column \"%s\" has an unchanged TOAST", NameStr(attr->attname));
			continue;
        }

        /* Begin bulding out the hashmaps for display */
        appendStringInfo(
            &coldata, "%s%s%s%s%s%s",
            comma, data->nl, data->ht, data->ht, data->ht, data->ht);
        escape_json(&coldata, NameStr(attr->attname));
        appendStringInfo(&coldata, ":%s", data->sp);

        if (data->include_types)
        {
            appendStringInfo(
                &coltypedata, "%s%s%s%s%s%s",
                comma, data->nl, data->ht, data->ht, data->ht, data->ht);
            escape_json(&coltypedata, NameStr(attr->attname));
            appendStringInfo(&coltypedata, ":%s", data->sp);

            if (data->include_typmod)
            {
                char   *type_str;

                type_str = TextDatumGetCString(DirectFunctionCall2(
                                                   format_type,
                                                   attr->atttypid,
                                                   attr->atttypmod));
                escape_json(&coltypedata, type_str);
                pfree(type_str);
            }
            else
            {
                Form_pg_type type_form = (Form_pg_type) GETSTRUCT(type_tuple);
                escape_json(&coltypedata, NameStr(type_form->typname));
            }

            if (data->include_type_oids)
            {
                appendStringInfo(
                    &coltypeoids, "%s%s%s%s%s%s",
                    comma, data->nl, data->ht, data->ht, data->ht, data->ht);
                escape_json(&coltypeoids, NameStr(attr->attname));
                appendStringInfo(&coltypeoids, ":%s%u", data->sp, typid);
            }

            if (!replident && data->include_not_null)
            {

                appendStringInfo(
                    &colnotnulls, "%s%s%s%s%s%s",
                    comma, data->nl, data->ht, data->ht, data->ht, data->ht);
                escape_json(&colnotnulls, NameStr(attr->attname));
                appendStringInfo(&colnotnulls, ":%s", data->sp);

                if (attr->attnotnull)
                    appendStringInfo(&colnotnulls, "false");
                else
                    appendStringInfo(&colnotnulls, "true");

            }

        }

        ReleaseSysCache(type_tuple);
        if (isnull)
        {
            appendStringInfo(&coldata, "null");
            if (strcmp(comma, "") == 0)
                snprintf(comma, 3, ",%s", data->sp);
            continue;
        }

        if (typisvarlena)
            val = PointerGetDatum(PG_DETOAST_DATUM(origval));
        else
            val = origval;

        /* Finally got the value */
        outputstr = OidOutputFunctionCall(typoutput, val);

        /*
         * Data types are printed with quotes unless they are number, true,
         * false, null, an array or an object.
         *
         * The NaN and Infinity are not valid JSON symbols. Hence,
         * regardless of sign they are represented as the string null.
         */
        switch (typid)
        {
        case INT2OID:
        case INT4OID:
        case INT8OID:
        case OIDOID:
        case FLOAT4OID:
        case FLOAT8OID:
        case NUMERICOID:
            if (pg_strncasecmp(outputstr, "NaN", 3) == 0 ||
                pg_strncasecmp(outputstr, "Infinity", 8) == 0 ||
                pg_strncasecmp(outputstr, "-Infinity", 9) == 0)
            {
                appendStringInfo(&coldata, "null");
                elog(DEBUG1, "attribute \"%s\" is special: %s",
                     NameStr(attr->attname), outputstr);
            }
            else if (strspn(outputstr, "0123456789+-eE.") == strlen(outputstr))
                appendStringInfo(&coldata, "%s", outputstr);
            else
                elog(ERROR, "%s is not a number", outputstr);
            break;
        case BOOLOID:
            if (strcmp(outputstr, "t") == 0)
                appendStringInfo(&coldata, "true");
            else
                appendStringInfo(&coldata, "false");
            break;
        case BYTEAOID:
            /* string is "\x54617069727573", start after "\x" */
            escape_json(&coldata, (outputstr + 2));
            break;
        default:
            escape_json(&coldata, outputstr);
            break;
        }

		/* The first column does not have comma */
		if (strcmp(comma, "") == 0)
			snprintf(comma, 3, ",%s", data->sp);
    }

    appendStringInfo(
        &coldata, "%s%s%s%s}", data->nl, data->ht, data->ht, data->ht);

    if (data->include_types)
    {
        appendStringInfo(&coldata, "%s%s", comma, data->nl);
        appendStringInfo(
            &coltypedata, "%s%s%s%s}", data->nl, data->ht, data->ht, data->ht);
    }

    if (data->include_type_oids)
    {
        appendStringInfo(&coltypedata, "%s%s", comma, data->nl);
        appendStringInfo(
            &coltypeoids, "%s%s%s%s}", data->nl, data->ht, data->ht, data->ht);
    }

    if (!replident && data->include_not_null)
    {
        appendStringInfo(&coltypeoids, "%s%s", comma, data->nl);
        appendStringInfo(
            &colnotnulls, "%s%s%s%s}", data->nl, data->ht, data->ht, data->ht);
    }

    appendStringInfoString(ctx->out, coldata.data);
    if (data->include_types)
        appendStringInfoString(ctx->out, coltypedata.data);
    if (data->include_type_oids)
        appendStringInfoString(ctx->out, coltypeoids.data);
    if (!replident && data->include_not_null)
        appendStringInfoString(ctx->out, colnotnulls.data);

    if (hasreplident)
        appendStringInfoString(ctx->out, ",");

    appendStringInfoString(ctx->out, data->nl);

    pfree(coldata.data);
    pfree(coltypedata.data);

    if (data->include_type_oids)
        pfree(coltypeoids.data);
    if (data->include_not_null)
        pfree(colnotnulls.data);
}

/* Print columns information */
static void
columns_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, bool hasreplident)
{
    JsonDecodingData *data;
    data = ctx->output_plugin_private;

    if (!data->use_key_value_hash)
        tuple_to_stringinfo(ctx, tupdesc, tuple, NULL, false, hasreplident);
    else
        tuple_to_hashmap(ctx, tupdesc, tuple, NULL, false, hasreplident);
}

/* Print replica identity information */
static void
identity_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, TupleDesc indexdesc)
{
    JsonDecodingData *data;
    data = ctx->output_plugin_private;

    /* Last parameter does not matter */
    if (!data->use_key_value_hash)
        tuple_to_stringinfo(ctx, tupdesc, tuple, indexdesc, true, false);
    else
        tuple_to_hashmap(ctx, tupdesc, tuple, indexdesc, true, false);

}

/* Callback for individual changed tuples */
static void
pg_decode_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	if (data->format_version == 2)
		pg_decode_change_v2(ctx, txn, relation, change);
	else if (data->format_version == 1)
		pg_decode_change_v1(ctx, txn, relation, change);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_change_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data;
	Form_pg_class class_form;
	TupleDesc	tupdesc;
	MemoryContext old;

	Relation	indexrel;
	TupleDesc	indexdesc;

	char		*schemaname;
	char		*tablename;

	AssertVariableIsOfType(&pg_decode_change, LogicalDecodeChangeCB);

	data = ctx->output_plugin_private;
	class_form = RelationGetForm(relation);
	tupdesc = RelationGetDescr(relation);

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/* schema and table names are used for select tables */
	schemaname = get_namespace_name(class_form->relnamespace);
	tablename = NameStr(class_form->relname);

	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	/* Make sure rd_replidindex is set */
	RelationGetIndexList(relation);

	/* Filter tables, if available */
	if (list_length(data->filter_tables) > 0)
	{
		ListCell	*lc;

		foreach(lc, data->filter_tables)
		{
			SelectTable	*t = lfirst(lc);

			if (t->allschemas || strcmp(t->schemaname, schemaname) == 0)
			{
				if (t->alltables || strcmp(t->tablename, tablename) == 0)
				{
					elog(DEBUG2, "\"%s\".\"%s\" was filtered out",
								((t->allschemas) ? "*" : t->schemaname),
								((t->alltables) ? "*" : t->tablename));
					return;
				}
			}
		}
	}

	/* Add tables */
	if (list_length(data->add_tables) > 0)
	{
		ListCell	*lc;
		bool		skip = true;

		/* all tables in all schemas are added by default */
		foreach(lc, data->add_tables)
		{
			SelectTable	*t = lfirst(lc);

			if (t->allschemas || strcmp(t->schemaname, schemaname) == 0)
			{
				if (t->alltables || strcmp(t->tablename, tablename) == 0)
				{
					elog(DEBUG2, "\"%s\".\"%s\" was added",
								((t->allschemas) ? "*" : t->schemaname),
								((t->alltables) ? "*" : t->tablename));
					skip = false;
				}
			}
		}

		/* table was not found */
		if (skip)
			return;
	}

	/* Sanity checks */
	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for INSERT in table \"%s\"", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			/*
			 * Bail out iif:
			 * (i) doesn't have a pk and replica identity is not full;
			 * (ii) replica identity is nothing.
			 */
			if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
			{
				/* FIXME this sentence is imprecise */
				elog(WARNING, "table \"%s\" without primary key or replica identity is nothing", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}

			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for UPDATE in table \"%s\"", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			/*
			 * Bail out iif:
			 * (i) doesn't have a pk and replica identity is not full;
			 * (ii) replica identity is nothing.
			 */
			if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
			{
				/* FIXME this sentence is imprecise */
				elog(WARNING, "table \"%s\" without primary key or replica identity is nothing", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}

			if (change->data.tp.oldtuple == NULL)
			{
				elog(WARNING, "no tuple data for DELETE in table \"%s\"", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}
			break;
		default:
			Assert(false);
	}

	/* Change counter */
	data->nr_changes++;

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks)
		appendStringInfo(ctx->out, "%s", data->nl);

	appendStringInfo(ctx->out, "%s%s", data->ht, data->ht);

	if (data->nr_changes > 1)
		appendStringInfoChar(ctx->out, ',');

	appendStringInfo(ctx->out, "{%s", data->nl);

	/* Print change kind */
	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			appendStringInfo(ctx->out, "%s%s%s\"kind\":%s\"insert\",%s", data->ht, data->ht, data->ht, data->sp, data->nl);
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			appendStringInfo(ctx->out, "%s%s%s\"kind\":%s\"update\",%s", data->ht, data->ht, data->ht, data->sp, data->nl);
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			appendStringInfo(ctx->out, "%s%s%s\"kind\":%s\"delete\",%s", data->ht, data->ht, data->ht, data->sp, data->nl);
			break;
		default:
			Assert(false);
	}

	/* Print table name (possibly) qualified */
	if (data->include_schemas)
	{
		appendStringInfo(ctx->out, "%s%s%s\"schema\":%s", data->ht, data->ht, data->ht, data->sp);
		escape_json(ctx->out, get_namespace_name(class_form->relnamespace));
		appendStringInfo(ctx->out, ",%s", data->nl);
	}
	appendStringInfo(ctx->out, "%s%s%s\"table\":%s", data->ht, data->ht, data->ht, data->sp);
	escape_json(ctx->out, NameStr(class_form->relname));
	appendStringInfo(ctx->out, ",%s", data->nl);

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			/* Print the new tuple */
			columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, false);
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			/* Print the new tuple */
			columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, true);

			/*
			 * The old tuple is available when:
			 * (i) pk changes;
			 * (ii) replica identity is full;
			 * (iii) replica identity is index and indexed column changes.
			 *
			 * FIXME if old tuple is not available we must get only the indexed
			 * columns (the whole tuple is printed).
			 */
			if (change->data.tp.oldtuple == NULL)
			{
				elog(DEBUG1, "old tuple is null");

				indexrel = RelationIdGetRelation(relation->rd_replidindex);
				if (indexrel != NULL)
				{
					indexdesc = RelationGetDescr(indexrel);
					identity_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, indexdesc);
					RelationClose(indexrel);
				}
				else
				{
					identity_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, NULL);
				}
			}
			else
			{
				elog(DEBUG1, "old tuple is not null");
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, NULL);
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			/* Print the replica identity */
			indexrel = RelationIdGetRelation(relation->rd_replidindex);
			if (indexrel != NULL)
			{
				indexdesc = RelationGetDescr(indexrel);
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, indexdesc);
				RelationClose(indexrel);
			}
			else
			{
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, NULL);
			}

			if (change->data.tp.oldtuple == NULL)
				elog(DEBUG1, "old tuple is null");
			else
				elog(DEBUG1, "old tuple is not null");
			break;
		default:
			Assert(false);
	}

	appendStringInfo(ctx->out, "%s%s}", data->ht, data->ht);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
}

static void
pg_decode_write_value(LogicalDecodingContext *ctx, Datum value, bool isnull, Oid typid)
{
	Oid		typoutfunc;
	bool	isvarlena;
	char	*outstr;

	if (isnull)
	{
		appendStringInfoString(ctx->out, "null");
		return;
	}

	/* get type information and call its output function */
	getTypeOutputInfo(typid, &typoutfunc, &isvarlena);

	/* XXX dead code? check is one level above. */
	if (isvarlena && VARATT_IS_EXTERNAL_ONDISK(value))
	{
		elog(DEBUG1, "unchanged TOAST Datum");
		return;
	}

	/* if value is varlena, detoast Datum */
	if (isvarlena)
	{
		Datum	detoastedval;

		detoastedval = PointerGetDatum(PG_DETOAST_DATUM(value));
		outstr = OidOutputFunctionCall(typoutfunc, detoastedval);
	}
	else
	{
		outstr = OidOutputFunctionCall(typoutfunc, value);
	}

	/*
	 * Data types are printed with quotes unless they are number, true, false,
	 * null, an array or an object.
	 *
	 * The NaN an Infinity are not valid JSON symbols. Hence, regardless of
	 * sign they are represented as the string null.
	 */
	switch (typid)
	{
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case OIDOID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
			if (pg_strncasecmp(outstr, "NaN", 3) == 0 ||
					pg_strncasecmp(outstr, "Infinity", 8) == 0 ||
					pg_strncasecmp(outstr, "-Infinity", 9) == 0)
			{
				appendStringInfoString(ctx->out, "null");
				elog(DEBUG1, "special value: %s", outstr);
			}
			else if (strspn(outstr, "0123456789+-eE.") == strlen(outstr))
				appendStringInfo(ctx->out, "%s", outstr);
			else
				elog(ERROR, "%s is not a number", outstr);
			break;
		case BOOLOID:
			if (strcmp(outstr, "t") == 0)
				appendStringInfoString(ctx->out, "true");
			else
				appendStringInfoString(ctx->out, "false");
			break;
		case BYTEAOID:
			/* string is "\x54617069727573", start after \x */
			escape_json(ctx->out, (outstr + 2));
			break;
		default:
			escape_json(ctx->out, outstr);
			break;
	}

	pfree(outstr);
}

static void
pg_decode_write_tuple(LogicalDecodingContext *ctx, Relation relation, HeapTuple tuple, PGOutputJsonKind kind)
{
	JsonDecodingData	*data;
	TupleDesc			tupdesc;
	Relation			idxrel;
	TupleDesc			idxdesc = NULL;
	int					i;
	Datum				*values;
	bool				*nulls;
	bool				need_sep = false;

	data = ctx->output_plugin_private;

	tupdesc = RelationGetDescr(relation);
	values = (Datum *) palloc(tupdesc->natts * sizeof(Datum));
	nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));

	/* break down the tuple into fields */
	heap_deform_tuple(tuple, tupdesc, values, nulls);

	/* figure out replica identity columns */
	if (kind == PGOUTPUTJSON_IDENTITY)
	{
		if (OidIsValid(relation->rd_replidindex))		/* REPLICA IDENTITY INDEX */
		{
			idxrel = RelationIdGetRelation(relation->rd_replidindex);
			idxdesc = RelationGetDescr(idxrel);
		}
#if	PG_VERSION_NUM >= 100000
		else if (OidIsValid(relation->rd_pkindex))	/* REPLICA IDENTITY DEFAULT + PK (10+) */
		{
			idxrel = RelationIdGetRelation(relation->rd_pkindex);
			idxdesc = RelationGetDescr(idxrel);
		}
#else
		else if (relation->rd_rel->relreplident == REPLICA_IDENTITY_DEFAULT && OidIsValid(relation->rd_replidindex))	/* 9.4, 9.5 and 9.6 */
		{
			idxrel = RelationIdGetRelation(relation->rd_replidindex);
			idxdesc = RelationGetDescr(idxrel);
		}
#endif
		else if (relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
			elog(ERROR, "table does not have primary key or replica identity");
	}

	for (i = 0; i < tupdesc->natts; i++)
	{
		Form_pg_attribute	attr;
		int					j;
		bool				found = false;
		char				*type_str;

		attr = TupleDescAttr(tupdesc, i);

		/* skip dropped or system columns */
		if (attr->attisdropped || attr->attnum < 0)
			continue;

		/*
		 * oldtuple contains NULL on those values that are not defined by
		 * REPLICA IDENTITY. In this case, print only non-null values.
		 */
		if (nulls[i] && kind == PGOUTPUTJSON_IDENTITY)
			continue;

		/* don't send unchanged TOAST Datum */
		if (!nulls[i] && attr->attlen == -1 && VARATT_IS_EXTERNAL_ONDISK(values[i]))
			continue;

		/*
		 * Is it replica identity column? Print only those columns or all
		 * columns if REPLICA IDENTITY FULL is set.
		 */
		if (kind == PGOUTPUTJSON_IDENTITY && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
		{
			for (j = 0; j < idxdesc->natts; j++)
			{
				Form_pg_attribute	iattr;

				iattr = TupleDescAttr(idxdesc, j);
				if (strcmp(NameStr(attr->attname), NameStr(iattr->attname)) == 0)
					found = true;
			}

			if (!found)
				continue;
		}

		if (need_sep)
			appendStringInfoChar(ctx->out, ',');
		need_sep = true;

		appendStringInfoChar(ctx->out, '{');
		appendStringInfoString(ctx->out, "\"name\":");
		escape_json(ctx->out, NameStr(attr->attname));

		/* type name (with typmod, if available) */
		if (data->include_types)
		{
			type_str = format_type_with_typemod(attr->atttypid, attr->atttypmod);
			appendStringInfoString(ctx->out, ",\"type\":");
			appendStringInfo(ctx->out, "\"%s\"", type_str);
			pfree(type_str);
		}

		appendStringInfoString(ctx->out, ",\"value\":");
		pg_decode_write_value(ctx, values[i], nulls[i], attr->atttypid);

		/*
		 * Print optional for columns. This information is redundant for
		 * replica identity (index) because all attributes are not null.
		 */
		if (kind == PGOUTPUTJSON_CHANGE && data->include_not_null)
		{
			if (attr->attnotnull)
				appendStringInfoString(ctx->out, ",\"optional\":false");
			else
				appendStringInfoString(ctx->out, ",\"optional\":true");
		}

		appendStringInfoChar(ctx->out, '}');
	}

	pfree(values);
	pfree(nulls);
}

static void
pg_decode_write_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn, Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* make sure rd_pkindex and rd_replidindex are set */
	RelationGetIndexList(relation);

	/* sanity checks */
	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for INSERT in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
				return;
			}
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for UPDATE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
				return;
			}
			if (change->data.tp.oldtuple == NULL)
			{
				if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
				{
					elog(WARNING, "no tuple identifier for UPDATE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
					return;
				}
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			if (change->data.tp.oldtuple == NULL)
			{
				if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
				{
					elog(WARNING, "no tuple identifier for DELETE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
					return;
				}
			}
			break;
		default:
			Assert(false);
	}

	OutputPluginPrepareWrite(ctx, true);

	appendStringInfoChar(ctx->out, '{');

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			appendStringInfoString(ctx->out, "\"action\":\"I\"");
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			appendStringInfoString(ctx->out, "\"action\":\"U\"");
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			appendStringInfoString(ctx->out, "\"action\":\"D\"");
			break;
		default:
			Assert(false);
	}

	if (data->include_xids)
		appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);

	if (data->include_timestamp)
		appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, change->lsn));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	if (data->include_schemas)
	{
		appendStringInfo(ctx->out, ",\"schema\":");
		escape_json(ctx->out, get_namespace_name(RelationGetNamespace(relation)));
	}

	appendStringInfo(ctx->out, ",\"table\":");
	escape_json(ctx->out, RelationGetRelationName(relation));

	/* print new tuple (INSERT, UPDATE) */
	if (change->data.tp.newtuple != NULL)
	{
		appendStringInfoString(ctx->out, ",\"columns\":[");
		pg_decode_write_tuple(ctx, relation, &change->data.tp.newtuple->tuple, PGOUTPUTJSON_CHANGE);
		appendStringInfoChar(ctx->out, ']');
	}

	/*
	 * Print old tuple (UPDATE, DELETE)
	 *
	 * old tuple is available when:
	 * (1) primary key changes;
	 * (2) replica identity is index and one of the indexed columns changes;
	 * (3) replica identity is full.
	 *
	 * If old tuple is not available (because (a) primary key does not change
	 * or (b) replica identity is index and none of indexed columns does not
	 * change) identity is obtained from new tuple (because it doesn't change).
	 *
	 */
	if (change->data.tp.oldtuple != NULL)
	{
		appendStringInfoString(ctx->out, ",\"identity\":[");
		pg_decode_write_tuple(ctx, relation, &change->data.tp.oldtuple->tuple, PGOUTPUTJSON_IDENTITY);
		appendStringInfoChar(ctx->out, ']');
	}
	else
	{
		/*
		 * Old tuple is not available, however, identity can be obtained from
		 * new tuple (because it doesn't change).
		 */
		if (change->action == REORDER_BUFFER_CHANGE_UPDATE)
		{
			elog(DEBUG2, "old tuple is null on UPDATE");

			/*
			 * Before v10, there is not rd_pkindex then rely on REPLICA
			 * IDENTITY DEFAULT to obtain primary key.
			 */
#if	PG_VERSION_NUM >= 100000
			if (OidIsValid(relation->rd_pkindex) || OidIsValid(relation->rd_replidindex))
#else
			if (OidIsValid(relation->rd_replidindex))
#endif
			{
				elog(DEBUG1, "REPLICA IDENTITY: obtain old tuple using new tuple");
				appendStringInfoString(ctx->out, ",\"identity\":[");
				pg_decode_write_tuple(ctx, relation, &change->data.tp.newtuple->tuple, PGOUTPUTJSON_IDENTITY);
				appendStringInfoChar(ctx->out, ']');
			}
			else
			{
				/* old tuple is not available and can't be obtained, report it */
				elog(WARNING, "no old tuple data for UPDATE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
			}
		}

		/* old tuple is not available and can't be obtained, report it */
		if (change->action == REORDER_BUFFER_CHANGE_DELETE)
		{
			elog(WARNING, "no old tuple data for DELETE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
		}
	}

	appendStringInfoChar(ctx->out, '}');

	OutputPluginWrite(ctx, true);
}

static void
pg_decode_change_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;
	MemoryContext old;

	char	*schemaname;
	char	*tablename;

	/* avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/* schema and table names are used for chosen tables */
	schemaname = get_namespace_name(RelationGetNamespace(relation));
	tablename = RelationGetRelationName(relation);

	/* Exclude tables, if available */
	if (list_length(data->filter_tables) > 0)
	{
		ListCell	*lc;

		foreach(lc, data->filter_tables)
		{
			SelectTable	*t = lfirst(lc);

			if (t->allschemas || strcmp(t->schemaname, schemaname) == 0)
			{
				if (t->alltables || strcmp(t->tablename, tablename) == 0)
				{
					elog(DEBUG2, "\"%s\".\"%s\" was filtered out",
								((t->allschemas) ? "*" : t->schemaname),
								((t->alltables) ? "*" : t->tablename));
					return;
				}
			}
		}
	}

	/* Add tables */
	if (list_length(data->add_tables) > 0)
	{
		ListCell	*lc;
		bool		skip = true;

		/* all tables in all schemas are added by default */
		foreach(lc, data->add_tables)
		{
			SelectTable	*t = lfirst(lc);

			if (t->allschemas || strcmp(t->schemaname, schemaname) == 0)
			{
				if (t->alltables || strcmp(t->tablename, tablename) == 0)
				{
					elog(DEBUG2, "\"%s\".\"%s\" was added",
								((t->allschemas) ? "*" : t->schemaname),
								((t->alltables) ? "*" : t->tablename));
					skip = false;
				}
			}
		}

		/* table was not found */
		if (skip)
			return;
	}

	pg_decode_write_change(ctx, txn, relation, change);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);
}

#if	PG_VERSION_NUM >= 90600
/* Callback for generic logical decoding messages */
static void
pg_decode_message(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* Filter message prefixes, if available */
	if (list_length(data->filter_msg_prefixes) > 0)
	{
		ListCell	*lc;

		foreach(lc, data->filter_msg_prefixes)
		{
			char	*p = lfirst(lc);

			if (strcmp(p, prefix) == 0)
			{
				elog(DEBUG2, "message prefix \"%s\" was filtered out", p);
				return;
			}
		}
	}

	/* Add messages by prefix */
	if (list_length(data->add_msg_prefixes) > 0)
	{
		ListCell	*lc;
		bool		skip = true;

		foreach(lc, data->add_msg_prefixes)
		{
			char	*p = lfirst(lc);

			if (strcmp(p, prefix) == 0)
				skip = false;
		}

		if (skip)
		{
			elog(DEBUG2, "message prefix \"%s\" was skipped", prefix);
			return;
		}
	}

	if (data->format_version == 2)
		pg_decode_message_v2(ctx, txn, lsn, transactional, prefix, content_size, content);
	else if (data->format_version == 1)
		pg_decode_message_v1(ctx, txn, lsn, transactional, prefix, content_size, content);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_message_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData *data;
	MemoryContext old;
	char *content_str;

	data = ctx->output_plugin_private;

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/*
	 * write immediately iif (i) write-in-chunks=1 or (ii) non-transactional
	 * messages.
	 */
	if (data->write_in_chunks || !transactional)
		OutputPluginPrepareWrite(ctx, true);

	/*
	 * increment counter only for transactional messages because
	 * non-transactional message has only one object.
	 */
	if (transactional)
		data->nr_changes++;

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks && transactional)
			appendStringInfo(ctx->out, "%s", data->nl);

	/* build a complete JSON object for non-transactional message */
	if (!transactional)
		appendStringInfo(ctx->out, "{%s%s\"change\":%s[%s", data->nl, data->ht, data->sp, data->nl);

	appendStringInfo(ctx->out, "%s%s", data->ht, data->ht);

	if (data->nr_changes > 1)
		appendStringInfoChar(ctx->out, ',');

	appendStringInfo(ctx->out, "{%s%s%s%s\"kind\":%s\"message\",%s", data->nl, data->ht, data->ht, data->ht, data->sp, data->nl);

	if (transactional)
		appendStringInfo(ctx->out, "%s%s%s\"transactional\":%strue,%s", data->ht, data->ht, data->ht, data->sp, data->nl);
	else
		appendStringInfo(ctx->out, "%s%s%s\"transactional\":%sfalse,%s", data->ht, data->ht, data->ht, data->sp, data->nl);

	appendStringInfo(ctx->out, "%s%s%s\"prefix\":%s", data->ht, data->ht, data->ht, data->sp);
	escape_json(ctx->out, prefix);
	appendStringInfo(ctx->out, ",%s%s%s%s\"content\":%s", data->nl, data->ht, data->ht, data->ht, data->sp);

	content_str = (char *) palloc0((content_size + 1) * sizeof(char));
	strncpy(content_str, content, content_size);
	escape_json(ctx->out, content_str);
	pfree(content_str);

	appendStringInfo(ctx->out, "%s%s%s}", data->nl, data->ht, data->ht);

	/* build a complete JSON object for non-transactional message */
	if (!transactional)
		appendStringInfo(ctx->out, "%s%s]%s}", data->nl, data->ht, data->nl);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks || !transactional)
		OutputPluginWrite(ctx, true);
}

static void
pg_decode_message_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData	*data = ctx->output_plugin_private;
	MemoryContext		old;
	char				*content_str;

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	OutputPluginPrepareWrite(ctx, true);
	appendStringInfoChar(ctx->out, '{');
	appendStringInfoString(ctx->out, "\"action\":\"M\"");

	if (data->include_xids)
	{
		/*
		 * Non-transactional messages can have no xid, hence, assigns null in
		 * this case.  Assigns null for xid in non-transactional messages
		 * because in some cases there isn't an assigned xid.
		 * This same logic is valid for timestamp and origin.
		 */
		if (transactional)
			appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);
		else
			appendStringInfoString(ctx->out, ",\"xid\":null");
	}

	if (data->include_timestamp)
	{
		if (transactional)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));
		else
			appendStringInfoString(ctx->out, ",\"timestamp\":null");
	}

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, lsn));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	if (transactional)
		appendStringInfoString(ctx->out, ",\"transactional\":true");
	else
		appendStringInfoString(ctx->out, ",\"transactional\":false");

	appendStringInfoString(ctx->out, ",\"prefix\":");
	escape_json(ctx->out, prefix);

	appendStringInfoString(ctx->out, ",\"content\":");
	content_str = (char *) palloc0((content_size + 1) * sizeof(char));
	strncpy(content_str, content, content_size);
	escape_json(ctx->out, content_str);
	pfree(content_str);

	appendStringInfoChar(ctx->out, '}');
	OutputPluginWrite(ctx, true);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);
}
#endif

static bool
parse_table_identifier(List *qualified_tables, char separator, List **select_tables)
{
	ListCell	*lc;

	foreach(lc, qualified_tables)
	{
		char		*str = lfirst(lc);
		char		*startp;
		char		*nextp;
		int			len;
		SelectTable	*t = palloc0(sizeof(SelectTable));

		/*
		 * Detect a special character that means all schemas. There could be a
		 * schema named "*" thus this test should be before we remove the
		 * escape character.
		 */
		if (str[0] == '*' && str[1] == '.')
			t->allschemas = true;
		else
			t->allschemas = false;

		startp = nextp = str;
		while (*nextp && *nextp != separator)
		{
			/* remove escape character */
			if (*nextp == '\\')
				memmove(nextp, nextp + 1, strlen(nextp));
			nextp++;
		}
		len = nextp - startp;

		/* if separator was not found, schema was not informed */
		if (*nextp == '\0')
		{
			pfree(t);
			return false;
		}
		else
		{
			/* schema name */
			t->schemaname = (char *) palloc0((len + 1) * sizeof(char));
			strncpy(t->schemaname, startp, len);

			nextp++;			/* jump separator */
			startp = nextp;		/* start new identifier (table name) */

			/*
			 * Detect a special character that means all tables. There could be
			 * a table named "*" thus this test should be before that we remove
			 * the escape character.
			 */
			if (startp[0] == '*' && startp[1] == '\0')
				t->alltables = true;
			else
				t->alltables = false;

			while (*nextp)
			{
				/* remove escape character */
				if (*nextp == '\\')
					memmove(nextp, nextp + 1, strlen(nextp));
				nextp++;
			}
			len = nextp - startp;

			/* table name */
			t->tablename = (char *) palloc0((len + 1) * sizeof(char));
			strncpy(t->tablename, startp, len);
		}

		*select_tables = lappend(*select_tables, t);
	}

	return true;
}

static bool
string_to_SelectTable(char *rawstring, char separator, List **select_tables)
{
	char	   *nextp;
	bool		done = false;
	List	   *qualified_tables = NIL;

	nextp = rawstring;

	while (isspace(*nextp))
		nextp++;				/* skip leading whitespace */

	if (*nextp == '\0')
		return true;			/* allow empty string */

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char	   *curname;
		char	   *endp;
		char	   *qname;

		curname = nextp;
		while (*nextp && *nextp != separator && !isspace(*nextp))
		{
			if (*nextp == '\\')
				nextp++;	/* ignore next character because of escape */
			nextp++;
		}
		endp = nextp;
		if (curname == nextp)
			return false;	/* empty unquoted name not allowed */

		while (isspace(*nextp))
			nextp++;			/* skip trailing whitespace */

		if (*nextp == separator)
		{
			nextp++;
			while (isspace(*nextp))
				nextp++;		/* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false;		/* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating current name --- add it to list
		 */
		qname = pstrdup(curname);
		qualified_tables = lappend(qualified_tables, qname);

		/* Loop back if we didn't reach end of string */
	} while (!done);

	if (!parse_table_identifier(qualified_tables, '.', select_tables))
		return false;

	list_free_deep(qualified_tables);

	return true;
}

static bool
split_string_to_list(char *rawstring, char separator, List **sl)
{
	char	   *nextp;
	bool		done = false;

	nextp = rawstring;

	while (isspace(*nextp))
		nextp++;				/* skip leading whitespace */

	if (*nextp == '\0')
		return true;			/* allow empty string */

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char	   *curname;
		char	   *endp;
		char	   *pname;

		curname = nextp;
		while (*nextp && *nextp != separator && !isspace(*nextp))
		{
			if (*nextp == '\\')
				nextp++;	/* ignore next character because of escape */
			nextp++;
		}
		endp = nextp;
		if (curname == nextp)
			return false;	/* empty unquoted name not allowed */

		while (isspace(*nextp))
			nextp++;			/* skip trailing whitespace */

		if (*nextp == separator)
		{
			nextp++;
			while (isspace(*nextp))
				nextp++;		/* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false;		/* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating current name --- add it to list
		 */
		pname = pstrdup(curname);
		*sl = lappend(*sl, pname);

		/* Loop back if we didn't reach end of string */
	} while (!done);

	return true;
}
