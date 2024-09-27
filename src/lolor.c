/*-------------------------------------------------------------------------
 * lolor.c
 *	  PostgreSQL definitions for Large Objects for logical replication.
 *
 * Copyright (c) 2022-2024, pgEdge, Inc.
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	contrib/lolor/src/lolor.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "fmgr.h"
#include "access/xact.h"
#include "catalog/namespace.h"
#include "commands/event_trigger.h"
#include "executor/spi.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "nodes/print.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"

#include "lolor.h"

PG_MODULE_MAGIC;

int32 lolor_node_id = 0;
/*
 * Parameters to determine when to emit a log message in
 * LOLOR_GetNewOidWithIndex()
 */
#define GETNEWOID_LOG_THRESHOLD 1000000
#define GETNEWOID_LOG_MAX_INTERVAL 128000000

/* Parameters to determine new unique Oid. */
#define MAX_NODEID_BITS 4
#define MAX_OID_BITS 28

void	_PG_init(void);

/*
 * Entry point for this module.
 */
void
_PG_init(void)
{
	DefineCustomIntVariable("lolor.node",
							"Unique id of current node.",
							NULL,
							&lolor_node_id,
							0,
							0,
							16,
							PGC_SUSET,
							0,
							NULL, NULL, NULL);
}

/*
 * LOLOR_GetNewOidWithIndex
 *		Generate a new OID that is unique within the given relation.
 *
 * The lower 4 bits contains the lolor_node_id. The 2^28 bits consist of Oid
 * returned from GetNewObjectId and adjusted to remain within the range.
 *
 * See comments for GetNewOidWithIndex() for more details.
 */
Oid
LOLOR_GetNewOidWithIndex(Relation relation, Oid indexId, AttrNumber oidcolumn)
{
	Oid			newOid;
	SysScanDesc scan;
	ScanKeyData key;
	bool		collides;
	uint64		retries = 0;
	uint64		retries_before_log = GETNEWOID_LOG_THRESHOLD;

	/* Check that GUC lolor.node is set */
	if (lolor_node_id == 0)
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("value for lolor.node is not set")));

	/* Generate new OIDs until we find one not in the table */
	do
	{
		CHECK_FOR_INTERRUPTS();

		newOid = GetNewObjectId();

		/*
		 * Keep the range within 1..2^28. Restart from start on overflow and see
		 * if any of the Oids are avaialbe.
		 */
		newOid = newOid % (1 << MAX_OID_BITS);
		if (newOid == 0)
			newOid = 1;

		newOid = (newOid << MAX_NODEID_BITS) | lolor_node_id;

		if (IsBootstrapProcessingMode())
			return newOid;

		ScanKeyInit(&key,
					oidcolumn,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(newOid));

		/* see notes above about using SnapshotAny */
		scan = systable_beginscan(relation, indexId, true,
								  SnapshotAny, 1, &key);

		collides = HeapTupleIsValid(systable_getnext(scan));

		systable_endscan(scan);

		/*
		 * Log that we iterate more than GETNEWOID_LOG_THRESHOLD but have not
		 * yet found OID unused in the relation. Then repeat logging with
		 * exponentially increasing intervals until we iterate more than
		 * GETNEWOID_LOG_MAX_INTERVAL. Finally repeat logging every
		 * GETNEWOID_LOG_MAX_INTERVAL unless an unused OID is found. This
		 * logic is necessary not to fill up the server log with the similar
		 * messages.
		 */
		if (retries >= retries_before_log)
		{
			ereport(LOG,
					(errmsg("still searching for an unused OID in relation \"%s\"",
							RelationGetRelationName(relation)),
					 errdetail_plural("OID candidates have been checked %llu time, but no unused OID has been found yet.",
									  "OID candidates have been checked %llu times, but no unused OID has been found yet.",
									  retries,
									  (unsigned long long) retries)));

			/*
			 * Double the number of retries to do before logging next until it
			 * reaches GETNEWOID_LOG_MAX_INTERVAL.
			 */
			if (retries_before_log * 2 <= GETNEWOID_LOG_MAX_INTERVAL)
				retries_before_log *= 2;
			else
				retries_before_log += GETNEWOID_LOG_MAX_INTERVAL;
		}

		retries++;
	} while (collides);

	/*
	 * If at least one log message is emitted, also log the completion of OID
	 * assignment.
	 */
	if (retries > GETNEWOID_LOG_THRESHOLD)
	{
		ereport(LOG,
				(errmsg_plural("new OID has been assigned in relation \"%s\" after %llu retry",
							   "new OID has been assigned in relation \"%s\" after %llu retries",
							   retries,
							   RelationGetRelationName(relation), (unsigned long long) retries)));
	}

	return newOid;
}
