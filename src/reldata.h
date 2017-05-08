#ifndef _RELDATA_H_
#define _RELDATA_H_

#include "postgres.h"

#include "utils/hsearch.h"

typedef struct
{
	Oid reloid;

	/* should this table be emitted? If both are false we don't know yet */
	bool include;
	bool exclude;

} JsonRelationEntry;


HTAB *reldata_create(void);
JsonRelationEntry *reldata_find(HTAB *reldata, Oid relid);
JsonRelationEntry *reldata_enter(HTAB *reldata, Oid relid);

#endif
