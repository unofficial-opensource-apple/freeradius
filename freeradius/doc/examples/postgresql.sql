/*
 * $Id: postgresql.sql,v 1.1.2.10 2007/07/16 11:22:49 pnixon Exp $
 *
 * Postgresql schema for FreeRADIUS
 *
 * All field lengths need checking as some are still suboptimal. -pnixon 2003-07-13
 *
 */

/*
 * Table structure for table 'radacct'
 *
 * Note: Column type BIGSERIAL does not exist prior to Postgres 7.2
 *       If you run an older version you need to change this to SERIAL
 */
CREATE TABLE radacct (
	RadAcctId		BIGSERIAL PRIMARY KEY,
	AcctSessionId		VARCHAR(32) NOT NULL,
	AcctUniqueId		VARCHAR(32) NOT NULL,
	UserName		VARCHAR(253),
	Realm			VARCHAR(64),
	NASIPAddress		INET NOT NULL,
	NASPortId		VARCHAR(15),
	NASPortType		VARCHAR(32),
	AcctStartTime		TIMESTAMP with time zone,
	AcctStopTime		TIMESTAMP with time zone,
	AcctSessionTime		BIGINT,
	AcctAuthentic		VARCHAR(32),
	ConnectInfo_start	VARCHAR(50),
	ConnectInfo_stop	VARCHAR(50),
	AcctInputOctets		BIGINT,
	AcctOutputOctets	BIGINT,
	CalledStationId		VARCHAR(50),
	CallingStationId	VARCHAR(50),
	AcctTerminateCause	VARCHAR(32),
	ServiceType		VARCHAR(32),
	FramedProtocol		VARCHAR(32),
	FramedIPAddress		INET,
	AcctStartDelay		BIGINT,
	AcctStopDelay		BIGINT,
	XAscendSessionSvrKey	VARCHAR(10)
);
-- This index may be usefull..
-- CREATE UNIQUE INDEX radacct_whoson on radacct (AcctStartTime, nasipaddress);

-- For use by onoff-, update-, stop- and simul_* queries
CREATE INDEX radacct_active_user_idx ON radacct (UserName, NASIPAddress, AcctSessionId) WHERE AcctStopTime IS NULL;
-- and for common statistic queries:
CREATE INDEX radacct_start_user_idx ON radacct (AcctStartTime, UserName);
-- and, optionally
-- CREATE INDEX radacct_stop_user_idx ON radacct (acctStopTime, UserName);

/*
 * There was WAAAY too many indexes previously. This combo index
 * should take care of the most common searches.
 * I have commented out all the old indexes, but left them in case
 * someone wants them. I don't recomend anywone use them all at once
 * as they will slow down your DB too much.
 *  - pnixon 2003-07-13
 */

/*
 * create index radacct_UserName on radacct (UserName);
 * create index radacct_AcctSessionId on radacct (AcctSessionId);
 * create index radacct_AcctUniqueId on radacct (AcctUniqueId);
 * create index radacct_FramedIPAddress on radacct (FramedIPAddress);
 * create index radacct_NASIPAddress on radacct (NASIPAddress);
 * create index radacct_AcctStartTime on radacct (AcctStartTime);
 * create index radacct_AcctStopTime on radacct (AcctStopTime);
*/



/*
 * Table structure for table 'radcheck'
 */
CREATE TABLE radcheck (
	id		SERIAL PRIMARY KEY,
	UserName	VARCHAR(64) NOT NULL DEFAULT '',
	Attribute	VARCHAR(64) NOT NULL DEFAULT '',
	op		VARCHAR(2) NOT NULL DEFAULT '==',
	Value		VARCHAR(253) NOT NULL DEFAULT ''
);
create index radcheck_UserName on radcheck (UserName,Attribute);
/*
 * Use this index if you use case insensitive queries
 */
-- create index radcheck_UserName_lower on radcheck (lower(UserName),Attribute);

/*
 * Table structure for table 'radgroupcheck'
 */
CREATE TABLE radgroupcheck (
	id		SERIAL PRIMARY KEY,
	GroupName	VARCHAR(64) NOT NULL DEFAULT '',
	Attribute	VARCHAR(64) NOT NULL DEFAULT '',
	op		VARCHAR(2) NOT NULL DEFAULT '==',
	Value		VARCHAR(253) NOT NULL DEFAULT ''
);
create index radgroupcheck_GroupName on radgroupcheck (GroupName,Attribute);

/*
 * Table structure for table 'radgroupreply'
 */
CREATE TABLE radgroupreply (
	id		SERIAL PRIMARY KEY,
	GroupName	VARCHAR(64) NOT NULL DEFAULT '',
	Attribute	VARCHAR(64) NOT NULL DEFAULT '',
	op		VARCHAR(2) NOT NULL DEFAULT '=',
	Value		VARCHAR(253) NOT NULL DEFAULT ''
);
create index radgroupreply_GroupName on radgroupreply (GroupName,Attribute);

/*
 * Table structure for table 'radreply'
 */
CREATE TABLE radreply (
	id		SERIAL PRIMARY KEY,
	UserName	VARCHAR(64) NOT NULL DEFAULT '',
	Attribute	VARCHAR(64) NOT NULL DEFAULT '',
	op		VARCHAR(2) NOT NULL DEFAULT '=',
	Value		VARCHAR(253) NOT NULL DEFAULT ''
);
create index radreply_UserName on radreply (UserName,Attribute);
/*
 * Use this index if you use case insensitive queries
 */
-- create index radreply_UserName_lower on radreply (lower(UserName),Attribute);

/*
 * Table structure for table 'usergroup'
 */
CREATE TABLE usergroup (
	UserName	VARCHAR(64) NOT NULL DEFAULT '',
	GroupName	VARCHAR(64) NOT NULL DEFAULT '',
	priority	INTEGER NOT NULL DEFAULT 0
);
create index usergroup_UserName on usergroup (UserName);
/*
 * Use this index if you use case insensitive queries
 */
-- create index usergroup_UserName_lower on usergroup (lower(UserName));

/*
 * Table structure for table 'realmgroup'
 * Commented out because currently not used
 */
--CREATE TABLE realmgroup (
--	id		SERIAL PRIMARY KEY,
--	RealmName	VARCHAR(30) DEFAULT '' NOT NULL,
--	GroupName	VARCHAR(30)
--);
--create index realmgroup_RealmName on realmgroup (RealmName);

/*
 * Table structure for table 'realms'
 * This is not yet used by FreeRADIUS
 */
--CREATE TABLE realms (
--	id		SERIAL PRIMARY KEY,
--	realmname	VARCHAR(64),
--	nas		VARCHAR(128),
--	authport	int4,
--	options		VARCHAR(128) DEFAULT ''
--);

/*
 * Table structure for table 'nas'
 */
CREATE TABLE nas (
	id		SERIAL PRIMARY KEY,
	nasname		VARCHAR(128) NOT NULL,
	shortname	VARCHAR(32) NOT NULL,
	type		VARCHAR(30) NOT NULL DEFAULT 'other',
	ports		int4,
	secret		VARCHAR(60) NOT NULL,
	community	VARCHAR(50),
	description	VARCHAR(200)
);
create index nas_nasname on nas (nasname);

--
-- Table structure for table 'radpostauth'
--

CREATE TABLE radpostauth (
	id		BIGSERIAL PRIMARY KEY,
	username	VARCHAR(253) NOT NULL,
	pass		VARCHAR(128),
	reply		VARCHAR(32),
	authdate	TIMESTAMP with time zone NOT NULL default 'now'
) ;

--
-- Table structure for table 'radippool'
--

CREATE TABLE radippool (
	id			BIGSERIAL PRIMARY KEY,
	pool_name		text NOT NULL,
	FramedIPAddress		INET,
	NASIPAddress		text NOT NULL,
	CalledStationId		VARCHAR(64),
	CallingStationId	text NOT NULL DEFAULT ''::text,
	expiry_time		TIMESTAMP(0) without time zone NOT NULL,
	username		text DEFAULT ''::text,
	pool_key		VARCHAR(30) NOT NULL
);

--
-- Table structure for table 'dictionary'
-- This is not currently used by FreeRADIUS
--
-- CREATE TABLE dictionary (
--     id              SERIAL PRIMARY KEY,
--     Type            VARCHAR(30),
--     Attribute       VARCHAR(64),
--     Value           VARCHAR(64),
--     Format          VARCHAR(20),
--     Vendor          VARCHAR(32)
-- );

