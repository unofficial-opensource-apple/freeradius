/*
 * xlat.c	Translate strings.  This is the first version of xlat
 * 		incorporated to RADIUS
 *
 * Version:	$Id$
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
 * Copyright 2000  Alan DeKok <aland@ox.org>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include	<freeradius-devel/radiusd.h>
#include	<freeradius-devel/rad_assert.h>

#include	<ctype.h>

typedef struct xlat_t {
	char		module[MAX_STRING_LEN];
	int		length;
	void		*instance;
	RAD_XLAT_FUNC	do_xlat;
	int		internal;	/* not allowed to re-define these */
} xlat_t;

static rbtree_t *xlat_root = NULL;

/*
 *	Define all xlat's in the structure.
 */
static const char * const internal_xlat[] = {"check",
					     "request",
					     "reply",
					     "proxy-request",
					     "proxy-reply",
					     "outer.request",
					     "outer.reply",
					     NULL};

#if REQUEST_MAX_REGEX > 8
#error Please fix the following line
#endif
static const int xlat_inst[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };	/* up to 8 for regex */


/*
 *	Convert the value on a VALUE_PAIR to string
 */
static int valuepair2str(char * out,int outlen,VALUE_PAIR * pair,
			 int type, RADIUS_ESCAPE_STRING func)
{
	char buffer[MAX_STRING_LEN * 4];

	if (pair != NULL) {
		vp_prints_value(buffer, sizeof(buffer), pair, -1);
		return func(out, outlen, buffer);
	}

	switch (type) {
	case PW_TYPE_STRING :
		strlcpy(out,"_",outlen);
		break;
	case PW_TYPE_INTEGER :
		strlcpy(out,"0",outlen);
		break;
	case PW_TYPE_IPADDR :
		strlcpy(out,"?.?.?.?",outlen);
		break;
	case PW_TYPE_IPV6ADDR :
		strlcpy(out,":?:",outlen);
		break;
	case PW_TYPE_DATE :
		strlcpy(out,"0",outlen);
		break;
	default :
		strlcpy(out,"unknown_type",outlen);
	}
	return strlen(out);
}


/*
 *	Dynamically translate for check:, request:, reply:, etc.
 */
static size_t xlat_packet(void *instance, REQUEST *request,
			  char *fmt, char *out, size_t outlen,
			  RADIUS_ESCAPE_STRING func)
{
	DICT_ATTR	*da;
	VALUE_PAIR	*vp;
	VALUE_PAIR	*vps = NULL;
	RADIUS_PACKET	*packet = NULL;

	switch (*(int*) instance) {
	case 0:
		vps = request->config_items;
		break;

	case 1:
		vps = request->packet->vps;
		packet = request->packet;
		break;

	case 2:
		vps = request->reply->vps;
		packet = request->reply;
		break;

	case 3:
#ifdef WITH_PROXY
		if (request->proxy) vps = request->proxy->vps;
		packet = request->proxy;
#endif
		break;

	case 4:
#ifdef WITH_PROXY
		if (request->proxy_reply) vps = request->proxy_reply->vps;
		packet = request->proxy_reply;
#endif
		break;

	case 5:
		if (request->parent) {
			vps = request->parent->packet->vps;
			packet = request->parent->packet;
		}
		break;
			
	case 6:
		if (request->parent && request->parent->reply) {
			vps = request->parent->reply->vps;
			packet = request->parent->reply;
		}
		break;
			
	default:		/* WTF? */
		return 0;
	}

	/*
	 *	The "format" string is the attribute name.
	 */
	da = dict_attrbyname(fmt);
	if (!da) {
		size_t count;
		const char *p = strchr(fmt, '[');
		char buffer[256];

		if (!p) return 0;
		if (strlen(fmt) > sizeof(buffer)) return 0;

		strlcpy(buffer, fmt, p - fmt + 1);

		da = dict_attrbyname(buffer);
		if (!da) return 0;

		/*
		 *	%{Attribute-Name[#]} returns the count of
		 *	attributes of that name in the list.
		 */
		if ((p[1] == '#') && (p[2] == ']')) {
			count = 0;

			for (vp = pairfind(vps, da->attr);
			     vp != NULL;
			     vp = pairfind(vp->next, da->attr)) {
				count++;
			}
			snprintf(out, outlen, "%d", count);
			return strlen(out);
		}

		/*
		 *	%{Attribute-Name[*]} returns ALL of the
		 *	the attributes, separated by a newline.
		 */
		if ((p[1] == '*') && (p[2] == ']')) {
			int total = 0;

			for (vp = pairfind(vps, da->attr);
			     vp != NULL;
			     vp = pairfind(vp->next, da->attr)) {
				count = valuepair2str(out, outlen - 1, vp, da->type, func);
				rad_assert(count <= outlen);
				total += count + 1;
				outlen -= (count + 1);
				out += count;

				*(out++) = '\n';

				if (outlen == 0) break;
			}

			return total;
		}

		count = atoi(p + 1);

		/*
		 *	Skip the numbers.
		 */
		p += 1 + strspn(p + 1, "0123456789");
		if (*p != ']') {
			RDEBUG2("xlat: Invalid array reference in string at %s %s",
			       fmt, p);
			return 0;
		}

		/*
		 *	Find the N'th value.
		 */
		for (vp = pairfind(vps, da->attr);
		     vp != NULL;
		     vp = pairfind(vp->next, da->attr)) {
			if (count == 0) break;
			count--;
		}

		/*
		 *	Non-existent array reference.
		 */
		if (!vp) return 0;

		return valuepair2str(out, outlen, vp, da->type, func);
	}

	vp = pairfind(vps, da->attr);
	if (!vp) {
		/*
		 *	Some "magic" handlers, which are never in VP's, but
		 *	which are in the packet.
		 *
		 *	FIXME: We should really do this in a more
		 *	intelligent way...
		 */
		if (packet) {
			VALUE_PAIR localvp;

			memset(&localvp, 0, sizeof(localvp));

			switch (da->attr) {
			case PW_PACKET_TYPE:
			{
				DICT_VALUE *dval;

				dval = dict_valbyattr(da->attr, packet->code);
				if (dval) {
					snprintf(out, outlen, "%s", dval->name);
				} else {
					snprintf(out, outlen, "%d", packet->code);
				}
				return strlen(out);
			}
			break;

			case PW_CLIENT_SHORTNAME:
				if (request->client && request->client->shortname) {
					strlcpy(out, request->client->shortname, outlen);
				} else {
					strlcpy(out, "<UNKNOWN-CLIENT>", outlen);
				}
				return strlen(out);

			case PW_CLIENT_IP_ADDRESS: /* the same as below */
			case PW_PACKET_SRC_IP_ADDRESS:
				if (packet->src_ipaddr.af != AF_INET) {
					return 0;
				}
				localvp.attribute = da->attr;
				localvp.vp_ipaddr = packet->src_ipaddr.ipaddr.ip4addr.s_addr;
				break;

			case PW_PACKET_DST_IP_ADDRESS:
				if (packet->dst_ipaddr.af != AF_INET) {
					return 0;
				}
				localvp.attribute = da->attr;
				localvp.vp_ipaddr = packet->dst_ipaddr.ipaddr.ip4addr.s_addr;
				break;

			case PW_PACKET_SRC_PORT:
				localvp.attribute = da->attr;
				localvp.vp_integer = packet->src_port;
				break;

			case PW_PACKET_DST_PORT:
				localvp.attribute = da->attr;
				localvp.vp_integer = packet->dst_port;
				break;

			case PW_PACKET_AUTHENTICATION_VECTOR:
				localvp.attribute = da->attr;
				memcpy(localvp.vp_strvalue, packet->vector,
				       sizeof(packet->vector));
				localvp.length = sizeof(packet->vector);
				break;

				/*
				 *	Authorization, accounting, etc.
				 */
			case PW_REQUEST_PROCESSING_STAGE:
				if (request->component) {
					strlcpy(out, request->component, outlen);
				} else {
					strlcpy(out, "server_core", outlen);
				}
				return strlen(out);

			case PW_PACKET_SRC_IPV6_ADDRESS:
				if (packet->src_ipaddr.af != AF_INET6) {
					return 0;
				}
				localvp.attribute = da->attr;
				memcpy(localvp.vp_strvalue,
				       &packet->src_ipaddr.ipaddr.ip6addr,
				       sizeof(packet->src_ipaddr.ipaddr.ip6addr));
				break;

			case PW_PACKET_DST_IPV6_ADDRESS:
				if (packet->dst_ipaddr.af != AF_INET6) {
					return 0;
				}
				localvp.attribute = da->attr;
				memcpy(localvp.vp_strvalue,
				       &packet->dst_ipaddr.ipaddr.ip6addr,
				       sizeof(packet->dst_ipaddr.ipaddr.ip6addr));
				break;

			case PW_VIRTUAL_SERVER:
				if (!request->server) return 0;

				snprintf(out, outlen, "%s", request->server);
				return strlen(out);
				break;

			case PW_MODULE_RETURN_CODE:
				localvp.attribute = da->attr;

				/*
				 *	See modcall.c for a bit of a hack.
				 */
				localvp.vp_integer = request->simul_max;
				break;

			default:
				return 0; /* not found */
				break;
			}

			localvp.type = da->type;
			return valuepair2str(out, outlen, &localvp,
					     da->type, func);
		}

		/*
		 *	Not found, die.
		 */
		return 0;
	}

	if (!vps) return 0;	/* silently fail */

	/*
	 *	Convert the VP to a string, and return it.
	 */
	return valuepair2str(out, outlen, vp, da->type, func);
}

#ifdef HAVE_REGEX_H
/*
 *	Pull %{0} to %{8} out of the packet.
 */
static size_t xlat_regex(void *instance, REQUEST *request,
			 char *fmt, char *out, size_t outlen,
			 RADIUS_ESCAPE_STRING func)
{
	char *regex;

	/*
	 *	We cheat: fmt is "0" to "8", but those numbers
	 *	are already in the "instance".
	 */
	fmt = fmt;		/* -Wunused */
	func = func;		/* -Wunused FIXME: do escaping? */

	regex = request_data_reference(request, request,
				 REQUEST_DATA_REGEX | *(int *)instance);
	if (!regex) return 0;

	/*
	 *	Copy UP TO "freespace" bytes, including
	 *	a zero byte.
	 */
	strlcpy(out, regex, outlen);
	return strlen(out);
}
#endif				/* HAVE_REGEX_H */


/*
 *	Change the debugging level.
 */
static size_t xlat_debug(UNUSED void *instance, REQUEST *request,
			  char *fmt, char *out, size_t outlen,
			  UNUSED RADIUS_ESCAPE_STRING func)
{
	int level = 0;

	if (*fmt) level = atoi(fmt);

	if (level == 0) {
		request->options = RAD_REQUEST_OPTION_NONE;
		request->radlog = NULL;
	} else {
		if (level > 4) level = 4;

		request->options = level;
		request->radlog = radlog_request;
	}

	snprintf(out, outlen, "%d", level);
	return strlen(out);
}


/*
 *	Compare two xlat_t structs, based ONLY on the module name.
 */
static int xlat_cmp(const void *a, const void *b)
{
	if (((const xlat_t *)a)->length != ((const xlat_t *)b)->length) {
		return ((const xlat_t *)a)->length - ((const xlat_t *)b)->length;
	}

	return memcmp(((const xlat_t *)a)->module,
		      ((const xlat_t *)b)->module,
		      ((const xlat_t *)a)->length);
}


/*
 *	find the appropriate registered xlat function.
 */
static xlat_t *xlat_find(const char *module)
{
	xlat_t my_xlat;

	/*
	 *	Look for dictionary attributes first.
	 */
	if ((dict_attrbyname(module) != NULL) ||
	    (strchr(module, '[') != NULL)) {
		module = "request";
	}

	strlcpy(my_xlat.module, module, sizeof(my_xlat.module));
	my_xlat.length = strlen(my_xlat.module);

	return rbtree_finddata(xlat_root, &my_xlat);
}


/*
 *      Register an xlat function.
 */
int xlat_register(const char *module, RAD_XLAT_FUNC func, void *instance)
{
	xlat_t	*c;
	xlat_t	my_xlat;

	if ((module == NULL) || (strlen(module) == 0)) {
		DEBUG("xlat_register: Invalid module name");
		return -1;
	}

	/*
	 *	First time around, build up the tree...
	 *
	 *	FIXME: This code should be hoisted out of this function,
	 *	and into a global "initialization".  But it isn't critical...
	 */
	if (!xlat_root) {
		int i;
#ifdef HAVE_REGEX_H
		char buffer[2];
#endif

		xlat_root = rbtree_create(xlat_cmp, free, 0);
		if (!xlat_root) {
			DEBUG("xlat_register: Failed to create tree.");
			return -1;
		}

		/*
		 *	Register the internal packet xlat's.
		 */
		for (i = 0; internal_xlat[i] != NULL; i++) {
			xlat_register(internal_xlat[i], xlat_packet, &xlat_inst[i]);
			c = xlat_find(internal_xlat[i]);
			rad_assert(c != NULL);
			c->internal = TRUE;
		}

		/*
		 *	New name: "control"
		 */
		xlat_register("control", xlat_packet, &xlat_inst[0]);
		c = xlat_find("control");
		rad_assert(c != NULL);
		c->internal = TRUE;

#ifdef HAVE_REGEX_H
		/*
		 *	Register xlat's for regexes.
		 */
		buffer[1] = '\0';
		for (i = 0; i <= REQUEST_MAX_REGEX; i++) {
			buffer[0] = '0' + i;
			xlat_register(buffer, xlat_regex, &xlat_inst[i]);
			c = xlat_find(buffer);
			rad_assert(c != NULL);
			c->internal = TRUE;
		}
#endif /* HAVE_REGEX_H */


		xlat_register("debug", xlat_debug, &xlat_inst[0]);
		c = xlat_find("debug");
		rad_assert(c != NULL);
		c->internal = TRUE;
	}

	/*
	 *	If it already exists, replace the instance.
	 */
	strlcpy(my_xlat.module, module, sizeof(my_xlat.module));
	my_xlat.length = strlen(my_xlat.module);
	c = rbtree_finddata(xlat_root, &my_xlat);
	if (c) {
		if (c->internal) {
			DEBUG("xlat_register: Cannot re-define internal xlat");
			return -1;
		}

		c->do_xlat = func;
		c->instance = instance;
		return 0;
	}

	/*
	 *	Doesn't exist.  Create it.
	 */
	c = rad_malloc(sizeof(*c));
	memset(c, 0, sizeof(*c));

	c->do_xlat = func;
	strlcpy(c->module, module, sizeof(c->module));
	c->length = strlen(c->module);
	c->instance = instance;

	rbtree_insert(xlat_root, c);

	return 0;
}

/*
 *      Unregister an xlat function.
 *
 *	We can only have one function to call per name, so the
 *	passing of "func" here is extraneous.
 */
void xlat_unregister(const char *module, RAD_XLAT_FUNC func)
{
	rbnode_t	*node;
	xlat_t		my_xlat;

	func = func;		/* -Wunused */

	if (!module) return;

	strlcpy(my_xlat.module, module, sizeof(my_xlat.module));
	my_xlat.length = strlen(my_xlat.module);

	node = rbtree_find(xlat_root, &my_xlat);
	if (!node) return;

	rbtree_delete(xlat_root, node);
}

/*
 *	De-register all xlat functions,
 *	used mainly for debugging.
 */
void xlat_free(void)
{
	rbtree_free(xlat_root);
}


/*
 *	Decode an attribute name into a string.
 */
static void decode_attribute(const char **from, char **to, int freespace,
			     int *open_p, REQUEST *request,
			     RADIUS_ESCAPE_STRING func)
{
	int	do_length = 0;
	char	xlat_name[128];
	char	*xlat_string = NULL; /* can be large */
	int	free_xlat_string = FALSE;
	const char *p;
	char *q, *pa;
	int found=0, retlen=0;
	int openbraces = *open_p;
	const xlat_t *c;
	int spaces = FALSE;

	p = *from;
	q = *to;
	pa = &xlat_name[0];

	*q = '\0';

	/*
	 * Skip the '{' at the front of 'p'
	 * Increment open braces
	 */
	p++;
	openbraces++;

	if (*p == '#') {
		p++;
		do_length = 1;
	}

	/*
	 *	Handle %{%{foo}:-%{bar}}, which is useful, too.
	 *
	 *	Did I mention that this parser is garbage?
	 */
	if ((p[0] == '%') && (p[1] == '{')) {
		/*
		 *	This is really bad, but it works.
		 */
		int len1, len2;
		size_t mylen = strlen(p);
		char *first = rad_malloc(mylen + 1);
		char *second = rad_malloc(mylen + 1);
		int expand2 = FALSE;

		len1 = rad_copy_variable(first, p);
		if (len1 < 0) {
			RDEBUG2("Badly formatted variable: %s", p);
			goto free_and_done;
		}

		if ((p[len1] != ':') || (p[len1 + 1] != '-')) {
			RDEBUG2("No trailing :- after variable at %s", p);
			goto free_and_done;
		}

		p += len1 + 2;

		if ((p[0] == '%') && (p[1] == '{')) {
			len2 = rad_copy_variable(second, p);

			expand2 = TRUE;
			if (len2 < 0) {
				RDEBUG2("Invalid text after :- at %s", p);
				goto free_and_done;
			}
			p += len2;

		} else if ((p[0] == '"') || p[0] == '\'') {
			getstring(&p, second, mylen);

		} else {
			char *s = second;

			while (*p && (*p != '}')) {
				*(s++) = *(p++);
			}
			*s = '\0';
		}

		if (*p != '}') {
			RDEBUG2("Failed to find trailing '}' in string");
			goto free_and_done;
		}

		mylen = radius_xlat(q, freespace, first, request, func);
		if (mylen) {
			q += mylen;
			goto free_and_done;
		}

		if (!expand2) {
			strlcpy(q, second, freespace);
			q += strlen(q);
		} else {
			mylen = radius_xlat(q, freespace, second,
					    request, func);
			if (mylen) {
				q += mylen;
				goto free_and_done;
			}
		}

		/*
		 *	Else the output is an empty string.
		 */
	free_and_done:
		free(first);
		free(second);
		goto done;
	}


	/*
	 *	First, copy the xlat key name to one buffer
	 */
	while (*p && (*p != '}') && (*p != ':')) {
		*pa++ = *p++;

		if (pa >= (xlat_name + sizeof(xlat_name) - 1)) {
			/*
			 *	Skip to the end of the input
			 */
			p += strlen(p);
			RDEBUG("xlat: Module name is too long in string %%%s",
			      *from);
			goto done;
		}
	}
	*pa = '\0';

	if (!*p) {
		RDEBUG("xlat: Invalid syntax in %s", *from);

		/*
		 *	%{name} is a simple attribute reference,
		 *	or regex reference.
		 */
	} else if (*p == '}') {
		openbraces--;
		rad_assert(openbraces == *open_p);

		p++;
		xlat_string = xlat_name;
		goto do_xlat;

	} else if ((p[0] == ':') && (p[1] == '-')) { /* handle ':- */
		RDEBUG2("WARNING: Deprecated conditional expansion \":-\".  See \"man unlang\" for details");
		p += 2;
		xlat_string = xlat_name;
		goto do_xlat;

	} else {      /* module name, followed by per-module string */
		int stop = 0;
		int delimitbrace = *open_p;

		rad_assert(*p == ':');
		p++;			/* skip the ':' */

		/*
		 *  If there's a brace immediately following the colon,
		 *  then we've chosen to delimite the per-module string,
		 *  so keep track of that.
		 */
		if (*p == '{') {
			delimitbrace = openbraces;
			openbraces++;
			p++;
		}

		xlat_string = rad_malloc(strlen(p) + 1); /* always returns */
		free_xlat_string = TRUE;
		pa = xlat_string;

		/*
		 *  Copy over the rest of the string, which is per-module
		 *  data.
		 */
		while (*p && !stop) {
			switch(*p) {

				/*
				 *	What the heck is this supposed
				 *	to be doing?
				 */
			case '\\':
				p++; /* skip it */
				*pa++ = *p++;
				break;

			case ':':
				if (!spaces && p[1] == '-') {
					p += 2;
					stop = 1;
					break;
				}
				*pa++ = *p++;
				break;

				/*
				 *	This is pretty hokey...  we
				 *	should use the functions in
				 *	util.c
				 */
			case '{':
				openbraces++;
				*pa++ = *p++;
				break;

			case '}':
				openbraces--;
				if (openbraces == delimitbrace) {
					p++;
					stop=1;
				} else {
					*pa++ = *p++;
				}
				break;

			case ' ':
			case '\t':
				spaces = TRUE;
				/* FALL-THROUGH */

			default:
				*pa++ = *p++;
				break;
			}
		}

		*pa = '\0';

		/*
		 *	Now check to see if we're at the end of the string
		 *	we were sent.  If we're not, check for :-
		 */
		if (openbraces == delimitbrace) {
			if (p[0] == ':' && p[1] == '-') {
				p += 2;
			}
		}

		/*
		 *	Look up almost everything in the new tree of xlat
		 *	functions.  This makes it a little quicker...
		 */
	do_xlat:
		if ((c = xlat_find(xlat_name)) != NULL) {
			if (!c->internal) RDEBUG3("radius_xlat: Running registered xlat function of module %s for string \'%s\'",
						c->module, xlat_string);
			retlen = c->do_xlat(c->instance, request, xlat_string,
					    q, freespace, func);
			/* If retlen is 0, treat it as not found */
			if (retlen > 0) found = 1;

#ifndef NRDEBUG
		} else {
			/*
			 *	No attribute by that name, return an error.
			 */
			RDEBUG2("WARNING: Unknown module \"%s\" in string expansion \"%%%s\"", xlat_name, *from);
#endif
		}
	}

	/*
	 * Skip to last '}' if attr is found
	 * The rest of the stuff within the braces is
	 * useless if we found what we need
	 */
	if (found) {
		if (do_length) {
			snprintf(q, freespace, "%d", retlen);
			retlen = strlen(q);
		}

		q += retlen;

		while((*p != '\0') && (openbraces > *open_p)) {
			/*
			 *	Handle escapes outside of the loop.
			 */
			if (*p == '\\') {
				p++;
				if (!*p) break;
				p++; /* get & ignore next character */
				continue;
			}

			switch (*p) {
			default:
				break;

				/*
				 *  Bare brace
				 */
			case '{':
				openbraces++;
				break;

			case '}':
				openbraces--;
				break;
			}
			p++;	/* skip the character */
		}
	}

	done:
	if (free_xlat_string) free(xlat_string);

	*open_p = openbraces;
	*from = p;
	*to = q;
}

/*
 *  If the caller doesn't pass xlat an escape function, then
 *  we use this one.  It simplifies the coding, as the check for
 *  func == NULL only happens once.
 */
static size_t xlat_copy(char *out, size_t outlen, const char *in)
{
	int freespace = outlen;

	rad_assert(outlen > 0);

	while ((*in) && (freespace > 1)) {
		/*
		 *  Copy data.
		 *
		 *  FIXME: Do escaping of bad stuff!
		 */
		*(out++) = *(in++);

		freespace--;
	}
	*out = '\0';

	return (outlen - freespace); /* count does not include NUL */
}

/*
 *	Replace %<whatever> in a string.
 *
 *	See 'doc/variables.txt' for more information.
 */
int radius_xlat(char *out, int outlen, const char *fmt,
		REQUEST *request, RADIUS_ESCAPE_STRING func)
{
	int c, len, freespace;
	const char *p;
	char *q;
	char *nl;
	VALUE_PAIR *tmp;
	struct tm *TM, s_TM;
	char tmpdt[40]; /* For temporary storing of dates */
	int openbraces=0;

	/*
	 *	Catch bad modules.
	 */
	if (!fmt || !out || !request) return 0;

	/*
	 *  Ensure that we always have an escaping function.
	 */
	if (func == NULL) {
		func = xlat_copy;
	}

       	q = out;
	p = fmt;
	while (*p) {
		/* Calculate freespace in output */
		freespace = outlen - (q - out);
		if (freespace <= 1)
			break;
		c = *p;

		if ((c != '%') && (c != '$') && (c != '\\')) {
			/*
			 * We check if we're inside an open brace.  If we are
			 * then we assume this brace is NOT literal, but is
			 * a closing brace and apply it
			 */
			if ((c == '}') && openbraces) {
				openbraces--;
				p++; /* skip it */
				continue;
			}
			*q++ = *p++;
			continue;
		}

		/*
		 *	There's nothing after this character, copy
		 *	the last '%' or "$' or '\\' over to the output
		 *	buffer, and exit.
		 */
		if (*++p == '\0') {
			*q++ = c;
			break;
		}

		if (c == '\\') {
			switch(*p) {
			case '\\':
				*q++ = *p;
				break;
			case 't':
				*q++ = '\t';
				break;
			case 'n':
				*q++ = '\n';
				break;
			default:
				*q++ = c;
				*q++ = *p;
				break;
			}
			p++;

		} else if (c == '%') switch(*p) {
			case '{':
				decode_attribute(&p, &q, freespace, &openbraces, request, func);
				break;

			case '%':
				*q++ = *p++;
				break;
			case 'a': /* Protocol: */
				q += valuepair2str(q,freespace,pairfind(request->reply->vps,PW_FRAMED_PROTOCOL),PW_TYPE_INTEGER, func);
				p++;
				break;
			case 'c': /* Callback-Number */
				q += valuepair2str(q,freespace,pairfind(request->reply->vps,PW_CALLBACK_NUMBER),PW_TYPE_STRING, func);
				p++;
				break;
			case 'd': /* request day */
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%d", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'f': /* Framed IP address */
				q += valuepair2str(q,freespace,pairfind(request->reply->vps,PW_FRAMED_IP_ADDRESS),PW_TYPE_IPADDR, func);
				p++;
				break;
			case 'i': /* Calling station ID */
				q += valuepair2str(q,freespace,pairfind(request->packet->vps,PW_CALLING_STATION_ID),PW_TYPE_STRING, func);
				p++;
				break;
			case 'l': /* request timestamp */
				snprintf(tmpdt, sizeof(tmpdt), "%lu",
					 (unsigned long) request->received.tv_sec);
				strlcpy(q,tmpdt,freespace);
				q += strlen(q);
				p++;
				break;
			case 'm': /* request month */
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%m", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'n': /* NAS IP address */
				q += valuepair2str(q,freespace,pairfind(request->packet->vps,PW_NAS_IP_ADDRESS),PW_TYPE_IPADDR, func);
				p++;
				break;
			case 'p': /* Port number */
				q += valuepair2str(q,freespace,pairfind(request->packet->vps,PW_NAS_PORT),PW_TYPE_INTEGER, func);
				p++;
				break;
			case 's': /* Speed */
				q += valuepair2str(q,freespace,pairfind(request->packet->vps,PW_CONNECT_INFO),PW_TYPE_STRING, func);
				p++;
				break;
			case 't': /* request timestamp */
				CTIME_R(&request->timestamp, tmpdt, sizeof(tmpdt));
				nl = strchr(tmpdt, '\n');
				if (nl) *nl = '\0';
				strlcpy(q, tmpdt, freespace);
				q += strlen(q);
				p++;
				break;
			case 'u': /* User name */
				q += valuepair2str(q,freespace,pairfind(request->packet->vps,PW_USER_NAME),PW_TYPE_STRING, func);
				p++;
				break;
			case 'A': /* radacct_dir */
				strlcpy(q,radacct_dir,freespace);
				q += strlen(q);
				p++;
				break;
			case 'C': /* ClientName */
				strlcpy(q,request->client->shortname,freespace);
				q += strlen(q);
				p++;
				break;
			case 'D': /* request date */
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%Y%m%d", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'H': /* request hour */
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%H", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'L': /* radlog_dir */
				strlcpy(q,radlog_dir,freespace);
				q += strlen(q);
				p++;
				break;
			case 'M': /* MTU */
				q += valuepair2str(q,freespace,pairfind(request->reply->vps,PW_FRAMED_MTU),PW_TYPE_INTEGER, func);
				p++;
				break;
			case 'R': /* radius_dir */
				strlcpy(q,radius_dir,freespace);
				q += strlen(q);
				p++;
				break;
			case 'S': /* request timestamp in SQL format*/
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%Y-%m-%d %H:%M:%S", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'T': /* request timestamp */
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%Y-%m-%d-%H.%M.%S.000000", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'U': /* Stripped User name */
				q += valuepair2str(q,freespace,pairfind(request->packet->vps,PW_STRIPPED_USER_NAME),PW_TYPE_STRING, func);
				p++;
				break;
			case 'V': /* Request-Authenticator */
				strlcpy(q,"Verified",freespace);
				q += strlen(q);
				p++;
				break;
			case 'Y': /* request year */
				TM = localtime_r(&request->timestamp, &s_TM);
				len = strftime(tmpdt, sizeof(tmpdt), "%Y", TM);
				if (len > 0) {
					strlcpy(q, tmpdt, freespace);
					q += strlen(q);
				}
				p++;
				break;
			case 'Z': /* Full request pairs except password */
				tmp = request->packet->vps;
				while (tmp && (freespace > 3)) {
					if (tmp->attribute != PW_USER_PASSWORD) {
						*q++ = '\t';
						len = vp_prints(q, freespace - 2, tmp);
						q += len;
						freespace -= (len + 2);
						*q++ = '\n';
					}
					tmp = tmp->next;
				}
				p++;
				break;
			default:
				RDEBUG2("WARNING: Unknown variable '%%%c': See 'doc/variables.txt'", *p);
				if (freespace > 2) {
					*q++ = '%';
					*q++ = *p++;
				}
				break;
		}
	}
	*q = '\0';

	RDEBUG2("\texpand: %s -> %s", fmt, out);

	return strlen(out);
}
