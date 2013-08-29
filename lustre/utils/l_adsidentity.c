/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
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
#define NO_OF_PARAMS_REQD  4
#define STRING_MAX_SIZE    256
#define PROC_NAME_MAX_SIZE 1024
#define PERM_NAME_MAX_SIZE 64
/** */
#define FILE_LINE_BUF_SIZE 1024

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
        char *desc;      /* conf parameter */
        bool  isvisited; /* already read?  */
};

/*
 * Maintaining required ad conf parameters in a list.
 */
struct conf_params reqd_params[] = {
        { "uri"        , FALSE },
        { "base"       , FALSE },
        { "binddn"     , FALSE },
        { "credentials", FALSE },
        { 0 }
};

static char *progname;

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

static void errlog(const char *fmt, ...)
{
        va_list args;

        openlog(progname, LOG_PERROR | LOG_PID, LOG_AUTHPRIV);

        va_start(args, fmt);
        vsyslog(LOG_NOTICE, fmt, args);
        va_end(args);

        closelog();
}

static inline int comment_line(char *line)
{
        char *p = line;

        while (*p && (*p == ' ' || *p == '\t')) p++;

        if (!*p || *p == '\n' || *p == '#')
                return 1;
        return 0;
}

int getindex(char *str, char params[][2][STRING_MAX_SIZE])
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
        char desc[STRING_MAX_SIZE];
        char value[STRING_MAX_SIZE];
        int  num_params = 0;
        int  i;

        memset(line, 0, sizeof(line));
        memset(desc, 0, sizeof(desc));
        memset(value, 0, sizeof(value));

        while (fgets(line, FILE_LINE_BUF_SIZE, fp)) {
                if (comment_line(line))
                        continue;

                sscanf(line, "%s %s", desc, value);
                for (i = 0; i < NO_OF_PARAMS_REQD; i++) {
                        if (reqd_params[i].isvisited == FALSE &&
                            !strcmp(reqd_params[i].desc, desc)) {
                                strncpy(params[num_params][0], desc,
                                        STRING_MAX_SIZE - 1);
                                strncpy(params[num_params][1], value,
                                        STRING_MAX_SIZE - 1);
                                num_params++;
                                reqd_params[i].isvisited = TRUE;
                                break;
                        }
                }
        }
        return num_params;
}

int ldap_connect(LDAP **ld, char params[][2][STRING_MAX_SIZE])
{
        /* Read from the config file. */
        struct berval credentials;
        int    proto              = 0;
        int    ret                = 0;
        int    index              = 0;

        index = getindex("uri", params);

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

        index = getindex("credentials", params);

        credentials.bv_len = strlen(params[index][1]);
        credentials.bv_val = params[index][1];

        index = getindex("binddn", params);

        ret = ldap_sasl_bind_s(*ld, params[index][1], LDAP_SASL_SIMPLE,
                               &credentials, NULL, NULL, NULL);
        if (ret != LDAP_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        return 0;
}

/*
 * Get only user's info from ADS server.
 */
int get_ads_userinfo(LDAP **ld, struct adspasswd *adspwuid, uid_t uid,
                     char *base)
{
        char*           attrs[]  = {"msSFU30UidNumber", "msSFU30Name",
                                    "msSFU30GidNumber", "cn", NULL};
        char            str[STRING_MAX_SIZE];
        char*           end;
        int             ret      = 0;
        unsigned long   id       = 0; /* For temp uid and gid. */
        int             i        = 0;
        struct berval **bers     = NULL;
        LDAPMessage    *entry;
        LDAPMessage    *res;
        BerElement     *bptr;
        char           *attrib;

        memset(str, 0, sizeof(str));
        /* Form a filter. */
        sprintf(str, "(msSFU30UidNumber=%hu)", uid);

        ret = ldap_search_ext_s(*ld, base, LDAP_SCOPE_SUBTREE, str, attrs, 0,
                                NULL, NULL, NULL, 0, &res);
        if (ret != LDAP_SUCCESS ) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        /* step through each entry returned */
        for (entry = ldap_first_entry(*ld, res); entry != NULL;
             entry = ldap_next_entry(*ld, entry) ) {
                for (attrib = ldap_first_attribute(*ld, entry, &bptr);
                     attrib != NULL;
                     attrib = ldap_next_attribute(*ld, entry, bptr)) {
                        bers = ldap_get_values_len( *ld, entry, attrib );
                        for (i = 0; bers[i] != NULL; i++) {
                                if (!strncmp(attrib, "msSFU30Name",
                                             strlen(attrib))) {
                                        strncpy(adspwuid->pw_name,
                                                bers[i]->bv_val,
                                                STRING_MAX_SIZE - 1);
                                } else if (!strncmp(attrib, "msSFU30UidNumber",
                                           strlen(attrib))) {
                                        id = strtoul(bers[i]->bv_val, &end, 0);
                                        if ((end == bers[i]->bv_val) || *end ||
                                            errno) {
                                                errlog("invalid uid '%s'\n",
                                                        bers[i]->bv_val);
                                                ldap_msgfree( res );
                                                return -1;
                                        }
                                        adspwuid->pw_uid = id;
                                } else if (!strncmp(attrib, "msSFU30GidNumber",
                                           strlen(attrib))) {
                                        id = strtoul(bers[i]->bv_val, &end, 0);
                                        if ((end == bers[i]->bv_val) || *end ||
                                            errno) {
                                                errlog("invalid gid '%s'\n",
                                                        bers[i]->bv_val);
                                                ldap_msgfree( res );
                                                return -1;
                                        }
                                        adspwuid->pw_gid = id;
                                } else if(!strncmp(attrib, "cn",
                                                   strlen(attrib))) {
                                        strncpy(adspwuid->cn, bers[i]->bv_val,
                                                STRING_MAX_SIZE - 1);
                                } else {
                                        errlog("Invalid entries\n");
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
 * Get only groups list info from ADS server.
 */
int get_ads_groupinfo(LDAP *ld, struct adspasswd *adspwuid, gid_t *gid,
                      char *base)
{

        LDAPMessage         *entry;
        LDAPMessage         *res;
        BerElement          *ptr;
        struct      berval **bers      = NULL;
        char*                grattrs[] = {"msSFU30GidNumber", NULL};
        int                  ngroups   = 0;
        int                  maxgroups = 0;
        int                  ret       = 0;
        int                  i         = 0;
        char                 str[STRING_MAX_SIZE];
        char                *attrib;

        /* Get the groups info. */
        memset(str, 0, sizeof(str));
        sprintf(str, "(&(objectClass=Group)(objectCategory=Group)"
                       "(member=CN=%s, CN=Users,%s))", adspwuid->cn, base);

        maxgroups = sysconf(_SC_NGROUPS_MAX);
        if (maxgroups > NGROUPS_MAX)
                maxgroups = NGROUPS_MAX;

        gid[ngroups++] = adspwuid->pw_gid;

        /* For groups */
        ret = ldap_search_ext_s(ld, base, LDAP_SCOPE_SUBTREE, str, grattrs, 0,
                                NULL, NULL, NULL, 0, &res);
        if (ret != LDAP_SUCCESS) {
                errlog("%s\n", ldap_err2string(ret));
                return -1;
        }

        /* step through each entry returned */
        for (entry = ldap_first_entry( ld, res); entry != NULL;
             entry = ldap_next_entry(ld, entry)) {
                for (attrib = ldap_first_attribute(ld, entry, &ptr);
                     attrib != NULL;
                     attrib = ldap_next_attribute(ld, entry, ptr)) {
                        bers = ldap_get_values_len(ld, entry, attrib);
                        for (i = 0; bers[i] != NULL; i++) {
                                if (!strncmp(attrib, "msSFU30GidNumber",
                                             strlen(attrib))) {
                                        if (atoi(bers[i]->bv_val) == gid[0])
                                                continue;

                                        if (ngroups == maxgroups)
                                                break;

                                        gid[ngroups++] = atoi(bers[i]->bv_val);
                                } else {
                                        errlog("Invalid entries\n");
                                        return -1;
                                }
                        }
                        ldap_value_free_len(bers);

                        if (ngroups == maxgroups)
                                break;
                }
        }
        qsort(gid, ngroups, sizeof(*gid), compare_gid_t);

        return ngroups;
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

/*
 * Get particular user info and its groups list info from ADS server.
 */
int get_groups_ads(struct identity_downcall_data *data)
{
        struct   adspasswd  pw;
        unsigned int        ngroups = 0;
        LDAP               *ld;
        FILE               *ads_fp;
        gid_t              *groups;
        char                params[NO_OF_PARAMS_REQD][2][STRING_MAX_SIZE];
        char               *pw_name;
        int                 namelen;
        int                 i;

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

        i = getindex("base", params);

        if (get_ads_userinfo(&ld, &pw, data->idd_uid, params[i][1]) != 0) {
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

        ngroups = get_ads_groupinfo(ld, &pw, groups, params[i][1]);
        if (ngroups <= 0) {
                errlog("Failed to get group info\n");
                data->idd_err = EIDRM;
                goto out;
        }

        if (ldap_disconnect(ld) != 0) {
                errlog("ldap disconnect failed\n");
                data->idd_err = EPROTO;
                goto out;
        }

        qsort(groups, ngroups, sizeof(*groups), compare_gid_t);
        data->idd_ngroups = ngroups;
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
