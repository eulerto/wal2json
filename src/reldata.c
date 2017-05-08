#include "reldata.h"

HTAB *
reldata_create()
{
	HTAB *reldata;
	HASHCTL		ctl;

	reldata = palloc0(sizeof(reldata));

	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(JsonRelationEntry);
	ctl.hash = oid_hash;
	reldata = hash_create(
		"json relations cache", 32, &ctl, HASH_ELEM | HASH_FUNCTION);

	return reldata;
}


JsonRelationEntry *
reldata_find(HTAB *reldata, Oid relid)
{
	JsonRelationEntry *entry;
	entry = (JsonRelationEntry *)hash_search(
		reldata, (void *)&(relid), HASH_FIND, NULL);
	return entry;
}


JsonRelationEntry *
reldata_enter(HTAB *reldata, Oid relid)
{
	JsonRelationEntry *entry;
	bool found;

	entry = (JsonRelationEntry *)hash_search(
		reldata, (void *)&(relid), HASH_ENTER, &found);

	if (!found)
	{
		elog(DEBUG1, "entry for relation %u is new", relid);
		entry->include = false;
		entry->exclude = false;
	}

	return entry;
}
