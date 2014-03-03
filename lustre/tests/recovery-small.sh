#!/bin/bash

set -e

#         bug  5494 5493
ALWAYS_EXCEPT="24   52 $RECOVERY_SMALL_EXCEPT"

PTLDEBUG=${PTLDEBUG:--1}
LUSTRE=${LUSTRE:-`dirname $0`/..}
. $LUSTRE/tests/test-framework.sh
init_test_env $@
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}
init_logging

require_dsh_mds || exit 0

# also long tests: 19, 21a, 21e, 21f, 23, 27
#                                   1  2.5  2.5    4    4          (min)"
[ "$SLOW" = "no" ] && EXCEPT_SLOW="17  26a  26b    50   51     57"

build_test_filter

# Allow us to override the setup if we already have a mounted system by
# setting SETUP=" " and CLEANUP=" "
SETUP=${SETUP:-""}
CLEANUP=${CLEANUP:-""}

check_and_setup_lustre

assert_DIR
rm -rf $DIR/[df][0-9]*

SAMPLE_NAME=f0.recovery-small.junk

test_1() {
    local F1="$DIR/f1"
    local F2="$DIR/f2"
    drop_request     "mcreate $F1"    || error_noexit "Can't create drop req on file '$F1'"
    drop_reint_reply "mcreate $F2"    || error_noexit "Can't create dopr rep on file '$F2'"
    drop_request     "tchmod 111 $F2" || error_noexit "Can't chmod drop req on file $F2"
    drop_reint_reply "tchmod 666 $F2" || error_noexit "Can't chmod drop rep on file $F2"
    drop_request     "statone $F2"    || error_noexit "Can't stat drop req on file $F2"
    drop_reply       "statone $F2"    || error_noexit "Can't stat drop rep on file $F2"
}
run_test 1 "basic file op: drop req, drop rep"

test_4() {
    local T=$DIR/$FUNCNAME.$SAMPLE_NAME
    do_facet_random_file client $T 10K    || error_noexit "Create random file $T"
    drop_request    "cat $T > /dev/null"  || error_noexit "Open request for $T file"
    drop_reply      "cat $T > /dev/null"  || error_noexit "Open replay for $T file"
    do_facet client "rm $T"               || error_noexit "Can't remove file $T"
}
run_test 4 "open: drop req, drop rep"

test_5() {
    local T=$DIR/$FUNCNAME.$SAMPLE_NAME
    local R="$T-renamed"
    local RR="$T-renamed-again"
    do_facet_random_file client $T 10K  || error_noexit "Create random file $T"
    drop_request     "mv $T $R"         || error_noexit "Rename $T"
    drop_reint_reply "mv $R $RR"        || error_noexit "Failed rename replay on $R"
    do_facet client  "checkstat -v $RR" || error_noexit "checkstat error on $RR"
    do_facet client  "rm $RR"           || error_noexit "Can't remove file $RR"
}
run_test 5 "rename: drop req, drop rep"

test_6() {
    local T=$DIR/$FUNCNAME.$SAMPLE_NAME
    local LINK1=$DIR/f0.link1
    local LINK2=$DIR/f0.link2
    do_facet_random_file client $T 10K || error_noexit "Create random file $T"
    drop_request     "mlink $T $LINK1" || error_noexit "mlink request for $T"
    drop_reint_reply "mlink $T $LINK2" || error_noexit "mlink replay for $T"
    drop_request     "munlink $LINK1"  || error_noexit "munlink request for $T"
    drop_reint_reply "munlink $LINK2"  || error_noexit "munlink replay for $T"
    do_facet client  "rm $T"           || error_noexit "Can't remove file $T"
}
run_test 6 "link: drop req, drop rep"

#bug 1423
test_8() {
    drop_reint_reply "touch $DIR/$tfile"    || return 1
}
run_test 8 "touch: drop rep (bug 1423)"

#bug 1420
test_9() {
    remote_ost_nodsh && skip "remote OST with nodsh" && return 0

    local T=$FUNCNAME.$SAMPLE_NAME
    # make this big, else test 9 doesn't wait for bulk -- bz 5595
    do_facet_random_file client $TMP/$T 4M           || error_noexit "Create random file $TMP/$T"
    do_facet client "cp $TMP/$T $DIR/$T"             || error_noexit "Can't copy to $DIR/$T file"
    pause_bulk "cp /etc/profile $DIR/$tfile"         || error_noexit "Can't pause_bulk copy"
    do_facet client "cp $TMP/$T $DIR/${tfile}.2"     || error_noexit "Can't copy file"
    do_facet client "sync"
    do_facet client "rm $DIR/$tfile $DIR/${tfile}.2" || error_noexit "Can't remove files"
    do_facet client "rm $DIR/$T"                     || error_noexit "Can't remove file $DIR/$T"
    do_facet client "rm $TMP/$T"
}
run_test 9 "pause bulk on OST (bug 1420)"

#bug 1521
test_10a() {
	local BEFORE=`date +%s`
	local EVICT

	do_facet client "stat $DIR > /dev/null"  ||
		error "failed to stat $DIR: $?"
	drop_bl_callback "chmod 0777 $DIR" ||
		error "failed to chmod $DIR: $?"

	# let the client reconnect
	client_reconnect
	EVICT=$(do_facet client $LCTL get_param mdc.$FSNAME-MDT*.state | \
	    awk -F"[ [,]" '/EVICTED]$/ { if (mx<$4) {mx=$4;} } END { print mx }')
	[ ! -z "$EVICT" ] && [[ $EVICT -gt $BEFORE ]] ||
		(do_facet client $LCTL get_param mdc.$FSNAME-MDT*.state;
		    error "no eviction: $EVICT before:$BEFORE")

	do_facet client checkstat -v -p 0777 $DIR ||
		{ error "client checkstat failed: $?"; return 3; }
}
run_test 10a "finish request on server after client eviction (bug 1521)"

test_10b() {
	local BEFORE=`date +%s`
	local EVICT

	do_facet client "stat $DIR > /dev/null"  ||
		error "failed to stat $DIR: $?"
	drop_bl_callback_once "chmod 0777 $DIR" ||
		error "failed to chmod $DIR: $?"

	# let the client reconnect
	client_reconnect
	EVICT=$(do_facet client $LCTL get_param mdc.$FSNAME-MDT*.state | \
	    awk -F"[ [,]" '/EVICTED]$/ { if (mx<$4) {mx=$4;} } END { print mx }')

	[ -z "$EVICT" ] || [[ $EVICT -le $BEFORE ]] ||
		(do_facet client $LCTL get_param mdc.$FSNAME-MDT*.state;
		    error "eviction happened: $EVICT before:$BEFORE")

	do_facet client checkstat -v -p 0777 $DIR ||
		{ error "client checkstat failed: $?"; return 3; }
}
run_test 10b "re-send BL AST"

test_10c() {
	mount_client $DIR2

	#grant lock1, export2
	do_facet client $SETSTRIPE -i -0 $DIR2/$tfile || return 1
	do_facet client $MULTIOP $DIR2/$tfile Ow  || return 1

#define OBD_FAIL_LDLM_BL_EVICT            0x31e
	do_facet ost $LCTL set_param fail_loc=0x31e
	#get waiting lock2, export1
	do_facet client $MULTIOP $DIR/$tfile Ow &
	PID1=$!
	# let enqueue to get asleep
	sleep 2

	#get lock2 blocked
	do_facet client $MULTIOP $DIR2/$tfile Ow &
	PID2=$!
	sleep 2

	#evict export1
	ost_evict_client

	sleep 2
	do_facet ost $LCTL set_param fail_loc=0

	wait $PID1
	wait $PID2

	umount_client $DIR2
}
run_test 10c "lock enqueue for destroyed export"

#bug 2460
# wake up a thread waiting for completion after eviction
test_11(){
    do_facet client multiop $DIR/$tfile Ow  || return 1
    do_facet client multiop $DIR/$tfile or  || return 2

    cancel_lru_locks osc

    do_facet client multiop $DIR/$tfile or  || return 3
    drop_bl_callback multiop $DIR/$tfile Ow || echo "evicted as expected"

    do_facet client munlink $DIR/$tfile  || return 4
}
run_test 11 "wake up a thread waiting for completion after eviction (b=2460)"

#b=2494
test_12(){
    $LCTL mark multiop $DIR/$tfile OS_c 
    do_facet $SINGLEMDS "lctl set_param fail_loc=0x115"
    clear_failloc $SINGLEMDS $((TIMEOUT * 2)) &
    multiop_bg_pause $DIR/$tfile OS_c || return 1
    PID=$!
#define OBD_FAIL_MDS_CLOSE_NET           0x115
    kill -USR1 $PID
    echo "waiting for multiop $PID"
    wait $PID || return 2
    do_facet client munlink $DIR/$tfile  || return 3
}
run_test 12 "recover from timed out resend in ptlrpcd (b=2494)"

# Bug 113, check that readdir lost recv timeout works.
test_13() {
    mkdir -p $DIR/$tdir || return 1
    touch $DIR/$tdir/newentry || return
# OBD_FAIL_MDS_READPAGE_NET|OBD_FAIL_ONCE
    do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000104"
    ls $DIR/$tdir || return 3
    do_facet $SINGLEMDS "lctl set_param fail_loc=0"
    rm -rf $DIR/$tdir || return 4
}
run_test 13 "mdc_readpage restart test (bug 1138)"

# Bug 113, check that readdir lost send timeout works.
test_14() {
    mkdir -p $DIR/$tdir
    touch $DIR/$tdir/newentry
# OBD_FAIL_MDS_SENDPAGE|OBD_FAIL_ONCE
    do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000106"
    ls $DIR/$tdir || return 1
    do_facet $SINGLEMDS "lctl set_param fail_loc=0"
}
run_test 14 "mdc_readpage resend test (bug 1138)"

test_15() {
    do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000128"
    touch $DIR/$tfile && return 1
    return 0
}
run_test 15 "failed open (-ENOMEM)"

READ_AHEAD=`lctl get_param -n llite.*.max_read_ahead_mb | head -n 1`
stop_read_ahead() {
   lctl set_param -n llite.*.max_read_ahead_mb 0
}

start_read_ahead() {
   lctl set_param -n llite.*.max_read_ahead_mb $READ_AHEAD
}

test_16() {
    remote_ost_nodsh && skip "remote OST with nodsh" && return 0

    local T=$FUNCNAME.$SAMPLE_NAME
    do_facet_random_file client $TMP/$T 100K       || error_noexit "Create random file $TMP/$T"
    do_facet client "cp $TMP/$T $DIR/$T"           || error_noexit "Copy to $DIR/$T file"
    sync
    stop_read_ahead

#define OBD_FAIL_PTLRPC_BULK_PUT_NET 0x504 | OBD_FAIL_ONCE
    do_facet ost1 "lctl set_param fail_loc=0x80000504"
    cancel_lru_locks osc
    # OST bulk will time out here, client resends
    do_facet client "cmp $TMP/$T $DIR/$T" || return 1
    do_facet ost1 lctl set_param fail_loc=0
    # give recovery a chance to finish (shouldn't take long)
    sleep $TIMEOUT
    do_facet client "cmp $TMP/$T $DIR/$T" || return 2
    start_read_ahead
}
run_test 16 "timeout bulk put, don't evict client (2732)"

test_17() {
    local at_max_saved=0

    remote_ost_nodsh && skip "remote OST with nodsh" && return 0

    local SAMPLE_FILE=$TMP/$FUNCNAME.$SAMPLE_NAME
    do_facet_random_file client $SAMPLE_FILE 20K    || error_noexit "Create random file $SAMPLE_FILE"

    # With adaptive timeouts, bulk_get won't expire until adaptive_timeout_max
    if at_is_enabled; then
        at_max_saved=$(at_max_get ost1)
        at_max_set $TIMEOUT ost1
    fi

    # OBD_FAIL_PTLRPC_BULK_GET_NET 0x0503 | OBD_FAIL_ONCE
    # OST bulk will time out here, client retries
    do_facet ost1 lctl set_param fail_loc=0x80000503
    # need to ensure we send an RPC
    do_facet client cp $SAMPLE_FILE $DIR/$tfile
    sync

    # with AT, client will wait adaptive_max*factor+net_latency before
    # expiring the req, hopefully timeout*2 is enough
    sleep $(($TIMEOUT*2))

    do_facet ost1 lctl set_param fail_loc=0
    do_facet client "df $DIR"
    # expect cmp to succeed, client resent bulk
    do_facet client "cmp $SAMPLE_FILE $DIR/$tfile" || return 3
    do_facet client "rm $DIR/$tfile" || return 4
    [ $at_max_saved -ne 0 ] && at_max_set $at_max_saved ost1
    return 0
}
run_test 17 "timeout bulk get, don't evict client (2732)"

test_18a() {
    [ -z ${ost2_svc} ] && skip_env "needs 2 osts" && return 0

    local SAMPLE_FILE=$TMP/$FUNCNAME.$SAMPLE_NAME
    do_facet_random_file client $SAMPLE_FILE 20K    || error_noexit "Create random file $SAMPLE_FILE"

    do_facet client mkdir -p $DIR/$tdir
    f=$DIR/$tdir/$tfile

    cancel_lru_locks osc
    pgcache_empty || return 1

    # 1 stripe on ost2
    $LFS setstripe -i 1 -c 1 $f
    stripe_index=$($LFS getstripe -i $f)
    if [ $stripe_index -ne 1 ]; then
        $LFS getstripe $f
        error "$f: stripe_index $stripe_index != 1" && return
    fi

    do_facet client cp $SAMPLE_FILE $f
    sync
    local osc2dev=`lctl get_param -n devices | grep ${ost2_svc}-osc- | egrep -v 'MDT' | awk '{print $1}'`
    $LCTL --device $osc2dev deactivate || return 3
    # my understanding is that there should be nothing in the page
    # cache after the client reconnects?     
    rc=0
    pgcache_empty || rc=2
    $LCTL --device $osc2dev activate
    rm -f $f
    return $rc
}
run_test 18a "manual ost invalidate clears page cache immediately"

test_18b() {
    remote_ost_nodsh && skip "remote OST with nodsh" && return 0

    local SAMPLE_FILE=$TMP/$FUNCNAME.$SAMPLE_NAME
    do_facet_random_file client $SAMPLE_FILE 20K    || error_noexit "Create random file $SAMPLE_FILE"

    do_facet client mkdir -p $DIR/$tdir
    f=$DIR/$tdir/$tfile

    cancel_lru_locks osc
    pgcache_empty || return 1

    $LFS setstripe -i 0 -c 1 $f
    stripe_index=$($LFS getstripe -i $f)
    if [ $stripe_index -ne 0 ]; then
        $LFS getstripe $f
        error "$f: stripe_index $stripe_index != 0" && return
    fi

    do_facet client cp $SAMPLE_FILE $f
    sync
    ost_evict_client
    # allow recovery to complete
    sleep $((TIMEOUT + 2))
    # my understanding is that there should be nothing in the page
    # cache after the client reconnects?     
    rc=0
    pgcache_empty || rc=2
    rm -f $f
    return $rc
}
run_test 18b "eviction and reconnect clears page cache (2766)"

test_18c() {
    remote_ost_nodsh && skip "remote OST with nodsh" && return 0

    local SAMPLE_FILE=$TMP/$FUNCNAME.$SAMPLE_NAME
    do_facet_random_file client $SAMPLE_FILE 20K    || error_noexit "Create random file $SAMPLE_FILE"

    do_facet client mkdir -p $DIR/$tdir
    f=$DIR/$tdir/$tfile

    cancel_lru_locks osc
    pgcache_empty || return 1

    $LFS setstripe -i 0 -c 1 $f
    stripe_index=$($LFS getstripe -i $f)
    if [ $stripe_index -ne 0 ]; then
        $LFS getstripe $f
        error "$f: stripe_index $stripe_index != 0" && return
    fi

    do_facet client cp $SAMPLE_FILE $f
    sync
    ost_evict_client

    # OBD_FAIL_OST_CONNECT_NET2
    # lost reply to connect request
    do_facet ost1 lctl set_param fail_loc=0x80000225
    # force reconnect
    sleep 1
    df $MOUNT > /dev/null 2>&1
    sleep 2
    # my understanding is that there should be nothing in the page
    # cache after the client reconnects?     
    rc=0
    pgcache_empty || rc=2
    rm -f $f
    return $rc
}
run_test 18c "Dropped connect reply after eviction handing (14755)"

test_19a() {
    local BEFORE=`date +%s`

    mount_client $DIR2

    do_facet client mcreate $DIR/$tfile        || return 1
    drop_ldlm_cancel "chmod 0777 $DIR2"

    umount_client $DIR2
    do_facet client "munlink $DIR/$tfile"

    # let the client reconnect
    sleep 5
    EVICT=`do_facet client $LCTL get_param mdc.$FSNAME-MDT*.state | \
        awk -F"[ [,]" '/EVICTED]$/ { if (mx<$4) {mx=$4;} } END { print mx }'`

    [ ! -z "$EVICT" ] && [[ $EVICT -gt $BEFORE ]] || error "no eviction"
}
run_test 19a "test expired_lock_main on mds (2867)"

test_19b() {
    local BEFORE=`date +%s`

    mount_client $DIR2

    do_facet client multiop $DIR/$tfile Ow  || return 1
    drop_ldlm_cancel multiop $DIR2/$tfile Ow
    umount_client $DIR2
    do_facet client munlink $DIR/$tfile  || return 4

    # let the client reconnect
    sleep 5
    EVICT=`do_facet client $LCTL get_param osc.$FSNAME-OST*.state | \
        awk -F"[ [,]" '/EVICTED]$/ { if (mx<$4) {mx=$4;} } END { print mx }'`

    [ ! -z "$EVICT" ] && [[ $EVICT -gt $BEFORE ]] || error "no eviction"
}
run_test 19b "test expired_lock_main on ost (2867)"

test_19c() {
    local BEFORE=`date +%s`

    mount_client $DIR2
    $LCTL set_param ldlm.namespaces.*.early_lock_cancel=0

    mkdir -p $DIR1/$tfile
    stat $DIR1/$tfile

#define OBD_FAIL_PTLRPC_CANCEL_RESEND 0x516
    do_facet mds $LCTL set_param fail_loc=0x80000516

    touch $DIR2/$tfile/file1 &
    PID1=$!
    # let touch to get blocked on the server
    sleep 2

    wait $PID1
    $LCTL set_param ldlm.namespaces.*.early_lock_cancel=1
    umount_client $DIR2

    # let the client reconnect
    sleep 5
    EVICT=`do_facet client $LCTL get_param mdc.$FSNAME-MDT*.state | \
        awk -F"[ [,]" '/EVICTED]$/ { if (mx<$4) {mx=$4;} } END { print mx }'`

    [ -z "$EVICT" ] || [[ $EVICT -le $BEFORE ]] || error "eviction happened"
}
run_test 19c "check reconnect and lock resend do not trigger expired_lock_main"

test_20a() {	# bug 2983 - ldlm_handle_enqueue cleanup
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

	mkdir -p $DIR/$tdir
	$LFS setstripe -i 0 -c 1 $DIR/$tdir/${tfile}
	multiop_bg_pause $DIR/$tdir/${tfile} O_wc || return 1
	MULTI_PID=$!
	cancel_lru_locks osc
#define OBD_FAIL_LDLM_ENQUEUE_EXTENT_ERR 0x308
	do_facet ost1 lctl set_param fail_loc=0x80000308
	kill -USR1 $MULTI_PID
	wait $MULTI_PID
	rc=$?
	[ $rc -eq 0 ] && error "multiop didn't fail enqueue: rc $rc" || true
}
run_test 20a "ldlm_handle_enqueue error (should return error)" 

test_20b() {	# bug 2986 - ldlm_handle_enqueue error during open
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

	mkdir -p $DIR/$tdir
	$LFS setstripe -i 0 -c 1 $DIR/$tdir/${tfile}
	cancel_lru_locks osc
#define OBD_FAIL_LDLM_ENQUEUE_EXTENT_ERR 0x308
	do_facet ost1 lctl set_param fail_loc=0x80000308
	dd if=/etc/hosts of=$DIR/$tdir/$tfile && \
		error "didn't fail open enqueue" || true
}
run_test 20b "ldlm_handle_enqueue error (should return error)"

test_21a() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       close_pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000129"
       multiop $DIR/$tdir-2/f Oc &
       open_pid=$!
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000115"
       kill -USR1 $close_pid
       cancel_lru_locks mdc
       wait $close_pid || return 1
       wait $open_pid || return 2
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 3
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 4

       rm -rf $DIR/$tdir-*
}
run_test 21a "drop close request while close and open are both in flight"

test_21b() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       close_pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000107"
       mcreate $DIR/$tdir-2/f &
       open_pid=$!
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       kill -USR1 $close_pid
       cancel_lru_locks mdc
       wait $close_pid || return 1
       wait $open_pid || return 3

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 4
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 5
       rm -rf $DIR/$tdir-*
}
run_test 21b "drop open request while close and open are both in flight"

test_21c() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       close_pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000107"
       mcreate $DIR/$tdir-2/f &
       open_pid=$!
       sleep 3
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000115"
       kill -USR1 $close_pid
       cancel_lru_locks mdc
       wait $close_pid || return 1
       wait $open_pid || return 2

       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 2
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 3
       rm -rf $DIR/$tdir-*
}
run_test 21c "drop both request while close and open are both in flight"

test_21d() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000129"
       multiop $DIR/$tdir-2/f Oc &
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000122"
       kill -USR1 $pid
       cancel_lru_locks mdc
       wait $pid || return 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 2
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 3

       rm -rf $DIR/$tdir-*
}
run_test 21d "drop close reply while close and open are both in flight"

test_21e() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000119"
       touch $DIR/$tdir-2/f &
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       kill -USR1 $pid
       cancel_lru_locks mdc
       wait $pid || return 1

       sleep $TIMEOUT
       $CHECKSTAT -t file $DIR/$tdir-1/f || return 2
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 3
       rm -rf $DIR/$tdir-*
}
run_test 21e "drop open reply while close and open are both in flight"

test_21f() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000119"
       touch $DIR/$tdir-2/f &
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000122"
       kill -USR1 $pid
       cancel_lru_locks mdc
       wait $pid || return 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 2
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 3
       rm -rf $DIR/$tdir-*
}
run_test 21f "drop both reply while close and open are both in flight"

test_21g() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000119"
       touch $DIR/$tdir-2/f &
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000115"
       kill -USR1 $pid
       cancel_lru_locks mdc
       wait $pid || return 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 2
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 3
       rm -rf $DIR/$tdir-*
}
run_test 21g "drop open reply and close request while close and open are both in flight"

test_21h() {
       mkdir -p $DIR/$tdir-1
       mkdir -p $DIR/$tdir-2
       multiop_bg_pause $DIR/$tdir-1/f O_c || return 1
       pid=$!

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000107"
       touch $DIR/$tdir-2/f &
       touch_pid=$!
       sleep 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000122"
       cancel_lru_locks mdc
       kill -USR1 $pid
       wait $pid || return 1
       do_facet $SINGLEMDS "lctl set_param fail_loc=0"

       wait $touch_pid || return 2

       $CHECKSTAT -t file $DIR/$tdir-1/f || return 3
       $CHECKSTAT -t file $DIR/$tdir-2/f || return 4
       rm -rf $DIR/$tdir-*
}
run_test 21h "drop open request and close reply while close and open are both in flight"

# bug 3462 - multiple MDC requests
test_22() {
    f1=$DIR/${tfile}-1
    f2=$DIR/${tfile}-2
    
    do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000115"
    multiop $f2 Oc &
    close_pid=$!

    sleep 1
    multiop $f1 msu || return 1

    cancel_lru_locks mdc
    do_facet $SINGLEMDS "lctl set_param fail_loc=0"

    wait $close_pid || return 2
    rm -rf $f2 || return 4
}
run_test 22 "drop close request and do mknod"

test_23() { #b=4561
    multiop_bg_pause $DIR/$tfile O_c || return 1
    pid=$!
    # give a chance for open
    sleep 5

    # try the close
    drop_request "kill -USR1 $pid"

    fail $SINGLEMDS
    wait $pid || return 1
    return 0
}
run_test 23 "client hang when close a file after mds crash"

test_24() { # bug 11710 details correct fsync() behavior
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

	mkdir -p $DIR/$tdir
	$LFS setstripe -i 0 -c 1 $DIR/$tdir
	cancel_lru_locks osc
	multiop_bg_pause $DIR/$tdir/$tfile Owy_wyc || return 1
	MULTI_PID=$!
	ost_evict_client
	kill -USR1 $MULTI_PID
	wait $MULTI_PID
	rc=$?
	lctl set_param fail_loc=0x0
	client_reconnect
	[ $rc -eq 0 ] && error_ignore 5494 "multiop didn't fail fsync: rc $rc" || true
}
run_test 24 "fsync error (should return error)"

wait_client_evicted () {
	local facet=$1
	local exports=$2
	local varsvc=${facet}_svc

	wait_update $(facet_active_host $facet) \
                "lctl get_param -n *.${!varsvc}.num_exports | cut -d' ' -f2" \
                $((exports - 1)) $3
}

test_26a() {      # was test_26 bug 5921 - evict dead exports by pinger
# this test can only run from a client on a separate node.
	remote_ost || { skip "local OST" && return 0; }
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0
	remote_mds || { skip "local MDS" && return 0; }

        if [ $(facet_host mgs) = $(facet_host ost1) ]; then
                skip "msg and ost1 are at the same node"
                return 0
        fi

	check_timeout || return 1

	local OST_NEXP=$(do_facet ost1 lctl get_param -n obdfilter.${ost1_svc}.num_exports | cut -d' ' -f2)

	echo starting with $OST_NEXP OST exports
# OBD_FAIL_PTLRPC_DROP_RPC 0x505
	do_facet client lctl set_param fail_loc=0x505
        # evictor takes PING_EVICT_TIMEOUT + 3 * PING_INTERVAL to evict.
        # But if there's a race to start the evictor from various obds,
        # the loser might have to wait for the next ping.

	local rc=0
	wait_client_evicted ost1 $OST_NEXP $((TIMEOUT * 2 + TIMEOUT * 3 / 4))
	rc=$?
	do_facet client lctl set_param fail_loc=0x0
        [ $rc -eq 0 ] || error "client not evicted from OST"
}
run_test 26a "evict dead exports"

test_26b() {      # bug 10140 - evict dead exports by pinger
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

        if [ $(facet_host mgs) = $(facet_host ost1) ]; then
                skip "msg and ost1 are at the same node"
                return 0
        fi

	check_timeout || return 1
	clients_up
	zconf_mount `hostname` $MOUNT2 ||
                { error "Failed to mount $MOUNT2"; return 2; }
	sleep 1 # wait connections being established

	local MDS_NEXP=$(do_facet $SINGLEMDS lctl get_param -n mdt.${mds1_svc}.num_exports | cut -d' ' -f2)
	local OST_NEXP=$(do_facet ost1 lctl get_param -n obdfilter.${ost1_svc}.num_exports | cut -d' ' -f2)

	echo starting with $OST_NEXP OST and $MDS_NEXP MDS exports

	zconf_umount `hostname` $MOUNT2 -f

	# PING_INTERVAL max(obd_timeout / 4, 1U)
	# PING_EVICT_TIMEOUT (PING_INTERVAL * 6)

	# evictor takes PING_EVICT_TIMEOUT + 3 * PING_INTERVAL to evict.  
	# But if there's a race to start the evictor from various obds, 
	# the loser might have to wait for the next ping.
	# = 9 * PING_INTERVAL + PING_INTERVAL
	# = 10 PING_INTERVAL = 10 obd_timeout / 4 = 2.5 obd_timeout
	# let's wait $((TIMEOUT * 3)) # bug 19887
	local rc=0
	wait_client_evicted ost1 $OST_NEXP $((TIMEOUT * 3)) || \
		error "Client was not evicted by ost" rc=1
	wait_client_evicted $SINGLEMDS $MDS_NEXP $((TIMEOUT * 3)) || \
		error "Client was not evicted by mds"
}
run_test 26b "evict dead exports"

test_27() {
	mkdir -p $DIR/$tdir
	writemany -q -a $DIR/$tdir/$tfile 0 5 &
	CLIENT_PID=$!
	sleep 1
	local save_FAILURE_MODE=$FAILURE_MODE
	FAILURE_MODE="SOFT"
	facet_failover $SINGLEMDS
#define OBD_FAIL_OSC_SHUTDOWN            0x407
	do_facet $SINGLEMDS lctl set_param fail_loc=0x80000407
	# need to wait for reconnect
	echo waiting for fail_loc
	wait_update_facet $SINGLEMDS "lctl get_param -n fail_loc" "-2147482617"
	facet_failover $SINGLEMDS
	#no crashes allowed!
        kill -USR1 $CLIENT_PID
	wait $CLIENT_PID 
	true
	FAILURE_MODE=$save_FAILURE_MODE
}
run_test 27 "fail LOV while using OSC's"

test_28() {      # bug 6086 - error adding new clients
	do_facet client mcreate $DIR/$tfile       || return 1
	drop_bl_callback "chmod 0777 $DIR/$tfile" ||echo "evicted as expected"
	#define OBD_FAIL_MDS_CLIENT_ADD 0x12f
	do_facet $SINGLEMDS "lctl set_param fail_loc=0x8000012f"
	# fail once (evicted), reconnect fail (fail_loc), ok
	client_up || (sleep 10; client_up) || (sleep 10; client_up) || error "reconnect failed"
	rm -f $DIR/$tfile
	fail $SINGLEMDS		# verify MDS last_rcvd can be loaded
}
run_test 28 "handle error adding new clients (bug 6086)"

test_29a() { # bug 22273 - error adding new clients
	#define OBD_FAIL_TGT_CLIENT_ADD 0x711
	do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000711"
	# fail abort so client will be new again
	fail_abort $SINGLEMDS
	client_up || error "reconnect failed"
	return 0
}
run_test 29a "error adding new clients doesn't cause LBUG (bug 22273)"

test_29b() { # bug 22273 - error adding new clients
	#define OBD_FAIL_TGT_CLIENT_ADD 0x711
	do_facet ost1 "lctl set_param fail_loc=0x80000711"
	# fail abort so client will be new again
	fail_abort ost1
	client_up || error "reconnect failed"
	return 0
}
run_test 29b "error adding new clients doesn't cause LBUG (bug 22273)"

test_50() {
	mkdir -p $DIR/$tdir
	# put a load of file creates/writes/deletes
	writemany -q $DIR/$tdir/$tfile 0 5 &
	CLIENT_PID=$!
	echo writemany pid $CLIENT_PID
	sleep 10
	FAILURE_MODE="SOFT"
	fail $SINGLEMDS
	# wait for client to reconnect to MDS
	sleep 60
	fail $SINGLEMDS
	sleep 60
	fail $SINGLEMDS
	# client process should see no problems even though MDS went down
	sleep $TIMEOUT
        kill -USR1 $CLIENT_PID
	wait $CLIENT_PID 
	rc=$?
	echo writemany returned $rc
	#these may fail because of eviction due to slow AST response.
	[ $rc -eq 0 ] || error_ignore 13652 "writemany returned rc $rc" || true
}
run_test 50 "failover MDS under load"

test_51() {
	#define OBD_FAIL_MDS_SYNC_CAPA_SL                    0x1310
	do_facet ost1 lctl set_param fail_loc=0x00001310

	mkdir -p $DIR/$tdir
	# put a load of file creates/writes/deletes
	writemany -q $DIR/$tdir/$tfile 0 5 &
	CLIENT_PID=$!
	sleep 1
	FAILURE_MODE="SOFT"
	facet_failover $SINGLEMDS
	# failover at various points during recovery
	SEQ="1 5 10 $(seq $TIMEOUT 5 $(($TIMEOUT+10)))"
        echo will failover at $SEQ
        for i in $SEQ
          do
          echo failover in $i sec
          sleep $i
          facet_failover $SINGLEMDS
        done
	# client process should see no problems even though MDS went down
	# and recovery was interrupted
	sleep $TIMEOUT
        kill -USR1 $CLIENT_PID
	wait $CLIENT_PID 
	rc=$?
	echo writemany returned $rc
	[ $rc -eq 0 ] || error_ignore 13652 "writemany returned rc $rc" || true
}
run_test 51 "failover MDS during recovery"

test_52_guts() {
	do_facet client "mkdir -p $DIR/$tdir"
	do_facet client "writemany -q -a $DIR/$tdir/$tfile 300 5" &
	CLIENT_PID=$!
	echo writemany pid $CLIENT_PID
	sleep 10
	FAILURE_MODE="SOFT"
	fail ost1
	rc=0
	wait $CLIENT_PID || rc=$?
	# active client process should see an EIO for down OST
	[ $rc -eq 5 ] && { echo "writemany correctly failed $rc" && return 0; }
	# but timing or failover setup may allow success
	[ $rc -eq 0 ] && { echo "writemany succeeded" && return 0; }
	echo "writemany returned $rc"
	return $rc
}

test_52() {
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

	mkdir -p $DIR/$tdir
	test_52_guts
	rc=$?
	[ $rc -ne 0 ] && { return $rc; }
	# wait for client to reconnect to OST
	sleep 30
	test_52_guts
	rc=$?
	[ $rc -ne 0 ] && { return $rc; }
	sleep 30
	test_52_guts
	rc=$?
	client_reconnect
	#return $rc
}
run_test 52 "failover OST under load"

# test of open reconstruct
test_53() {
	touch $DIR/$tfile
	drop_ldlm_reply "openfile -f O_RDWR:O_CREAT -m 0755 $DIR/$tfile" ||\
		return 2
}
run_test 53 "touch: drop rep"

test_54() {
	zconf_mount `hostname` $MOUNT2
        touch $DIR/$tfile
        touch $DIR2/$tfile.1
        sleep 10
        cat $DIR2/$tfile.missing # save transno = 0, rc != 0 into last_rcvd
        fail $SINGLEMDS
        umount $MOUNT2
        ERROR=`dmesg | egrep "(test 54|went back in time)" | tail -n1 | grep "went back in time"`
        [ x"$ERROR" == x ] || error "back in time occured"
}
run_test 54 "back in time"

# bug 11330 - liblustre application death during I/O locks up OST
test_55() {
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

	mkdir -p $DIR/$tdir

	# first dd should be finished quickly
	$LFS setstripe -c 1 -i 0 $DIR/$tdir/$tfile-1
	dd if=/dev/zero of=$DIR/$tdir/$tfile-1 bs=32M count=4  &
	DDPID=$!
	count=0
	echo  "step1: testing ......"
	while [ true ]; do
	    if [ -z `ps x | awk '$1 == '$DDPID' { print $5 }'` ]; then break; fi
	    count=$[count+1]
	    if [ $count -gt 64 ]; then
		error "dd should be finished!"
	    fi
	    sleep 1
	done	
	echo "(dd_pid=$DDPID, time=$count)successful"

	$LFS setstripe -c 1 -i 0 $DIR/$tdir/$tfile-2
	#define OBD_FAIL_OST_DROP_REQ            0x21d
	do_facet ost1 lctl set_param fail_loc=0x0000021d
	# second dd will be never finished
	dd if=/dev/zero of=$DIR/$tdir/$tfile-2 bs=32M count=4  &	
	DDPID=$!
	count=0
	echo  "step2: testing ......"
	while [ $count -le 64 ]; do
	    dd_name="`ps x | awk '$1 == '$DDPID' { print $5 }'`"	    
	    if [ -z  $dd_name ]; then 
                ls -l $DIR/$tdir
		echo  "debug: (dd_name=$dd_name, dd_pid=$DDPID, time=$count)"
		error "dd shouldn't be finished!"
	    fi
	    count=$[count+1]
	    sleep 1
	done	
	echo "(dd_pid=$DDPID, time=$count)successful"

	#Recover fail_loc and dd will finish soon
	do_facet ost1 lctl set_param fail_loc=0
	count=0
	echo  "step3: testing ......"
	while [ true ]; do
	    if [ -z `ps x | awk '$1 == '$DDPID' { print $5 }'` ]; then break; fi
	    count=$[count+1]
	    if [ $count -gt 500 ]; then
		error "dd should be finished!"
	    fi
	    sleep 1
	done	
	echo "(dd_pid=$DDPID, time=$count)successful"

        rm -rf $DIR/$tdir
}
run_test 55 "ost_brw_read/write drops timed-out read/write request"

test_56() { # b=11277
#define OBD_FAIL_MDS_RESEND      0x136
        touch $DIR/$tfile
        do_facet $SINGLEMDS "lctl set_param fail_loc=0x80000136"
        stat $DIR/$tfile
        do_facet $SINGLEMDS "lctl set_param fail_loc=0"
        rm -f $DIR/$tfile
}
run_test 56 "do not allow reconnect to busy exports"

test_57_helper() {
        # no oscs means no client or mdt 
        while lctl get_param osc.*.* > /dev/null 2>&1; do
                : # loop until proc file is removed
        done
}

test_57() { # bug 10866
        test_57_helper &
        pid=$!
        sleep 1
#define OBD_FAIL_LPROC_REMOVE            0xB00
        lctl set_param fail_loc=0x80000B00
        zconf_umount `hostname` $DIR
        lctl set_param fail_loc=0x80000B00
        fail_abort $SINGLEMDS
        kill -9 $pid
        lctl set_param fail_loc=0
        mount_client $DIR
        do_facet client "df $DIR"
}
run_test 57 "read procfs entries causes kernel crash"

test_58() { # bug 11546
#define OBD_FAIL_MDC_ENQUEUE_PAUSE        0x801
        touch $DIR/$tfile
        ls -la $DIR/$tfile
        lctl set_param fail_loc=0x80000801
        cp $DIR/$tfile /dev/null &
        pid=$!
        sleep 1
        lctl set_param fail_loc=0
        drop_bl_callback rm -f $DIR/$tfile
        wait $pid
        do_facet client "df $DIR"
}
run_test 58 "Eviction in the middle of open RPC reply processing"

test_59() { # bug 10589
	zconf_mount `hostname` $MOUNT2 || error "Failed to mount $MOUNT2"
	echo $DIR2 | grep -q $MOUNT2 || error "DIR2 is not set properly: $DIR2"
#define OBD_FAIL_LDLM_CANCEL_EVICT_RACE  0x311
	lctl set_param fail_loc=0x311
	writes=$(LANG=C dd if=/dev/zero of=$DIR2/$tfile count=1 2>&1)
	[ $? = 0 ] || error "dd write failed"
	writes=$(echo $writes | awk  -F '+' '/out/ {print $1}')
	lctl set_param fail_loc=0
	sync
	zconf_umount `hostname` $MOUNT2 -f
	reads=$(LANG=C dd if=$DIR/$tfile of=/dev/null 2>&1)
	[ $? = 0 ] || error "dd read failed"
	reads=$(echo $reads | awk -F '+' '/in/ {print $1}')
	[ "$reads" -eq "$writes" ] || error "read" $reads "blocks, must be" $writes
}
run_test 59 "Read cancel race on client eviction"

err17935 () {
    # we assume that all md changes are in the MDT0 changelog
    if [ $MDSCOUNT -gt 1 ]; then
	error_ignore 17935 $*
    else
	error $*
    fi
}

test_60() {
        MDT0=$($LCTL get_param -n mdc.*.mds_server_uuid | \
	    awk '{gsub(/_UUID/,""); print $1}' | head -1)

	NUM_FILES=15000
	mkdir -p $DIR/$tdir

	# Register (and start) changelog
	USER=$(do_facet $SINGLEMDS lctl --device $MDT0 changelog_register -n)
	echo "Registered as $MDT0 changelog user $USER"

	# Generate a large number of changelog entries
	createmany -o $DIR/$tdir/$tfile $NUM_FILES
	sync
	sleep 5

	# Unlink files in the background
	unlinkmany $DIR/$tdir/$tfile $NUM_FILES	&
	CLIENT_PID=$!
	sleep 1

	# Failover the MDS while unlinks are happening
	facet_failover $SINGLEMDS

	# Wait for unlinkmany to finish
	wait $CLIENT_PID

	# Check if all the create/unlink events were recorded
	# in the changelog
	$LFS changelog $MDT0 >> $DIR/$tdir/changelog
	local cl_count=$(grep UNLNK $DIR/$tdir/changelog | wc -l)
	echo "$cl_count unlinks in $MDT0 changelog"

	do_facet $SINGLEMDS lctl --device $MDT0 changelog_deregister $USER
	USERS=$(( $(do_facet $SINGLEMDS lctl get_param -n \
	    mdd.$MDT0.changelog_users | wc -l) - 2 ))
	if [ $USERS -eq 0 ]; then
	    [ $cl_count -eq $NUM_FILES ] || \
		err17935 "Recorded ${cl_count} unlinks out of $NUM_FILES"
	    # Also make sure we can clear large changelogs
	    cl_count=$($LFS changelog $FSNAME | wc -l)
	    [ $cl_count -le 2 ] || \
		error "Changelog not empty: $cl_count entries"
	else
	    # If there are other users, there may be other unlinks in the log
	    [ $cl_count -ge $NUM_FILES ] || \
		err17935 "Recorded ${cl_count} unlinks out of $NUM_FILES"
	    echo "$USERS other changelog users; can't verify clear"
	fi
}
run_test 60 "Add Changelog entries during MDS failover"

test_61()
{
	local mdtosc=$(get_mdtosc_proc_path $SINGLEMDS $FSNAME-OST0000)
	mdtosc=${mdtosc/-MDT*/-MDT\*}
	local cflags="osc.$mdtosc.connect_flags"
	do_facet $SINGLEMDS "lctl get_param -n $cflags" |grep -q skip_orphan
	[ $? -ne 0 ] && skip "don't have skip orphan feature" && return

	mkdir -p $DIR/$tdir || error "mkdir dir $DIR/$tdir failed"
	# Set the default stripe of $DIR/$tdir to put the files to ost1
	$LFS setstripe -c 1 -i 0 $DIR/$tdir

	replay_barrier $SINGLEMDS
	createmany -o $DIR/$tdir/$tfile-%d 10 
	local oid=`do_facet ost1 "lctl get_param -n obdfilter.${ost1_svc}.last_id"`

	fail_abort $SINGLEMDS
	
	touch $DIR/$tdir/$tfile
	local id=`$LFS getstripe $DIR/$tdir/$tfile | awk '$1 == 0 { print $2 }'`
	[ $id -le $oid ] && error "the orphan objid was reused, failed"

	# Cleanup
	rm -rf $DIR/$tdir
}
run_test 61 "Verify to not reuse orphan objects - bug 17025"

# LU-793
test_112a() {
	remote_ost_nodsh && skip "remote OST with nodsh" && return 0

	local t1=${tfile}.1
	local t2=${tfile}.2
	do_facet_random_file client $TMP/$tfile 100K ||
		error_noexit "Create random file $TMP/$tfile"
	pause_bulk_long "cp $TMP/$tfile $DIR/$tfile" ||
		error_noexit "Can't pause_bulk copy"
	do_facet client "cmp $TMP/$tfile $DIR/$tfile" ||
		error_noexit "Wrong data has being written"
	do_facet client "rm $DIR/$tfile" ||
		error_noexit "Can't remove file"
	do_facet client "rm $TMP/$tfile"
}
run_test 112a "bulk resend while orignal request is in progress"

complete $(basename $0) $SECONDS
check_and_cleanup_lustre
exit_status
