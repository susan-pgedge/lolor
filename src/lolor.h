/*-------------------------------------------------------------------------
 *
 * lolor.h
 *	  large object logical replication
 *
 * Copyright (c) 2022-2024, pgEdge, Inc.
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOLOR_LARGEOBJECT_H
#define LOLOR_LARGEOBJECT_H

#include "storage/large_object.h"
#include "utils/acl.h"

#define EXTENSION_NAME					"lolor"

/* lolor.c */
extern int32 lolor_node_id;
extern PGDLLEXPORT Oid LOLOR_GetNewOidWithIndex(Relation relation, Oid indexId, AttrNumber oidcolumn);

#endif							/* LOLOR_LARGEOBJECT_H */
