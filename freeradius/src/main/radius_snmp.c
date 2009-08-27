/*
 * radius_snmp.c	Radius SNMP support
 *
 * Version:	$Id: radius_snmp.c,v 1.45 2008/06/06 12:56:22 aland Exp $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2000,2006  The FreeRADIUS server project
 * Copyright 2000  Jochen Friedrich <jochen@scram.de>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: radius_snmp.c,v 1.45 2008/06/06 12:56:22 aland Exp $")

#include <freeradius-devel/radiusd.h>

#ifdef WITH_SNMP

#include <freeradius-devel/smux.h>
#include <freeradius-devel/radius_snmp.h>

/*
 *	More globals (sigh);
 */
rad_snmp_t		rad_snmp;


#define RADACCOID  1,3,6,1,2,1,67,2,1,1,1
#define RADAUTHOID 1,3,6,1,2,1,67,1,1,1,1
#define RADIUSOID  1,3,6,1,4,1,11344,1,1,1

static const oid radacc_oid [] = { RADACCOID };
static const oid radauth_oid [] = { RADAUTHOID };
static const oid radius_oid [] = { RADIUSOID };

#define COUNTER ASN_COUNTER
#define INTEGER ASN_INTEGER
#define GAUGE ASN_GAUGE
#define TIMETICKS ASN_TIMETICKS
#define IPADDRESS ASN_IPADDRESS
#define STRING ASN_OCTET_STR

#define RADIUSACCSERVIDENT               1
#define RADIUSACCSERVUPTIME              2
#define RADIUSACCSERVRESETTIME           3
#define RADIUSACCSERVCONFIGRESET         4
#define RADIUSACCSERVTOTALREQUESTS       5
#define RADIUSACCSERVTOTALINVALIDREQUESTS 6
#define RADIUSACCSERVTOTALDUPREQUESTS    7
#define RADIUSACCSERVTOTALRESPONSES      8
#define RADIUSACCSERVTOTALMALFORMEDREQUESTS 9
#define RADIUSACCSERVTOTALBADAUTHENTICATORS 10
#define RADIUSACCSERVTOTALPACKETSDROPPED 11
#define RADIUSACCSERVTOTALNORECORDS      12
#define RADIUSACCSERVTOTALUNKNOWNTYPES   13
/* */
#define RADIUSACCCLIENTADDRESS           2
#define RADIUSACCCLIENTID                3
#define RADIUSACCSERVPACKETSDROPPED      4
#define RADIUSACCSERVREQUESTS            5
#define RADIUSACCSERVDUPREQUESTS         6
#define RADIUSACCSERVRESPONSES           7
#define RADIUSACCSERVBADAUTHENTICATORS   8
#define RADIUSACCSERVMALFORMEDREQUESTS   9
#define RADIUSACCSERVNORECORDS           10
#define RADIUSACCSERVUNKNOWNTYPES        11
/* */
#define RADIUSAUTHSERVIDENT              1
#define RADIUSAUTHSERVUPTIME             2
#define RADIUSAUTHSERVRESETTIME          3
#define RADIUSAUTHSERVCONFIGRESET        4
#define RADIUSAUTHSERVTOTALACCESSREQUESTS 5
#define RADIUSAUTHSERVTOTALINVALIDREQUESTS 6
#define RADIUSAUTHSERVTOTALDUPACCESSREQUESTS 7
#define RADIUSAUTHSERVTOTALACCESSACCEPTS 8
#define RADIUSAUTHSERVTOTALACCESSREJECTS 9
#define RADIUSAUTHSERVTOTALACCESSCHALLENGES 10
#define RADIUSAUTHSERVTOTALMALFORMEDACCESSREQUESTS 11
#define RADIUSAUTHSERVTOTALBADAUTHENTICATORS 12
#define RADIUSAUTHSERVTOTALPACKETSDROPPED 13
#define RADIUSAUTHSERVTOTALUNKNOWNTYPES  14
/* */
#define RADIUSAUTHCLIENTADDRESS          2
#define RADIUSAUTHCLIENTID               3
#define RADIUSAUTHSERVACCESSREQUESTS     4
#define RADIUSAUTHSERVDUPACCESSREQUESTS  5
#define RADIUSAUTHSERVACCESSACCEPTS      6
#define RADIUSAUTHSERVACCESSREJECTS      7
#define RADIUSAUTHSERVACCESSCHALLENGES   8
#define RADIUSAUTHSERVMALFORMEDACCESSREQUESTS 9
#define RADIUSAUTHSERVBADAUTHENTICATORS  10
#define RADIUSAUTHSERVPACKETSDROPPED     11
#define RADIUSAUTHSERVUNKNOWNTYPES       12

/* Hook functions. */
#ifdef WITH_ACCOUNTING
static const u_char *radAccServ(struct variable *vp,
	oid     *name,
	size_t  *length,
	int     exact,
	size_t  *var_len,
	WriteMethod **write_method);
static const u_char *radAccEntry(struct variable *vp,
	oid     *name,
	size_t  *length,
	int     exact,
	size_t  *var_len,
	WriteMethod **write_method);
#endif
static const u_char *radAuthServ(struct variable *vp,
	oid     *name,
	size_t  *length,
	int     exact,
	size_t  *var_len,
	WriteMethod **write_method);
static const u_char *radAuthEntry(struct variable *vp,
	oid     *name,
	size_t  *length,
	int     exact,
	size_t  *var_len,
	WriteMethod **write_method);

#ifdef WITH_ACCOUNTING
static const struct variable radiusacc_variables[] =
{
	{RADIUSACCSERVIDENT, STRING, RONLY, radAccServ, 1, {1}},
	{RADIUSACCSERVUPTIME, TIMETICKS, RONLY, radAccServ, 1, {2}},
	{RADIUSACCSERVRESETTIME, TIMETICKS, RONLY, radAccServ, 1, {3}},
	{RADIUSACCSERVCONFIGRESET, INTEGER, RWRITE, radAccServ, 1, {4}},
	{RADIUSACCSERVTOTALREQUESTS, COUNTER, RONLY, radAccServ, 1, {5}},
	{RADIUSACCSERVTOTALINVALIDREQUESTS, COUNTER, RONLY, radAccServ, 1, {6}},
	{RADIUSACCSERVTOTALDUPREQUESTS, COUNTER, RONLY, radAccServ, 1, {7}},
	{RADIUSACCSERVTOTALRESPONSES, COUNTER, RONLY, radAccServ, 1, {8}},
	{RADIUSACCSERVTOTALMALFORMEDREQUESTS, COUNTER, RONLY, radAccServ, 1, {9}},
	{RADIUSACCSERVTOTALBADAUTHENTICATORS, COUNTER, RONLY, radAccServ, 1, {10}},
	{RADIUSACCSERVTOTALPACKETSDROPPED, COUNTER, RONLY, radAccServ, 1, {11}},
	{RADIUSACCSERVTOTALNORECORDS, COUNTER, RONLY, radAccServ, 1, {12}},
	{RADIUSACCSERVTOTALUNKNOWNTYPES, COUNTER, RONLY, radAccServ, 1, {13}},
	{RADIUSACCCLIENTADDRESS, IPADDRESS, RONLY, radAccEntry, 3, {14,1,2}},
	{RADIUSACCCLIENTID, STRING, RONLY, radAccEntry, 3, {14,1,3}},
	{RADIUSACCSERVPACKETSDROPPED, COUNTER, RONLY, radAccEntry, 3, {14,1,4}},
	{RADIUSACCSERVREQUESTS, COUNTER, RONLY, radAccEntry, 3, {14,1,5}},
	{RADIUSACCSERVDUPREQUESTS, COUNTER, RONLY, radAccEntry, 3, {14,1,6}},
	{RADIUSACCSERVRESPONSES, COUNTER, RONLY, radAccEntry, 3, {14,1,7}},
	{RADIUSACCSERVBADAUTHENTICATORS, COUNTER, RONLY, radAccEntry, 3, {14,1,8}},
	{RADIUSACCSERVMALFORMEDREQUESTS, COUNTER, RONLY, radAccEntry, 3, {14,1,9}},
	{RADIUSACCSERVNORECORDS, COUNTER, RONLY, radAccEntry, 3, {14,1,10}},
	{RADIUSACCSERVUNKNOWNTYPES, COUNTER, RONLY, radAccEntry, 3, {14,1,11}},
};
#endif

static const struct variable radiusauth_variables[] =
{
	{RADIUSAUTHSERVIDENT, STRING, RONLY, radAuthServ, 1, {1}},
	{RADIUSAUTHSERVUPTIME, TIMETICKS, RONLY, radAuthServ, 1, {2}},
	{RADIUSAUTHSERVRESETTIME, TIMETICKS, RONLY, radAuthServ, 1, {3}},
	{RADIUSAUTHSERVCONFIGRESET, INTEGER, RWRITE, radAuthServ, 1, {4}},
	{RADIUSAUTHSERVTOTALACCESSREQUESTS, COUNTER, RONLY, radAuthServ, 1, {5}},
	{RADIUSAUTHSERVTOTALINVALIDREQUESTS, COUNTER, RONLY, radAuthServ, 1, {6}},
	{RADIUSAUTHSERVTOTALDUPACCESSREQUESTS, COUNTER, RONLY, radAuthServ, 1, {7}},
	{RADIUSAUTHSERVTOTALACCESSACCEPTS, COUNTER, RONLY, radAuthServ, 1, {8}},
	{RADIUSAUTHSERVTOTALACCESSREJECTS, COUNTER, RONLY, radAuthServ, 1, {9}},
	{RADIUSAUTHSERVTOTALACCESSCHALLENGES, COUNTER, RONLY, radAuthServ, 1, {10}},
	{RADIUSAUTHSERVTOTALMALFORMEDACCESSREQUESTS, COUNTER, RONLY, radAuthServ, 1, {11}},
	{RADIUSAUTHSERVTOTALBADAUTHENTICATORS, COUNTER, RONLY, radAuthServ, 1, {12}},
	{RADIUSAUTHSERVTOTALPACKETSDROPPED, COUNTER, RONLY, radAuthServ, 1, {13}},
	{RADIUSAUTHSERVTOTALUNKNOWNTYPES, COUNTER, RONLY, radAuthServ, 1, {14}},
	{RADIUSAUTHCLIENTADDRESS, IPADDRESS, RONLY, radAuthEntry, 3, {15,1,2}},
	{RADIUSAUTHCLIENTID, STRING, RONLY, radAuthEntry, 3, {15,1,3}},
	{RADIUSAUTHSERVACCESSREQUESTS, COUNTER, RONLY, radAuthEntry, 3, {15,1,4}},
	{RADIUSAUTHSERVDUPACCESSREQUESTS, COUNTER, RONLY, radAuthEntry, 3, {15,1,5}},
	{RADIUSAUTHSERVACCESSACCEPTS, COUNTER, RONLY, radAuthEntry, 3, {15,1,6}},
	{RADIUSAUTHSERVACCESSREJECTS, COUNTER, RONLY, radAuthEntry, 3, {15,1,7}},
	{RADIUSAUTHSERVACCESSCHALLENGES, COUNTER, RONLY, radAuthEntry, 3, {15,1,8}},
	{RADIUSAUTHSERVMALFORMEDACCESSREQUESTS, COUNTER, RONLY, radAuthEntry, 3, {15,1,9}},
	{RADIUSAUTHSERVBADAUTHENTICATORS, COUNTER, RONLY, radAuthEntry, 3, {15,1,10}},
	{RADIUSAUTHSERVPACKETSDROPPED, COUNTER, RONLY, radAuthEntry, 3, {15,1,11}},
	{RADIUSAUTHSERVUNKNOWNTYPES, COUNTER, RONLY, radAuthEntry, 3, {15,1,12}},
};

static RADCLIENT *
get_client(struct variable *v, oid objid[], size_t *objid_len, int exact)
{
	RADCLIENT *c;
	int i, len;

	len = *objid_len - v->namelen;

	if (exact) {
		/* Check the length. */
		if (len != 1)
			return NULL;
		if (objid[v->namelen] == 0)
			return NULL;

		i = objid[v->namelen]-1;

		return client_findbynumber(NULL, i);
	}
	*objid_len = v->namelen + 1;
	if (!len || (objid[v->namelen] == 0)) {
		objid[v->namelen]=1;
		return client_findbynumber(NULL, 0);
	}

	i = objid[v->namelen]-1;
	c = client_findbynumber(NULL, i);
	if (c) {
		objid[v->namelen]++;
	}
	return c;
}

static int
radServReset(int action, u_char *var_val, u_char var_val_type,
	     size_t var_val_len, UNUSED const u_char *statP,
	     UNUSED oid *name, UNUSED size_t name_len)
{
	long i;
	size_t big = SNMP_MAX_LEN;

	switch (action) {
		case RESERVE1:
			if (var_val_type != INTEGER)
				return SNMP_ERR_WRONGTYPE;
			if (var_val_len != sizeof (long))
				return SNMP_ERR_WRONGLENGTH;
			if (! asn_parse_int(var_val, &big, &var_val_type, &i, sizeof(long)))
				return SNMP_ERR_WRONGENCODING;
			if (i != 2)
				return SNMP_ERR_WRONGVALUE;
			break;
		case COMMIT:
			radius_signal_self(RADIUS_SIGNAL_SELF_HUP);
			break;
		case FREE:
			break;
		default:
			return SNMP_ERR_GENERR;
	}
	return SNMP_ERR_NOERROR;
}

#ifdef WITH_ACCOUNTING
static const u_char *
radAccServ(struct variable *vp, oid *name, size_t *length, int exact,
		size_t *var_len, WriteMethod **write_method) {

	static int result;

	/* check whether the instance identifier is valid */
	if (smux_header_generic(vp, name, length, exact, var_len,
		write_method) == MATCH_FAILED) {
		return NULL;
	}

	/* return the current value of the variable */
	switch (vp->magic) {
		case RADIUSACCSERVIDENT:
			*var_len = strlen(rad_snmp.acct.ident);
			return (const u_char *) rad_snmp.acct.ident;

		case RADIUSACCSERVUPTIME:
			rad_snmp.acct.uptime = (time(NULL) - rad_snmp.acct.start_time) * 100;
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.uptime;

		case RADIUSACCSERVRESETTIME:
			rad_snmp.acct.reset_time = (time(NULL) - rad_snmp.acct.last_reset_time) * 100;
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.reset_time;

		case RADIUSACCSERVCONFIGRESET:
			*write_method = radServReset;
			result = 4;
			return (u_char *) &result;

		case RADIUSACCSERVTOTALREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_requests;

		case RADIUSACCSERVTOTALINVALIDREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_invalid_requests;

		case RADIUSACCSERVTOTALDUPREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_dup_requests;

		case RADIUSACCSERVTOTALRESPONSES:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_responses;

		case RADIUSACCSERVTOTALMALFORMEDREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_malformed_requests;

		case RADIUSACCSERVTOTALBADAUTHENTICATORS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_bad_authenticators;

		case RADIUSACCSERVTOTALPACKETSDROPPED:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_packets_dropped;

		case RADIUSACCSERVTOTALNORECORDS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_no_records;

		case RADIUSACCSERVTOTALUNKNOWNTYPES:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.acct.total_unknown_types;

	}

	return NULL;
}

static const u_char *
radAccEntry(struct variable *vp, oid *name, size_t *length, int exact,
		size_t *var_len, WriteMethod **write_method) {

	RADCLIENT *c;
	static uint32_t zero = 0;

	*write_method = NULL; /* table is read only */
	c = get_client(vp, name, length, exact);
	if (!c)
		return NULL;

	/* return the current value of the variable */

	switch (vp->magic) {
		case RADIUSACCCLIENTADDRESS:
			if (c->ipaddr.af != AF_INET) return NULL;

			*var_len = sizeof(c->ipaddr.ipaddr.ip4addr);
			return (u_char *)&(c->ipaddr.ipaddr.ip4addr);

		case RADIUSACCCLIENTID:
			if (c->shortname && c->shortname[0]) {
				*var_len = strlen(c->shortname);
				return (u_char *) c->shortname;
			}
			*var_len = strlen(c->longname);
			return (u_char *) c->longname;

		case RADIUSACCSERVPACKETSDROPPED:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->packets_dropped;

		case RADIUSACCSERVREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->requests;

		case RADIUSACCSERVDUPREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->dup_requests;

		case RADIUSACCSERVRESPONSES:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->responses;

		case RADIUSACCSERVBADAUTHENTICATORS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->bad_authenticators;

		case RADIUSACCSERVMALFORMEDREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->malformed_requests;

			/*
			 *	Received && responded to, but not
			 *	recorded anywhere.  This is always
			 *	zero.
			 */
		case RADIUSACCSERVNORECORDS:
			*var_len = sizeof(int32_t);
			return (u_char *) &zero;

		case RADIUSACCSERVUNKNOWNTYPES:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->acct->unknown_types;
	}
	return NULL;
}
#endif

static const u_char *
radAuthServ(struct variable *vp, oid *name, size_t *length, int exact,
		size_t *var_len, WriteMethod **write_method) {

	static int result;
	/* check whether the instance identifier is valid */

	if (smux_header_generic(vp, name, length, exact, var_len,
			write_method) == MATCH_FAILED) {
		return NULL;
	}

	/* return the current value of the variable */

	switch (vp->magic) {
		case RADIUSAUTHSERVIDENT:
			*var_len = strlen(rad_snmp.auth.ident);
			return (const u_char *) rad_snmp.auth.ident;

		case RADIUSAUTHSERVUPTIME:
			rad_snmp.auth.uptime = (time(NULL) - rad_snmp.auth.start_time) * 100;
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.uptime;

		case RADIUSAUTHSERVRESETTIME:
			rad_snmp.auth.reset_time = (time(NULL) - rad_snmp.auth.last_reset_time) * 100;
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.reset_time;

		case RADIUSAUTHSERVCONFIGRESET:
			*write_method = radServReset;
			result = 4;
			return (u_char *) &result;

		case RADIUSAUTHSERVTOTALACCESSREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_requests;

		case RADIUSAUTHSERVTOTALINVALIDREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_invalid_requests;

		case RADIUSAUTHSERVTOTALDUPACCESSREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_dup_requests;

		case RADIUSAUTHSERVTOTALACCESSACCEPTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_access_accepts;

		case RADIUSAUTHSERVTOTALACCESSREJECTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_access_rejects;

		case RADIUSAUTHSERVTOTALACCESSCHALLENGES:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_access_challenges;

		case RADIUSAUTHSERVTOTALMALFORMEDACCESSREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_malformed_requests;

		case RADIUSAUTHSERVTOTALBADAUTHENTICATORS:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_bad_authenticators;

		case RADIUSAUTHSERVTOTALPACKETSDROPPED:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_packets_dropped;

		case RADIUSAUTHSERVTOTALUNKNOWNTYPES:
			*var_len = sizeof(int32_t);
			return (u_char *) &rad_snmp.auth.total_unknown_types;

	}

    return NULL;
}

static const u_char *
radAuthEntry(struct variable *vp, oid	 *name, size_t *length, int exact,
		size_t *var_len, WriteMethod **write_method) {

	RADCLIENT *c;

	*write_method = NULL; /* table is read only */
	c = get_client(vp, name, length, exact);
	if (!c)
		return NULL;

	/* return the current value of the variable */

	switch (vp->magic) {

		case RADIUSAUTHCLIENTADDRESS:
			if (c->ipaddr.af != AF_INET) return NULL;

			*var_len = sizeof(c->ipaddr.ipaddr.ip4addr);
			return (u_char *)&(c->ipaddr.ipaddr.ip4addr);

		case RADIUSAUTHCLIENTID:
			if (c->shortname && c->shortname[0]) {
				*var_len = strlen(c->shortname);
				return (u_char *) c->shortname;
			}
			*var_len = strlen(c->longname);
			return (u_char *) c->longname;

		case RADIUSAUTHSERVACCESSREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->requests;

		case RADIUSAUTHSERVDUPACCESSREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->dup_requests;

		case RADIUSAUTHSERVACCESSACCEPTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->accepts;

		case RADIUSAUTHSERVACCESSREJECTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->rejects;

		case RADIUSAUTHSERVACCESSCHALLENGES:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->challenges;

		case RADIUSAUTHSERVMALFORMEDACCESSREQUESTS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->malformed_requests;

		case RADIUSAUTHSERVBADAUTHENTICATORS:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->bad_authenticators;

		case RADIUSAUTHSERVPACKETSDROPPED:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->packets_dropped;

		case RADIUSAUTHSERVUNKNOWNTYPES:
			*var_len = sizeof(int32_t);
			return (u_char *) &c->auth->unknown_types;

	}
	return NULL;
}

static const CONF_PARSER snmp_config[] = {
	{ "snmp",              PW_TYPE_BOOLEAN,
	  0, &mainconfig.do_snmp,      "no" },
	{ "smux_password",     PW_TYPE_STRING_PTR,
	  0, &rad_snmp.smux_password, "" },
	{ "snmp_write_access", PW_TYPE_BOOLEAN,
	  0, &rad_snmp.snmp_write_access, "no" },
	{ NULL, -1, 0, NULL, NULL }
};


/* Register RADIUS MIBs. */
int
radius_snmp_init (CONF_SECTION *cs)
{
	static int initialized = FALSE;

	if (!initialized) {
		/*
		 *  Initialize the RADIUS SNMP data structure.
		 */
		memset(&rad_snmp, 0, sizeof(rad_snmp));

		rad_snmp.auth.ident = radiusd_version;
#ifdef WITH_ACCOUNTING
		rad_snmp.acct.ident = radiusd_version;


#endif
		rad_snmp.smux_event = SMUX_NONE;
		rad_snmp.smux_password = NULL;
		rad_snmp.snmp_write_access = FALSE;
		rad_snmp.smux_fd = -1;
		rad_snmp.smux_max_failures = 3; /* FIXME! get from config */
		rad_snmp.smux_failures = 0;

		rad_snmp.auth.start_time = time(NULL);
		rad_snmp.auth.last_reset_time = rad_snmp.auth.start_time;

#ifdef WITH_ACCOUNTING
		rad_snmp.acct.start_time = rad_snmp.auth.start_time;
		rad_snmp.acct.last_reset_time = rad_snmp.auth.start_time;
#endif
	} else {
		rad_snmp.auth.last_reset_time = time(NULL);
#ifdef WITH_ACCOUNTING
		rad_snmp.acct.last_reset_time = rad_snmp.auth.last_reset_time;
#endif
		rad_snmp.smux_failures = 0;
	}

	/*
	 *  Parse the SNMP configuration information.
	 */
	cf_section_parse(cs, NULL, snmp_config);

	smux_stop();

	if (!mainconfig.do_snmp) return 0;

	/*
	 *  Do SMUX initialization.
	 */
	smux_init (radius_oid, sizeof (radius_oid) / sizeof (oid));

	if (!initialized) {
#ifdef WITH_ACCOUNTING
		SMUX_REGISTER_MIB("mibII/radius-acc-server", radiusacc_variables, variable, radacc_oid);
#endif
		SMUX_REGISTER_MIB("mibII/radius-auth-server", radiusauth_variables, variable, radauth_oid);
	}

	smux_start ();

	initialized = TRUE;

	return 1;
}

#endif /* WITH_SNMP */
