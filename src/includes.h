#ifndef _INCLUDES_H_
#define _INCLUDES_H_

#include "postgres.h"

#include "catalog/pg_class.h"

#include "lib/ilist.h"

#include "nodes/parsenodes.h"

typedef enum
{
	CMD_INCLUDE_TABLE,
	CMD_EXCLUDE_TABLE,
} CommandType;


typedef struct
{
	char		*schema_name;		/* name of schema to include/exclude */
	char		*table_name;		/* name of table to include/exclude */
	dlist_node	node;				/* double-linked list */
	CommandType	type;				/* what command is this? */
} InclusionCommand;


typedef struct
{
	dlist_head	head;				/* commands included in the list */
} InclusionCommands;


void includes_parse_table(DefElem *elem, InclusionCommands **cmds);
bool includes_should_emit(InclusionCommands *cmds, Form_pg_class class_form);


#endif
