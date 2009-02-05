/*
 * x99_rlm.c
 * $Id: x99_rlm.c,v 1.2 2005/10/28 23:34:08 snsimon Exp $
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright 2000,2001,2002  The FreeRADIUS server project
 * Copyright 2001,2002  Google, Inc.
 */

/*
 * STRONG WARNING SECTION:
 *
 * ANSI X9.9 has been withdrawn as a standard, due to the weakness of DES.
 * An attacker can learn the token's secret by observing two
 * challenge/response pairs.  See ANSI document X9 TG-24-1999
 * <URL:http://www.x9.org/TG24_1999.pdf>.
 *
 * Please read the accompanying docs.
 */

/*
 * TODO: support setting multiple auth-types in authorize()
 * TODO: support soft PIN? ???
 * TODO: support other than ILP32 (for State)
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>	/* htonl() */

#ifdef FREERADIUS
#include "radiusd.h"
#include "modules.h"
#endif
#include "x99.h"

static const char rcsid[] = "$Id: x99_rlm.c,v 1.2 2005/10/28 23:34:08 snsimon Exp $";

/* Global data */
static int rnd_fd;			/* fd for random device           */
static unsigned char hmac_key[16];	/* to protect State attribute     */

/* A mapping of configuration file names to internal variables. */
static CONF_PARSER module_config[] = {
    { "pwdfile", PW_TYPE_STRING_PTR, offsetof(x99_token_t, pwdfile),
      NULL, PWDFILE },
    { "syncdir", PW_TYPE_STRING_PTR, offsetof(x99_token_t, syncdir),
      NULL, SYNCDIR },
    { "challenge_prompt", PW_TYPE_STRING_PTR, offsetof(x99_token_t,chal_prompt),
      NULL, CHALLENGE_PROMPT },
    { "challenge_length", PW_TYPE_INTEGER, offsetof(x99_token_t, chal_len),
      NULL, "6" },
    { "challenge_delay", PW_TYPE_INTEGER, offsetof(x99_token_t, chal_delay),
      NULL, "30" },
    { "softfail", PW_TYPE_INTEGER, offsetof(x99_token_t, softfail),
      NULL, "5" },
    { "hardfail", PW_TYPE_INTEGER, offsetof(x99_token_t, hardfail),
      NULL, "0" },
    { "allow_sync", PW_TYPE_BOOLEAN, offsetof(x99_token_t, allow_sync),
      NULL, "yes" },
    { "fast_sync", PW_TYPE_BOOLEAN, offsetof(x99_token_t, fast_sync),
      NULL, "yes" },
    { "allow_async", PW_TYPE_BOOLEAN, offsetof(x99_token_t, allow_async),
      NULL, "no" },
    { "challenge_req", PW_TYPE_STRING_PTR, offsetof(x99_token_t, chal_req),
      NULL, CHALLENGE_REQ },
    { "resync_req", PW_TYPE_STRING_PTR, offsetof(x99_token_t, resync_req),
      NULL, RESYNC_REQ },
    { "ewindow_size", PW_TYPE_INTEGER, offsetof(x99_token_t, ewindow_size),
      NULL, "0" },
    { "ewindow2_size", PW_TYPE_INTEGER, offsetof(x99_token_t, ewindow2_size),
      NULL, "0" },
    { "ewindow2_delay", PW_TYPE_INTEGER, offsetof(x99_token_t, ewindow2_delay),
      NULL, "60" },
    { "mschapv2_mppe", PW_TYPE_INTEGER,
      offsetof(x99_token_t, mschapv2_mppe_policy), NULL, "2" },
    { "mschapv2_mppe_bits", PW_TYPE_INTEGER,
      offsetof(x99_token_t, mschapv2_mppe_types), NULL, "2" },
    { "mschap_mppe", PW_TYPE_INTEGER,
      offsetof(x99_token_t, mschap_mppe_policy), NULL, "2" },
    { "mschap_mppe_bits", PW_TYPE_INTEGER,
      offsetof(x99_token_t, mschap_mppe_types), NULL, "2" },
#if 0
    { "twindow_min", PW_TYPE_INTEGER, offsetof(x99_token_t, twindow_min),
      NULL, "0" },
    { "twindow_max", PW_TYPE_INTEGER, offsetof(x99_token_t, twindow_max),
      NULL, "0" },
#endif

    { NULL, -1, 0, NULL, NULL }		/* end the list */
};


/* per-module initialization */
static int
x99_token_init(void)
{
    if ((rnd_fd = open(DEVURANDOM, O_RDONLY)) == -1) {
	x99_log(X99_LOG_ERR, "init: error opening %s: %s", DEVURANDOM,
		strerror(errno));
	return -1;
    }

    /* Generate a random key, used to protect the State attribute. */
    if (x99_get_random(rnd_fd, hmac_key, sizeof(hmac_key)) == -1) {
	x99_log(X99_LOG_ERR, "init: failed to obtain random data for hmac_key");
	return -1;
    }

    /* Initialize the passcode encoding/checking functions. */
    x99_pwe_init();

    return 0;
}


/* per-instance initialization */
static int
x99_token_instantiate(CONF_SECTION *conf, void **instance)
{
    x99_token_t *data;
    char *p;
    struct stat st;

    /* Set up a storage area for instance data. */
    data = rad_malloc(sizeof(*data));
    if (!data)
	return -1;
    memset(data, 0, sizeof(*data));

    /* If the configuration parameters can't be parsed, then fail. */
    if (cf_section_parse(conf, data, module_config) < 0) {
	free(data);
	return -1;
    }

    /* Verify ranges for those vars that are limited. */
    if ((data->chal_len < 5) || (data->chal_len > MAX_CHALLENGE_LEN)) {
	data->chal_len = 6;
	x99_log(X99_LOG_ERR,
		"invalid challenge_length, range 5-%d, using default of 6",
		MAX_CHALLENGE_LEN);

    }

    /* Enforce a single "%" sequence, which must be "%s" */
    p = strchr(data->chal_prompt, '%');
    if ((p == NULL) || (p != strrchr(data->chal_prompt, '%')) ||
	strncmp(p,"%s",2)){
	free(data->chal_prompt);
	data->chal_prompt = strdup(CHALLENGE_PROMPT);
	x99_log(X99_LOG_ERR,
		"invalid challenge_prompt, using default of \"%s\"",
		CHALLENGE_PROMPT);
    }

    if (data->softfail < 0) {
	data->softfail = 5;
	x99_log(X99_LOG_ERR, "softfail must be at least 1 "
		"(or 0 == infinite), using default of 5");
    }

    if (data->hardfail < 0) {
	data->hardfail = 0;
	x99_log(X99_LOG_ERR, "hardfail must be at least 1 "
		"(or 0 == infinite), using default of 0");
    }

    if (data->fast_sync && !data->allow_sync) {
	data->fast_sync = 0;
	x99_log(X99_LOG_INFO,
		"fast_sync is yes, but allow_sync is no; disabling fast_sync");
    }

    if (!data->allow_sync && !data->allow_async) {
	x99_log(X99_LOG_ERR,
		"at least one of {allow_async, allow_sync} must be set");
	free(data);
	return -1;
    }

    if ((data->ewindow_size > MAX_EWINDOW_SIZE) || (data->ewindow_size < 0)) {
	data->ewindow_size = 0;
	x99_log(X99_LOG_ERR, "max ewindow_size is %d, using default of 0",
		MAX_EWINDOW_SIZE);
    }

    if (data->ewindow2_size && (data->ewindow2_size < data->ewindow_size)) {
	data->ewindow2_size = 0;
	x99_log(X99_LOG_ERR, "ewindow2_size must be at least as large as "
			     "ewindow_size, using default of 0");
    }

    if (data->ewindow2_size && !data->ewindow2_delay) {
	data->ewindow2_size = 0;
	x99_log(X99_LOG_ERR, "ewindow2_size is non-zero, "
			     "but ewindow2_delay is zero; disabling ewindow2");
    }

    if ((data->mschapv2_mppe_policy > 2) || (data->mschapv2_mppe_policy < 0)) {
	data->mschapv2_mppe_policy = 2;
	x99_log(X99_LOG_ERR,
		"invalid value for mschapv2_mppe, using default of 2");
    }

    if ((data->mschapv2_mppe_types > 2) || (data->mschapv2_mppe_types < 0)) {
	data->mschapv2_mppe_types = 2;
	x99_log(X99_LOG_ERR,
		"invalid value for mschapv2_mppe_bits, using default of 2");
    }

    if ((data->mschap_mppe_policy > 2) || (data->mschap_mppe_policy < 0)) {
	data->mschap_mppe_policy = 2;
	x99_log(X99_LOG_ERR,
		"invalid value for mschap_mppe, using default of 2");
    }

    if (data->mschap_mppe_types != 2) {
	data->mschap_mppe_types = 2;
	x99_log(X99_LOG_ERR,
		"invalid value for mschap_mppe_bits, using default of 2");
    }

#if 0
    if (data->twindow_max - data->twindow_min > MAX_TWINDOW_SIZE) {
	data->twindow_min = data->twindow_max = 0;
	x99_log(X99_LOG_ERR, "max time window size is %d, using default of 0",
		MAX_TWINDOW_SIZE);
    }
    if ((data->twindow_min > 0) || (data->twindow_max < 0) ||
	(data->twindow_max < data->twindow_min)) {
	data->twindow_min = data->twindow_max = 0;
	x99_log(X99_LOG_ERR,
		"invalid values for time window, using default of 0");
    }
#endif

    if (stat(data->syncdir, &st) != 0) {
	x99_log(X99_LOG_ERR, "syncdir %s error: %s",
		data->syncdir, strerror(errno));
	free(data);
	return -1;
    }
    if (st.st_mode != (S_IFDIR|S_IRWXU)) {
	x99_log(X99_LOG_ERR, "syncdir %s has loose permissions", data->syncdir);
	free(data);
	return -1;
    }

    /* Set the instance name (for use with authorize()) */
    data->name = cf_section_name2(conf);
    if (!data->name)
	data->name = cf_section_name1(conf);
    if (!data->name) {
	x99_log(X99_LOG_ERR, "no instance name (this can't happen)");
	free(data);
	return -1;
    }

    *instance = data;
    return 0;
}


/* Generate a challenge to be presented to the user. */
static int
x99_token_authorize(void *instance, REQUEST *request)
{
    x99_token_t *inst = (x99_token_t *) instance;

    char challenge[MAX_CHALLENGE_LEN + 1];	/* +1 for '\0' terminator */
    char *state;
    int rc;

    x99_user_info_t user_info;
    int user_found, auth_type_found;
    int pwattr;
    int32_t sflags = 0; /* flags for state */
    VALUE_PAIR *vp;

    /* Early exit if Auth-Type != inst->name */
    auth_type_found = 0;
    if ((vp = pairfind(request->config_items, PW_AUTHTYPE)) != NULL) {
	auth_type_found = 1;
	if (strcmp(vp->strvalue, inst->name)) {
	    return RLM_MODULE_NOOP;
	}
    }

    /* The State attribute will be present if this is a response. */
    if (pairfind(request->packet->vps, PW_STATE) != NULL) {
	DEBUG("rlm_x99_token: autz: Found response to access challenge");
	return RLM_MODULE_OK;
    }

    /* User-Name attribute required. */
    if (!request->username) {
	x99_log(X99_LOG_AUTH,
		"autz: Attribute \"User-Name\" required for authentication.");
	return RLM_MODULE_INVALID;
    }

    if ((pwattr = x99_pw_present(request)) == 0) {
	x99_log(X99_LOG_AUTH, "autz: Attribute \"User-Password\" "
		"or equivalent required for authentication.");
	return RLM_MODULE_INVALID;
    }

    /* Look up the user's info. */
    user_found = 1;
    if ((rc = x99_get_user_info(inst->pwdfile, request->username->strvalue,
				&user_info)) == -2) {
#if 0
	/* x99_get_user_info() logs a more useful message, this is noisy. */
	x99_log(X99_LOG_ERR, "autz: error reading user [%s] info",
		request->username->strvalue);
#endif
	return RLM_MODULE_FAIL;
    }
    if (rc == -1) {
	x99_log(X99_LOG_AUTH, "autz: user [%s] not found in %s",
		request->username->strvalue, inst->pwdfile);
	memset(&user_info, 0, sizeof(user_info)); /* X99_CF_NONE */
	user_found = 0;
    }

    /* fast_sync mode (challenge only if requested) */
    if (inst->fast_sync &&
	((user_info.card_id & X99_CF_SM) || !user_found)) {

	if ((x99_pw_valid(request, inst, pwattr, inst->resync_req, NULL) &&
		/* Set a bit indicating resync */ (sflags |= htonl(1))) ||
	    x99_pw_valid(request, inst, pwattr, inst->chal_req, NULL)) {
	    /*
	     * Generate a challenge if requested.  We don't test for card
	     * support [for async] because it's tricky for unknown users.
	     * Some configurations would have a problem where known users
	     * cannot request a challenge, but unknown users can.  This
	     * reveals information.  The easiest fix seems to be to always
	     * hand out a challenge on request.
	     * We also don't test if the server allows async mode, this
	     * would also reveal information.
	     */
	    DEBUG("rlm_x99_token: autz: fast_sync challenge requested");
	    goto gen_challenge;

	} else {
	    /*
	     * Otherwise, this is the token sync response.  Signal
	     * the authenticate code to ignore State.  We don't need
	     * to set a value, /existence/ of the vp is the signal.
	     */
	    if ((vp = paircreate(PW_X99_FAST, PW_TYPE_INTEGER)) == NULL) {
		x99_log(X99_LOG_CRIT, "autz: no memory");
		return RLM_MODULE_FAIL;
	    }
	    pairadd(&request->config_items, vp);
	    DEBUG("rlm_x99_token: autz: using fast_sync");

	    if (!auth_type_found)
		pairadd(&request->config_items,
			pairmake("Auth-Type", "x99_token", T_OP_EQ));
	    return RLM_MODULE_OK;

	}
    } /* if (fast_sync && card supports sync mode) */

gen_challenge:
    /* Set the resync bit by default if the user can't request it. */
    if (!inst->fast_sync)
	sflags |= htonl(1);

    /* Generate a random challenge. */
    if (x99_get_challenge(rnd_fd, challenge, inst->chal_len) == -1) {
	x99_log(X99_LOG_ERR, "autz: failed to obtain random challenge");
	return RLM_MODULE_FAIL;
    }

    /*
     * Create the State attribute, which will be returned to us along with
     * the response.  We will need this to verify the response.  Create
     * a strong state if the user will be able use this with their token.
     * Otherwise, we discard it anyway, so don't "waste" time with hmac.
     * We also don't do the hmac if the user wasn't found (mask won't match).
     * We always create at least a trivial state, so x99_token_authorize()
     * can easily pass on to x99_token_authenticate().
     */
    if (user_info.card_id & X99_CF_AM) {
	time_t now = time(NULL);

	if (sizeof(now) != 4 || sizeof(long) != 4) {
	    x99_log(X99_LOG_ERR, "autz: only ILP32 arch is supported");
	    return RLM_MODULE_FAIL;
	}
	now = htonl(now);

	if (x99_gen_state(&state, NULL, challenge, sflags, now, hmac_key) != 0){
	    x99_log(X99_LOG_ERR, "autz: failed to generate state");
	    return RLM_MODULE_FAIL;
	}
    } else {
	/* x2 b/c pairmake() string->octet needs even num of digits */
	state = rad_malloc(3 + inst->chal_len * 2);
	(void) sprintf(state, "0x%s%s", challenge, challenge);
    }
    pairadd(&request->reply->vps, pairmake("State", state, T_OP_EQ));
    free(state);

    /* Add the challenge to the reply. */
    {
	char *u_challenge;	/* challenge with addt'l presentation text */

	u_challenge = rad_malloc(strlen(inst->chal_prompt)+MAX_CHALLENGE_LEN+1);
	(void) sprintf(u_challenge, inst->chal_prompt, challenge);
	pairadd(&request->reply->vps,
		pairmake("Reply-Message", u_challenge, T_OP_EQ));
	free(u_challenge);
    }

    /*
     * Mark the packet as an Access-Challenge packet.
     * The server will take care of sending it to the user.
     */
    request->reply->code = PW_ACCESS_CHALLENGE;
    DEBUG("rlm_x99_token: Sending Access-Challenge.");

    /* TODO: support config-specific auth-type */
    if (!auth_type_found)
	pairadd(&request->config_items,
		pairmake("Auth-Type", "x99_token", T_OP_EQ));
    return RLM_MODULE_HANDLED;
}


/* Verify the response entered by the user. */
static int
x99_token_authenticate(void *instance, REQUEST *request)
{
    x99_token_t *inst = (x99_token_t *) instance;

    x99_user_info_t user_info;
    char *username;
    int i, pwattr, rc, fc;
    int32_t sflags = 0; 	/* flags from state */
    time_t last_auth;		/* time of last authentication */
    unsigned auth_pos = 0;	/* window position of last authentication */

    char challenge[MAX_CHALLENGE_LEN + 1];
    char e_response[9];		/* expected response */
    VALUE_PAIR *add_vps = NULL;

    /* User-Name attribute required. */
    if (!request->username) {
	x99_log(X99_LOG_AUTH,
		"auth: Attribute \"User-Name\" required for authentication.");
	return RLM_MODULE_INVALID;
    }
    username = request->username->strvalue;

    if ((pwattr = x99_pw_present(request)) == 0) {
	x99_log(X99_LOG_AUTH, "auth: Attribute \"User-Password\" "
		"or equivalent required for authentication.");
	return RLM_MODULE_INVALID;
    }

    /* Add a message to the auth log. */
    pairadd(&request->packet->vps, pairmake("Module-Failure-Message",
					    X99_MODULE_NAME, T_OP_EQ));
    pairadd(&request->packet->vps, pairmake("Module-Success-Message",
					    X99_MODULE_NAME, T_OP_EQ));

    /* Look up the user's info. */
    if (x99_get_user_info(inst->pwdfile, username, &user_info) != 0) {
#if 0
	/* x99_get_user_info() logs a more useful message, this is noisy. */
	x99_log(X99_LOG_AUTH, "auth: error reading user [%s] info", username);
#endif
	return RLM_MODULE_REJECT;
    }

    /* Retrieve the challenge (from State attribute), unless (fast_sync). */
    if (pairfind(request->config_items, PW_X99_FAST) == NULL) {
	VALUE_PAIR	*vp;
	unsigned char	*state;
	time_t		then;

	if ((vp = pairfind(request->packet->vps, PW_STATE)) != NULL) {
	    int e_length = inst->chal_len;

	    /* Extend expected length if state should have been protected. */
	    if (user_info.card_id & X99_CF_AM)
		e_length += 4 + 4 + 16; /* sflags + time + hmac */

	    if (vp->length != e_length) {
		x99_log(X99_LOG_AUTH,
			"auth: bad state for [%s]: length", username);
		return RLM_MODULE_INVALID;
	    }

	    /* Fast path if we didn't protect the state. */
	    if (!(user_info.card_id & X99_CF_AM))
		goto good_state;

	    /* Verify the state. */
	    (void) memset(challenge, 0, sizeof(challenge));
	    (void) memcpy(challenge, vp->strvalue, inst->chal_len);
	    (void) memcpy(&sflags, vp->strvalue + inst->chal_len, 4);
	    (void) memcpy(&then, vp->strvalue + inst->chal_len + 4, 4);
	    if (x99_gen_state(NULL,&state,challenge,sflags,then,hmac_key) != 0){
		x99_log(X99_LOG_ERR, "auth: failed to generate state");
		return RLM_MODULE_FAIL;
	    }
	    if (memcmp(state, vp->strvalue, vp->length)) {
		x99_log(X99_LOG_AUTH,
			"auth: bad state for [%s]: hmac", username);
		free(state);
		return RLM_MODULE_REJECT;
	    }
	    free(state);

	    /* State is valid, but check expiry. */
	    then = ntohl(then);
	    if (time(NULL) - then > inst->chal_delay) {
		x99_log(X99_LOG_AUTH,
			"auth: bad state for [%s]: expired", username);
		return RLM_MODULE_REJECT;
	    }
	} else {
	    /* This should only happen if the authorize code didn't run. */
	    x99_log(X99_LOG_ERR, "auth: bad state for [%s]: missing "
		    "(is x99_token listed in radiusd.conf's authorize stanza?)",
		    username);
	    return RLM_MODULE_FAIL;
	}
    } /* if (!fast_sync) */

good_state:
	    /* State is good! */

    /* Get the time of the last authentication. */
    if (x99_get_last_auth(inst->syncdir, username, &last_auth) != 0) {
	x99_log(X99_LOG_ERR,
		"auth: unable to get last auth time for [%s]", username);
	return RLM_MODULE_FAIL;
    }

    /* Check failure count. */
    fc = x99_check_failcount(username, inst);
    if ((fc == FAIL_ERR) || (fc == FAIL_HARD))
	return RLM_MODULE_USERLOCK;

    /* Some checks for ewindow2_size logic. */
    if (fc == FAIL_SOFT) {
	if (!inst->ewindow2_size)	/* no auto-resync */
	    return RLM_MODULE_USERLOCK;

	if (!pairfind(request->config_items, PW_X99_FAST)) {
	    /*
	     * ewindow2 softfail override requires two consecutive sync
	     * responses.  Fail, and record that this was async.
	     */
	    if (x99_set_last_auth_pos(inst->syncdir, username, 0))
		x99_log(X99_LOG_ERR,
			"auth: failed to record last auth pos for [%s]",
			username);
	    return RLM_MODULE_USERLOCK;
	}

	/* We're now in "ewindow2 mode" ... subsequent logic must test fc */
	goto sync_response;
    }

    /*
     * Don't bother to check async response if either
     * - the card doesn't support it, or
     * - we're doing fast_sync.
     */
    if (!(user_info.card_id & X99_CF_AM) ||
	pairfind(request->config_items, PW_X99_FAST)) {
	goto sync_response;
    }

    /* Perform any site-specific transforms of the challenge. */
    if (x99_challenge_transform(username, challenge) != 0) {
	x99_log(X99_LOG_ERR,
		"auth: challenge transform failed for [%s]", username);
	return RLM_MODULE_FAIL;
	/* NB: last_auth, failcount not updated. */
    }

    /* Calculate and test the async response. */
    if (x99_response(challenge, e_response, user_info.card_id,
		     user_info.keyblock) != 0) {
	x99_log(X99_LOG_ERR,
		"auth: unable to calculate async response for [%s], "
		"to challenge %s", username, challenge);
	return RLM_MODULE_FAIL;
	/* NB: last_auth, failcount not updated. */
    }
    DEBUG("rlm_x99_token: auth: [%s], async challenge %s, "
	  "expecting response %s", username, challenge, e_response);

    if (x99_pw_valid(request, inst, pwattr, e_response, &add_vps)) {
	/* Password matches.  Is this allowed? */
	if (!inst->allow_async) {
	    x99_log(X99_LOG_AUTH,
		    "auth: bad async for [%s]: disallowed by config", username);
	    rc = RLM_MODULE_REJECT;
	    goto return_pw_valid;
	    /* NB: last_auth, failcount not updated. */
	}

	/* Make sure this isn't a replay by forcing a delay. */
	if (time(NULL) - last_auth < inst->chal_delay) {
	    x99_log(X99_LOG_AUTH,
		    "auth: bad async for [%s]: too soon", username);
	    rc = RLM_MODULE_REJECT;
	    goto return_pw_valid;
	    /* NB: last_auth, failcount not updated. */
	}

	if (user_info.card_id & X99_CF_SM) {
	    x99_log(X99_LOG_INFO,
		    "auth: [%s] authenticated in async mode", username);
	}

	rc = RLM_MODULE_OK;
	if (ntohl(sflags) & 1) {
	    /*
	     * Resync the card.  The sync data doesn't mean anything for
	     * async-only cards, but we want the side effects of resetting
	     * the failcount and the last auth time.  We "fail-out" if we
	     * can't do this, because if we can't update the last auth time,
	     * we will be open to replay attacks over the lifetime of the
	     * State attribute (inst->chal_delay).
	     */
	    if (x99_get_sync_data(inst->syncdir, username, user_info.card_id,
				  1, 0, challenge, user_info.keyblock) != 0) {
		x99_log(X99_LOG_ERR, "auth: unable to get sync data "
			"e:%d t:%d for [%s] (for resync)", 1, 0, username);
		rc = RLM_MODULE_FAIL;
	    } else if (x99_set_sync_data(inst->syncdir, username, challenge,
					 user_info.keyblock) != 0) {
		x99_log(X99_LOG_ERR,
			"auth: unable to set sync data for [%s] (for resync)",
			username);
		rc = RLM_MODULE_FAIL;
	    }
	} else {
	    /* Just update failcount, last_auth, auth_pos. */
	    if (x99_reset_failcount(inst->syncdir, username) != 0) {
		x99_log(X99_LOG_ERR,
			"auth: unable to reset failcount for [%s]", username);
		rc = RLM_MODULE_FAIL;
	    }
	}
	goto return_pw_valid;
    } /* if (user authenticated async) */

sync_response:
    /*
     * Calculate and test sync responses in the window.
     * Note that we always accept a sync response, even
     * if a challenge or resync was explicitly requested.
     */
    if ((user_info.card_id & X99_CF_SM) && inst->allow_sync) {
	int start = 0, end = inst->ewindow_size;

	/*
	 * Tweak start,end for ewindow2_size logic.
	 *
	 * If user is in softfail, and their last response was correct,
	 * start at that response.  We used to start at the NEXT
	 * response (the one that will let them in), but the MS Windows
	 * "incorrect password" dialog is confusing and users end up
	 * reusing the same password twice; this has the effect that
	 * ewindow2 doesn't work at all for them (they enter 1,1,2,2,3,3;
	 * the 1,2 or 2,3 wouldn't work since the repeat would reset the
	 * sequence).
	 *
	 * The response sequence 6,5,6 won't work (but 6,5,6,7 will).
	 * That's OK; we want to optimize for the 6,7 sequence.  The user
	 * can't generate the 6,5 sequence from the token anyway.
	 *
	 * If the user starts at the left edge of the window (0,1,2) they
	 * have to enter three responses.  We don't accept the zeroeth
	 * response as part of the sequence because we can't differentiate
	 * between a correct entry of the zeroeth response (which stores
	 * 0 as the last_auth_pos) and an incorrect entry (which "resets"
	 * the last_auth_pos to 0).
	 */
	if (fc == FAIL_SOFT) {
	    start = x99_get_last_auth_pos(inst->syncdir, username);
	    end = inst->ewindow2_size;
	}

	challenge[0] = '\0';	/* initialize for x99_get_sync_data() */
	for (i = start; i <= end; ++i) {
	    /* Get sync challenge and key. */
	    if (x99_get_sync_data(inst->syncdir, username, user_info.card_id,
				  i, 0, challenge, user_info.keyblock) != 0) {
		x99_log(X99_LOG_ERR,
			"auth: unable to get sync data e:%d t:%d for [%s]",
			i, 0, username);
		rc = RLM_MODULE_FAIL;
		goto return_pw_valid;
		/* NB: last_auth, failcount not updated. */
	    }

	    /* Calculate sync response. */
	    if (x99_response(challenge, e_response, user_info.card_id,
			     user_info.keyblock) != 0) {
		x99_log(X99_LOG_ERR, "auth: unable to calculate sync response "
			"e:%d t:%d for [%s], to challenge %s",
			i, 0, username, challenge);
		rc = RLM_MODULE_FAIL;
		goto return_pw_valid;
		/* NB: last_auth, failcount not updated. */
	    }
	    DEBUG("rlm_x99_token: auth: [%s], sync challenge %d %s, "
		  "expecting response %s", username, i, challenge, e_response);

	    /* Test user-supplied passcode. */
	    if (x99_pw_valid(request, inst, pwattr, e_response, &add_vps)) {
		/*
		 * Yay!  User authenticated via sync mode.  Resync.
		 */
		rc = RLM_MODULE_OK;

		/*
		 * ewindow2_size logic
		 */
		if (fc == FAIL_SOFT) {
		    /* User must authenticate twice in a row, ... */
		    if (start && (i == start + 1) &&
			/* ... within ewindow2_delay seconds. */
			(time(NULL) - last_auth < inst->ewindow2_delay)) {
			/* This is the 2nd of two consecutive responses. */
			x99_log(X99_LOG_AUTH,
				"auth: ewindow2 softfail override for [%s] at "
				"window position %d", username, i);
		    } else {
			/* correct, but not consecutive or not soon enough */
			DEBUG("rlm_x99_token: auth: [%s] ewindow2 candidate "
			      "at position %i", username, i);
			auth_pos = i;
			rc = RLM_MODULE_REJECT;
			break;
		    }
		}

		/*
		 * The same failure/replay issue applies here as in the
		 * identical code block in the async section above, with
		 * the additional problem that a response can be reused
		 * indefinitely!  (until the sync data is updated)
		 */
		if (x99_get_sync_data(inst->syncdir,username,user_info.card_id,
				      1, 0, challenge,user_info.keyblock) != 0){
		    x99_log(X99_LOG_ERR, "auth: unable to get sync data "
			    "e:%d t:%d for [%s] (for resync)", 1, 0, username);
		    rc = RLM_MODULE_FAIL;
		} else if (x99_set_sync_data(inst->syncdir, username, challenge,
					     user_info.keyblock) != 0) {
		    x99_log(X99_LOG_ERR,
			    "auth: unable to set sync data for [%s] "
			    "(for resync)", username);
		    rc = RLM_MODULE_FAIL;
		}
		goto return_pw_valid;

	    } /* if (passcode is valid) */
	} /* for (each slot in the window) */
    } /* if (card is in sync mode and sync mode allowed) */

    /* Both async and sync mode failed. */
    if ((fc != FAIL_SOFT) /* !already incremented by x99_check_failcount() */ &&
	(x99_incr_failcount(inst->syncdir, username) != 0)) {
	x99_log(X99_LOG_ERR,
		"auth: unable to increment failure count for user [%s]",
		username);
    }
    if (x99_set_last_auth_pos(inst->syncdir, username, auth_pos)) {
	x99_log(X99_LOG_ERR,
		"auth: unable to set ewindow2 position for user [%s]",
		username);
    }
    return RLM_MODULE_REJECT;

    /* Must exit here after a successful return from x99_pw_valid(). */
return_pw_valid:

    /* Handle any vps returned from x99_pw_valid(). */
    if (rc == RLM_MODULE_OK) {
	pairadd(&request->reply->vps, add_vps);
    } else {
	pairfree(&add_vps);
    }
    return rc;
}


/* per-instance destruction */
static int
x99_token_detach(void *instance)
{
    x99_token_t *inst = (x99_token_t *) instance;

    free(inst->pwdfile);
    free(inst->syncdir);
    free(inst->chal_prompt);
    free(inst->chal_req);
    free(inst->resync_req);
    free(instance);
    return 0;
}


/* per-module destruction */
static int
x99_token_destroy(void)
{
    (void) memset(hmac_key, 0, sizeof(hmac_key));
    (void) close(rnd_fd);
    return 0;
}

/*
 *	If the module needs to temporarily modify it's instantiation
 *	data, the type should be changed to RLM_TYPE_THREAD_UNSAFE.
 *	The server will then take care of ensuring that the module
 *	is single-threaded.
 */
module_t rlm_x99_token = {
	"x99_token",
	RLM_TYPE_THREAD_SAFE,		/* type */
	x99_token_init,			/* initialization */
	x99_token_instantiate,		/* instantiation */
	{
		x99_token_authenticate,	/* authentication */
		x99_token_authorize,	/* authorization */
		NULL,			/* preaccounting */
		NULL,			/* accounting */
		NULL,			/* checksimul */
		NULL,			/* pre-proxy */
		NULL,			/* post-proxy */
		NULL			/* post-auth */
	},
	x99_token_detach,		/* detach */
	x99_token_destroy,		/* destroy */
};
