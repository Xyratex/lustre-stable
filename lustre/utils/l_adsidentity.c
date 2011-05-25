/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stdarg.h>
#include <stddef.h>
#include <libgen.h>
#include <syslog.h>
#include <ldap.h>

#include <linux/lnet/nidstr.h>
#include <linux/lustre/lustre_user.h>
#include <linux/lustre/lustre_idl.h>

#define PERM_PATHNAME      "/etc/lustre/perm.conf"
#define ADSCONF_PATHNAME   "/etc/lustre/ads.conf"
#define NO_OF_PARAMS_REQD  5
#define STRING_MAX_SIZE    256
#define PROC_NAME_MAX_SIZE 1024
#define PERM_NAME_MAX_SIZE 64
/** */
#define FILE_LINE_BUF_SIZE 1024

static char *progname;
static char *fs_name;

static void errlog(const char *fmt, ...)
{
        va_list args;

        openlog(progname, LOG_PERROR | LOG_PID, LOG_AUTHPRIV);

        va_start(args, fmt);
        vsyslog(LOG_NOTICE, fmt, args);
        va_end(args);

        closelog();
}

/*
 * permission file format is like this:
 * {nid} {uid} {perms}
 *
 * '*' nid means any nid
 * '*' uid means any uid
 * the valid values for perms are:
 * setuid/setgid/setgrp/rmtacl           -- enable corresponding perm
 * nosetuid/nosetgid/nosetgrp/normtacl   -- disable corresponding perm
 * they can be listed together, seperated by ',',
 * when perm and noperm are in the same line (item), noperm is preferential,
 * when they are in different lines (items), the latter is preferential,
 * '*' nid is as default perm, and is not preferential.
 */

/*
 * adspasswd structure is simulation of passwd structure.
 */
struct adspasswd {
        char  pw_name[STRING_MAX_SIZE]; /* username */
        char  cn[STRING_MAX_SIZE];      /* cn       */
        uid_t pw_uid;                   /* user ID  */
        gid_t pw_gid;                   /* group ID */
};

typedef enum {
        FALSE = 0,
        TRUE  = 1
} bool;

struct conf_params {
        const char *desc; /* conf parameter */
        bool  isvisited;  /* already read?  */
};


enum attr_id {
        LDAP_ATTR_NONE,
        LDAP_ATTR_UID,
        LDAP_ATTR_GID,
        LDAP_ATTR_USER,
        LDAP_ATTR_CN,
        LDAP_ATTR_MAX,
};

typedef int (*ldap_handle)(void *data, char *ldap_data);

struct ldap_attr {
        const char    *la_name;
        enum attr_id   la_id;
        ldap_handle    la_hand;
};

struct ldap_attr *ldap_by_name(struct ldap_attr *attrs, char *name)
{
        int i = 0;

        while (attrs[i].la_name != NULL) {
                if (strcasecmp(attrs[i].la_name, name) == 0)
                        return &attrs[i];
                i ++;
        }

        return NULL;
}

struct ldap_attr *ldap_by_id(struct ldap_attr *attrs, enum attr_id id)
{
        int i = 0;

        while (attrs[i].la_name != NULL) {
                if (attrs[i].la_id == id)
                        return &attrs[i];
                i ++;
        }

        return NULL;
}

void ldap_attr2arr(struct ldap_attr *attrs, char **data)
{
        int i = 0;

        memset(data, 0, sizeof(void *) * LDAP_ATTR_MAX);
        while (attrs[i].la_name != NULL) {
                data[i] = (char *)attrs[i].la_name;
                i ++;
        }
}

int ldap_name_get(void *data, char *ldap_data)
{
        struct adspasswd *adspwuid = data;

        strncpy(adspwuid->pw_name, ldap_data, STRING_MAX_SIZE - 1);

        return 0;
}

int ldap_cn_get(void *data, char *ldap_data)
{
        struct adspasswd *adspwuid = data;

        strncpy(adspwuid->cn, ldap_data, STRING_MAX_SIZE - 1);

        return 0;
}


int ldap_uid_get(void *data, char *ldap_data)
{
        struct adspasswd *adspwuid = data;
        char *end;
        long id;

        id = strtoul(ldap_data, &end, 0);
        if (*end || errno) {
                errlog("invalid uid '%s'\n", ldap_data);
                return -1;
        }
        adspwuid->pw_uid = id;

        return 0;
}


int ldap_gid_get(void *data, char *ldap_data)
{
        struct adspasswd *adspwuid = data;
        char *end;
        long id;

        id = strtoul(ldap_data, &end, 0);
        if (*end || errno) {
                errlog("invalid uid '%s'\n", ldap_data);
                return -1;
        }
        adspwuid->pw_gid = id;

        return 0;
}

int ldap_gids_get(void *data, char *ldap_data)
{
        gid_t *gids = data;
        char *end;
        long id;
        int i;

        id = strtoul(ldap_data, &end, 0);
        if (*end || errno) {
                errlog("invalid uid '%s'\n", ldap_data);
                return -1;
        }

        /* check for dups */
        for (i = 0; i < NGROUPS_MAX; i++) {
                if (gids[i] == 0) {
                        /* not found dup - add */
                        gids[i] = id;
                        break;
                }
                /* found dup - skip it */
                if (gids[i] == id)
                        break;
        }

        return 0;
}

/** POSIX schema attributes */

struct ldap_attr posix_uid[] = {
        {
         .la_name = "uidNumber",
         .la_id = LDAP_ATTR_UID,
         .la_hand = ldap_uid_get,
        } , {
        .la_name = "gidNumber",
        .la_id = LDAP_ATTR_GID,
        .la_hand  = ldap_gid_get,
        } , {
        .la_name = "uid",
        .la_id = LDAP_ATTR_USER,
        .la_hand = ldap_name_get,
        } , {
        .la_name = "cn",
        .la_id = LDAP_ATTR_CN,
        .la_hand = ldap_cn_get,
        },{
        .la_name = NULL
        }
};


struct ldap_attr posix_gid[] = {
        {
        .la_name = "gidNumber",
        .la_id = LDAP_ATTR_GID,
        .la_hand  = ldap_gids_get,
        } , {
        .la_name = NULL
        }
};


/** SFU */
struct ldap_attr sfu_uid[] = {
        {
         .la_name = "msSFU30UidNumber",
         .la_id = LDAP_ATTR_UID,
         .la_hand = ldap_uid_get,
        } , {
        .la_name = "msSFU30GidNumber",
        .la_id = LDAP_ATTR_GID,
        .la_hand  = ldap_gid_get,
        } , {
        .la_name = "msSFU30Name",
        .la_id = LDAP_ATTR_USER,
        .la_hand = ldap_name_get,
        } , {
        .la_name = "cn",
        .la_id = LDAP_ATTR_CN,
        .la_hand = ldap_cn_get,
        },{
        .la_name = NULL}
};

struct ldap_attr sfu_gid[] = {
        {
        .la_name = "msSFU30GidNumber",
        .la_id = LDAP_ATTR_GID,
        .la_hand  = ldap_gids_get,
        },
        {.la_name = NULL}
};

enum {
        SCH_UID = 0,
        SCH_GID = 1,
        SCH_SFU = 0,
        SCH_POSIX = 1,
};

struct ldap_scheme {
        struct ldap_attr *ls_attr[2];
} ldap_sch[] = {
        {
        .ls_attr = { sfu_uid, sfu_gid}
        },{
        .ls_attr = { posix_uid, posix_gid }
        }
};

struct ldap_scheme *ldap_active = &ldap_sch[SCH_SFU];

/*
 * Maintaining required ad conf parameters in a list.
 */
const char param_uri[] = "uri";
const char param_base[] = "base";
const char param_binddn[] = "binddn";
const char param_cred[] = "credentials";
const char param_scheme[] = "scheme";
struct conf_params reqd_params[NO_OF_PARAMS_REQD] = {
        { param_uri    , FALSE },
        { param_base   , FALSE },
        { param_binddn , FALSE },
        { param_cred   , FALSE },
        { param_scheme , FALSE }
};


static void usage(void)
{
        fprintf(stderr,
                "\nusage: %s {mdtname} {uid}\n"
                "Normally invoked as an upcall from Lustre, set via:\n"
                "  /proc/fs/lustre/mdt/{mdtname}/identity_upcall\n",
                progname);
}

static int compare_gid_t(const void *v1, const void *v2)
{
        return (*(gid_t*)v1 - *(gid_t*)v2);
}


static int comment_line(char *line)
{
        if (!*line || *line == '\n' || *line == '#')
                return 1;
        return 0;
}

static bool section_start = FALSE;

static bool conf_section(char *line, char *sec_name)
{
        char *start;
        char *end;
        int len;

        start = strchr(line, '[');
        end = strrchr(line, ']');
        /* skip all non section lines */
        if ((start == NULL) || (end == NULL))
                goto end;
        len = strlen(sec_name);
        section_start = strncasecmp(start + 1, sec_name, len) == 0 ? TRUE : FALSE;
end:
        return section_start;
}

int getindex(const char *str, char params[][2][STRING_MAX_SIZE])
{
        int index;
        int len = strlen(str);

        for (index = 0; index < NO_OF_PARAMS_REQD; index++) {
                /* get the appropriate index. */
                if (strncmp(str, params[index][0], len) == 0)
                        break;
        }
        return index;
}

int get_params(FILE *fp, char params[][2][STRING_MAX_SIZE])
{
        char line[FILE_LINE_BUF_SIZE];
        char *ln;
        char desc[STRING_MAX_SIZE];
        char value[STRING_MAX_SIZE];
        int  num_params = 0;
        int  i;

        memset(line, 0, sizeof(line));
        memset(desc, 0, sizeof(desc));
        memset(value, 0, sizeof(value));

        while (fgets(line, FILE_LINE_BUF_SIZE, fp)) {
                ln = &line[0];

                /* trim spaces */
                while (*ln && (isspace(*ln))) ln++;

                if (comment_line(ln))
                        continue;

                if (!conf_section(ln, fs_name))
                        continue;

                i = sscanf(ln, "%s %s", desc, value);
                if (i != 2)
                        continue;
                for (i = 0; i < NO_OF_PARAMS_REQD; i++) {
                        if (reqd_params[i].isvisited == FALSE &&
                            !strcmp(reqd_params[i].desc, desc)) {
                                strncpy(params[i][0], desc,
                                        STRING_MAX_SIZE - 1);
                                strncpy(params[i][1], value,
                                        STRING_MAX_SIZE - 1);
                                num_params++;
                                reqd_params[i].isvisited = TRUE;
                                break;
                        }
                }
        }

        if (num_params < NO_OF_PARAMS_REQD)
                return -1;

        i = getindex(param_scheme, params);
        while (1) {
                if (strcasecmp(params[i][1], "sfu") == 0) {
                        ldap_active = &ldap_sch[SCH_SFU];
                        break;
                }
                if (strcasecmp(params[i][1], "posix") == 0) {
                        ldap_active = &ldap_sch[SCH_POSIX];
                        break;
                }

                num_params = -1;
                break;
        }
        return NO_OF_PARAMS_REQD;
}

int ldap_connect(LDAP **ld, char params[][2][STRING_MAX_SIZE])
{
        /* Read from the config file. */
        struct berval credentials;
        int    proto              = 0;
        int    ret                = 0;
        int    index              = 0;

        index = getindex(param_uri, params);

        /* open a connection */
        ret = ldap_initialize(ld, params[index][1]);
        if (ret != LDAP_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        /* Set the LDAP protocol version supported by the client
           to 3. (By default, this is set to 2. SASL authentication
           is part of version 3 of the LDAP protocol.) */
        proto = LDAP_VERSION3;

        ret = ldap_set_option(*ld, LDAP_OPT_PROTOCOL_VERSION, &proto);
        if (ret != LDAP_OPT_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        ret = ldap_set_option(*ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);
        if (ret != LDAP_OPT_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        index = getindex(param_cred, params);

        credentials.bv_len = strlen(params[index][1]);
        credentials.bv_val = params[index][1];

        index = getindex(param_binddn, params);

        ret = ldap_sasl_bind_s(*ld, params[index][1], LDAP_SASL_SIMPLE,
                               &credentials, NULL, NULL, NULL);
        if (ret != LDAP_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        return 0;
}

int ldap_get_info(LDAP *ld, char *base, char *filter,
                   struct ldap_attr *scheme, void *data)
{
        char *          attrs[LDAP_ATTR_MAX];
        struct berval **bers     = NULL;
        LDAPMessage    *entry;
        LDAPMessage    *res;
        BerElement     *bptr;
        char           *attrib;
        int             ret;
        int             i;
        int             rc;
        struct ldap_attr *tmp;

        ldap_attr2arr(scheme, attrs);
        ret = ldap_search_ext_s(ld, base, LDAP_SCOPE_SUBTREE, filter, attrs, 0,
                                NULL, NULL, NULL, 0, &res);
        if (ret != LDAP_SUCCESS ) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        /* step through each entry returned */
        for (entry = ldap_first_entry(ld, res); entry != NULL;
             entry = ldap_next_entry(ld, entry) ) {
                for (attrib = ldap_first_attribute(ld, entry, &bptr);
                     attrib != NULL;
                     attrib = ldap_next_attribute(ld, entry, bptr)) {
                        bers = ldap_get_values_len(ld, entry, attrib );
                        for (i = 0; bers[i] != NULL; i++) {
                                tmp = ldap_by_name(scheme, attrib);
                                if (tmp != NULL) {
                                        rc = tmp->la_hand(data, bers[i]->bv_val);
                                }
                                if ((tmp == NULL) || (rc < 0)) {
                                        errlog("Invalid attribute %s\n", attrib);
                                        ldap_msgfree( res );
                                        return -1;
                                }
                        }
                        ldap_value_free_len( bers );
                }
        }
        /* free the search results */
        ldap_msgfree( res );

        return 0;
}

/*
 * Get only user's info from ADS server.
 */
int get_ads_userinfo(LDAP *ld, struct adspasswd *adspwuid, uid_t uid,
                     char *base)
{
        char            str[STRING_MAX_SIZE];
        struct          ldap_attr *tmp;

        memset(str, 0, sizeof(str));
        tmp = ldap_by_id(ldap_active->ls_attr[SCH_UID], LDAP_ATTR_UID);
        if (tmp == NULL)
                return -EINVAL;
        /* Form a filter. */
        sprintf(str, "(%s=%hu)", tmp->la_name, uid);

        return ldap_get_info(ld, base, str, ldap_active->ls_attr[SCH_UID],
                             adspwuid);
}


/*
 * Get only groups list info from ADS server.
 */
int get_ads_groupinfo(LDAP *ld, struct adspasswd *adspwuid, gid_t *gid,
                      char *base)
{
        char                 str[STRING_MAX_SIZE];

        /* Get the groups info. */
        memset(str, 0, sizeof(str));
        sprintf(str, "(&(objectClass=Group)(objectCategory=Group)"
                       "(member=CN=%s, CN=Users,%s))", adspwuid->cn, base);

        gid[0] = adspwuid->pw_gid;
        return ldap_get_info(ld, base, str, ldap_active->ls_attr[SCH_GID], gid);
}

int ldap_disconnect(LDAP *ld)
{
        int ret = 0;

        /* close and free connection resources */
        ret = ldap_unbind_ext_s(ld, NULL, NULL);
        if (ret != LDAP_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        } else {
                return 0;
        }
}

int count_groups(gid_t *gids)
{
        int i;

        for(i = 0; i < NGROUPS_MAX; i++)
                if (gids[i] == 0)
                        break;
        return i;
}
/*
 * Get particular user info and its groups list info from ADS server.
 */
int get_groups_ads(struct identity_downcall_data *data)
{
        struct   adspasswd  pw;
        LDAP               *ld;
        FILE               *ads_fp;
        gid_t              *groups;
        char                params[NO_OF_PARAMS_REQD][2][STRING_MAX_SIZE];
        char               *pw_name;
        int                 namelen;
        int                 i;
        int                 rc;

        /* Read config file */
        ads_fp = fopen(ADSCONF_PATHNAME, "r");
        if (ads_fp == NULL) {
                errlog("open %s failed: %s\n",
                ADSCONF_PATHNAME, strerror(errno));
                errlog("Please provide ADS configuration in"
                       "/etc/lustre/ads.config\n");
                data->idd_err = errno;
                return -1;
        }

        memset(params, 0, sizeof(params));

        if (get_params(ads_fp, params) < NO_OF_PARAMS_REQD) {
                errlog("Please specify all parameters in config file\n");
                data->idd_err = EINVAL;
                fclose(ads_fp);
                return -1;
        }

        if (fclose(ads_fp))
                errlog("close failed %s: %s\n",
                       ADSCONF_PATHNAME, strerror(errno));

        if (ldap_connect(&ld, params) != 0) {
                errlog("ldap setup failed\n");
                data->idd_err = EPROTO;
                return -1;
        }

        memset(&pw, 0, sizeof(pw));

        i = getindex(param_base, params);

        if (get_ads_userinfo(ld, &pw, data->idd_uid, params[i][1]) != 0) {
                errlog("Failed to get user info\n");
                data->idd_err = EPROTO;
                return -1;
        }

        namelen = sysconf(_SC_LOGIN_NAME_MAX);
        if (namelen < _POSIX_LOGIN_NAME_MAX)
                namelen = _POSIX_LOGIN_NAME_MAX;
        pw_name = (char *)malloc(namelen);
        if (!pw_name) {
                errlog("malloc error\n");
                data->idd_err = errno;
                return -1;
        }

        if (strlen(pw.pw_name) == 0) {
                errlog("no such user %u\n", data->idd_uid);
                data->idd_err = EIDRM;
                goto out;
        }
        memset(pw_name, 0, namelen);
        strncpy(pw_name, pw.pw_name, namelen - 1);

        groups = data->idd_groups;

        rc = get_ads_groupinfo(ld, &pw, groups, params[i][1]);
        if (rc < 0) {
                errlog("Failed to get group info\n");
                data->idd_err = EIDRM;
                goto out;
        }

        if (ldap_disconnect(ld) != 0) {
                errlog("ldap disconnect failed\n");
                data->idd_err = EPROTO;
                goto out;
        }

        data->idd_ngroups = count_groups(groups);
        qsort(groups, data->idd_ngroups, sizeof(*groups), compare_gid_t);
out:
        free(pw_name);
        return data->idd_err != 0 ? -1 : 0;
}

static inline int match_uid(uid_t uid, const char *str)
{
        char  *end;
        uid_t  uid2;

        if(!strcmp(str, "*"))
                return -1;

        uid2 = strtoul(str, &end, 0);
        if ((end == str) || *end || errno)
                return 0;

        return (uid == uid2);
}

typedef struct {
        char   *name;
        __u32   bit;
} perm_type_t;

static perm_type_t perm_types[] = {
        { "setuid", CFS_SETUID_PERM },
        { "setgid", CFS_SETGID_PERM },
        { "setgrp", CFS_SETGRP_PERM },
	{ "rmtacl", 0 },
	{ "rmtown", 0 },
        { 0 }
};

static perm_type_t noperm_types[] = {
        { "nosetuid", CFS_SETUID_PERM },
        { "nosetgid", CFS_SETGID_PERM },
        { "nosetgrp", CFS_SETGRP_PERM },
	{ "normtacl", 0 },
	{ "normtown", 0 },
        { 0 }
};

int parse_perm(__u32 *perm, __u32 *noperm, char *str)
{
        char        *start;
        char        *end;
        char         name[PERM_NAME_MAX_SIZE];
        perm_type_t *pt;

        *perm = 0;
        *noperm = 0;
        start = str;
        while (1) {
                memset(name, 0, sizeof(name));
                end = strchr(start, ',');
                if (!end)
                        end = str + strlen(str);
                if (start >= end)
                        break;
                strncpy(name, start, end - start);
                for (pt = perm_types; pt->name; pt++) {
                        if (!strcasecmp(name, pt->name)) {
                                *perm |= pt->bit;
                                break;
                        }
                }

                if (!pt->name) {
                        for (pt = noperm_types; pt->name; pt++) {
                                if (!strcasecmp(name, pt->name)) {
                                        *noperm |= pt->bit;
                                        break;
                                }
                        }

                        if (!pt->name) {
                                printf("unkown type: %s\n", name);
                                return -1;
                        }
                }

                start = end + 1;
        }
        return 0;
}

int parse_perm_line(struct identity_downcall_data *data, char *line)
{
        char       uid_str[STRING_MAX_SIZE];
        char       nid_str[STRING_MAX_SIZE];
        char       perm_str[STRING_MAX_SIZE];
        int        rc;
        int        i;
        lnet_nid_t nid;
        __u32      perm;
        __u32      noperm;

        if (data->idd_nperms >= N_PERMS_MAX) {
                errlog("permission count %d > max %d\n",
                        data->idd_nperms, N_PERMS_MAX);
                return -1;
        }

        rc = sscanf(line, "%s %s %s", nid_str, uid_str, perm_str);
        if (rc != 3) {
                errlog("can't parse line %s\n", line);
                return -1;
        }

        if (!match_uid(data->idd_uid, uid_str))
                return 0;

        if (!strcmp(nid_str, "*")) {
                nid = LNET_NID_ANY;
        } else {
                nid = libcfs_str2nid(nid_str);
                if (nid == LNET_NID_ANY) {
                        errlog("can't parse nid %s\n", nid_str);
                        return -1;
                }
        }

        if (parse_perm(&perm, &noperm, perm_str)) {
                errlog("invalid perm %s\n", perm_str);
                return -1;
        }

        /* merge the perms with the same nid.
         *
         * If there is LNET_NID_ANY in data->idd_perms[i].pdd_nid,
         * it must be data->idd_perms[0].pdd_nid, and act as default perm.
         */
        if (nid != LNET_NID_ANY) {
                int found = 0;

                /* search for the same nid */
                for (i = data->idd_nperms - 1; i >= 0; i--) {
                        if (data->idd_perms[i].pdd_nid == nid) {
                                data->idd_perms[i].pdd_perm =
                                        (data->idd_perms[i].pdd_perm | perm) &
                                        ~noperm;
                                found = 1;
                                break;
                        }
                }

                /* NOT found, add to tail */
                if (!found) {
                        data->idd_perms[data->idd_nperms].pdd_nid = nid;
                        data->idd_perms[data->idd_nperms].pdd_perm =
                                perm & ~noperm;
                        data->idd_nperms++;
                }
        } else {
                if (data->idd_nperms > 0) {
                        /* the first one isn't LNET_NID_ANY, need exchange */
                        if (data->idd_perms[0].pdd_nid != LNET_NID_ANY) {
                                data->idd_perms[data->idd_nperms].pdd_nid =
                                        data->idd_perms[0].pdd_nid;
                                data->idd_perms[data->idd_nperms].pdd_perm =
                                        data->idd_perms[0].pdd_perm;
                                data->idd_perms[0].pdd_nid = LNET_NID_ANY;
                                data->idd_perms[0].pdd_perm = perm & ~noperm;
                                data->idd_nperms++;
                        } else {
                                /* only fix LNET_NID_ANY item */
                                data->idd_perms[0].pdd_perm =
                                        (data->idd_perms[0].pdd_perm | perm) &
                                        ~noperm;
                        }
                } else {
                        /* it is the first one, only add to head */
                        data->idd_perms[0].pdd_nid = LNET_NID_ANY;
                        data->idd_perms[0].pdd_perm = perm & ~noperm;
                        data->idd_nperms = 1;
                }
        }

        return 0;
}

int get_perms(FILE *fp, struct identity_downcall_data *data)
{
        char line[FILE_LINE_BUF_SIZE];

        while (fgets(line, FILE_LINE_BUF_SIZE, fp)) {
                if (comment_line(line))
                        continue;

                if (parse_perm_line(data, line)) {
                        errlog("parse line %s failed!\n", line);
                        return -1;
                }
        }

        return 0;
}

static void show_result(struct identity_downcall_data *data)
{
        int i;

        if (data->idd_err) {
                errlog("failed to get identity for uid %d: %s\n",
                       data->idd_uid, strerror(data->idd_err));
                return;
        }

        printf("uid=%d gid=", data->idd_uid);
        for (i = 0; i < data->idd_ngroups; i++)
                printf("%s%u", i > 0 ? "," : "", data->idd_groups[i]);
        printf("\n");
        printf("permissions:\n"
               "  nid\t\t\tperm\n");
        for (i = 0; i < data->idd_nperms; i++) {
                struct perm_downcall_data *pdd;

                pdd = &data->idd_perms[i];

                printf("  %#llx\t0x%x\n", pdd->pdd_nid, pdd->pdd_perm);
        }
        printf("\n");
}

int main(int argc, char **argv)
{
        struct         identity_downcall_data *data;
        FILE          *perms_fp;
        char          *end;
        char           procname[PROC_NAME_MAX_SIZE];
        unsigned long  uid;
        int            fd;
        int            rc;
        size_t         datasize;

        progname = basename(argv[0]);

        if (argc != 3) {
                usage();
                return 1;
        }

        if (!*argv[1]) {
                errlog("%s: mdtname should not be NULL\n", progname);
                usage();
                return 1;
        }
        fs_name = argv[1];

        errno = 0;
        uid = strtoul(argv[2], &end, 0);
        if ((end == argv[2]) || *end || errno) {
                errlog("%s: invalid uid '%s'\n", progname, argv[2]);
                usage();
                return 1;
        }

        datasize = sizeof(*data) + NGROUPS_MAX * sizeof(gid_t);
        data = (struct identity_downcall_data *) malloc(datasize);
        if (!data) {
                errlog("malloc identity downcall data(%d) failed!\n", datasize);
                return 1;
        }
        memset(data, 0, datasize);
        data->idd_magic = IDENTITY_DOWNCALL_MAGIC;
        data->idd_uid = uid;

        /* get groups for uid */
        rc = get_groups_ads(data);
        if (rc)
                goto downcall;

        /* read permission database */
        perms_fp = fopen(PERM_PATHNAME, "r");
        if (perms_fp) {
                if (get_perms(perms_fp, data)) {
                        data->idd_err = errno ? errno : EIDRM;
                }

                if (fclose(perms_fp))
                        errlog("close failed %s: %s\n",
                               PERM_PATHNAME, strerror(errno));
        } else if (errno != ENOENT) {
                errlog("open %s failed: %s\n",
                       PERM_PATHNAME, strerror(errno));
        }

downcall:
        do {
                if (getenv("L_ADSIDENTITY_TEST")) {
                        show_result(data);
                        rc = 0;
                        break;
                }

                snprintf(procname, sizeof(procname),
                         "/proc/fs/lustre/mdt/%s/identity_info", argv[1]);
                fd = open(procname, O_WRONLY);
                if (fd < 0) {
                        errlog("can't open file %s: %s\n",
                               procname, strerror(errno));
                        rc = 1;
                        break;
                }

                rc = write(fd, data, datasize);

                if (close(fd))
                        errlog("close failed %s: %s\n",
                               procname, strerror(errno));
                if (rc != datasize) {
                        errlog("partial write ret %d: %s\n",
                               rc, strerror(errno));
                        rc = 1;
                        break;
                }
                rc = 0;
        } while (0);

        free(data);
        return rc;
}
