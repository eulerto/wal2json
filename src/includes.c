#include "includes.h"

#include "wal2json.h"

#include "catalog/pg_collation.h"


/* forward declarations */

#define cmd_cont(n) dlist_container(InclusionCommand, node, n)
static void cmds_init(InclusionCommands **cmds);
static void cmds_push(InclusionCommands *cmds, InclusionCommand *cmd);
static InclusionCommand *cmds_tail(InclusionCommands *cmds);

static void re_compile(regex_t *re, const char *p);
static bool re_match(regex_t *re, const char *s);


void
inc_parse_include_table(DefElem *elem, InclusionCommands **cmds)
{
	InclusionCommand *cmd;
	char *val;

	if (elem->arg == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("parameter \"%s\" requires a value",
					 elem->defname)));
	}

	cmds_init(cmds);

	cmd = palloc0(sizeof(InclusionCommand));
	val = strVal(elem->arg);

	if (val[0] == '~') {
		cmd->type = CMD_INCLUDE_TABLE_PATTERN;
		re_compile(&cmd->table_re, val + 1);
	}
	else {
		cmd->type = CMD_INCLUDE_TABLE;
		cmd->table_name = pstrdup(val);
	}

	cmds_push(*cmds, cmd);
}


void
inc_parse_exclude_table(DefElem *elem, InclusionCommands **cmds)
{
	InclusionCommand *cmd;

	inc_parse_include_table(elem, cmds);
	cmd = cmds_tail(*cmds);
	switch (cmd->type)
	{
		case CMD_INCLUDE_TABLE:
			cmd->type = CMD_EXCLUDE_TABLE;
			break;

		case CMD_INCLUDE_TABLE_PATTERN:
			cmd->type = CMD_EXCLUDE_TABLE_PATTERN;
			break;

		default:
			Assert(false);
	}
}

/* Return True if a table should be included in the output */
bool
inc_should_emit(InclusionCommands *cmds, Form_pg_class class_form)
{
	dlist_iter iter;
	bool rv = false;

	/* No command: include everything by default */
	if (cmds == NULL)
		return true;

	dlist_foreach(iter, &(cmds)->head)
	{
		InclusionCommand *cmd = cmd_cont(iter.cur);
		switch (cmd->type)
		{
			case CMD_INCLUDE_TABLE:
				if (strcmp(cmd->table_name, NameStr(class_form->relname)) == 0)
					rv = true;
				break;

			case CMD_EXCLUDE_TABLE:
				if (strcmp(cmd->table_name, NameStr(class_form->relname)) == 0)
					rv = false;
				break;

			case CMD_INCLUDE_TABLE_PATTERN:
				if (re_match(&cmd->table_re, NameStr(class_form->relname)))
					rv = true;
				break;

			case CMD_EXCLUDE_TABLE_PATTERN:
				if (re_match(&cmd->table_re, NameStr(class_form->relname)))
					rv = false;
				break;

			default:
				Assert(false);
		}
	}

	return rv;
}


static void
cmds_init(InclusionCommands **cmds)
{
	if (*cmds == NULL)
		*cmds = palloc0(sizeof(InclusionCommands));
}


static void
cmds_push(InclusionCommands *cmds, InclusionCommand *cmd)
{
	dlist_push_tail(&cmds->head, &cmd->node);
}


static InclusionCommand *
cmds_tail(InclusionCommands *cmds)
{
	dlist_node *n = dlist_tail_node(&cmds->head);
	return cmd_cont(n);
}


static void
re_compile(regex_t *re, const char *p)
{
	pg_wchar *wstr;
	int wlen;
	int r;

	wstr = palloc((strlen(p) + 1) * sizeof(pg_wchar));
	wlen = pg_mb2wchar(p, wstr);

	r = pg_regcomp(re, wstr, wlen, REG_ADVANCED, C_COLLATION_OID);
	if (r)
	{
		char errstr[100];
		pg_regerror(r, re, errstr, sizeof(errstr));
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
				 errmsg("invalid regular expression \"%s\": %s", p, errstr)));
	}

	pfree(wstr);
}

static bool
re_match(regex_t *re, const char *s)
{
	pg_wchar *wstr;
	int wlen;
	int r;

	wstr = palloc((strlen(s) + 1) * sizeof(pg_wchar));
	wlen = pg_mb2wchar(s, wstr);

	r = pg_regexec(re, wstr, wlen, 0, NULL, 0, NULL, 0);
	pfree(wstr);
	if (r == REG_NOMATCH)
		return false;
	if (!r)
		return true;

	{
		char errstr[100];

		/* REG_NOMATCH is not an error, everything else is */
		pg_regerror(r, re, errstr, sizeof(errstr));
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_REGULAR_EXPRESSION),
				 errmsg("regular expression match for \"%s\" failed: %s",
						s, errstr)));
	}
}
