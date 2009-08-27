/* SNMP support
 * Copyright (C) 2000 Jochen Friedrich <jochen@scram.de>
 * Copyright (C) 1999 Kunihiro Ishiguro <kunihiro@zebra.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _SMUX_H
#define _SMUX_H

#include <freeradius-devel/ident.h>
RCSIDH(smux_h, "$Id: smux.h,v 1.12 2007/12/02 16:52:51 aland Exp $")

#ifdef HAVE_ASN1_SNMP_SNMPIMPL_H
#include <asn1.h>
#include <snmp.h>
#include <snmp_impl.h>

#elif defined (HAVE_NET_SNMP_NET_SNMP_CONFIG_H)
# include <net-snmp/net-snmp-config.h>
# include <net-snmp/definitions.h>
# include <net-snmp/types.h>

#elif defined(HAVE_UCD_SNMP_ASN1_SNMP_SNMPIMPL_H)
# include <ucd-snmp/ucd-snmp-config.h>
# include <ucd-snmp/asn1.h>
# include <ucd-snmp/snmp.h>
# include <ucd-snmp/snmp_impl.h>
#endif

#define SMUX_PORT_DEFAULT 199

#define SMUXMAXPKTSIZE    1500
#define SMUXMAXSTRLEN      256

#define SMUX_OPEN       (ASN_APPLICATION | ASN_CONSTRUCTOR | 0)
#define SMUX_CLOSE      (ASN_APPLICATION | ASN_PRIMITIVE | 1)
#define SMUX_RREQ       (ASN_APPLICATION | ASN_CONSTRUCTOR | 2)
#define SMUX_RRSP       (ASN_APPLICATION | ASN_PRIMITIVE | 3)
#define SMUX_SOUT       (ASN_APPLICATION | ASN_PRIMITIVE | 4)

#define SMUX_GET        (ASN_CONTEXT | ASN_CONSTRUCTOR | 0)
#define SMUX_GETNEXT    (ASN_CONTEXT | ASN_CONSTRUCTOR | 1)
#define SMUX_GETRSP     (ASN_CONTEXT | ASN_CONSTRUCTOR | 2)
#define SMUX_SET	(ASN_CONTEXT | ASN_CONSTRUCTOR | 3)

/* Structures here are mostly compatible with UCD SNMP 4.1.1 */

#define MATCH_FAILED     (-1)
#define MATCH_SUCCEEDED  0

struct variable;

#define SMUX_REGISTER_MIB(descr, var, vartype, theoid)		\
    smux_register_mib(descr, (struct variable *)var, sizeof(struct vartype), \
    sizeof(var)/sizeof(struct vartype),			\
    theoid, sizeof(theoid)/sizeof(oid))

typedef int (WriteMethod)(int action,
			  u_char  *var_val,
			  u_char   var_val_type,
			  size_t   var_val_len,
			  const unsigned char  *statP,
			  oid     *name,
			  size_t   length);

typedef const unsigned char *(FindVarMethod)(struct variable *vp,
  oid     *name,
  size_t  *length,
  int      exact,
  size_t  *var_len,
  WriteMethod   **write_method);

/* List */
struct list
{
  struct list *next;
  void        *data;
};

/* SNMP variable */
struct variable
{
  /* Index of the MIB.*/
  u_char magic;

  /* Type of variable. */
  char type;

  /* Access control list. */
  u_short acl;

  /* Callback function. */
  FindVarMethod *findVar;

  /* Suffix of the MIB. */
  u_char namelen;
  oid name[MAX_OID_LEN];
};

/* SNMP tree. */
struct subtree
{
  /* Tree's oid. */
  oid name[MAX_OID_LEN];
  u_char name_len;

  /* List of the variables. */
  struct variable *variables;

  /* Length of the variables list. */
  int variables_num;

  /* Width of the variables list. */
  int variables_width;

  /* Registered flag. */
  int registered;
};

/* Declare SMUX return value. */
#define SNMP_LOCAL_VARIABLES \
  static int32_t snmp_int_val; \
  static struct in_addr snmp_in_addr_val;

#define SNMP_INTEGER(V) \
  ( \
    *var_len = sizeof (int32_t), \
    snmp_int_val = V, \
    (u_char *) &snmp_int_val \
  )

#define SNMP_IPADDRESS(V) \
  ( \
    *var_len = sizeof (struct in_addr), \
    snmp_in_addr_val = V, \
    (u_char *) &snmp_in_addr_val \
  )

void smux_init(oid [], size_t);
void smux_start(void);
void smux_stop(void);
void smux_register_mib(const char *, struct variable *, size_t, int, oid [], size_t);
int smux_header_generic (struct variable *, oid [], size_t *, int, size_t *,
    WriteMethod **);
int smux_open(void);
int smux_str2oid (char *str, oid *my_oid, size_t *oid_len);
oid *smux_oid_dup (oid *objid, size_t objid_len);
int smux_register(void);

#endif /* _SMUX_H */
