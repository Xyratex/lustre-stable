#!/bin/bash
#set -x

LUSTRE=${LUSTRE:-`dirname $0`/..}
. $LUSTRE/tests/test-framework.sh
init_test_env $@

nobjhi=${nobjhi:-1}
thrhi=${thrhi:-16} 
size=${size:-1024}

# the summary file a bit smaller than OSTSIZE  
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

[ "$SLOW" = no ] && { nobjhi=1; thrhi=4; }
thrlo=${thrlo:-$(( thrhi / 2))}

# Skip these tests
ALWAYS_EXCEPT="$OBDFILTER_SURVEY_EXCEPT"

OBDSURVEY=${OBDSURVEY:-$(which obdfilter-survey)}

build_test_filter
check_and_setup_lustre

# FIXME: the summary file a bit smaller than OSTSIZE, add estimation
minsize=$(min_ost_size)
if [ $(( size * 1024 )) -ge $minsize  ]; then
    size=$((minsize * 10 / 1024 / 12 ))
    echo min kbytesavail: $minsize using size=${size} MBytes per obd instance
fi

get_targets () {
        local targets
        local devs
        local oss

        for oss in $(osts_nodes); do
                devs=$(do_node $oss "lctl dl |grep obdfilter |sort" | awk '{print $4}')
                for d in $devs; do
                        # if oss is local -- obdfilter-survey needs dev wo/ host
                        target=$d
                        [[ $oss = `hostname` ]] || target=$oss:$target
                        targets="$targets $target"
                done
        done

	echo $targets
}

obdflter_survey_targets () {
	local case=$1
	local targets

	case $case in
		disk)    targets=$(get_targets);;
		netdisk) targets="";;
		# use ost1 host as a default target for case=network
		network) targets=$(facet_host ost1);;
		*) error "unknown obdflter-survey case!" ;;
	esac
	echo $targets
}

obdflter_survey_run () {
	local case=$1
	local targets=${2:-$(obdflter_survey_targets $case)}
	local rc=0

	rm -f ${TMP}/obdfilter_survey*

	local cmd="NETTYPE=$NETTYPE thrlo=$thrlo thrhi=$thrhi nobjhi=$nobjhi size=$size case=$case rslt_loc=${TMP} targets=\"$targets\" $OBDSURVEY"
	echo + $cmd
	eval $cmd

	rc=${PIPESTATUS[0]}
	cat ${TMP}/obdfilter_survey*
	return $rc
}

test_1a () {
	obdflter_survey_run disk
}
run_test 1a "Object Storage Targets survey"

print_jbd () {
	local file=$1
	local facet=$2
	local varsvc=${facet}_svc
	local dev=$(ldiskfs_canon "*.${!varsvc}.mntdev" $facet)

	# ext4: /proc/fs/jbd2/sda1:8/history 
	# ext3: /proc/fs/jbd/sdb1/history

	do_facet $facet cat /proc/fs/jbd*/${dev}*/$file
}

check_jbd_values () {
	local facet=$1
	local thrhi=$2

	# last two lines from history
	# $4: run >= 5000
	# $8: hndls >= thrhi * 2
	local hist=("$(print_jbd history $facet | tail -3 | head -2)")
	echo "$hist"
	local run=($(echo "${hist[*]}" | awk '{print $4}'))
	local hndls=($(echo "${hist[*]}" | awk '{print $8}'))

	local rc=0
	for (( i=0; i<1; i++)); do
		[[ ${run[i]} -lt 5000 ]] && \
			error "$facet: run expected 5000, have ${run[i]}" && rc=1
		[[ ${hndls[i]} -lt $((thrhi * 2)) ]] && \
			error "$facet: hndls expected > $((thrhi * 2)), have ${hndls[i]}" && rc=2
	done
	return $rc
}

check_jbd_values_facets () {
	local facets=$1
	local thrhi=$2
	local facet
	local rc=0
	for facet in  ${facets//,/ }; do
		check_jbd_values $facet $thrhi || rc=$((rc+$?))
	done
	return $rc
}

test_1b () {
	local major=$(get_mds_kernel_major)
	local minor=$(get_mds_kernel_minor)
	[ "$major"=="2.6" -a $minor -eq 32 ]  && \
		skip "There is no jbd history in this kernel version." && return
	local param_file=$TMP/$tfile-params

	do_nodesv $(comma_list $(osts_nodes)) lctl get_param obdfilter.${FSNAME}-*.sync_journal

	save_lustre_params $(comma_list $(osts_nodes)) "obdfilter.${FSNAME}-*.sync_journal" >$param_file
	do_nodesv $(comma_list $(osts_nodes)) lctl set_param obdfilter.${FSNAME}-*.sync_journal=0

	local rc=0
	thrlo=4 nobjhi=1 thrhi=4 obdflter_survey_run disk
	rc=$?
	[[ $rc -eq 0 ]] || return $rc

	check_jbd_values_facets $(get_facets OST) 4 || rc=$((rc+$?))

	restore_lustre_params < $param_file

	rm -f $param_file
	return $rc
}
run_test 1b "Object Storage Targets survey, async journal"

test_2a () {
	obdflter_survey_run netdisk
}
run_test 2a "Stripe F/S over the Network"

test_2b () {
	local major=$(get_mds_kernel_major)
	local minor=$(get_mds_kernel_minor)
	[ "$major"=="2.6" -a $minor -eq 32 ]  && \
		skip "There is no jbd history in this kernel version." && return
	local param_file=$TMP/$tfile-params

	do_nodesv $(comma_list $(osts_nodes)) lctl get_param obdfilter.${FSNAME}-*.sync_journal

	save_lustre_params $(comma_list $(osts_nodes)) "obdfilter.${FSNAME}-*.sync_journal" >$param_file
	do_nodesv $(comma_list $(osts_nodes)) lctl set_param obdfilter.${FSNAME}-*.sync_journal=0

	local rc=0
	thrlo=4 nobjhi=1 thrhi=4 obdflter_survey_run netdisk
	rc=$?
	[[ $rc -eq 0 ]] || return $rc

	check_jbd_values_facets $(get_facets OST) 4 || rc=$((rc+$?))

	restore_lustre_params < $param_file

	rm -f $param_file
	return $rc
}
run_test 2b "Stripe F/S over the Network, async journal"


# README.obdfilter-survey: In network test only automated run is supported.
test_3a () {
	remote_servers || { skip "Local servers" && return 0; }

	# cleanup all, because obdfilter=survey case=network test requires
	# the empty device list on the client node
	cleanupall

	# run the test for each oss
	local oss
	rc=0
	for oss in $(osts_nodes); do
		obdflter_survey_run network $oss || rc=$((rc + $?))
	done
	setupall
	return $rc
}
run_test 3a "Network survey"

complete $(basename $0) $SECONDS
cleanup_echo_devs
check_and_cleanup_lustre
exit_status
