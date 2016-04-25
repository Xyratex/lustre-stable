#!/bin/bash
#
#

set -e

ONLY=${ONLY:-"$*"}

LUSTRE=${LUSTRE:-$(cd $(dirname $0)/..; echo $PWD)}
. $LUSTRE/tests/test-framework.sh
init_test_env $@
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}
init_logging

. $LUSTRE/tests/setup-nfs.sh

# bug number:
ALWAYS_EXCEPT=""

NFSVERSION=3
EXPORT_OPTS="rw"

build_test_filter
check_and_setup_lustre

# first unmount all the lustre client
cleanup_mount $MOUNT
# mount lustre on mds
lustre_client=${NFS_SERVER:-$(facet_active_host $SINGLEMDS)}
NFS_CLIENT=${NFS_CLIENT:-$SINGLECLIENT}
NFS_CLIENT=$(exclude_items_from_list $NFS_CLIENT $lustre_client)
CL_MNT_OPT=""
zconf_mount_clients $lustre_client $MOUNT "$CL_MNT_OPT" ||
    error "mount lustre on $lustre_client failed"

assert_DIR

NFS_MNT=$MOUNT
LUSTRE_MNT=$MOUNT
LOCKF=lockfile
[ -z $NFS_CLIENT ] && error "NFS_CLIENT is empty"

nfs_stop() {
	cleanup_nfs "$NFS_MNT" "$lustre_client" "$NFS_CLIENT" || \
	        error_noexit false "failed to cleanup nfs"
	echo "NFS stopped"
}

cleanup() {
	nfs_stop
	stopall -f
	cleanup_client3
}

trap cleanup SIGHUP SIGINT SIGTERM

nfs_start() {
	do_node $NFS_CLIENT "umount -f $NFS_MNT || true"
	# setup the nfs
	if ! setup_nfs "$NFSVERSION" "$NFS_MNT" "$lustre_client" "$NFS_CLIENT"; then
	    error_noexit false "setup nfs failed!"
	    cleanup_nfs "$NFS_MNT" "$lustre_client" "$NFS_CLIENT" || \
	        error_noexit false "failed to cleanup nfs"
	    check_and_cleanup_lustre
	    exit
	fi

	echo "NFS server mounted"
	do_node $lustre_client chmod a+xrw $LUSTRE_MNT
	do_node $NFS_CLIENT "flock $NFS_MNT/$LOCKF -c 'echo test lock obtained'"
}

cleanup_client3() {
	do_node $NFS_CLIENT "killall flock"
	do_node $NFS_CLIENT "umount -f $NFS_MNT"
	echo "client cleanup successful"
}

test_1a() {
	[ -z $TEST_DIR_LTP_FLOCK ] && \
		skip_env "TEST_DIR_LTP_FLOCK is empty" && return
	nfs_start || return 1
	for i in $(do_node $NFS_CLIENT "ls $TEST_DIR_LTP_FLOCK|sort")
	do
		do_node $NFS_CLIENT \
			"export TMPDIR=$NFS_MNT;$TEST_DIR_LTP_FLOCK/$i"
	done
	nfs_stop
}
run_test 1a "LTP testsuite"

test_1b() {
	LOCKTESTS=${LOCKTESTS:-$(which locktests 2> /dev/null || true)}
	echo $LOCKTESTS
	[ x$LOCKTESTS = x ] &&
		{ skip_env "locktests not found" && return; }
	nfs_start || return 1
	do_node $NFS_CLIENT "$LOCKTESTS -n 50 -f $NFS_MNT/locktests"
	nfs_stop
}
run_test 1b "locktests"

test_2b() {
	nfs_start || return 1
	do_node $NFS_CLIENT "flock -e $NFS_MNT/$LOCKF -c 'sleep 10'" &
	sleep 1
	stop mds1
	echo "MDS unmounted"
	cleanup_client3
	do_node $lustre_client "umount -f $LUSTRE_MNT"
	nfs_stop
	do_node $lustre_client "umount -f $LUSTRE_MNT"
	# wait for flock on NFS client to release mountpoint
	sleep 10
	do_node $NFS_CLIENT "umount -f $NFS_MNT"
	stopall -f || error "cleanup failed"
	check_and_setup_lustre
}
run_test 2b "simple cleanup"

test_3a() {
	do_node $lustre_client "touch $LUSTRE_MNT/$LOCKF"
	do_node $lustre_client \
		"flocks_test 5 set write sleep 10 $LUSTRE_MNT/$LOCKF" &
	sleep 1
	do_node $lustre_client \
		"flocks_test 5 set write sleep 5 $LUSTRE_MNT/$LOCKF" &
	sleep 1
	echo "umount -f /mnt/mds1"
	stop mds1 -f || error "MDS umount failed"
	echo "umount -f $LUSTRE_MNT"
	do_node $lustre_client "umount -f $LUSTRE_MNT"
	wait
	do_node $lustre_client "umount -f $LUSTRE_MNT" || \
		error "client umount failed"
	stopall -f || error "cleanup failed"
	check_and_setup_lustre
}
run_test 3a "MDS umount with blocked flock"

test_3b() {
	nfs_start || return 1
	do_node $lustre_client "flock -e $LUSTRE_MNT/$LOCKF -c 'sleep 15'" &
	sleep 1
	do_node $NFS_CLIENT "flock -e $NFS_MNT/$LOCKF -c 'sleep 10'" &
	sleep 1
	echo "umount -f $LUSTRE_MNT"
	do_node $lustre_client "umount -f $LUSTRE_MNT"
	nfs_stop
	cleanup_client3
	do_node $lustre_client "umount -f $LUSTRE_MNT"
	wait
	do_node $lustre_client "umount -f $LUSTRE_MNT" || error "client umount failed"
	stopall -f || error "cleanup failed"
	check_and_setup_lustre
}
run_test 3b "cleanup with blocked lock"

test_4() {
	nfs_start || return 1
#define OBD_FAIL_LLITE_FLOCK_UNLOCK_RACE       0x140a
	do_node $lustre_client "lctl set_param fail_loc=0x140a"
	do_node $lustre_client "flocks_test 5 set write sleep 10 $LUSTRE_MNT/$LOCKF" &
	sleep 1
	do_node $NFS_CLIENT "flocks_test 5 set write sleep 5 $LUSTRE_MNT/$LOCKF"
	nfs_stop
	do_node $lustre_client "umount $LUSTRE_MNT" || error "umount failed"
	stopall -f || error "cleanup failed"
	check_and_setup_lustre
}
run_test 4 "kernel flock unlock race"

test_5() {
	nfs_start || return 1
	do_node $lustre_client \
		"flocks_test 5 set write sleep 10 $LUSTRE_MNT/$LOCKF" &
	sleep 1
#define OBD_FAIL_LLITE_FLOCK_BL_GRANT_RACE     0x140b
	do_node $lustre_client "lctl set_param fail_loc=0x140b"
	do_node $NFS_CLIENT "flocks_test 5 set write sleep 5 $LUSTRE_MNT/$LOCKF"
	wait
	do_node $lustre_client "lctl set_param fail_loc=0"
	nfs_stop
	do_node $lustre_client "umount $LUSTRE_MNT" || error "umount failed"
	stopall -f || error "cleanup failed"
	check_and_setup_lustre
}
run_test 5 "flock grant blocked race"


do_node $lustre_client "umount -f $LUSTRE_MNT || true"
restore_mount $MOUNT

complete $(basename $0) $SECONDS
check_and_cleanup_lustre
exit_status
