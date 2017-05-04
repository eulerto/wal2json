#include "postgres.h"

#include "includes.h"

#include "wal2json.h"

void
includes_parse_table(DefElem *elem, InclusionCommands **cmds)
{
	if (elem->arg == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("parameter \"%s\" requires a value",
					 elem->defname)));
	}
	else {
		InclusionCommand *cmd = palloc0(sizeof(InclusionCommand));
		cmd->type = CMD_INCLUDE_TABLE;
		cmd->table_name = pstrdup(strVal(elem->arg));
		if (*cmds == NULL)
			*cmds = palloc0(sizeof(InclusionCommands));
		dlist_push_tail(&(*cmds)->head, &cmd->node);
	}
}

/* Return True if a table should be included in the output */
bool
includes_should_emit(InclusionCommands *cmds, Form_pg_class class_form)
{
	dlist_iter iter;

	/* No command: include everything by default */
	if (cmds == NULL)
		return true;

	dlist_foreach(iter, &(cmds)->head)
	{
		InclusionCommand *cmd = dlist_container(
			InclusionCommand, node, iter.cur);
		switch (cmd->type)
		{
			case CMD_INCLUDE_TABLE:
				if (strcmp(cmd->table_name, NameStr(class_form->relname)) == 0)
					return true;
				break;
			default:
				Assert(false);
		}
	}

	return false;
}
