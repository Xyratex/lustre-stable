#!/bin/bash

# FIXME - there is no reason to use all of these different
#   return codes, espcially when most of them are mapped to something
#   else anyway.  The combination of test number and return code
#   figure out what failed.

set -e

ONLY=${ONLY:-"$*"}

# bug number for skipped test:
#               15977
ALWAYS_EXCEPT="$CONF_SANITY_EXCEPT"
# UPDATE THE COMMENT ABOVE WITH BUG NUMBERS WHEN CHANGING ALWAYS_EXCEPT!

if [ "$FAILURE_MODE" = "HARD" ]; then
	CONFIG_EXCEPTIONS="24a " && \
	echo "Except the tests: $CONFIG_EXCEPTIONS for FAILURE_MODE=$FAILURE_MODE, bug 23573" && \
	ALWAYS_EXCEPT="$ALWAYS_EXCEPT $CONFIG_EXCEPTIONS"
fi

SRCDIR=`dirname $0`
PATH=$PWD/$SRCDIR:$SRCDIR:$SRCDIR/../utils:$PATH

PTLDEBUG=${PTLDEBUG:--1}
SAVE_PWD=$PWD
LUSTRE=${LUSTRE:-`dirname $0`/..}
RLUSTRE=${RLUSTRE:-$LUSTRE}
export MULTIOP=${MULTIOP:-multiop}

. $LUSTRE/tests/test-framework.sh
init_test_env $@

# use small MDS + OST size to speed formatting time
# do not use too small MDSSIZE/OSTSIZE, which affect the default jouranl size
MDSSIZE=200000
OSTSIZE=200000
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

if ! combined_mgs_mds; then
    # bug number for skipped test:    23954
    ALWAYS_EXCEPT="$ALWAYS_EXCEPT       24b"
fi

# STORED_MDSSIZE is used in test_18
if [ -n "$MDSSIZE" ]; then
    STORED_MDSSIZE=$MDSSIZE
fi

# pass "-E lazy_itable_init" to mke2fs to speed up the formatting time
for facet in MGS MDS OST; do
    opts=${facet}_MKFS_OPTS
    if [[ ${!opts} != *lazy_itable_init* ]]; then
        eval SAVED_${facet}_MKFS_OPTS=\"${!opts}\"
        if [[ ${!opts} != *mkfsoptions* ]]; then
            eval ${facet}_MKFS_OPTS=\"${!opts} --mkfsoptions='\\\"-E lazy_itable_init\\\"'\"
        else
            val=${!opts//--mkfsoptions=\\\"/--mkfsoptions=\\\"-E lazy_itable_init }
            eval ${facet}_MKFS_OPTS='${val}'
        fi
    fi
done

init_logging

#
require_dsh_mds || exit 0
require_dsh_ost || exit 0
#
[ "$SLOW" = "no" ] && EXCEPT_SLOW="30a 31 45"


assert_DIR

reformat() {
        formatall
}

writeconf1() {
	local facet=$1
	local dev=$2

	stop ${facet} -f
	rm -f ${facet}active
	# who knows if/where $TUNEFS is installed?  Better reformat if it fails...
	do_facet ${facet} "$TUNEFS --quiet --writeconf $dev" ||
		{ echo "tunefs failed, reformatting instead" && reformat_and_config && return 1; }
	return 0
}

writeconf() {
	# we need ldiskfs
	load_modules
	# if writeconf fails anywhere, we reformat everything
	writeconf1 mds `mdsdevname 1` || return 0
	writeconf1 ost1 `ostdevname 1` || return 0
	writeconf1 ost2 `ostdevname 2` || return 0
}

gen_config() {
	# The MGS must be started before the OSTs for a new fs, so start
	# and stop to generate the startup logs.
	start_mds
	start_ost
        wait_osc_import_state mds ost FULL
	stop_ost
	stop_mds
}

reformat_and_config() {
	reformat
	if ! combined_mgs_mds ; then
		start_mgs
	fi
	gen_config
}

start_mgs () {
	echo "start mgs service on $(facet_active_host mgs)"
	start mgs $MGSDEV $MGS_MOUNT_OPTS $@
}

start_mds() {
	local facet=$SINGLEMDS
	# we can not use MDSDEV1 here because SINGLEMDS could be set not to mds1 only
	local num=$(echo $facet | tr -d "mds")
	local dev=$(mdsdevname $num)
	echo "start mds service on `facet_active_host $facet`"
	start $facet ${dev} $MDS_MOUNT_OPTS $@ || return 94
}

start_mgsmds() {
	if ! combined_mgs_mds ; then
		start_mgs
	fi
	start_mds $@
}

stop_mds() {
	echo "stop mds service on `facet_active_host $SINGLEMDS`"
	# These tests all use non-failover stop
	stop $SINGLEMDS -f  || return 97
}

stop_mgs() {
       echo "stop mgs service on `facet_active_host mgs`"
       # These tests all use non-failover stop
       stop mgs -f  || return 97
}

stop_mgsmds_with_timeout() {
	local timeout=${1:-0}
	local facet
	if ! combined_mgs_mds ; then
		facet=mgs
	else
		facet=$SINGLEMDS
	fi
	echo stop $facet on $(facet_active_host $facet), \
timeout $timeout seconds
	stop_with_timeout $timeout $facet -f
}

start_ost() {
	echo "start ost1 service on `facet_active_host ost1`"
	start ost1 `ostdevname 1` $OST_MOUNT_OPTS $@ || return 95
}

stop_ost() {
	echo "stop ost1 service on `facet_active_host ost1`"
	# These tests all use non-failover stop
	stop ost1 -f  || return 98
}

start_ost2() {
	echo "start ost2 service on `facet_active_host ost2`"
	start ost2 `ostdevname 2` $OST_MOUNT_OPTS $@ || return 92
}

stop_ost2() {
	echo "stop ost2 service on `facet_active_host ost2`"
	# These tests all use non-failover stop
	stop ost2 -f  || return 93
}

mount_client() {
	local MOUNTPATH=$1
	echo "mount $FSNAME on ${MOUNTPATH}....."
	zconf_mount `hostname` $MOUNTPATH  || return 96
}

remount_client() {
	local mountopt="-o remount,$1"
	local MOUNTPATH=$2
	echo "remount '$1' lustre on ${MOUNTPATH}....."
	zconf_mount `hostname`  $MOUNTPATH "$mountopt"  || return 96
}

umount_client() {
	local MOUNTPATH=$1
	echo "umount lustre on ${MOUNTPATH}....."
	zconf_umount `hostname` $MOUNTPATH || return 97
}

manual_umount_client(){
	local rc
	local FORCE=$1
	echo "manual umount lustre on ${MOUNT}...."
	do_facet client "umount -d ${FORCE} $MOUNT"
	rc=$?
	return $rc
}

setup() {
	start_mds || error "MDT start failed"
	start_ost || error "OST start failed"
	mount_client $MOUNT || error "client start failed"
	client_up || error "client_up failed"
}

setup2() {
	start_mds || error "MDT start failed"
	start_ost || error "OST1 start failed"
	start_ost2 || error "OST2 start failed"
	mount_client $MOUNT || error "client start failed"
	client_up || error "client_up failed"
}

setup_noconfig() {
	if ! combined_mgs_mds ; then
		start_mgs
	fi

	start_mds
	start_ost
	mount_client $MOUNT
}

unload_modules_conf () {
	if combined_mgs_mds || ! local_mode; then
		unload_modules || return 1
	fi
}

cleanup_nocli() {
	stop_ost || return 202
	stop_mds || return 201
	unload_modules_conf || return 203
}

cleanup() {
	umount_client $MOUNT || return 200
	cleanup_nocli || return $?
}

check_mount() {
	do_facet client "cp /etc/passwd $DIR/a" || return 71
	do_facet client "rm $DIR/a" || return 72
	# make sure lustre is actually mounted (touch will block,
        # but grep won't, so do it after)
        do_facet client "grep $MOUNT' ' /proc/mounts > /dev/null" || return 73
	echo "setup single mount lustre success"
}

check_mount2() {
	do_facet client "touch $DIR/a" || return 71
	do_facet client "rm $DIR/a" || return 72
	do_facet client "touch $DIR2/a" || return 73
	do_facet client "rm $DIR2/a" || return 74
	echo "setup double mount lustre success"
}

build_test_filter

if [ "$ONLY" == "setup" ]; then
	setup
	exit
fi

if [ "$ONLY" == "cleanup" ]; then
	cleanup
	exit
fi

init_gss

#create single point mountpoint

reformat_and_config

test_0() {
        setup
	check_mount || return 41
	cleanup || return $?
}
run_test 0 "single mount setup"

test_1() {
	start_mds || error "MDT start failed"
	start_ost
	echo "start ost second time..."
	start_ost && error "2nd OST start should fail"
	mount_client $MOUNT || error "client start failed"
	check_mount || return 42
	cleanup || return $?
}
run_test 1 "start up ost twice (should return errors)"

test_2() {
	start_mds
	echo "start mds second time.."
	start_mds && error "2nd MDT start should fail"
	start_ost
	mount_client $MOUNT
	check_mount || return 43
	cleanup || return $?
}
run_test 2 "start up mds twice (should return err)"

test_3() {
	setup
	#mount.lustre returns an error if already in mtab
	mount_client $MOUNT && error "2nd client mount should fail"
	check_mount || return 44
	cleanup || return $?
}
run_test 3 "mount client twice (should return err)"

test_4() {
	setup
	touch $DIR/$tfile || return 85
	stop_ost -f
	cleanup
	eno=$?
	# ok for ost to fail shutdown
	if [ 202 -ne $eno ]; then
		return $eno;
	fi
	return 0
}
run_test 4 "force cleanup ost, then cleanup"

test_5a() {	# was test_5
	setup
	touch $DIR/$tfile || return 1
	fuser -m -v $MOUNT && echo "$MOUNT is in use by user space process."

	stop_mds -f || return 2

	# cleanup may return an error from the failed
	# disconnects; for now I'll consider this successful
	# if all the modules have unloaded.
	umount -d $MOUNT &
	UMOUNT_PID=$!
	sleep 6
	echo "killing umount"
	kill -TERM $UMOUNT_PID
	echo "waiting for umount to finish"
	wait $UMOUNT_PID
	if grep " $MOUNT " /proc/mounts; then
		echo "test 5: /proc/mounts after failed umount"
		umount $MOUNT &
		UMOUNT_PID=$!
		sleep 2
		echo "killing umount"
		kill -TERM $UMOUNT_PID
		echo "waiting for umount to finish"
		wait $UMOUNT_PID
		grep " $MOUNT " /proc/mounts && echo "test 5: /proc/mounts after second umount" && return 11
	fi

	manual_umount_client
	# stop_mds is a no-op here, and should not fail
	cleanup_nocli || return $?
	# df may have lingering entry
	manual_umount_client
	# mtab may have lingering entry
	local WAIT=0
	local MAX_WAIT=20
	local sleep=1
	while [ "$WAIT" -ne "$MAX_WAIT" ]; do
		sleep $sleep
		grep -q $MOUNT" " /etc/mtab || break
		echo "Waiting /etc/mtab updated ... "
		WAIT=$(( WAIT + sleep))
	done
	[ "$WAIT" -eq "$MAX_WAIT" ] && error "/etc/mtab is not updated in $WAIT secs"
	echo "/etc/mtab updated in $WAIT secs"
}
run_test 5a "force cleanup mds, then cleanup"

cleanup_5b () {
	trap 0
	start_mgs
}

test_5b() {
	grep " $MOUNT " /etc/mtab && \
		error false "unexpected entry in mtab before mount" && return 10

	local rc=0
	start_ost
	if ! combined_mgs_mds ; then
		trap cleanup_5b EXIT ERR
		start_mds
		stop mgs
	fi

	[ -d $MOUNT ] || mkdir -p $MOUNT
	mount_client $MOUNT && rc=1
	grep " $MOUNT " /etc/mtab && \
		error "$MOUNT entry in mtab after failed mount" && rc=11
	umount_client $MOUNT
	# stop_mds is a no-op here, and should not fail
	cleanup_nocli || rc=$?
	if ! combined_mgs_mds ; then
		cleanup_5b
	fi
	return $rc
}
run_test 5b "Try to start a client with no MGS (should return errs)"

test_5c() {
	grep " $MOUNT " /etc/mtab && \
		error false "unexpected entry in mtab before mount" && return 10

	local rc=0
	start_mds
	start_ost
	[ -d $MOUNT ] || mkdir -p $MOUNT
	local oldfs="${FSNAME}"
	FSNAME="wrong.${FSNAME}"
	mount_client $MOUNT || :
	FSNAME=${oldfs}
	grep " $MOUNT " /etc/mtab && \
		error "$MOUNT entry in mtab after failed mount" && rc=11
	umount_client $MOUNT
	cleanup_nocli  || rc=$?
	return $rc
}
run_test 5c "cleanup after failed mount (bug 2712) (should return errs)"

test_5d() {
	grep " $MOUNT " /etc/mtab && \
		error false "unexpected entry in mtab before mount" && return 10

	local rc=0
	start_ost
	start_mds
	stop_ost -f
	mount_client $MOUNT || rc=1
	cleanup  || rc=$?
	grep " $MOUNT " /etc/mtab && \
		error "$MOUNT entry in mtab after unmount" && rc=11
	return $rc
}
run_test 5d "mount with ost down"

test_5e() {
	grep " $MOUNT " /etc/mtab && \
		error false "unexpected entry in mtab before mount" && return 10

	local rc=0
	start_mds
	start_ost

#define OBD_FAIL_PTLRPC_DELAY_SEND       0x506
	do_facet client "lctl set_param fail_loc=0x80000506"
	mount_client $MOUNT || echo "mount failed (not fatal)"
	cleanup  || rc=$?
	grep " $MOUNT " /etc/mtab && \
		error "$MOUNT entry in mtab after unmount" && rc=11
	return $rc
}
run_test 5e "delayed connect, don't crash (bug 10268)"

test_5f() {
	if combined_mgs_mds ; then
		skip "combined mgs and mds"
		return 0
	fi

	grep " $MOUNT " /etc/mtab && \
		error false "unexpected entry in mtab before mount" && return 10

	local rc=0
	start_ost
	[ -d $MOUNT ] || mkdir -p $MOUNT
	mount_client $MOUNT &
	local pid=$!
	echo client_mount pid is $pid

	sleep 5

	if ! ps -f -p $pid >/dev/null; then
		wait $pid
		rc=$?
		grep " $MOUNT " /etc/mtab && echo "test 5f: mtab after mount"
		error "mount returns $rc, expected to hang"
		rc=11
		cleanup || rc=$?
		return $rc
	fi

	# start mds
	start_mds

	# mount should succeed after start mds
	wait $pid
	rc=$?
	[ $rc -eq 0 ] || error "mount returned $rc"
	grep " $MOUNT " /etc/mtab && echo "test 5f: mtab after mount"
	cleanup || return $?
	return $rc
}
run_test 5f "mds down, cleanup after failed mount (bug 2712)"

test_6() {
	setup
	manual_umount_client
	mount_client ${MOUNT} || return 87
	touch $DIR/a || return 86
	cleanup  || return $?
}
run_test 6 "manual umount, then mount again"

test_7() {
	setup
	manual_umount_client
	cleanup_nocli || return $?
}
run_test 7 "manual umount, then cleanup"

test_8() {
	setup
	mount_client $MOUNT2
	check_mount2 || return 45
	umount_client $MOUNT2
	cleanup  || return $?
}
run_test 8 "double mount setup"

test_9() {
        start_ost

	do_facet ost1 lctl set_param debug=\'inode trace\' || return 1
	do_facet ost1 lctl set_param subsystem_debug=\'mds ost\' || return 1

        CHECK_PTLDEBUG="`do_facet ost1 lctl get_param -n debug`"
        if [ "$CHECK_PTLDEBUG" ] && { \
	   [ "$CHECK_PTLDEBUG" = "trace inode warning error emerg console" ] ||
	   [ "$CHECK_PTLDEBUG" = "trace inode" ]; }; then
           echo "lnet.debug success"
        else
           echo "lnet.debug: want 'trace inode', have '$CHECK_PTLDEBUG'"
           return 1
        fi
        CHECK_SUBSYS="`do_facet ost1 lctl get_param -n subsystem_debug`"
        if [ "$CHECK_SUBSYS" ] && [ "$CHECK_SUBSYS" = "mds ost" ]; then
           echo "lnet.subsystem_debug success"
        else
           echo "lnet.subsystem_debug: want 'mds ost', have '$CHECK_SUBSYS'"
           return 1
        fi
        stop_ost || return $?
}
run_test 9 "test ptldebug and subsystem for mkfs"

is_blkdev () {
        local facet=$1
        local dev=$2
        local size=${3:-""}

        local rc=0
        do_facet $facet "test -b $dev" || rc=1
        if [[ "$size" ]]; then
                local in=$(do_facet $facet "dd if=$dev of=/dev/null bs=1k count=1 skip=$size 2>&1" |\
                        awk '($3 == "in") { print $1 }')
                [[ $in  = "1+0" ]] || rc=1
        fi
        return $rc
}

#
# Test 16 was to "verify that lustre will correct the mode of OBJECTS".
# But with new MDS stack we don't care about the mode of local objects
# anymore, so this test is removed. See bug 22944 for more details.
#

test_17() {
        local mgsdev

        setup
        check_mount || return 41
        cleanup || return $?

        echo "Remove mds config log"
        if combined_mgs_mds ; then
                mgsdev=$(mdsdevname 1)
        else
                mgsdev=$MGSDEV
                stop mgs
        fi

        do_facet mgs "$DEBUGFS -w -R 'unlink CONFIGS/$FSNAME-MDT0000' $mgsdev || return \$?" || return $?

        if ! combined_mgs_mds ; then
                start_mgs
        fi

        start_ost
        start_mds && return 42
        reformat_and_config
}
run_test 17 "Verify failed mds_postsetup won't fail assertion (2936) (should return errs)"

test_18() {
        [ "$FSTYPE" != "ldiskfs" ] && skip "not needed for FSTYPE=$FSTYPE" && return

        local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

        local MIN=2000000

        local OK=
        # check if current MDSSIZE is large enough
        [ $MDSSIZE -ge $MIN ] && OK=1 && myMDSSIZE=$MDSSIZE && \
                log "use MDSSIZE=$MDSSIZE"

        # check if the global config has a large enough MDSSIZE
        [ -z "$OK" -a ! -z "$STORED_MDSSIZE" ] && [ $STORED_MDSSIZE -ge $MIN ] && \
                OK=1 && myMDSSIZE=$STORED_MDSSIZE && \
                log "use STORED_MDSSIZE=$STORED_MDSSIZE"

        # check if the block device is large enough
        [ -z "$OK" ] && $(is_blkdev $SINGLEMDS $MDSDEV $MIN) && OK=1 &&
                myMDSSIZE=$MIN && log "use device $MDSDEV with MIN=$MIN"

        # check if a loopback device has enough space for fs metadata (5%)

        if [ -z "$OK" ]; then
                local SPACE=$(do_facet $SINGLEMDS "[ -f $MDSDEV -o ! -e $MDSDEV ] && df -P \\\$(dirname $MDSDEV)" |
                        awk '($1 != "Filesystem") {print $4}')
                ! [ -z "$SPACE" ]  &&  [ $SPACE -gt $((MIN / 20)) ] && \
                        OK=1 && myMDSSIZE=$MIN && \
                        log "use file $MDSDEV with MIN=$MIN"
        fi

        [ -z "$OK" ] && skip_env "$MDSDEV too small for ${MIN}kB MDS" && return


        echo "mount mds with large journal..."
        local OLD_MDS_MKFS_OPTS=$MDS_MKFS_OPTS

        local opts="--mdt --fsname=$FSNAME --device-size=$myMDSSIZE --param sys.timeout=$TIMEOUT $MDSOPT"

        if combined_mgs_mds ; then
            MDS_MKFS_OPTS="--mgs $opts"
        else
            MDS_MKFS_OPTS="--mgsnode=$MGSNID $opts"
        fi

        reformat_and_config
        echo "mount lustre system..."
        setup
        check_mount || return 41

        echo "check journal size..."
        local FOUNDSIZE=$(do_facet $SINGLEMDS "$DEBUGFS -c -R 'stat <8>' $MDSDEV" | awk '/Size: / { print $NF; exit;}')
        if [ $FOUNDSIZE -gt $((32 * 1024 * 1024)) ]; then
                log "Success: mkfs creates large journals. Size: $((FOUNDSIZE >> 20))M"
        else
                error "expected journal size > 32M, found $((FOUNDSIZE >> 20))M"
        fi

        cleanup || return $?

        MDS_MKFS_OPTS=$OLD_MDS_MKFS_OPTS
        reformat_and_config
}
run_test 18 "check mkfs creates large journals"

test_19a() {
	start_mds || return 1
	stop_mds -f || return 2
}
run_test 19a "start/stop MDS without OSTs"

test_19b() {
	start_ost || return 1
	stop_ost -f || return 2
}
run_test 19b "start/stop OSTs without MDS"

test_20() {
	# first format the ost/mdt
	start_mds
	start_ost
	mount_client $MOUNT
	check_mount || return 43
	rm -f $DIR/$tfile
	remount_client ro $MOUNT || return 44
	touch $DIR/$tfile && echo "$DIR/$tfile created incorrectly" && return 45
	[ -e $DIR/$tfile ] && echo "$DIR/$tfile exists incorrectly" && return 46
	remount_client rw $MOUNT || return 47
	touch $DIR/$tfile
	[ ! -f $DIR/$tfile ] && echo "$DIR/$tfile missing" && return 48
	MCNT=`grep -c $MOUNT /etc/mtab`
	[ "$MCNT" -ne 1 ] && echo "$MOUNT in /etc/mtab $MCNT times" && return 49
	umount_client $MOUNT
	stop_mds
	stop_ost
}
run_test 20 "remount ro,rw mounts work and doesn't break /etc/mtab"

test_21a() {
        start_mds
	start_ost
        wait_osc_import_state mds ost FULL
	stop_ost
	stop_mds
}
run_test 21a "start mds before ost, stop ost first"

test_21b() {
        start_ost
	start_mds
        wait_osc_import_state mds ost FULL
	stop_mds
	stop_ost
}
run_test 21b "start ost before mds, stop mds first"

test_21c() {
        start_ost
	start_mds
	start_ost2
        wait_osc_import_state mds ost2 FULL
	stop_ost
	stop_ost2
	stop_mds
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 21c "start mds between two osts, stop mds last"

test_21d() {
        if combined_mgs_mds ; then
                skip "need separate mgs device" && return 0
        fi
        stopall

        reformat

        start_mgs
        start_ost
        start_ost2
        start_mds
        wait_osc_import_state mds ost2 FULL

        stop_ost
        stop_ost2
        stop_mds
        stop_mgs
        #writeconf to remove all ost2 traces for subsequent tests
        writeconf
        start_mgs
}
run_test 21d "start mgs then ost and then mds"

test_22() {
	start_mds

	echo Client mount with ost in logs, but none running
	start_ost
	# wait until mds connected to ost and open client connection
        wait_osc_import_state mds ost FULL
	stop_ost
	mount_client $MOUNT
	# check_mount will block trying to contact ost
	mcreate $DIR/$tfile || return 40
	rm -f $DIR/$tfile || return 42
	umount_client $MOUNT
	pass

	echo Client mount with a running ost
	start_ost
	if $GSS; then
		# if gss enabled, wait full time to let connection from
		# mds to ost be established, due to the mismatch between
		# initial connect timeout and gss context negotiation timeout.
		# This perhaps could be remove after AT landed.
		echo "sleep $((TIMEOUT + TIMEOUT + TIMEOUT))s"
		sleep $((TIMEOUT + TIMEOUT + TIMEOUT))
	fi
	mount_client $MOUNT
        wait_osc_import_state mds ost FULL
        wait_osc_import_state client ost FULL
	check_mount || return 41
	pass

	cleanup
}
run_test 22 "start a client before osts (should return errs)"

test_23a() {	# was test_23
	setup
	# fail mds
	stop $SINGLEMDS
	# force down client so that recovering mds waits for reconnect
	local running=$(grep -c $MOUNT /proc/mounts) || true
	if [ $running -ne 0 ]; then
		echo "Stopping client $MOUNT (opts: -f)"
		umount -f $MOUNT
	fi

	# enter recovery on mds
	start_mds
	# try to start a new client
	mount_client $MOUNT &
	sleep 5
	MOUNT_PID=$(ps -ef | grep "t lustre" | grep -v grep | awk '{print $2}')
	MOUNT_LUSTRE_PID=`ps -ef | grep mount.lustre | grep -v grep | awk '{print $2}'`
	echo mount pid is ${MOUNT_PID}, mount.lustre pid is ${MOUNT_LUSTRE_PID}
	ps --ppid $MOUNT_PID
	ps --ppid $MOUNT_LUSTRE_PID
	echo "waiting for mount to finish"
	ps -ef | grep mount
	# "ctrl-c" sends SIGINT but it usually (in script) does not work on child process
	# SIGTERM works but it does not spread to offspring processses
	kill -s TERM $MOUNT_PID
	kill -s TERM $MOUNT_LUSTRE_PID
	# we can not wait $MOUNT_PID because it is not a child of this shell
	local PID1
	local PID2
	local WAIT=0
	local MAX_WAIT=30
	local sleep=1
	while [ "$WAIT" -lt "$MAX_WAIT" ]; do
		sleep $sleep
		PID1=$(ps -ef | awk '{print $2}' | grep -w $MOUNT_PID)
		PID2=$(ps -ef | awk '{print $2}' | grep -w $MOUNT_LUSTRE_PID)
		echo PID1=$PID1
		echo PID2=$PID2
		[ -z "$PID1" -a -z "$PID2" ] && break
		echo "waiting for mount to finish ... "
		WAIT=$(( WAIT + sleep))
	done
	if [ "$WAIT" -eq "$MAX_WAIT" ]; then
		error "MOUNT_PID $MOUNT_PID and "\
		"MOUNT_LUSTRE_PID $MOUNT_LUSTRE_PID still not killed in $WAIT secs"
		ps -ef | grep mount
	fi
	stop_mds || error "stopping MDSes failed"
	stop_ost || error "stopping OSSes failed"
}
run_test 23a "interrupt client during recovery mount delay"

umount_client $MOUNT
cleanup_nocli

test_23b() {    # was test_23
	start_mds
	start_ost
	# Simulate -EINTR during mount OBD_FAIL_LDLM_CLOSE_THREAD
	lctl set_param fail_loc=0x80000313
	mount_client $MOUNT
	cleanup
}
run_test 23b "Simulate -EINTR during mount"

fs2mds_HOST=$mds_HOST
fs2ost_HOST=$ost_HOST

cleanup_24a() {
	trap 0
	echo "umount $MOUNT2 ..."
	umount $MOUNT2 || true
	echo "stopping fs2mds ..."
	stop fs2mds -f || true
	echo "stopping fs2ost ..."
	stop fs2ost -f || true
}

test_24a() {
	local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

	if [ -z "$fs2ost_DEV" -o -z "$fs2mds_DEV" ]; then
		is_blkdev $SINGLEMDS $MDSDEV && \
		skip_env "mixed loopback and real device not working" && return
	fi

	[ -n "$ost1_HOST" ] && fs2ost_HOST=$ost1_HOST

	local fs2mdsdev=${fs2mds_DEV:-${MDSDEV}_2}
	local fs2ostdev=${fs2ost_DEV:-$(ostdevname 1)_2}

	# test 8-char fsname as well
	local FSNAME2=test1234

	add fs2mds $(mkfs_opts mds) --nomgs --mgsnode=$MGSNID \
		--fsname=$FSNAME2 --reformat $fs2mdsdev ||
		error "add fs2mds $fs2mdsdev failed"

	add fs2ost $(mkfs_opts ost) --fsname=$FSNAME2 --reformat $fs2ostdev ||
		error "add fs2ost $fs2ostdev failed"

	setup
	start fs2mds $fs2mdsdev $MDS_MOUNT_OPTS && trap cleanup_24a EXIT INT
	start fs2ost $fs2ostdev $OST_MOUNT_OPTS
	mkdir -p $MOUNT2
	mount -t lustre $MGSNID:/${FSNAME2} $MOUNT2 || return 1
	# 1 still works
	check_mount || return 2
	# files written on 1 should not show up on 2
	cp /etc/passwd $DIR/$tfile
	sleep 10
	[ -e $MOUNT2/$tfile ] && error "File bleed" && return 7
	# 2 should work
	sleep 5
	cp /etc/passwd $MOUNT2/b || return 3
	rm $MOUNT2/b || return 4
	# 2 is actually mounted
        grep $MOUNT2' ' /proc/mounts > /dev/null || return 5
	# failover
	facet_failover fs2mds
	facet_failover fs2ost
	df
	umount_client $MOUNT
	# the MDS must remain up until last MDT
	stop_mds
	MDS=$(do_facet $SINGLEMDS "lctl get_param -n devices" | awk '($3 ~ "mdt" && $4 ~ "MDT") { print $4 }' | head -1)
	[ -z "$MDS" ] && error "No MDT" && return 8
	cleanup_24a
	cleanup_nocli || return 6
}
run_test 24a "Multiple MDTs on a single node"

test_24b() {
	local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

	if [ -z "$fs2mds_DEV" ]; then
		local dev=${SINGLEMDS}_dev
		local MDSDEV=${!dev}
		is_blkdev $SINGLEMDS $MDSDEV && \
		skip_env "mixed loopback and real device not working" && return
	fi

	local fs2mdsdev=${fs2mds_DEV:-${MDSDEV}_2}

	add fs2mds $(mkfs_opts mds) --mgs --fsname=${FSNAME}2 \
		--reformat $fs2mdsdev || error "add fs2mds $fs2mdsdev failed"
	setup
	start fs2mds $fs2mdsdev $MDS_MOUNT_OPTS && return 2
	cleanup || return 6
}
run_test 24b "Multiple MGSs on a single node (should return err)"

test_25() {
	setup
	check_mount || return 2
	local MODULES=$($LCTL modules | awk '{ print $2 }')
	rmmod $MODULES 2>/dev/null || true
	cleanup || return 6
}
run_test 25 "Verify modules are referenced"

test_26() {
    load_modules
    # we need modules before mount for sysctl, so make sure...
    do_facet $SINGLEMDS "lsmod | grep -q lustre || modprobe lustre"
#define OBD_FAIL_MDS_FS_SETUP            0x135
    do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000135"
    start_mds && echo MDS started && return 1
    lctl get_param -n devices
    DEVS=$(lctl get_param -n devices | egrep -v MG | wc -l)
    [ $DEVS -gt 0 ] && return 2
    unload_modules_conf || return $?
}
run_test 26 "MDT startup failure cleans LOV (should return errs)"

test_27a() {
	start_ost || return 1
	start_mds || return 2
	echo "Requeue thread should have started: "
	ps -e | grep ll_cfg_requeue
	set_conf_param_and_check ost1					      \
	   "lctl get_param -n obdfilter.$FSNAME-OST0000.client_cache_seconds" \
	   "$FSNAME-OST0000.ost.client_cache_seconds" || return 3
	cleanup_nocli
}
run_test 27a "Reacquire MGS lock if OST started first"

test_27b() {
	# FIXME. ~grev
        setup
        local device=$(do_facet $SINGLEMDS "lctl get_param -n devices" | awk '($3 ~ "mdt" && $4 ~ "MDT") { print $4 }')

	facet_failover $SINGLEMDS
	set_conf_param_and_check $SINGLEMDS				\
		"lctl get_param -n mdt.$device.identity_acquire_expire"	\
		"$device.mdt.identity_acquire_expire" || return 3
	set_conf_param_and_check client					\
		"lctl get_param -n mdc.$device-mdc-*.max_rpcs_in_flight"\
		"$device.mdc.max_rpcs_in_flight" || return 4
	check_mount
	cleanup
}
run_test 27b "Reacquire MGS lock after failover"

test_27c() {
	if ! combined_mgs_mds ; then
		start_mgs
	else
		start_mds
	fi
# first OBD_FAIL_MGC_FAIL_NET            0x907 used to hit
# OBD_FAIL_MGC_FS_CLEANUP_RACE     0x908
	$LCTL set_param fail_loc=0x907
	start_ost
	cleanup
}
run_test 27c "do not panic on mgs fs cleanup vs lock enqueue race"

test_28() {
        setup
	TEST="lctl get_param -n llite.$FSNAME-*.max_read_ahead_whole_mb"
	PARAM="$FSNAME.llite.max_read_ahead_whole_mb"
	ORIG=$($TEST)
	FINAL=$(($ORIG + 1))
	set_conf_param_and_check client "$TEST" "$PARAM" $FINAL || return 3
	FINAL=$(($FINAL + 1))
	set_conf_param_and_check client "$TEST" "$PARAM" $FINAL || return 4
	umount_client $MOUNT || return 200
	mount_client $MOUNT
	RESULT=$($TEST)
	if [ $RESULT -ne $FINAL ]; then
	    echo "New config not seen: wanted $FINAL got $RESULT"
	    return 4
	else
	    echo "New config success: got $RESULT"
	fi
	set_conf_param_and_check client "$TEST" "$PARAM" $ORIG || return 5
	cleanup
}
run_test 28 "permanent parameter setting"

test_29() {
	[ "$OSTCOUNT" -lt "2" ] && skip_env "$OSTCOUNT < 2, skipping" && return
        setup > /dev/null 2>&1
	start_ost2
	sleep 10

	local PARAM="$FSNAME-OST0001.osc.active"
        local PROC_ACT="osc.$FSNAME-OST0001-osc-[^M]*.active"
        local PROC_UUID="osc.$FSNAME-OST0001-osc-[^M]*.ost_server_uuid"

        ACTV=$(lctl get_param -n $PROC_ACT)
	DEAC=$((1 - $ACTV))
	set_conf_param_and_check client \
		"lctl get_param -n $PROC_ACT" "$PARAM" $DEAC || return 2
        # also check ost_server_uuid status
	RESULT=$(lctl get_param -n $PROC_UUID | grep DEACTIV)
	if [ -z "$RESULT" ]; then
	    echo "Live client not deactivated: $(lctl get_param -n $PROC_UUID)"
	    return 3
	else
	    echo "Live client success: got $RESULT"
	fi

	# check MDT too
	local mdtosc=$(get_mdtosc_proc_path $SINGLEMDS $FSNAME-OST0001)
	mdtosc=${mdtosc/-MDT*/-MDT\*}
	local MPROC="osc.$mdtosc.active"
	local MAX=30
	local WAIT=0
	while [ 1 ]; do
	    sleep 5
	    RESULT=`do_facet $SINGLEMDS " lctl get_param -n $MPROC"`
	    [ ${PIPESTATUS[0]} = 0 ] || error "Can't read $MPROC"
	    if [ $RESULT -eq $DEAC ]; then
		echo "MDT deactivated also after $WAIT sec (got $RESULT)"
		break
	    fi
	    WAIT=$((WAIT + 5))
	    if [ $WAIT -eq $MAX ]; then
		echo "MDT not deactivated: wanted $DEAC got $RESULT"
		return 4
	    fi
	    echo "Waiting $(($MAX - $WAIT)) secs for MDT deactivated"
	done

        # quotacheck should not fail immediately after deactivate
	[ -n "$ENABLE_QUOTA" ] && { $LFS quotacheck -ug $MOUNT || error "quotacheck has failed" ; }

        # test new client starts deactivated
	umount_client $MOUNT || return 200
	mount_client $MOUNT
	RESULT=$(lctl get_param -n $PROC_UUID | grep DEACTIV | grep NEW)
	if [ -z "$RESULT" ]; then
	    echo "New client not deactivated from start: $(lctl get_param -n $PROC_UUID)"
	    return 5
	else
	    echo "New client success: got $RESULT"
	fi

        # quotacheck should not fail after umount/mount operation
	[ -n "$ENABLE_QUOTA" ] && { $LFS quotacheck -ug $MOUNT || error "quotacheck has failed" ; }

	# make sure it reactivates
	set_conf_param_and_check client \
		"lctl get_param -n $PROC_ACT" "$PARAM" $ACTV || return 6

	umount_client $MOUNT
	stop_ost2
	cleanup_nocli
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 29 "permanently remove an OST"

test_30a() {
	setup

	echo Big config llog
	TEST="lctl get_param -n llite.$FSNAME-*.max_read_ahead_whole_mb"
	ORIG=$($TEST)
	LIST=(1 2 3 4 5 4 3 2 1 2 3 4 5 4 3 2 1 2 3 4 5)
	for i in ${LIST[@]}; do
		set_conf_param_and_check client "$TEST" \
			"$FSNAME.llite.max_read_ahead_whole_mb" $i || return 3
	done
	# make sure client restart still works
	umount_client $MOUNT
	mount_client $MOUNT || return 4
	[ "$($TEST)" -ne "$i" ] && error "Param didn't stick across restart $($TEST) != $i"
	pass

	echo Erase parameter setting
	do_facet mgs "$LCTL conf_param -d $FSNAME.llite.max_read_ahead_whole_mb" || return 6
	umount_client $MOUNT
	mount_client $MOUNT || return 6
	FINAL=$($TEST)
	echo "deleted (default) value=$FINAL, orig=$ORIG"
	# assumes this parameter started at the default value
	[ "$FINAL" -eq "$ORIG" ] || fail "Deleted value=$FINAL, orig=$ORIG"

	cleanup
}
run_test 30a "Big config llog and conf_param deletion"

test_30b() {
	setup

	# Make a fake nid.  Use the OST nid, and add 20 to the least significant
	# numerical part of it. Hopefully that's not already a failover address for
	# the server.
	OSTNID=$(do_facet ost1 "$LCTL get_param nis" | tail -1 | awk '{print $1}')
	ORIGVAL=$(echo $OSTNID | egrep -oi "[0-9]*@")
	NEWVAL=$((($(echo $ORIGVAL | egrep -oi "[0-9]*") + 20) % 256))
	NEW=$(echo $OSTNID | sed "s/$ORIGVAL/$NEWVAL@/")
	echo "Using fake nid $NEW"

	TEST="$LCTL get_param -n osc.$FSNAME-OST0000-osc-[^M]*.import | grep failover_nids | sed -n 's/.*\($NEW\).*/\1/p'"
	set_conf_param_and_check client "$TEST" \
		"$FSNAME-OST0000.failover.node" $NEW ||
		error "didn't add failover nid $NEW"
	NIDS=$($LCTL get_param -n osc.$FSNAME-OST0000-osc-[^M]*.import | grep failover_nids)
	echo $NIDS
	NIDCOUNT=$(($(echo "$NIDS" | wc -w) - 1))
	echo "should have 2 failover nids: $NIDCOUNT"
	[ $NIDCOUNT -eq 2 ] || error "Failover nid not added"
	do_facet mgs "$LCTL conf_param -d $FSNAME-OST0000.failover.node" || error "conf_param delete failed"
	umount_client $MOUNT
	mount_client $MOUNT || return 3

	NIDS=$($LCTL get_param -n osc.$FSNAME-OST0000-osc-[^M]*.import | grep failover_nids)
	echo $NIDS
	NIDCOUNT=$(($(echo "$NIDS" | wc -w) - 1))
	echo "only 1 final nid should remain: $NIDCOUNT"
	[ $NIDCOUNT -eq 1 ] || error "Failover nids not removed"

	cleanup
}
run_test 30b "Remove failover nids"

test_31() { # bug 10734
        # ipaddr must not exist
        mount -t lustre 4.3.2.1@tcp:/lustre $MOUNT || true
	cleanup
}
run_test 31 "Connect to non-existent node (shouldn't crash)"

# Use these start32/stop32 fn instead of t-f start/stop fn,
# for local devices, to skip global facet vars init
stop32 () {
	local facet=$1
	shift
	echo "Stopping local ${MOUNT%/*}/${facet} (opts:$@)"
	umount -d $@ ${MOUNT%/*}/${facet}
	losetup -a
}

start32 () {
	local facet=$1
	shift
	local device=$1
	shift
	mkdir -p ${MOUNT%/*}/${facet}

	echo "Starting local ${facet}: $@ $device ${MOUNT%/*}/${facet}"
	mount -t lustre $@ ${device} ${MOUNT%/*}/${facet}
	local RC=$?
	if [ $RC -ne 0 ]; then
		echo "mount -t lustre $@ ${device} ${MOUNT%/*}/${facet}"
		echo "Start of ${device} of local ${facet} failed ${RC}"
	fi
	losetup -a
	return $RC
}

cleanup_nocli32 () {
	stop32 mds1 -f
	stop32 ost1 -f
	wait_exit_ST client
}

cleanup_32() {
	trap 0
	echo "Cleanup test_32 umount $MOUNT ..."
	umount -f $MOUNT || true
	echo "Cleanup local mds ost1 ..."
	cleanup_nocli32
	combined_mgs_mds || start_mgs
	unload_modules_conf
}

test_32a() {
	client_only && skip "client only testing" && return 0
	[ "$NETTYPE" = "tcp" ] || { skip "NETTYPE != tcp" && return 0; }
	[ -z "$TUNEFS" ] && skip_env "No tunefs" && return 0

	local DISK1_8=$LUSTRE/tests/disk1_8.tar.bz2
	[ ! -r $DISK1_8 ] && skip_env "Cannot find $DISK1_8" && return 0
	local tmpdir=$TMP/conf32a
	mkdir -p $tmpdir
	tar xjvf $DISK1_8 -C $tmpdir || \
		{ skip_env "Cannot untar $DISK1_8" && return 0; }

	# switch mds to external journal big enough for 1 ost
	local journal_size=$(mdd_max_txn_size_by_ost_nr 1)
	journal_size=$((journal_size * 4))
	set_external_journal $tmpdir/mds $tmpdir/mds.journal ${journal_size}

	load_modules
	$LCTL set_param debug="$PTLDEBUG"

	$TUNEFS $tmpdir/mds || error "tunefs failed"

	combined_mgs_mds || stop mgs

	# nids are wrong, so client wont work, but server should start
	start32 mds1 $tmpdir/mds "-o loop,exclude=lustre-OST0000" && \
		trap cleanup_32 EXIT INT || return 3

	local UUID=$($LCTL get_param -n mdt.lustre-MDT0000.uuid)
	echo MDS uuid $UUID
	[ "$UUID" == "lustre-MDT0000_UUID" ] || error "UUID is wrong: $UUID"

	$TUNEFS --mgsnode=$HOSTNAME $tmpdir/ost1 || error "tunefs failed"
	start32 ost1 $tmpdir/ost1 "-o loop" || return 5
	UUID=$($LCTL get_param -n obdfilter.lustre-OST0000.uuid)
	echo OST uuid $UUID
	[ "$UUID" == "lustre-OST0000_UUID" ] || error "UUID is wrong: $UUID"

	local NID=$($LCTL list_nids | head -1)

	echo "OSC changes should succeed:"
	$LCTL conf_param lustre-OST0000.osc.max_dirty_mb=15 || return 7
	$LCTL conf_param lustre-OST0000.failover.node=$NID || return 8
	echo "ok."

	echo "MDC changes should succeed:"
	$LCTL conf_param lustre-MDT0000.mdc.max_rpcs_in_flight=9 || return 9
	$LCTL conf_param lustre-MDT0000.failover.node=$NID || return 10
	echo "ok."

	echo "LOV changes should succeed:"
	$LCTL pool_new lustre.interop || return 11
	$LCTL conf_param lustre-MDT0000.lov.stripesize=4M || return 12
	echo "ok."

	cleanup_32

	# mount a second time to make sure we didnt leave upgrade flag on
	load_modules
	$TUNEFS --dryrun $tmpdir/mds || error "tunefs failed"

	combined_mgs_mds || stop mgs

	start32 mds1 $tmpdir/mds "-o loop,exclude=lustre-OST0000" && \
		trap cleanup_32 EXIT INT || return 12

	cleanup_32
	delete_external_journal $tmpdir/mds $tmpdir/mds.journal

	rm -rf $tmpdir || true	# true is only for TMP on NFS
}
run_test 32a "Upgrade from 1.8 (not live)"

test_32b() {
	client_only && skip "client only testing" && return 0
	[ "$NETTYPE" = "tcp" ] || { skip "NETTYPE != tcp" && return 0; }
	[ -z "$TUNEFS" ] && skip_env "No tunefs" && return

	local DISK1_8=$LUSTRE/tests/disk1_8.tar.bz2
	[ ! -r $DISK1_8 ] && skip_env "Cannot find $DISK1_8" && return 0
	local tmpdir=$TMP/conf32b
	mkdir -p $tmpdir
	tar xjvf $DISK1_8 -C $tmpdir || \
		{ skip_env "Cannot untar $DISK1_8" && return ; }

	# switch mds to external journal big enough for 1 ost
	local journal_size=$(mdd_max_txn_size_by_ost_nr 1)
	journal_size=$((journal_size * 4))
	set_external_journal $tmpdir/mds $tmpdir/mds.journal ${journal_size}

	load_modules
	$LCTL set_param debug="+config"
	local NEWNAME=lustre

	# writeconf will cause servers to register with their current nids
	$TUNEFS --writeconf --erase-params \
        --param mdt.identity_upcall=$L_GETIDENTITY \
        --fsname=$NEWNAME $tmpdir/mds || error "tunefs failed"
	combined_mgs_mds || stop mgs

	start32 mds1 $tmpdir/mds "-o loop" && \
		trap cleanup_32 EXIT INT || return 3

	local UUID=$($LCTL get_param -n mdt.${NEWNAME}-MDT0000.uuid)
	echo MDS uuid $UUID
	[ "$UUID" == "${NEWNAME}-MDT0000_UUID" ] || error "UUID is wrong: $UUID"

	$TUNEFS  --writeconf --erase-params \
        --mgsnode=$HOSTNAME --fsname=$NEWNAME $tmpdir/ost1 ||\
	    error "tunefs failed"
	start32 ost1 $tmpdir/ost1 "-o loop" || return 5
	UUID=$($LCTL get_param -n obdfilter.${NEWNAME}-OST0000.uuid)
	echo OST uuid $UUID
	[ "$UUID" == "${NEWNAME}-OST0000_UUID" ] || error "UUID is wrong: $UUID"

	local NID=$($LCTL list_nids | head -1)

	echo "OSC changes should succeed:"
	$LCTL conf_param ${NEWNAME}-OST0000.osc.max_dirty_mb=15 || return 7
	$LCTL conf_param ${NEWNAME}-OST0000.failover.node=$NID || return 8
	echo "ok."

	echo "MDC changes should succeed:"
	$LCTL conf_param ${NEWNAME}-MDT0000.mdc.max_rpcs_in_flight=9 || return 9
	$LCTL conf_param ${NEWNAME}-MDT0000.failover.node=$NID || return 10
	echo "ok."

	echo "LOV changes should succeed:"
	$LCTL pool_new ${NEWNAME}.interop || return 11
	$LCTL conf_param ${NEWNAME}-MDT0000.lov.stripesize=4M || return 12
	echo "ok."

	# MDT and OST should have registered with new nids, so we should have
	# a fully-functioning client
	echo "Check client and old fs contents"

	local device=`h2$NETTYPE $HOSTNAME`:/$NEWNAME
	echo "Starting local client: $HOSTNAME: $device $MOUNT"
	mount -t lustre $device $MOUNT || return 1

	local old=$($LCTL get_param -n mdc.*.max_rpcs_in_flight)
	local new=$((old + 5))
	$LCTL conf_param ${NEWNAME}-MDT0000.mdc.max_rpcs_in_flight=$new
	wait_update $HOSTNAME "$LCTL get_param -n mdc.*.max_rpcs_in_flight" $new || return 11

	[ "$(cksum $MOUNT/passwd | cut -d' ' -f 1,2)" == "94306271 1478" ] || return 12
	echo "ok."

	cleanup_32
	delete_external_journal $tmpdir/mds $tmpdir/mds.journal

	rm -rf $tmpdir || true  # true is only for TMP on NFS
}
run_test 32b "Upgrade from 1.8 with writeconf"

test_33a() { # bug 12333, was test_33
        local rc=0
        local FSNAME2=test-123
        local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

        [ -n "$ost1_HOST" ] && fs2ost_HOST=$ost1_HOST

        if [ -z "$fs2ost_DEV" -o -z "$fs2mds_DEV" ]; then
                local dev=${SINGLEMDS}_dev
                local MDSDEV=${!dev}
                is_blkdev $SINGLEMDS $MDSDEV && \
                skip_env "mixed loopback and real device not working" && return
        fi

        local fs2mdsdev=${fs2mds_DEV:-${MDSDEV}_2}
        local fs2ostdev=${fs2ost_DEV:-$(ostdevname 1)_2}

	add fs2mds $(mkfs_opts mds) --mkfsoptions='\"-J size=8\"' \
		--fsname=$FSNAME2 --reformat $fs2mdsdev ||
		error "add fs2mds $fs2mdsdev failed"
	add fs2ost $(mkfs_opts ost) --index=8191 \
		--fsname=$FSNAME2 --reformat $fs2ostdev ||
		error "add fs2ost $fs2ostdev failed"

        start fs2mds $fs2mdsdev $MDS_MOUNT_OPTS && trap cleanup_24a EXIT INT
        start fs2ost $fs2ostdev $OST_MOUNT_OPTS
        do_facet $SINGLEMDS "$LCTL conf_param $FSNAME2.sys.timeout=200" || rc=1
        mkdir -p $MOUNT2
        mount -t lustre $MGSNID:/${FSNAME2} $MOUNT2 || rc=2
        echo "ok."

        cp /etc/hosts $MOUNT2/ || rc=3
        $LFS getstripe $MOUNT2/hosts

        umount -d $MOUNT2
        stop fs2ost -f
        stop fs2mds -f
        rm -rf $MOUNT2 $fs2mdsdev $fs2ostdev
        cleanup_nocli || rc=6
        return $rc
}
run_test 33a "Mount ost with a large index number"

test_33b() {	# was test_34
        setup

        do_facet client dd if=/dev/zero of=$MOUNT/24 bs=1024k count=1
        # Drop lock cancelation reply during umount
	#define OBD_FAIL_LDLM_CANCEL             0x304
        do_facet client lctl set_param fail_loc=0x80000304
        #lctl set_param debug=-1
        umount_client $MOUNT
        cleanup
}
run_test 33b "Drop cancel during umount"

test_34a() {
        setup
	do_facet client "sh runmultiop_bg_pause $DIR/file O_c"
	manual_umount_client
	rc=$?
	do_facet client killall -USR1 multiop
	if [ $rc -eq 0 ]; then
		error "umount not fail!"
	fi
	sleep 1
        cleanup
}
run_test 34a "umount with opened file should be fail"


test_34b() {
	setup
	touch $DIR/$tfile || return 1
	stop_mds --force || return 2

	manual_umount_client --force
	rc=$?
	if [ $rc -ne 0 ]; then
		error "mtab after failed umount - rc $rc"
	fi

	cleanup
	return 0
}
run_test 34b "force umount with failed mds should be normal"

test_34c() {
	setup
	touch $DIR/$tfile || return 1
	stop_ost --force || return 2

	manual_umount_client --force
	rc=$?
	if [ $rc -ne 0 ]; then
		error "mtab after failed umount - rc $rc"
	fi

	cleanup
	return 0
}
run_test 34c "force umount with failed ost should be normal"

test_35a() { # bug 12459
	setup

	DBG_SAVE="`lctl get_param -n debug`"
	lctl set_param debug="ha"

	log "Set up a fake failnode for the MDS"
	FAKENID="127.0.0.2"
	local device=$(do_facet $SINGLEMDS "lctl get_param -n devices" |
		awk '($3 ~ "mdt" && $4 ~ "MDT") { print $4 }' | head -1)
	do_facet mgs "$LCTL conf_param \
		${device}.failover.node=$(h2$NETTYPE $FAKENID)" || return 4

	log "Wait for RECONNECT_INTERVAL seconds (10s)"
	sleep 10

	MSG="conf-sanity.sh test_35a `date +%F%kh%Mm%Ss`"
	$LCTL clear
	log "$MSG"
	log "Stopping the MDT:"
	stop_mds || return 5

	df $MOUNT > /dev/null 2>&1 &
	DFPID=$!
	log "Restarting the MDT:"
	start_mds || return 6
	log "Wait for df ($DFPID) ... "
	wait $DFPID
	log "done"
	lctl set_param debug="$DBG_SAVE"

	# retrieve from the log the first server that the client tried to
	# contact after the connection loss
	$LCTL dk $TMP/lustre-log-$TESTNAME.log
	NEXTCONN=`awk "/${MSG}/ {start = 1;}
		       /import_select_connection.*$device-mdc.* using connection/ {
				if (start) {
					if (\\\$NF ~ /$FAKENID/)
						print \\\$NF;
					else
						print 0;
					exit;
				}
		       }" $TMP/lustre-log-$TESTNAME.log`
	[ "$NEXTCONN" != "0" ] && log "The client didn't try to reconnect to the last active server (tried ${NEXTCONN} instead)" && return 7
	cleanup
	# remove nid settings
	writeconf
}
run_test 35a "Reconnect to the last active server first"

test_35b() { # bug 18674
	remote_mds || { skip "local MDS" && return 0; }
	setup

	debugsave
	$LCTL set_param debug="ha"
	$LCTL clear
	MSG="conf-sanity.sh test_35b `date +%F%kh%Mm%Ss`"
	log "$MSG"

	log "Set up a fake failnode for the MDS"
	FAKENID="127.0.0.2"
	local device=$(do_facet $SINGLEMDS "$LCTL get_param -n devices" |
		awk '($3 ~ "mdt" && $4 ~ "MDT") { print $4 }' | head -1)
	do_facet mgs "$LCTL conf_param \
		${device}.failover.node=$(h2$NETTYPE $FAKENID)" || return 1

	local at_max_saved=0
	# adaptive timeouts may prevent seeing the issue
	if at_is_enabled; then
		at_max_saved=$(at_max_get mds)
		at_max_set 0 mds client
	fi

	mkdir -p $MOUNT/$tdir

	log "Injecting EBUSY on MDS"
	# Setting OBD_FAIL_MDS_RESEND=0x136
	do_facet $SINGLEMDS "$LCTL set_param fail_loc=0x80000136" || return 2

	$LCTL set_param mdc.${FSNAME}*.stats=clear

	log "Creating a test file and stat it"
	touch $MOUNT/$tdir/$tfile
	stat $MOUNT/$tdir/$tfile

	log "Stop injecting EBUSY on MDS"
	do_facet $SINGLEMDS "$LCTL set_param fail_loc=0" || return 3
	rm -f $MOUNT/$tdir/$tfile

	log "done"
	# restore adaptive timeout
	[ $at_max_saved -ne 0 ] && at_max_set $at_max_saved mds client

	$LCTL dk $TMP/lustre-log-$TESTNAME.log

	CONNCNT=`$LCTL get_param mdc.${FSNAME}*.stats | awk '/mds_connect/{print $2}'`

	# retrieve from the log if the client has ever tried to
	# contact the fake server after the loss of connection
	FAILCONN=`awk "BEGIN {ret = 0;}
		       /import_select_connection.*${FSNAME}-MDT0000-mdc.* using connection/ {
				ret = 1;
				if (\\\$NF ~ /$FAKENID/) {
					ret = 2;
					exit;
				}
		       }
		       END {print ret}" $TMP/lustre-log-$TESTNAME.log`

	[ "$FAILCONN" == "0" ] && \
		log "ERROR: The client reconnection has not been triggered" && \
		return 4
	[ "$FAILCONN" == "2" ] && \
		log "ERROR: The client tried to reconnect to the failover server while the primary was busy" && \
		return 5

	# LU-290
	# When OBD_FAIL_MDS_RESEND is hit, we sleep for 2 * obd_timeout
	# Reconnects are supposed to be rate limited to one every 5s
	[ $CONNCNT -gt $((2 * $TIMEOUT / 5 + 1)) ] && \
		log "ERROR: Too many reconnects $CONNCNT" && \
		return 6

	cleanup
	# remove nid settings
	writeconf
}
run_test 35b "Continue reconnection retries, if the active server is busy"

test_36() { # 12743
        local rc=0
        local FSNAME2=test1234
        local fs2ost_HOST=$ost_HOST
        local fs3ost_HOST=$ost_HOST
        local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

        [ -n "$ost1_HOST" ] && fs2ost_HOST=$ost1_HOST && fs3ost_HOST=$ost1_HOST

        if [ -z "$fs2ost_DEV" -o -z "$fs2mds_DEV" -o -z "$fs3ost_DEV" ]; then
		is_blkdev $SINGLEMDS $MDSDEV && \
		skip_env "mixed loopback and real device not working" && return
        fi

        local fs2mdsdev=${fs2mds_DEV:-${MDSDEV}_2}
        local fs2ostdev=${fs2ost_DEV:-$(ostdevname 1)_2}
        local fs3ostdev=${fs3ost_DEV:-$(ostdevname 2)_2}

	add fs2mds $(mkfs_opts mds) --fsname=$FSNAME2 --reformat $fs2mdsdev ||
		error "add fs2mds $fs2mdsdev failed"
	# XXX after we support non 4K disk blocksize in ldiskfs, specify a
	#     different one than the default value here.
	add fs2ost $(mkfs_opts ost1) --fsname=$FSNAME2 --mkfsoptions='-b4096' \
		--reformat $fs2ostdev || error "add fs2ost $fs2ostdev failed"
	add fs3ost $(mkfs_opts ost2) --fsname=$FSNAME2 --mkfsoptions='-b4096' \
		--reformat $fs3ostdev || error "add fs3ost $fs3ostdev failed"
        start fs2mds $fs2mdsdev $MDS_MOUNT_OPTS
        start fs2ost $fs2ostdev $OST_MOUNT_OPTS
        start fs3ost $fs3ostdev $OST_MOUNT_OPTS
        mkdir -p $MOUNT2
        mount -t lustre $MGSNID:/${FSNAME2} $MOUNT2 || return 1

        sleep 5 # until 11778 fixed

        $LFS setstripe -c 2 $MOUNT2/$tfile
        dd if=/dev/zero of=$MOUNT2/$tfile bs=1M count=7 || return 2

        # both osts are on the same host, therefore run lctl on this host
        BKTOTAL=$(do_facet fs2ost lctl get_param -n obdfilter.*.kbytestotal | calc_sum)
        BKFREE=$(do_facet fs2ost lctl get_param -n obdfilter.*.kbytesfree | calc_sum)
        BKAVAIL=$(do_facet fs2ost lctl get_param -n obdfilter.*.kbytesavail | calc_sum)
        STRING=`df -P $MOUNT2 | tail -n 1 | awk '{print $2","$3","$4}'`
        DFTOTAL=`echo $STRING | cut -d, -f1`
        DFUSED=`echo $STRING  | cut -d, -f2`
        DFAVAIL=`echo $STRING | cut -d, -f3`
        DFFREE=$(($DFTOTAL - $DFUSED))

        ALLOWANCE=$((64 * 2))

        if [ $DFTOTAL -lt $(($BKTOTAL - $ALLOWANCE)) ] ||
           [ $DFTOTAL -gt $(($BKTOTAL + $ALLOWANCE)) ] ; then
                echo "**** FAIL: df total($DFTOTAL) mismatch OST total($BKTOTAL)"
                rc=1
        fi
        if [ $DFFREE -lt $(($BKFREE - $ALLOWANCE)) ] ||
           [ $DFFREE -gt $(($BKFREE + $ALLOWANCE)) ] ; then
                echo "**** FAIL: df free($DFFREE) mismatch OST free($BKFREE)"
                rc=2
        fi
        if [ $DFAVAIL -lt $(($BKAVAIL - $ALLOWANCE)) ] ||
           [ $DFAVAIL -gt $(($BKAVAIL + $ALLOWANCE)) ] ; then
                echo "**** FAIL: df avail($DFAVAIL) mismatch OST avail($BKAVAIL)"
                rc=3
       fi

        umount -d $MOUNT2
        stop fs3ost -f || return 200
        stop fs2ost -f || return 201
        stop fs2mds -f || return 202
        unload_modules_conf || return 203
        return $rc
}
run_test 36 "df report consistency on OSTs with different block size"

test_37() {
	local mntpt=$(facet_mntpt $SINGLEMDS)
	local mdsdev=$(mdsdevname ${SINGLEMDS//mds/})
	local mdsdev_sym="$TMP/sym_mdt.img"

	echo "MDS :     $mdsdev"
	echo "SYMLINK : $mdsdev_sym"
	do_facet $SINGLEMDS rm -f $mdsdev_sym

	do_facet $SINGLEMDS ln -s $mdsdev $mdsdev_sym

	echo "mount symlink device - $mdsdev_sym"

	local rc=0
	mount_op=$(do_facet $SINGLEMDS mount -v -t lustre $MDS_MOUNT_OPTS  $mdsdev_sym $mntpt 2>&1 )
	rc=${PIPESTATUS[0]}

	echo mount_op=$mount_op

	do_facet $SINGLEMDS "umount -d $mntpt && rm -f $mdsdev_sym"

	if $(echo $mount_op | grep -q "unable to set tunable"); then
		error "set tunables failed for symlink device"
	fi

	[ $rc -eq 0 ] || error "mount symlink $mdsdev_sym failed! rc=$rc"

	return 0
}
run_test 37 "verify set tunables works for symlink device"

test_38() { # bug 14222
	setup
	# like runtests
	COUNT=10
	SRC="/etc /bin"
	FILES=`find $SRC -type f -mtime +1 | head -n $COUNT`
	log "copying $(echo $FILES | wc -w) files to $DIR/$tdir"
	mkdir -p $DIR/$tdir
	tar cf - $FILES | tar xf - -C $DIR/$tdir || \
		error "copying $SRC to $DIR/$tdir"
	sync
	umount_client $MOUNT
	stop_mds
	log "rename lov_objid file on MDS"
	rm -f $TMP/lov_objid.orig

	local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})
	do_facet $SINGLEMDS "$DEBUGFS -c -R \\\"dump lov_objid $TMP/lov_objid.orig\\\" $MDSDEV"
	do_facet $SINGLEMDS "$DEBUGFS -w -R \\\"rm lov_objid\\\" $MDSDEV"

	do_facet $SINGLEMDS "od -Ax -td8 $TMP/lov_objid.orig"
	# check create in mds_lov_connect
	start_mds
	mount_client $MOUNT
	for f in $FILES; do
		[ $V ] && log "verifying $DIR/$tdir/$f"
		diff -q $f $DIR/$tdir/$f || ERROR=y
	done
	do_facet $SINGLEMDS "$DEBUGFS -c -R \\\"dump lov_objid $TMP/lov_objid.new\\\"  $MDSDEV"
	do_facet $SINGLEMDS "od -Ax -td8 $TMP/lov_objid.new"
	[ "$ERROR" = "y" ] && error "old and new files are different after connect" || true

	# check it's updates in sync
	umount_client $MOUNT
	stop_mds

	do_facet $SINGLEMDS dd if=/dev/zero of=$TMP/lov_objid.clear bs=4096 count=1
	do_facet $SINGLEMDS "$DEBUGFS -w -R \\\"rm lov_objid\\\" $MDSDEV"
	do_facet $SINGLEMDS "$DEBUGFS -w -R \\\"write $TMP/lov_objid.clear lov_objid\\\" $MDSDEV "

	start_mds
	mount_client $MOUNT
	for f in $FILES; do
		[ $V ] && log "verifying $DIR/$tdir/$f"
		diff -q $f $DIR/$tdir/$f || ERROR=y
	done
	do_facet $SINGLEMDS "$DEBUGFS -c -R \\\"dump lov_objid $TMP/lov_objid.new1\\\" $MDSDEV"
	do_facet $SINGLEMDS "od -Ax -td8 $TMP/lov_objid.new1"
	umount_client $MOUNT
	stop_mds
	[ "$ERROR" = "y" ] && error "old and new files are different after sync" || true

	log "files compared the same"
	cleanup
}
run_test 38 "MDS recreates missing lov_objid file from OST data"

test_39() {
        PTLDEBUG=+malloc
        setup
        cleanup
        perl $SRCDIR/leak_finder.pl $TMP/debug 2>&1 | egrep '*** Leak:' &&
                error "memory leak detected" || true
}
run_test 39 "leak_finder recognizes both LUSTRE and LNET malloc messages"

test_40() { # bug 15759
	start_ost
	#define OBD_FAIL_TGT_TOOMANY_THREADS     0x706
	do_facet $SINGLEMDS "$LCTL set_param fail_loc=0x80000706"
	start_mds
	cleanup
}
run_test 40 "race during service thread startup"

test_41a() { #bug 14134
        echo $MDS_MOUNT_OPTS | grep "loop" && skip " loop devices does not work with nosvc option" && return

        local rc
        local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

        start $SINGLEMDS $MDSDEV $MDS_MOUNT_OPTS -o nosvc -n
        start ost1 `ostdevname 1` $OST_MOUNT_OPTS
        start $SINGLEMDS $MDSDEV $MDS_MOUNT_OPTS -o nomgs,force
        mkdir -p $MOUNT
        mount_client $MOUNT || return 1
        sleep 5

        echo "blah blah" > $MOUNT/$tfile
        cat $MOUNT/$tfile

        umount_client $MOUNT
        stop ost1 -f || return 201
        stop_mds -f || return 202
        stop_mds -f || return 203
        unload_modules_conf || return 204
        return $rc
}
run_test 41a "mount mds with --nosvc and --nomgs"

test_41b() {
        echo $MDS_MOUNT_OPTS | grep "loop" && skip " loop devices does not work with nosvc option" && return

        ! combined_mgs_mds && skip "needs combined mgs device" && return 0

        stopall
        reformat
        local MDSDEV=$(mdsdevname ${SINGLEMDS//mds/})

        start $SINGLEMDS $MDSDEV $MDS_MOUNT_OPTS -o nosvc -n
        start_ost
        start $SINGLEMDS $MDSDEV $MDS_MOUNT_OPTS -o nomgs,force
        mkdir -p $MOUNT
        mount_client $MOUNT || return 1
        sleep 5

        echo "blah blah" > $MOUNT/$tfile
        cat $MOUNT/$tfile || return 200

        umount_client $MOUNT
        stop_ost || return 201
        stop_mds -f || return 202
        stop_mds -f || return 203

}
run_test 41b "mount mds with --nosvc and --nomgs on first mount"

test_42() { #bug 14693
	setup
	check_mount || error "client was not mounted"

	do_facet mgs $LCTL conf_param $FSNAME.llite.some_wrong_param=10
	umount_client $MOUNT ||
		error "unmounting client failed with invalid llite param"
	mount_client $MOUNT ||
		error "mounting client failed with invalid llite param"

	do_facet mgs $LCTL conf_param $FSNAME.sys.some_wrong_param=20
	cleanup || error "stopping $FSNAME failed with invalid sys param"
	load_modules
	setup
	check_mount || "client was not mounted with invalid sys param"
	cleanup || error "stopping $FSNAME failed with invalid sys param"
	return 0
}
run_test 42 "allow client/server mount/unmount with invalid config param"

test_43() {
	[ $UID -ne 0 -o $RUNAS_ID -eq 0 ] && skip_env "run as root"
	setup
	chmod ugo+x $DIR || error "chmod 0 failed"
	set_conf_param_and_check mds					\
		"lctl get_param -n mdt.$FSNAME-MDT0000.root_squash"	\
		"$FSNAME.mdt.root_squash"				\
		"0:0"
	set_conf_param_and_check mds					\
		"lctl get_param -n mdt.$FSNAME-MDT0000.nosquash_nids"	\
		"$FSNAME.mdt.nosquash_nids"				\
		"NONE"

    #
    # create set of test files
    #
    echo "111" > $DIR/$tfile-userfile || error "write 1 failed"
    chmod go-rw $DIR/$tfile-userfile  || error "chmod 1 failed"
    chown $RUNAS_ID.$RUNAS_ID $DIR/$tfile-userfile || error "chown failed"

    echo "222" > $DIR/$tfile-rootfile || error "write 2 failed"
    chmod go-rw $DIR/$tfile-rootfile  || error "chmod 2 faield"

    mkdir $DIR/$tdir-rootdir -p       || error "mkdir failed"
    chmod go-rwx $DIR/$tdir-rootdir   || error "chmod 3 failed"
    touch $DIR/$tdir-rootdir/tfile-1  || error "touch failed"

	#
	# check root_squash:
	#   set root squash UID:GID to RUNAS_ID
	#   root should be able to access only files owned by RUNAS_ID
	#
	set_conf_param_and_check mds					\
		"lctl get_param -n mdt.$FSNAME-MDT0000.root_squash"	\
		"$FSNAME.mdt.root_squash"				\
		"$RUNAS_ID:$RUNAS_ID"

    ST=$(stat -c "%n: owner uid %u (%A)" $DIR/$tfile-userfile)
    dd if=$DIR/$tfile-userfile 1>/dev/null 2>/dev/null || \
        error "$ST: root read permission is denied"
    echo "$ST: root read permission is granted - ok"

    echo "444" | \
    dd conv=notrunc if=$DIR/$tfile-userfile 1>/dev/null 2>/dev/null || \
        error "$ST: root write permission is denied"
    echo "$ST: root write permission is granted - ok"

    ST=$(stat -c "%n: owner uid %u (%A)" $DIR/$tfile-rootfile)
    dd if=$DIR/$tfile-rootfile 1>/dev/null 2>/dev/null && \
        error "$ST: root read permission is granted"
    echo "$ST: root read permission is denied - ok"

    echo "555" | \
    dd conv=notrunc of=$DIR/$tfile-rootfile 1>/dev/null 2>/dev/null && \
        error "$ST: root write permission is granted"
    echo "$ST: root write permission is denied - ok"

    ST=$(stat -c "%n: owner uid %u (%A)" $DIR/$tdir-rootdir)
    rm $DIR/$tdir-rootdir/tfile-1 1>/dev/null 2>/dev/null && \
        error "$ST: root unlink permission is granted"
    echo "$ST: root unlink permission is denied - ok"

    touch $DIR/tdir-rootdir/tfile-2 1>/dev/null 2>/dev/null && \
        error "$ST: root create permission is granted"
    echo "$ST: root create permission is denied - ok"

	#
	# check nosquash_nids:
	#   put client's NID into nosquash_nids list,
	#   root should be able to access root file after that
	#
	local NIDLIST=$(lctl list_nids all | tr '\n' ' ')
	NIDLIST="2@elan $NIDLIST 192.168.0.[2,10]@tcp"
	NIDLIST=$(echo $NIDLIST | tr -s ' ' ' ')
	set_conf_param_and_check mds					\
		"lctl get_param -n mdt.$FSNAME-MDT0000.nosquash_nids"	\
		"$FSNAME-MDTall.mdt.nosquash_nids"			\
		"$NIDLIST"

    ST=$(stat -c "%n: owner uid %u (%A)" $DIR/$tfile-rootfile)
    dd if=$DIR/$tfile-rootfile 1>/dev/null 2>/dev/null || \
        error "$ST: root read permission is denied"
    echo "$ST: root read permission is granted - ok"

    echo "666" | \
    dd conv=notrunc of=$DIR/$tfile-rootfile 1>/dev/null 2>/dev/null || \
        error "$ST: root write permission is denied"
    echo "$ST: root write permission is granted - ok"

    ST=$(stat -c "%n: owner uid %u (%A)" $DIR/$tdir-rootdir)
    rm $DIR/$tdir-rootdir/tfile-1 || \
        error "$ST: root unlink permission is denied"
    echo "$ST: root unlink permission is granted - ok"
    touch $DIR/$tdir-rootdir/tfile-2 || \
        error "$ST: root create permission is denied"
    echo "$ST: root create permission is granted - ok"

    return 0
}
run_test 43 "check root_squash and nosquash_nids"

umount_client $MOUNT
cleanup_nocli

test_44() { # 16317
        setup
        check_mount || return 2
        UUID=$($LCTL get_param llite.${FSNAME}*.uuid | cut -d= -f2)
        STATS_FOUND=no
        UUIDS=$(do_facet $SINGLEMDS "$LCTL get_param mdt.${FSNAME}*.exports.*.uuid")
        for VAL in $UUIDS; do
                NID=$(echo $VAL | cut -d= -f1)
                CLUUID=$(echo $VAL | cut -d= -f2)
                [ "$UUID" = "$CLUUID" ] && STATS_FOUND=yes && break
        done
        [ "$STATS_FOUND" = "no" ] && error "stats not found for client"
        cleanup
        return 0
}
run_test 44 "mounted client proc entry exists"

test_45() { #17310
        setup
        check_mount || return 2
        stop_mds
        df -h $MOUNT &
        log "sleep 60 sec"
        sleep 60
#define OBD_FAIL_PTLRPC_LONG_UNLINK   0x50f
        do_facet client "lctl set_param fail_loc=0x50f"
        log "sleep 10 sec"
        sleep 10
        manual_umount_client --force || return 3
        do_facet client "lctl set_param fail_loc=0x0"
        start_mds
        mount_client $MOUNT || return 4
        cleanup
        return 0
}
run_test 45 "long unlink handling in ptlrpcd"

cleanup_46a() {
	trap 0
	local rc=0
	local count=$1

	umount_client $MOUNT2 || rc=$?
	umount_client $MOUNT || rc=$?
	while [ $count -gt 0 ]; do
		stop ost${count} -f || rc=$?
		let count=count-1
	done	
	stop_mds || rc=$?
	cleanup_nocli || rc=$?
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
	return $rc
}

test_46a() {
	echo "Testing with $OSTCOUNT OSTs"
	reformat_and_config
	start_mds || return 1
	#first client should see only one ost
	start_ost || return 2
        wait_osc_import_state mds ost FULL
	#start_client
	mount_client $MOUNT || return 3
	trap "cleanup_46a $OSTCOUNT" EXIT ERR

	local i
	for (( i=2; i<=$OSTCOUNT; i++ )); do
	    start ost$i `ostdevname $i` $OST_MOUNT_OPTS || return $((i+2))
	done

	# wait until osts in sync
	for (( i=2; i<=$OSTCOUNT; i++ )); do
	    wait_osc_import_state mds ost$i FULL
	    wait_osc_import_state client ost$i FULL
	done

	#second client see all ost's

	mount_client $MOUNT2 || return 8
	$LFS setstripe -c -1 $MOUNT2 || return 9
	$LFS getstripe $MOUNT2 || return 10

	echo "ok" > $MOUNT2/widestripe
	$LFS getstripe $MOUNT2/widestripe || return 11
	# fill acl buffer for avoid expand lsm to them
	awk -F : '{if (FNR < 25) { print "u:"$1":rwx" }}' /etc/passwd | while read acl; do
	    setfacl -m $acl $MOUNT2/widestripe
	done

	# will be deadlock
	stat $MOUNT/widestripe || return 12

	cleanup_46a $OSTCOUNT || { echo "cleanup_46a failed!" && return 13; }
	return 0
}
run_test 46a "handle ost additional - wide striped file"

test_47() { #17674
	reformat
	setup_noconfig
        check_mount || return 2
        $LCTL set_param ldlm.namespaces.$FSNAME-*-*-*.lru_size=100

        local lru_size=[]
        local count=0
        for ns in $($LCTL get_param ldlm.namespaces.$FSNAME-*-*-*.lru_size); do
            if echo $ns | grep "MDT[[:digit:]]*"; then
                continue
            fi
            lrs=$(echo $ns | sed 's/.*lru_size=//')
            lru_size[count]=$lrs
            let count=count+1
        done

        facet_failover ost1
        facet_failover $SINGLEMDS
        client_up || return 3

        count=0
        for ns in $($LCTL get_param ldlm.namespaces.$FSNAME-*-*-*.lru_size); do
            if echo $ns | grep "MDT[[:digit:]]*"; then
                continue
            fi
            lrs=$(echo $ns | sed 's/.*lru_size=//')
            if ! test "$lrs" -eq "${lru_size[count]}"; then
                n=$(echo $ns | sed -e 's/ldlm.namespaces.//' -e 's/.lru_size=.*//')
                error "$n has lost lru_size: $lrs vs. ${lru_size[count]}"
            fi
            let count=count+1
        done

        cleanup
        return 0
}
run_test 47 "server restart does not make client loss lru_resize settings"

cleanup_48() {
	trap 0

	# reformat after this test is needed - if test will failed
	# we will have unkillable file at FS
	reformat_and_config
}

test_48() { # bug 17636
	reformat
	setup_noconfig
	check_mount || return 2

	$LFS setstripe -c -1 $MOUNT || return 9
	$LFS getstripe $MOUNT || return 10

	echo "ok" > $MOUNT/widestripe
	$LFS getstripe $MOUNT/widestripe || return 11

	trap cleanup_48 EXIT ERR

	# fill acl buffer for avoid expand lsm to them
	getent passwd | awk -F : '{ print "u:"$1":rwx" }' |  while read acl; do
	    setfacl -m $acl $MOUNT/widestripe
	done

	stat $MOUNT/widestripe || return 12

	cleanup_48
	return 0
}
run_test 48 "too many acls on file"

# check PARAM_SYS_LDLM_TIMEOUT option of MKFS.LUSTRE
test_49() { # bug 17710
	local OLD_MDS_MKFS_OPTS=$MDS_MKFS_OPTS
	local OLD_OST_MKFS_OPTS=$OST_MKFS_OPTS
	local LOCAL_TIMEOUT=20


	OST_MKFS_OPTS="--ost --fsname=$FSNAME --device-size=$OSTSIZE --mgsnode=$MGSNID --param sys.timeout=$LOCAL_TIMEOUT --param sys.ldlm_timeout=$LOCAL_TIMEOUT $MKFSOPT $OSTOPT"

	reformat
	setup_noconfig
	check_mount || return 1

	echo "check ldlm_timout..."
	LDLM_MDS="`do_facet $SINGLEMDS lctl get_param -n ldlm_timeout`"
	LDLM_OST1="`do_facet ost1 lctl get_param -n ldlm_timeout`"
	LDLM_CLIENT="`do_facet client lctl get_param -n ldlm_timeout`"

	if [ $LDLM_MDS -ne $LDLM_OST1 ] || [ $LDLM_MDS -ne $LDLM_CLIENT ]; then
		error "Different LDLM_TIMEOUT:$LDLM_MDS $LDLM_OST1 $LDLM_CLIENT"
	fi

	if [ $LDLM_MDS -ne $((LOCAL_TIMEOUT / 3)) ]; then
		error "LDLM_TIMEOUT($LDLM_MDS) is not correct"
	fi

	umount_client $MOUNT
	stop_ost || return 2
	stop_mds || return 3

	OST_MKFS_OPTS="--ost --fsname=$FSNAME --device-size=$OSTSIZE --mgsnode=$MGSNID --param sys.timeout=$LOCAL_TIMEOUT --param sys.ldlm_timeout=$((LOCAL_TIMEOUT - 1)) $MKFSOPT $OSTOPT"

	reformat
	setup_noconfig
	check_mount || return 7

	LDLM_MDS="`do_facet $SINGLEMDS lctl get_param -n ldlm_timeout`"
	LDLM_OST1="`do_facet ost1 lctl get_param -n ldlm_timeout`"
	LDLM_CLIENT="`do_facet client lctl get_param -n ldlm_timeout`"

	if [ $LDLM_MDS -ne $LDLM_OST1 ] || [ $LDLM_MDS -ne $LDLM_CLIENT ]; then
		error "Different LDLM_TIMEOUT:$LDLM_MDS $LDLM_OST1 $LDLM_CLIENT"
	fi

	if [ $LDLM_MDS -ne $((LOCAL_TIMEOUT - 1)) ]; then
		error "LDLM_TIMEOUT($LDLM_MDS) is not correct"
	fi

	cleanup || return $?

	MDS_MKFS_OPTS=$OLD_MDS_MKFS_OPTS
	OST_MKFS_OPTS=$OLD_OST_MKFS_OPTS
}
run_test 49 "check PARAM_SYS_LDLM_TIMEOUT option of MKFS.LUSTRE"

lazystatfs() {
        # Test both statfs and lfs df and fail if either one fails
	multiop_bg_pause $1 f_
	RC1=$?
	PID=$!
	killall -USR1 multiop
	[ $RC1 -ne 0 ] && log "lazystatfs multiop failed"
	wait $PID || { RC1=$?; log "multiop return error "; }

	$LFS df &
	PID=$!
	sleep 5
	kill -s 0 $PID
	RC2=$?
	if [ $RC2 -eq 0 ]; then
	    kill -s 9 $PID
	    log "lazystatfs df failed"
	fi

	RC=0
	[[ $RC1 -ne 0 || $RC2 -eq 0 ]] && RC=1
	return $RC
}

test_50a() {
	setup
	lctl set_param llite.$FSNAME-*.lazystatfs=1
	touch $DIR/$tfile

	lazystatfs $MOUNT || error "lazystatfs failed but no down servers"

	cleanup || return $?
}
run_test 50a "lazystatfs all servers available =========================="

test_50b() {
	setup
	lctl set_param llite.$FSNAME-*.lazystatfs=1
	touch $DIR/$tfile

	# Wait for client to detect down OST
	stop_ost || error "Unable to stop OST1"
        wait_osc_import_state mds ost DISCONN

	lazystatfs $MOUNT || error "lazystatfs should don't have returned EIO"

	umount_client $MOUNT || error "Unable to unmount client"
	stop_mds || error "Unable to stop MDS"
}
run_test 50b "lazystatfs all servers down =========================="

test_50c() {
	start_mds || error "Unable to start MDS"
	start_ost || error "Unable to start OST1"
	start_ost2 || error "Unable to start OST2"
	mount_client $MOUNT || error "Unable to mount client"
	lctl set_param llite.$FSNAME-*.lazystatfs=1
	touch $DIR/$tfile

	# Wait for client to detect down OST
	stop_ost || error "Unable to stop OST1"
        wait_osc_import_state mds ost DISCONN
	lazystatfs $MOUNT || error "lazystatfs failed with one down server"

	umount_client $MOUNT || error "Unable to unmount client"
	stop_ost2 || error "Unable to stop OST2"
	stop_mds || error "Unable to stop MDS"
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 50c "lazystatfs one server down =========================="

test_50d() {
	start_mds || error "Unable to start MDS"
	start_ost || error "Unable to start OST1"
	start_ost2 || error "Unable to start OST2"
	mount_client $MOUNT || error "Unable to mount client"
	lctl set_param llite.$FSNAME-*.lazystatfs=1
	touch $DIR/$tfile

	# Issue the statfs during the window where the client still
	# belives the OST to be available but it is in fact down.
	# No failure just a statfs which hangs for a timeout interval.
	stop_ost || error "Unable to stop OST1"
	lazystatfs $MOUNT || error "lazystatfs failed with one down server"

	umount_client $MOUNT || error "Unable to unmount client"
	stop_ost2 || error "Unable to stop OST2"
	stop_mds || error "Unable to stop MDS"
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 50d "lazystatfs client/server conn race =========================="

test_50e() {
	local RC1
	local pid

	reformat_and_config
	start_mds || return 1
	#first client should see only one ost
	start_ost || return 2
        wait_osc_import_state mds ost FULL

	# Wait for client to detect down OST
	stop_ost || error "Unable to stop OST1"
        wait_osc_import_state mds ost DISCONN

	mount_client $MOUNT || error "Unable to mount client"
        lctl set_param llite.$FSNAME-*.lazystatfs=0

	multiop_bg_pause $MOUNT _f
	RC1=$?
	pid=$!

	if [ $RC1 -ne 0 ]; then
		log "multiop failed $RC1"
	else
	    kill -USR1 $pid
	    sleep $(( $TIMEOUT+1 ))
	    kill -0 $pid
	    [ $? -ne 0 ] && error "process isn't sleep"
	    start_ost || error "Unable to start OST1"
	    wait $pid || error "statfs failed"
	fi

	umount_client $MOUNT || error "Unable to unmount client"
	stop_ost || error "Unable to stop OST1"
	stop_mds || error "Unable to stop MDS"
}
run_test 50e "normal statfs all servers down =========================="

test_50f() {
	local RC1
	local pid
	CONN_PROC="osc.$FSNAME-OST0001-osc-[M]*.ost_server_uuid"

	start_mds || error "Unable to start mds"
	#first client should see only one ost
	start_ost || error "Unable to start OST1"
        wait_osc_import_state mds ost FULL

        start_ost2 || error "Unable to start OST2"
        wait_osc_import_state mds ost2 FULL

	# Wait for client to detect down OST
	stop_ost2 || error "Unable to stop OST2"

	wait_osc_import_state mds ost2 DISCONN
	mount_client $MOUNT || error "Unable to mount client"
        lctl set_param llite.$FSNAME-*.lazystatfs=0

	multiop_bg_pause $MOUNT _f
	RC1=$?
	pid=$!

	if [ $RC1 -ne 0 ]; then
		log "lazystatfs multiop failed $RC1"
	else
	    kill -USR1 $pid
	    sleep $(( $TIMEOUT+1 ))
	    kill -0 $pid
	    [ $? -ne 0 ] && error "process isn't sleep"
	    start_ost2 || error "Unable to start OST2"
	    wait $pid || error "statfs failed"
	    stop_ost2 || error "Unable to stop OST2"
	fi

	umount_client $MOUNT || error "Unable to unmount client"
	stop_ost || error "Unable to stop OST1"
	stop_mds || error "Unable to stop MDS"
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 50f "normal statfs one server in down =========================="

test_50g() {
	[ "$OSTCOUNT" -lt "2" ] && skip_env "$OSTCOUNT < 2, skipping" && return
	setup
	start_ost2 || error "Unable to start OST2"
        wait_osc_import_state mds ost2 FULL
        wait_osc_import_state client ost2 FULL

	local PARAM="${FSNAME}-OST0001.osc.active"

	$LFS setstripe -c -1 $DIR/$tfile || error "Unable to lfs setstripe"
	do_facet mgs $LCTL conf_param $PARAM=0 || error "Unable to deactivate OST"

	umount_client $MOUNT || error "Unable to unmount client"
	mount_client $MOUNT || error "Unable to mount client"
	# This df should not cause a panic
	df -k $MOUNT

	do_facet mgs $LCTL conf_param $PARAM=1 || error "Unable to activate OST"
	rm -f $DIR/$tfile
	umount_client $MOUNT || error "Unable to unmount client"
	stop_ost2 || error "Unable to stop OST2"
	stop_ost || error "Unable to stop OST1"
	stop_mds || error "Unable to stop MDS"
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 50g "deactivated OST should not cause panic====================="

# LU-642
test_50h() {
	# prepare MDT/OST, make OSC inactive for OST1
	[ "$OSTCOUNT" -lt "2" ] && skip_env "$OSTCOUNT < 2, skipping" && return
	do_facet ost1 "$TUNEFS --param osc.active=0 `ostdevname 1`" ||
		error "tunefs OST1 failed"
	start_mds  || error "Unable to start MDT"
	start_ost  || error "Unable to start OST1"
	start_ost2 || error "Unable to start OST2"
	mount_client $MOUNT || error "client start failed"

	mkdir -p $DIR/$tdir

	# activatate OSC for OST1
	local TEST="$LCTL get_param -n osc.${FSNAME}-OST0000-osc-[!M]*.active"
	set_conf_param_and_check client					\
		"$TEST" "${FSNAME}-OST0000.osc.active" 1 ||
		error "Unable to activate OST1"

	mkdir -p $DIR/$tdir/2
	$LFS setstripe -c -1 -i 0 $DIR/$tdir/2
	sleep 1 && echo "create a file after OST1 is activated"
	# create some file
	createmany -o $DIR/$tdir/2/$tfile-%d 1

	# check OSC import is working
	stat $DIR/$tdir/2/* >/dev/null 2>&1 ||
		error "some OSC imports are still not connected"

	# cleanup
	umount_client $MOUNT || error "Unable to umount client"
	stop_ost2 || error "Unable to stop OST2"
	cleanup_nocli
}
run_test 50h "LU-642: activate deactivated OST  ==="

test_51() {
	local LOCAL_TIMEOUT=20

	reformat
	setup_noconfig
	check_mount || return 1

	mkdir $MOUNT/d1
	$LFS setstripe -c -1 $MOUNT/d1
        #define OBD_FAIL_MDS_REINT_DELAY         0x142
	do_facet $SINGLEMDS "lctl set_param fail_loc=0x142"
	touch $MOUNT/d1/f1 &
	local pid=$!
	sleep 2
	start_ost2 || return 2
	wait $pid
	stop_ost2 || return 3
	cleanup
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 51 "Verify that mdt_reint handles RMF_MDT_MD correctly when an OST is added"

copy_files_xattrs()
{
	local node=$1
	local dest=$2
	local xattrs=$3
	shift 3

	do_node $node mkdir -p $dest
	[ $? -eq 0 ] || { error "Unable to create directory"; return 1; }

	do_node $node  'tar cf - '$@' | tar xf - -C '$dest';
			[ \"\${PIPESTATUS[*]}\" = \"0 0\" ] || exit 1'
	[ $? -eq 0 ] || { error "Unable to tar files"; return 2; }

	do_node $node 'getfattr -d -m "[a-z]*\\." '$@' > '$xattrs
	[ $? -eq 0 ] || { error "Unable to read xattrs"; return 3; }
}

diff_files_xattrs()
{
	local node=$1
	local backup=$2
	local xattrs=$3
	shift 3

	local backup2=${TMP}/backup2

	do_node $node mkdir -p $backup2
	[ $? -eq 0 ] || { error "Unable to create directory"; return 1; }

	do_node $node  'tar cf - '$@' | tar xf - -C '$backup2';
			[ \"\${PIPESTATUS[*]}\" = \"0 0\" ] || exit 1'
	[ $? -eq 0 ] || { error "Unable to tar files to diff"; return 2; }

	do_node $node "diff -rq $backup $backup2"
	[ $? -eq 0 ] || { error "contents differ"; return 3; }

	local xattrs2=${TMP}/xattrs2
	do_node $node 'getfattr -d -m "[a-z]*\\." '$@' > '$xattrs2
	[ $? -eq 0 ] || { error "Unable to read xattrs to diff"; return 4; }

	do_node $node "diff $xattrs $xattrs2"
	[ $? -eq 0 ] || { error "xattrs differ"; return 5; }

	do_node $node "rm -rf $backup2 $xattrs2"
	[ $? -eq 0 ] || { error "Unable to delete temporary files"; return 6; }
}

test_52() {
	start_mds
	[ $? -eq 0 ] || { error "Unable to start MDS"; return 1; }
	start_ost
	[ $? -eq 0 ] || { error "Unable to start OST1"; return 2; }
	mount_client $MOUNT
	[ $? -eq 0 ] || { error "Unable to mount client"; return 3; }

	local nrfiles=8
	local ost1mnt=$(facet_mntpt ost1)
	local ost1node=$(facet_active_host ost1)
	local ost1tmp=$TMP/conf52

	mkdir -p $DIR/$tdir
	[ $? -eq 0 ] || { error "Unable to create tdir"; return 4; }
	touch $TMP/modified_first
	[ $? -eq 0 ] || { error "Unable to create temporary file"; return 5; }
	local mtime=$(stat -c %Y $TMP/modified_first)
	do_node $ost1node "mkdir -p $ost1tmp && touch -m -d @$mtime $ost1tmp/modified_first"

	[ $? -eq 0 ] || { error "Unable to create temporary file"; return 6; }
	sleep 1

	$LFS setstripe -c -1 -S 1M $DIR/$tdir
	[ $? -eq 0 ] || { error "lfs setstripe failed"; return 7; }

	for (( i=0; i < nrfiles; i++ )); do
		multiop $DIR/$tdir/$tfile-$i Ow1048576w1048576w524288c
		[ $? -eq 0 ] || { error "multiop failed"; return 8; }
		echo -n .
	done
	echo

	# backup files
	echo backup files to $TMP/files
	local files=$(find $DIR/$tdir -type f -newer $TMP/modified_first)
	copy_files_xattrs `hostname` $TMP/files $TMP/file_xattrs $files
	[ $? -eq 0 ] || { error "Unable to copy files"; return 9; }

	umount_client $MOUNT
	[ $? -eq 0 ] || { error "Unable to umount client"; return 10; }
	stop_ost
	[ $? -eq 0 ] || { error "Unable to stop ost1"; return 11; }

	echo mount ost1 as ldiskfs
	do_node $ost1node mkdir -p $ost1mnt
	[ $? -eq 0 ] || { error "Unable to create $ost1mnt"; return 23; }
	do_node $ost1node mount -t $FSTYPE $ost1_dev $ost1mnt $OST_MOUNT_OPTS
	[ $? -eq 0 ] || { error "Unable to mount ost1 as ldiskfs"; return 12; }

	# backup objects
	echo backup objects to $ost1tmp/objects
	local objects=$(do_node $ost1node 'find '$ost1mnt'/O/0 -type f -size +0'\
			'-newer '$ost1tmp'/modified_first -regex ".*\/[0-9]+"')
	copy_files_xattrs $ost1node $ost1tmp/objects $ost1tmp/object_xattrs $objects
	[ $? -eq 0 ] || { error "Unable to copy objects"; return 13; }

	# move objects to lost+found
	do_node $ost1node 'mv '$objects' '${ost1mnt}'/lost+found'
	[ $? -eq 0 ] || { error "Unable to move objects"; return 14; }

	# recover objects dry-run
	echo "ll_recover_lost_found_objs dry_run"
	do_node $ost1node "ll_recover_lost_found_objs -n -d $ost1mnt/O"
	[ $? -eq 0 ] || { error "ll_recover_lost_found_objs failed"; return 15; }

	# recover objects
	echo "ll_recover_lost_found_objs fix run"
	do_node $ost1node "ll_recover_lost_found_objs -d $ost1mnt/lost+found"
	[ $? -eq 0 ] || { error "ll_recover_lost_found_objs failed"; return 15; }

	# compare restored objects against saved ones
	diff_files_xattrs $ost1node $ost1tmp/objects $ost1tmp/object_xattrs $objects
	[ $? -eq 0 ] || { error "Unable to diff objects"; return 16; }

	do_node $ost1node "umount $ost1mnt"
	[ $? -eq 0 ] || { error "Unable to umount ost1 as ldiskfs"; return 17; }

	start_ost
	[ $? -eq 0 ] || { error "Unable to start ost1"; return 18; }
	mount_client $MOUNT
	[ $? -eq 0 ] || { error "Unable to mount client"; return 19; }

	# compare files
	diff_files_xattrs `hostname` $TMP/files $TMP/file_xattrs $files
	[ $? -eq 0 ] || { error "Unable to diff files"; return 20; }

	rm -rf $TMP/files $TMP/file_xattrs
	[ $? -eq 0 ] || { error "Unable to delete temporary files"; return 21; }
	do_node $ost1node "rm -rf $ost1tmp"
	[ $? -eq 0 ] || { error "Unable to delete temporary files"; return 22; }
	cleanup
}
run_test 52 "check recovering objects from lost+found"

# Checks threads_min/max/started for some service
#
# Arguments: service name (OST or MDT), facet (e.g., ost1, $SINGLEMDS), and a
# parameter pattern prefix like 'ost.*.ost'.
thread_sanity() {
        local modname=$1
        local facet=$2
        local parampat=$3
        local opts=$4
        local tmin
        local tmin2
        local tmax
        local tmax2
        local tstarted
        local paramp
        local msg="Insane $modname thread counts"
        shift 4

        check_mount || return 41

        # We need to expand $parampat, but it may match multiple parameters, so
        # we'll pick the first one
        if ! paramp=$(do_facet $facet "lctl get_param -N ${parampat}.threads_min"|head -1); then
                error "Couldn't expand ${parampat}.threads_min parameter name"
                return 22
        fi

        # Remove the .threads_min part
        paramp=${paramp%.threads_min}

        # Check for sanity in defaults
        tmin=$(do_facet $facet "lctl get_param -n ${paramp}.threads_min" || echo 0)
        tmax=$(do_facet $facet "lctl get_param -n ${paramp}.threads_max" || echo 0)
        tstarted=$(do_facet $facet "lctl get_param -n ${paramp}.threads_started" || echo 0)
        lassert 23 "$msg (PDSH problems?)" '(($tstarted && $tmin && $tmax))' || return $?
        lassert 24 "$msg" '(($tstarted >= $tmin && $tstarted <= $tmax ))' || return $?

        # Check that we can change min/max
        do_facet $facet "lctl set_param ${paramp}.threads_min=$((tmin + 1))"
        do_facet $facet "lctl set_param ${paramp}.threads_max=$((tmax - 1))"
        tmin2=$(do_facet $facet "lctl get_param -n ${paramp}.threads_min" || echo 0)
        tmax2=$(do_facet $facet "lctl get_param -n ${paramp}.threads_max" || echo 0)
        lassert 25 "$msg" '(($tmin2 == ($tmin + 1) && $tmax2 == ($tmax -1)))' || return $?

        # Check that we can set min/max to the same value
        tmin=$(do_facet $facet "lctl get_param -n ${paramp}.threads_min" || echo 0)
        do_facet $facet "lctl set_param ${paramp}.threads_max=$tmin"
        tmin2=$(do_facet $facet "lctl get_param -n ${paramp}.threads_min" || echo 0)
        tmax2=$(do_facet $facet "lctl get_param -n ${paramp}.threads_max" || echo 0)
        lassert 26 "$msg" '(($tmin2 == $tmin && $tmax2 == $tmin))' || return $?

        # Check that we can't set max < min
        do_facet $facet "lctl set_param ${paramp}.threads_max=$((tmin - 1))"
        tmin2=$(do_facet $facet "lctl get_param -n ${paramp}.threads_min" || echo 0)
        tmax2=$(do_facet $facet "lctl get_param -n ${paramp}.threads_max" || echo 0)
        lassert 27 "$msg" '(($tmin2 <= $tmax2))' || return $?

        # We need to ensure that we get the module options desired; to do this
        # we set LOAD_MODULES_REMOTE=true and we call setmodopts below.
        LOAD_MODULES_REMOTE=true
        cleanup
        local oldvalue
        setmodopts -a $modname "$opts" oldvalue

        load_modules
        setup
        check_mount || return 41

        # Restore previous setting of MODOPTS_*
        setmodopts $modname "$oldvalue"

        # Check that $opts took
        tmin=$(do_facet $facet "lctl get_param -n ${paramp}.threads_min")
        tmax=$(do_facet $facet "lctl get_param -n ${paramp}.threads_max")
        tstarted=$(do_facet $facet "lctl get_param -n ${paramp}.threads_started")
        lassert 28 "$msg" '(($tstarted == $tmin && $tstarted == $tmax ))' || return $?
        cleanup

        # Workaround a YALA bug where YALA expects that modules will remain
        # loaded on the servers
        LOAD_MODULES_REMOTE=false
        load_modules
        setup
}

test_53a() {
	setup
	thread_sanity OST ost1 'ost.*.ost' 'oss_num_threads=64'
	cleanup
}
run_test 53a "check OSS thread count params"

test_53b() {
	setup
	local mds=$(do_facet $SINGLEMDS "lctl get_param -N mds.*.*.threads_max \
		    2>/dev/null")
	if [ -z "$mds" ]; then
		thread_sanity MDT $SINGLEMDS 'mdt.*.*.' 'mdt_num_threads=64'
	else
		thread_sanity MDT $SINGLEMDS 'mds.*.*.' 'mds_num_threads=64'
	fi
	cleanup
}
run_test 53b "check MDT thread count params"

test_54a() {
    do_rpc_nodes $(facet_host ost1) run_llverdev $(ostdevname 1) -p
    [ $? -eq 0 ] || error "llverdev failed!"
    reformat_and_config
}
run_test 54a "test llverdev and partial verify of device"

test_54b() {
    setup
    run_llverfs $MOUNT -p
    [ $? -eq 0 ] || error "llverfs failed!"
    cleanup
}
run_test 54b "test llverfs and partial verify of filesystem"

lov_objid_size()
{
	local max_ost_index=$1
	echo -n $(((max_ost_index + 1) * 8))
}

test_55() {
	local mdsdev=$(mdsdevname 1)
	local ostdev=$(ostdevname 1)
	local saved_opts=$OST_MKFS_OPTS

	for i in 1023 2048
	do
		OST_MKFS_OPTS="$saved_opts --index $i"
		reformat

		setup_noconfig
		stopall

		setup_noconfig
		sync
		echo checking size of lov_objid for ost index $i
		LOV_OBJID_SIZE=$(do_facet mds1 "$DEBUGFS -R 'stat lov_objid' $mdsdev 2>/dev/null" | grep ^User | awk '{print $6}')
		if [ "$LOV_OBJID_SIZE" != $(lov_objid_size $i) ]; then
			error "lov_objid size has to be $(lov_objid_size $i), not $LOV_OBJID_SIZE"
		else
			echo ok, lov_objid size is correct: $LOV_OBJID_SIZE
		fi
		stopall
	done

	OST_MKFS_OPTS=$saved_opts
	reformat
}
run_test 55 "check lov_objid size"

test_56() {
	local mdsdev=$(mdsdevname ${SINGLEMDS//mds/})
	local ostdev1=$(ostdevname 1)
	local ostdev2=$(ostdevname 2)

	add $SINGLEMDS $(mkfs_opts $SINGLEMDS) --mkfsoptions='\"-J size=16\"' \
		--reformat $mdsdev || error "add $SINGLEMDS $mdsdev failed"
	add ost1 $(mkfs_opts ost1) --index=1000 --reformat $ostdev1 ||
		error "add ost1 $ostdev1 failed"
	add ost2 $(mkfs_opts ost2) --index=10000 --reformat $ostdev2 ||
		error "add ost2 $ostdev2 failed"

	start_mgsmds
	start_ost
	start_ost2 || error "Unable to start second ost"
	mount_client $MOUNT || error "Unable to mount client"
	echo ok
	$LFS osts
	[ -n "$ENABLE_QUOTA" ] && { $LFS quotacheck -ug $MOUNT || error "quotacheck has failed" ; }
	stopall
	reformat
}
run_test 56 "check big indexes"

test_57a() { # bug 22656
	local NID=$(do_facet ost1 "$LCTL get_param nis" | tail -1 | awk '{print $1}')
	writeconf
	do_facet ost1 "$TUNEFS --failnode=$NID `ostdevname 1`" || error "tunefs failed"
	start_mgsmds
	start_ost && error "OST registration from failnode should fail"
	reformat
}
run_test 57a "initial registration from failnode should fail (should return errs)"

test_57b() {
	local NID=$(do_facet ost1 "$LCTL get_param nis" | tail -1 | awk '{print $1}')
	writeconf
	do_facet ost1 "$TUNEFS --servicenode=$NID `ostdevname 1`" || error "tunefs failed"
	start_mgsmds
	start_ost || error "OST registration from servicenode should not fail"
	reformat
}
run_test 57b "initial registration from servicenode should not fail"

count_osts() {
        do_facet mgs $LCTL get_param mgs.MGS.live.$FSNAME | grep OST | wc -l
}

test_58() { # bug 22658
	[ "$FSTYPE" != "ldiskfs" ] && skip "not supported for $FSTYPE" && return
	setup_noconfig
	mkdir -p $DIR/$tdir
	createmany -o $DIR/$tdir/$tfile-%d 100
	# make sure that OSTs do not cancel llog cookies before we unmount the MDS
#define OBD_FAIL_OBD_LOG_CANCEL_NET      0x601
	do_facet mds "lctl set_param fail_loc=0x601"
	unlinkmany $DIR/$tdir/$tfile-%d 100
	stop mds
	local MNTDIR=$(facet_mntpt mds)
	# remove all files from the OBJECTS dir
	do_facet mds "mount -t ldiskfs $MDSDEV $MNTDIR"
	do_facet mds "find $MNTDIR/OBJECTS -type f -delete"
	do_facet mds "umount -d $MNTDIR"
	# restart MDS with missing llog files
	start_mds
	do_facet mds "lctl set_param fail_loc=0"
	reformat
}
run_test 58 "missing llog files must not prevent MDT from mounting"

test_59() {
	start_mgsmds >> /dev/null
	local C1=$(count_osts)
	if [ $C1 -eq 0 ]; then
		start_ost >> /dev/null
		C1=$(count_osts)
	fi
	stopall
	echo "original ost count: $C1 (expect > 0)"
	[ $C1 -gt 0 ] || error "No OSTs in $FSNAME log"
	start_mgsmds -o writeconf >> /dev/null || error "MDT start failed"
	local C2=$(count_osts)
	echo "after mdt writeconf count: $C2 (expect 0)"
	[ $C2 -gt 0 ] && error "MDT writeconf should erase OST logs"
	echo "OST start without writeconf should fail:"
	start_ost >> /dev/null && error "OST start without writeconf didn't fail"
	echo "OST start with writeconf should succeed:"
	start_ost -o writeconf >> /dev/null || error "OST1 start failed"
	local C3=$(count_osts)
	echo "after ost writeconf count: $C3 (expect 1)"
	[ $C3 -eq 1 ] || error "new OST writeconf should add:"
	start_ost2 -o writeconf >> /dev/null || error "OST2 start failed"
	local C4=$(count_osts)
	echo "after ost2 writeconf count: $C4 (expect 2)"
	[ $C4 -eq 2 ] || error "OST2 writeconf should add log"
	stop_ost2 >> /dev/null
	cleanup_nocli >> /dev/null
	#writeconf to remove all ost2 traces for subsequent tests
	writeconf
}
run_test 59 "writeconf mount option"

test_60() { # LU-471
	local mdsdev=$(mdsdevname ${SINGLEMDS//mds/})
	add $SINGLEMDS $(mkfs_opts $SINGLEMDS) \
		--mkfsoptions='\" -E stride=64 -O ^uninit_bg\"' \
		--reformat $mdsdev || error "add $SINGLEMDS $mdsdev failed"

	local dump
	dump=$(do_facet $SINGLEMDS dumpe2fs $mdsdev) ||
		error "dumpe2fs $mdsdev failed"

	# MDT default has dirdata feature
	echo $dump | grep dirdata > /dev/null || error "dirdata is not set"
	# we disable uninit_bg feature
	echo $dump | grep uninit_bg > /dev/null && error "uninit_bg is set"
	# we set stride extended options
	echo $dump | grep stride > /dev/null || error "stride is not set"
	reformat
}
run_test 60 "check mkfs.lustre --mkfsoptions -E -O options setting"

test_61() { # LU-80
    local reformat=false

    [ $(lustre_version_code $SINGLEMDS) -ge $(version_code 2.1.53) ] ||
        { skip "Need MDS version at least 2.1.53"; return 0; }

    if ! large_xattr_enabled; then
        reformat=true
        local mds_dev=$(mdsdevname ${SINGLEMDS//mds/})
        add $SINGLEMDS $MDS_MKFS_OPTS --mkfsoptions='\"-O large_xattr\"' \
            --reformat $mds_dev || error "reformatting $mds_dev failed"
    fi

    setup_noconfig || error "setting up the filesystem failed"
    client_up || error "starting client failed"

    local file=$DIR/$tfile
    touch $file

    local large_value="$(generate_string $(max_xattr_size))"
    local small_value="bar"

    local name="trusted.big"
    log "save large xattr $name on $file"
    setfattr -n $name -v $large_value $file ||
        error "saving $name on $file failed"

    local new_value=$(get_xattr_value $name $file)
    [[ "$new_value" != "$large_value" ]] &&
        error "$name different after saving"

    log "shrink value of $name on $file"
    setfattr -n $name -v $small_value $file ||
        error "shrinking value of $name on $file failed"

    new_value=$(get_xattr_value $name $file)
    [[ "$new_value" != "$small_value" ]] &&
        error "$name different after shrinking"

    log "grow value of $name on $file"
    setfattr -n $name -v $large_value $file ||
        error "growing value of $name on $file failed"

    new_value=$(get_xattr_value $name $file)
    [[ "$new_value" != "$large_value" ]] &&
        error "$name different after growing"

    log "check value of $name on $file after remounting MDS"
    fail $SINGLEMDS
    new_value=$(get_xattr_value $name $file)
    [[ "$new_value" != "$large_value" ]] &&
        error "$name different after remounting MDS"

    log "remove large xattr $name from $file"
    setfattr -x $name $file || error "removing $name from $file failed"

    rm -f $file
    stopall
    $reformat && reformat
}
run_test 61 "large xattr"

test_62() {
    # MRP-118
    local mdsdev=$(mdsdevname 1)
    local ostdev=$(ostdevname 1)

    echo "disable journal for mds"
    do_facet mds tune2fs -O ^has_journal $mdsdev || error "tune2fs failed"
    start_mds && error "MDT start should fail"
    echo "disable journal for ost"
    do_facet ost1 tune2fs -O ^has_journal $ostdev || error "tune2fs failed"
    start_ost && error "OST start should fail"
    cleanup || return $?
    reformat_and_config
}
run_test 62 "start with disabled journal"

test_63() {
	local inode_slab=$(do_facet $SINGLEMDS \
		"awk '/ldiskfs_inode_cache/ { print \\\$5 }' /proc/slabinfo")
	if [ -z "$inode_slab" ]; then
		skip "ldiskfs module has not been loaded"
		return
	fi

	echo "$inode_slab ldisk inodes per page"
	[ "$inode_slab" -ge "3" ] ||
		error "ldisk inode size is too big, $inode_slab objs per page"
	return
}
run_test 63 "Verify each page can at least hold 3 ldisk inodes"

test_65() { # LU-2237
	# Currently, the test is only valid for ldiskfs backend
	[ "$(facet_fstype $SINGLEMDS)" != "ldiskfs" ] &&
		skip "non-ldiskfs backend" && return

	local devname=$(mdsdevname ${SINGLEMDS//mds/})
	local brpt=$(facet_mntpt brpt)
	local opts=""

	if ! do_facet $SINGLEMDS "test -b $devname"; then
		opts="-o loop"
	fi

	stop_mds
	local obj=$(do_facet $SINGLEMDS \
		    "$DEBUGFS -c -R \\\"stat last_rcvd\\\" $devname" |
		    grep Inode)
	if [ -z "$obj" ]; then
		# The MDT may be just re-formatted, mount the MDT for the
		# first time to guarantee the "last_rcvd" file is there.
		start_mds || error "fail to mount the MDS for the first time"
		stop_mds
	fi

	# remove the "last_rcvd" file
	do_facet $SINGLEMDS "mkdir -p $brpt"
	do_facet $SINGLEMDS \
		"mount -t $(facet_fstype $SINGLEMDS) $opts $devname $brpt"
	do_facet $SINGLEMDS "rm -f ${brpt}/last_rcvd"
	do_facet $SINGLEMDS "umount -d $brpt"

	# restart MDS, the "last_rcvd" file should be recreated.
	start_mds || error "fail to restart the MDS"
	stop_mds
	obj=$(do_facet $SINGLEMDS \
	      "$DEBUGFS -c -R \\\"stat last_rcvd\\\" $devname" | grep Inode)
	[ -n "$obj" ] || error "fail to re-create the last_rcvd"
}
run_test 65 "re-create the lost last_rcvd file when server mount"

test_66() {
	setup
	local OST1_NID=$(do_facet ost1 $LCTL list_nids | head -1)
	local MDS_NID=$(do_facet $SINGLEMDS $LCTL list_nids | head -1)

	set_conf_param_and_check mds				     \
	    "$LCTL get_param -n osc.$FSNAME-OST0000-osc-MDT*.active" \
	    "$FSNAME-OST0000.osc.active"			     \
	    "0"

	echo "replace_nids should fail if MDS, OSTs and clients are UP"
	do_facet mgs $LCTL replace_nids $FSNAME-OST0000 $OST1_NID &&
		error "replace_nids fail"

	umount_client $MOUNT || error "unmounting client failed"
	echo "replace_nids should fail if MDS and OSTs are UP"
	do_facet mgs $LCTL replace_nids $FSNAME-OST0000 $OST1_NID &&
		error "replace_nids fail"

	stop_ost
	echo "replace_nids should fail if MDS is UP"
	do_facet mgs $LCTL replace_nids $FSNAME-OST0000 $OST1_NID &&
		error "replace_nids fail"

	stop_mds || error "stopping mds failed"

	if combined_mgs_mds; then
		start_mds "-o nosvc" ||
			error "starting mds with nosvc option failed"
	fi

	echo "command should accept two parameters"
	do_facet mgs $LCTL replace_nids $FSNAME-OST0000 &&
		error "command should accept two params"

	echo "correct device name should be passed"
	do_facet mgs $LCTL replace_nids $FSNAME-WRONG0000 $OST1_NID &&
		error "wrong devname"

	echo "wrong nids list should not destroy the system"
	do_facet mgs $LCTL replace_nids $FSNAME-OST0000 "wrong nids list" &&
		error "wrong parse"

	echo "replace OST nid"
	do_facet mgs $LCTL replace_nids $FSNAME-OST0000 $OST1_NID ||
		error "replace nids failed"

	echo "command should accept two parameters"
	do_facet mgs $LCTL replace_nids $FSNAME-MDT0000 &&
		error "command should accept two params"

	echo "wrong nids list should not destroy the system"
	do_facet mgs $LCTL replace_nids $FSNAME-MDT0000 "wrong nids list" &&
		error "wrong parse"

	echo "replace MDS nid"
	do_facet mgs $LCTL replace_nids $FSNAME-MDT0000 $MDS_NID ||
		error "replace nids failed"

	echo "conf_param after replace_nids"
	do_facet mgs "$LCTL conf_param $FSNAME-MDT0000.mdd.atime_diff=65" ||
		error "conf_param failed"

        local mgsdev
	if ! combined_mgs_mds ; then
		mgsdev=$MGSDEV
		stop_mgs
	else
		mgsdev=$(mdsdevname 1)
		stop_mds
	fi

	echo "check llog backup"
	do_facet mgs "$DEBUGFS -c -R \\\"dump CONFIGS/$FSNAME-MDT0000.bak \
$TMP/mdt_conf.bak\\\" $mgsdev"
	do_facet mgs "llog_reader $TMP/mdt_conf.bak > /dev/null" ||
		error "llog_reader $TMP/mdt_conf.bak failed"

	start_mgsmds || error "start mgsmds failed"
	set_conf_param_and_check mds				     \
	    "$LCTL get_param -n osc.$FSNAME-OST0000-osc-MDT*.active" \
	    "$FSNAME-OST0000.osc.active"			     \
	    "1"

	start_ost
	mount_client $MOUNT
	check_mount || error "error after nid replace"
	cleanup
	reformat
}
run_test 66 "replace nids"

test_67() { #LU-2950
	local legacy="$TMP/legacy_lnet_config"
	local new="$TMP/new_routes_test"
	local out="$TMP/config_out_file"
	local verify="$TMP/conv_verify"
	local verify_conf="$TMP/conf_verify"

	# Create the legacy file that will be run through the
	# lustre_routes_conversion script
	cat <<- LEGACY_LNET_CONFIG > $legacy
		tcp1 23 192.168.213.1@tcp:1; tcp5 34 193.30.4.3@tcp:4;
		tcp2 54 10.1.3.2@tcp;
		tcp3 10.3.4.3@tcp:3;
		tcp4 10.3.3.4@tcp;
	LEGACY_LNET_CONFIG

	# Create the verification file to verify the output of
	# lustre_routes_conversion script against.
	cat <<- VERIFY_LNET_CONFIG > $verify
		tcp1: { gateway: 192.168.213.1@tcp, hop: 23, priority: 1 }
		tcp5: { gateway: 193.30.4.3@tcp, hop: 34, priority: 4 }
		tcp2: { gateway: 10.1.3.2@tcp, hop: 54 }
		tcp3: { gateway: 10.3.4.3@tcp, priority: 3 }
		tcp4: { gateway: 10.3.3.4@tcp }
	VERIFY_LNET_CONFIG

	# Create the verification file to verify the output of
	# lustre_routes_config script against
	cat <<- VERIFY_LNET_CONFIG > $verify_conf
		lctl --net tcp1 add_route 192.168.213.1@tcp 23 1
		lctl --net tcp5 add_route 193.30.4.3@tcp 34 4
		lctl --net tcp2 add_route 10.1.3.2@tcp 54 4
		lctl --net tcp3 add_route 10.3.4.3@tcp 1 3
		lctl --net tcp4 add_route 10.3.3.4@tcp 1 3
	VERIFY_LNET_CONFIG

	lustre_routes_conversion $legacy $new > /dev/null
	if [ -f $new ]; then
		# verify the conversion output
		cmp -s $new $verify > /dev/null
		if [ $? -eq 1 ]; then
			error "routes conversion failed"
		fi

		lustre_routes_config --dry-run --verbose $new > $out
		# check that the script succeeded
		cmp -s $out $verify_conf > /dev/null
		if [ $? -eq 1 ]; then
			error "routes config failed"
		fi
	else
		error "routes conversion test failed"
	fi
	# remove generated files
	rm -f $new $legacy $verify $verify_conf $out
}
run_test 67 "test routes conversion and configuration"

test_70() { # MRP-153
	local paramkey="mgsnode"
	local paramval1="192.0.2.254@tcp" # Reserved IPs, see RFC 5735
	local paramval2="192.0.2.255@tcp"
	local params
	local count

	stopall

	do_facet mds "$TUNEFS --erase-params `mdsdevname 1` >/dev/null" || error "tunefs failed"

	# Check that parameter is added correctly
	do_facet mds "$TUNEFS --param $paramkey=$paramval1 `mdsdevname 1` >/dev/null" \
		|| error "tunefs failed"
	params=`do_facet mds "$TUNEFS --print \`mdsdevname 1\`" || error "tunefs failed" `
	params=${params#*Permanent}
	count=`echo $params | tr ' ' '\n' | grep -c $paramkey=$paramval1`
	[ $count = "1" ] || error "on-disk parameter not added correctly via tunefs"

	# Check that parameter replaces existing instances when added
	do_facet mds "$TUNEFS --param $paramkey=$paramval2 `mdsdevname 1` >/dev/null" \
		|| error "tunefs failed"
	params=`do_facet mds "$TUNEFS --print \`mdsdevname 1\`" || error "tunefs failed" `
	params=${params#*Permanent}
	count=`echo $params | tr ' ' '\n' | grep -c $paramkey=`
	[ $count = "1" ] || error "on-disk parameter not replaced correctly via tunefs"
	count=`echo $params | tr ' ' '\n' | grep -c $paramkey=$paramval2`
	[ $count = "1" ] || error "on-disk parameter not replaced correctly via tunefs"

	#Check that a parameter is erased properly
	do_facet mds "$TUNEFS --erase-param $paramkey `mdsdevname 1` >/dev/null" \
		|| error "tunefs failed"
	params=`do_facet mds "$TUNEFS --print \`mdsdevname 1\`" || error "tunefs failed" `
	params=${params#*Permanent}
	count=`echo $params | tr ' ' '\n' | grep -c $paramkey=`
	[ $count = "0" ] || error "on-disk parameter not erased correctly via tunefs"

	reformat
}
run_test 70 "check tunefs correctly handles parameter addition and removal"

test_72() { #LU-2634
	local mdsdev=$(mdsdevname ${SINGLEMDS//mds/})
	local ostdev=$(ostdevname 1)
	local cmd="$E2FSCK -fnvd $mdsdev"
	local fn=3

	#tune MDT with "-O extents"
	add $SINGLEMDS $(mkfs_opts $SINGLEMDS) --reformat $mdsdev ||
		error "add $SINGLEMDS $mdsdev failed"
	$TUNE2FS -O extents $mdsdev
	add ost1 $(mkfs_opts ost1) --reformat $ostdev ||
		error "add ost1 $ostdev failed"
	start_mgsmds || error "start mds failed"
	start_ost || error "start ost failed"
	mount_client $MOUNT || error "mount client failed"

	#create some short symlinks
	mkdir -p $DIR/$tdir
	createmany -o $DIR/$tdir/$tfile-%d $fn
	echo "create $fn short symlinks"
	for i in $(seq -w 1 $fn); do
		ln -s $DIR/$tdir/$tfile-$i $MOUNT/$tfile-$i
	done
	ls -al $MOUNT

	#umount
	umount_client $MOUNT || error "umount client failed"
	stop_mds || error "stop mds failed"
	stop_ost || error "stop ost failed"

	#run e2fsck
	run_e2fsck $(facet_active_host $SINGLEMDS) $mdsdev "-n"
}
run_test 72 "test fast symlink with extents flag enabled"

test_75() { # LU-2374
	[[ $(lustre_version_code $SINGLEMDS) -lt $(version_code 2.4.1) ]] &&
	                skip "Need MDS version at least 2.4.1" && return

	local index=0
	local opts_mds="$(mkfs_opts mds1 $(mdsdevname 1)) \
		--reformat $(mdsdevname 1) $(mdsvdevname 1)"
	local opts_ost="$(mkfs_opts ost1 $(ostdevname 1)) \
		--reformat $(ostdevname 1) $(ostvdevname 1)"

	#check with default parameters
	add mds1 $opts_mds || error "add mds1 failed for default params"
	add ost1 $opts_ost || error "add ost1 failed for default params"

	opts_mds=$(echo $opts_mds | sed -e "s/--mdt//")
	opts_mds=$(echo $opts_mds |
		   sed -e "s/--index=$index/--index=$index --mdt/")
	opts_ost=$(echo $opts_ost | sed -e "s/--ost//")
	opts_ost=$(echo $opts_ost |
		   sed -e "s/--index=$index/--index=$index --ost/")

	add mds1 $opts_mds || error "add mds1 failed for new params"
	add ost1 $opts_ost || error "add ost1 failed for new params"
	return 0
}
run_test 75 "The order of --index should be irrelevant"

recovery_time_min() {
        local CONNECTION_SWITCH_MIN=5
        local CONNECTION_SWITCH_INC=5
        local CONNECTION_SWITCH_MAX
        local RECONNECT_DELAY_MAX
        local INITIAL_CONNECT_TIMEOUT
        local max
        local TO_20

        #CONNECTION_SWITCH_MAX=min(50, max($CONNECTION_SWITCH_MIN,$TIMEOUT)
        (($CONNECTION_SWITCH_MIN>$TIMEOUT)) && max=$CONNECTION_SWITCH_MIN || max=$TIMEOUT
        (($max<50)) && CONNECTION_SWITCH_MAX=$max || CONNECTION_SWITCH_MAX=50

        #INITIAL_CONNECT_TIMEOUT max(CONNECTION_SWITCH_MIN,obd_timeout/20)
        TO_20=$(($TIMEOUT/20))
        (($CONNECTION_SWITCH_MIN>$TO_20)) && INITIAL_CONNECT_TIMEOUT=$CONNECTION_SWITCH_MIN || INITIAL_CONNECT_TIMEOUT=$TO_20

	RECONNECT_DELAY_MAX=$(($CONNECTION_SWITCH_MAX+$CONNECTION_SWITCH_INC+$INITIAL_CONNECT_TIMEOUT))
        echo $((2*$RECONNECT_DELAY_MAX))
}

test_79() {
        local facet=$SINGLEMDS
        local num=$(echo $facet | tr -d "mds")
        local dev=$(mdsdevname $num)
        local time_min=$(recovery_time_min)
        local recovery_duration
        local completed_clients

        echo "start mds service on `facet_active_host $facet`"
        start $facet ${dev} $MDS_MOUNT_OPTS "-o recovery_time_hard=$time_min,recovery_time_soft=$time_min" $@ || return 94

        start_ost
        start_ost2

        echo "recovery_time_hard $time_min, recovery_time_soft $time_min, timeout $TIMEOUT"

        mount_client $MOUNT1 || error "mount failed"
        mount_client $MOUNT2 || error "mount failed"

        createmany -o $DIR/$tfile-%d 4000
        createmany -o $DIR1/$tfile-%d 4000

        replay_barrier_nosync $SINGLEMDS

	# We need to catch the end of recovery window to extend it.
	# Skip 500 requests and add delay to request handling.
        #define OBD_FAIL_TGT_REPLAY_DELAY  0x709 | FAIL_SKIP
        do_facet $SINGLEMDS "lctl set_param fail_loc=0x20000709"
        do_facet $SINGLEMDS "lctl set_param fail_val=500"

        #fail $SINGLEMDS
        facet_failover $SINGLEMDS || error "failover: $?"
        client_up

        echo "recovery status"
        do_facet $SINGLEMDS "$LCTL get_param -n mdt.$FSNAME-MDT0000.recovery_status"

        recovery_duration=$(do_facet $SINGLEMDS "$LCTL get_param -n mdt.$FSNAME-MDT0000.recovery_status" |grep recovery_duration |awk '{print $2}')
        (($recovery_duration>$time_min)) && error "recovery_duration > recovery_time_hard"
        completed_clients=$(do_facet $SINGLEMDS "$LCTL get_param -n mdt.$FSNAME-MDT0000.recovery_status" |grep completed_clients |awk '{print $2}')
        [ "$completed_clients" = "1/2" ] || error "completed_clients != 1/2: "$completed_clients

        do_facet $SINGLEMDS "lctl set_param fail_loc=0"
        umount_client $MOUNT1
        umount_client $MOUNT2

        stop_ost
        stop_ost2
        stop_mds
}
run_test 79 "check recovery_hard_time"

test_80() {
	start_mds
	start_ost
	uuid=$(do_facet ost1 lctl get_param -n mgc.*.uuid)
#define OBD_FAIL_MGS_PAUSE_TARGET_CON       0x906
	do_facet ost1 "lctl set_param fail_loc=0x906"
	do_facet mgs "lctl set_param fail_loc=0x906"
	do_facet mgs "lctl set_param -n mgs/MGS/evict_client $uuid"
	sleep $((3*$TIMEOUT/2))
	start_ost2

	do_facet ost1 "lctl set_param fail_loc=0"
	stopall
}
run_test 80 "mgc import reconnect race"

test_86() {
	cleanup
	local NEWSIZE=1024
	local SAVED_OST_MKFS_OPTS=$OST_MKFS_OPTS
	local OLDSIZE=$(do_facet ost1 "$DEBUGFS -c -R stats `ostdevname 1`" \
		| awk '/Flex block group size: / { print $NF; exit;}')

	#uncomment this string to test "true" branch in next condition
	#OST_MKFS_OPTS="--ost --fsname=lustre --device-size=200000" \
	#	"--mgsnode=devvm-sl6-2@tcp --param sys.timeout=20"
	local opts=OST_MKFS_OPTS
        if [[ ${!opts} != *mkfsoptions* ]]; then
            eval OST_MKFS_OPTS=\"${!opts} \
	    --mkfsoptions='\\\"-O flex_bg -G $NEWSIZE\\\"'\"
	else
            val=${!opts//--mkfsoptions=\\\"/ \
		    --mkfsoptions=\\\"-O flex_bg -G $NEWSIZE }
            eval OST_MKFS_OPTS='${val}'
        fi

	echo "params: $OST_MKFS_OPTS"
	reformat

	local FOUNDSIZE=$(do_facet ost1 "$DEBUGFS -c -R stats `ostdevname 1`" \
		| awk '/Flex block group size: / { print $NF; exit;}')

	OST_MKFS_OPTS=$SAVED_OST_MKFS_OPTS
	echo "params: $OST_MKFS_OPTS"
	reformat
	setup

	[[ $FOUNDSIZE -ne $NEWSIZE ]] && error \
		"Flex block group size: "$FOUNDSIZE", expected: "$NEWSIZE""
	return 0
}
run_test 86 "Replacing mkfs.lustre -G option"

test_74()
{
	local mgsdev
	reformat_and_config

	if combined_mgs_mds ; then
		mgsdev=$(mdsdevname 1)
	else
		mgsdev=$MGSDEV
		stop mgs
	fi

	local mntdir=$(facet_mntpt mgs)
	do_facet mgs "mount -t $FSTYPE $mgsdev $mntdir $MDS_MOUNT_OPTS"
	#
	# Incorrect llh_tgtuuid in log header is to cause lctl
	# conf_param below to fail.
	#
	do_facet mgs "sed -i '1,/config_uuid/s/config_uuid/xxxxxx_xxxx/' \
$mntdir/CONFIGS/$FSNAME-MDT0000"
	do_facet mgs "umount $mgsdev"

	if combined_mgs_mds ; then
		start_mds
	else
		start_mgs
	fi
	do_facet mgs "$LCTL conf_param $FSNAME-MDT0000.mdd.atime_diff=65" && \
	    error "conf_param should fail"
	#
	# "Mount still busy" from server_wait_finished() appears after
	# 30 seconds waiting.
	#
	stop_mgsmds_with_timeout 40
	do_facet mgs "dmesg | grep \\\"Mount still busy with\\\"" && \
	    error "umount failed"

	reformat_and_config
}
run_test 74 "test conf_param failure"

#
# set number of permanent parameters
#
test_87_set_params() {
	local fsname=$1

	set_conf_param_and_check mds				    \
	    "$LCTL get_param -n mdd.$fsname-MDT0000.atime_diff"	    \
	    "$fsname-MDT0000.mdd.atime_diff"			    \
	    "62"
	set_conf_param_and_check mds				    \
	    "$LCTL get_param -n mdd.$fsname-MDT0000.atime_diff"	    \
	    "$fsname-MDT0000.mdd.atime_diff"			    \
	    "63"
	set_conf_param_and_check client				    \
	    "$LCTL get_param -n llite.$fsname*.max_read_ahead_mb"   \
	    "$fsname.llite.max_read_ahead_mb"			    \
	    "32"
	set_conf_param_and_check client				    \
	    "$LCTL get_param -n llite.$fsname*.max_read_ahead_mb"   \
	    "$fsname.llite.max_read_ahead_mb"			    \
	    "64"
	create_pool $fsname.pool1 || error "create pool failed"
	do_facet mgs $LCTL pool_add $fsname.pool1 OST0000 ||
		error "pool_add failed"
	do_facet mgs $LCTL pool_remove $fsname.pool1 OST0000 ||
		error "pool_remove failed"
	do_facet mgs $LCTL pool_add $fsname.pool1 OST0000 ||
		error "pool_add failed"
}

#
# check permanent parameters
#
test_87_test_params() {
	local fsname=$1

	local atime_diff=$(do_facet mds $LCTL \
get_param -n mdd.$fsname-MDT0000.atime_diff)
	[ $atime_diff == 63 ] || error "wrong mdd parameter after clear_conf"
	local max_read_ahead_mb=$(do_facet client $LCTL \
get_param -n llite.$fsname*.max_read_ahead_mb)
	[ $max_read_ahead_mb == 64 ] || error "wrong llite parameter after clear_conf"
	local ost_in_pool=$(do_facet mds $LCTL \
pool_list $fsname.pool1 | grep -v "^Pool:" | sed 's/_UUID//')
	[ $ost_in_pool = "$fsname-OST0000" ] || error "wrong pool after clear_conf"
}

#
# run lctl clear_conf, store CONFIGS before and after that
#
test_87_clear_conf()
{
	local clear_conf_arg=$1
	local mgsdev
	if ! combined_mgs_mds ; then
		mgsdev=$MGSDEV
		stop_mgs || error "stop_mgs failed"
		start_mgs "-o nosvc" || error "start_mgs nosvc failed"
	else
		mgsdev=$(mdsdevname 1)
		start_mds "-o nosvc" || error "start_mds nosvc failed"
	fi

	do_facet mgs "rm -rf $TMP/conf1; mkdir -p $TMP/conf1; \
$DEBUGFS -c -R \\\"rdump CONFIGS $TMP/conf1\\\" $mgsdev"

	#
	# the command being tested
	#
	do_facet mgs $LCTL clear_conf $clear_conf_arg ||
		error "clear_conf failed"

	if ! combined_mgs_mds ; then
		stop_mgs || error "stop_mgs failed"
	else
		stop_mds || error "stop_mds failed"
	fi

	do_facet mgs "rm -rf $TMP/conf2; mkdir -p $TMP/conf2; \
$DEBUGFS -c -R \\\"rdump CONFIGS $TMP/conf2\\\" $mgsdev"
}

test_87_file_shortened() {
	local file=$1
	local sizes=($(do_facet mgs stat -c %s $TMP/conf1/CONFIGS/$file \
$TMP/conf2/CONFIGS/$file))
	[ ${sizes[1]} -lt ${sizes[0]} ] && return 0
	return 1
}

test_87a()
{
	reformat
	setup_noconfig
	client_up || error "client_up failed"

	#
	# set number of permanent parameters
	#
	test_87_set_params $FSNAME

	umount_client $MOUNT || error "umount_client failed"
	stop_ost || error "stop_ost failed"
	stop_mds || error "stop_mds failed"

	test_87_clear_conf $FSNAME
	#
	# make sure that all configs are cleared
	#
	test_87_file_shortened $FSNAME-MDT0000 || error "faled to clear MDT0000"
	test_87_file_shortened $FSNAME-client || error "failed to clear client"
	test_87_file_shortened $FSNAME-params || error "failed to clear params"

	setup_noconfig

	#
	# check that configurations is intact
	#
	test_87_test_params $FSNAME

	cleanup
	reformat
}
run_test 87a "test lctl clear_conf fsname"

test_87b()
{
	reformat
	setup_noconfig
	client_up || error "client_up failed"

	#
	# set number of permanent parameters
	#
	test_87_set_params $FSNAME

	umount_client $MOUNT || error "umount_client failed"
	stop_ost || error "stop_ost failed"
	stop_mds || error "stop_mds failed"

	test_87_clear_conf $FSNAME-MDT0000
	#
	# make sure that only one config is cleared
	#
	test_87_file_shortened $FSNAME-MDT0000 || error "faled to clear MDT0000"
	test_87_file_shortened $FSNAME-client && error "client cleared"
	test_87_file_shortened $FSNAME-params && error "params cleared"

	setup_noconfig

	#
	# check that configurations is intact
	#
	test_87_test_params $FSNAME

	cleanup
	reformat
}
run_test 87b "test lctl clear_conf one config"

test_88()
{
	#define OBD_FAIL_MDS_NOTIFY_CREATE       0x157
	do_facet $SINGLEMDS lctl set_param fail_loc=0x80000157
	start_mds
	start_ost
	mount_client $MOUNT && error "client start should fail"
	cleanup
}
run_test 88 "test mds_notify failure"

change_dev_journal()
{
	local facet=$1
	local dev=$2
	local journal_size=$3
	do_facet $facet "$TUNE2FS -O ^has_journal $dev" ||
		error "journal removal failed"
	do_facet $facet "$TUNE2FS -J size=$journal_size $dev" ||
		error "journal size change failed"
}

test_89()
{
	local mdsdev=$(mdsdevname 1)
	local journal_size

	reformat
	add ost2 $(mkfs_opts ost2) --index=10000 --reformat $(ostdevname 2) ||
		error "add ost2 failed"

	#
	# get mdd txn size for 0 and 2 osts
	#
	start_mds || error "MDT start failed"
	declare -a mdd_txn_size0=( $(do_facet $SINGLEMDS $LCTL \
get_param -n mdd.$FSNAME-MDT0000.txn_max_size) )

	start_ost || error "OST start failed"
	start_ost2 || error "OST2 start failed"

	stopall

	start_mds || error "MDT start failed"

	declare -a mdd_txn_size2=( $(do_facet $SINGLEMDS $LCTL \
get_param -n mdd.$FSNAME-MDT0000.txn_max_size) )

	[ ${mdd_txn_size2[0]} -eq 2 -a ${mdd_txn_size0[0]} -eq 0 ] ||
	    error "failed to get max txn size for 0 or for 2 osts"

	stopall

	#
	# make mds journal too small even for 0 osts
	# mdd_txn_size0[1] is in 4k blocks, journal_size in mb
	#
	journal_size=$((((mdd_txn_size0[1] * 4 + 255) >> 8) - 2))
	change_dev_journal $SINGLEMDS $mdsdev $journal_size

	#
	# mds should fail to start with journal of this size
	#
	start_mds && error "start_mds should fail"

	#
	# make mds journal insufficiently big for 2 osts
	# mdd_txn_size2[1] is in 4k blocks, journal_size in mb
	#
	journal_size=$((((mdd_txn_size2[1] * 4 + 255) >> 8) - 1))
	change_dev_journal $SINGLEMDS $mdsdev $journal_size

	setup2

	#
	# only one ost should be functioning
	#
	$SETSTRIPE -c 2  $MOUNT/$tfile-1 || error "setstripe failed"
	[ $($GETSTRIPE -c $MOUNT/$tfile-1) -eq 1 ] ||
		error "incorrectly striped $MOUNT/$tfile-1"
	stopall

	#
	# make mds journal big enough for 2 osts
	#
	journal_size=$(((mdd_txn_size2[1] * 4 + 255) >> 8))
	change_dev_journal $SINGLEMDS $mdsdev $journal_size

	setup2

	$SETSTRIPE -c 2 $MOUNT/$tfile-2 || error "setstripe failed"
	[ $($GETSTRIPE -c $MOUNT/$tfile-2) -eq 2 ] ||
		error "incorrectly striped $MOUNT/$tfile-2"
	stopall
	reformat
}
run_test 89 "mdd txn size"

test_90()
{
	# this test needs 2 osts on one node separate from mds
	[ "$OSTCOUNT" -lt "2" ] && skip "2 osts are needed" && return 0
	[ $(facet_host ost1) != $(facet_host ost2) ] &&
		skip "osts on different nodes" && return 0
	[ $(facet_host $SINGLEMDS) == $(facet_host ost1) ] &&
		skip "osts and mds are to be on different nodes" && return 0

	start_mds || error "mds start failed"
	start_ost || error "ost1 start failed"
	start_ost2 || error "ost2 start failed"

#define OBD_FAIL_MGC_PAUSE_PROCESS_LOG   0x903
	do_facet ost1 "lctl set_param fail_loc=0x903"
	stop_ost &
	stop_ost2 &
	wait
	do_facet ost1 "lctl set_param fail_loc=0"
	stop_mds || error "mds stop failed"
	unload_modules_conf
}
run_test 90 "test umount race"

if ! combined_mgs_mds ; then
	stop mgs
fi

cleanup_gss

# restore the ${facet}_MKFS_OPTS variables
for facet in MGS MDS OST; do
    opts=SAVED_${facet}_MKFS_OPTS
    if [[ -n ${!opts} ]]; then
        eval ${facet}_MKFS_OPTS='${!opts}'
    fi
done

complete $SECONDS
exit_status
