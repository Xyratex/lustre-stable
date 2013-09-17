#!/bin/bash
#set -x

MAX_FILES=${MAX_FILES:-20}
DIR=${DIR:-$1}
DIR=${DIR:-"/mnt/lustre/racer"}
DURATION=${DURATION:-$((60*5))}

NUM_THREADS=${NUM_THREADS:-$2}
NUM_THREADS=${NUM_THREADS:-3}

mkdir -p $DIR

RACER_PROGS="file_create dir_create file_rm file_rename file_link file_symlink \
file_list file_concat file_exec file_truncate"

racer_cleanup()
{
	echo "racer cleanup"
	for P in $RACER_PROGS; do
		killall $P.sh
	done
	trap 0

	local TOT_WAIT=0
	local MAX_WAIT=$DURATION
	local SHORT_WAIT=5

	local rc
	while [[ $TOT_WAIT -le $MAX_WAIT ]]; do
		rc=0
		echo sleeping $SHORT_WAIT sec ...
		sleep $SHORT_WAIT
		# this only checks whether processes exist
		for P in $RACER_PROGS; do
			killall -0 $P.sh
			[[ $? -eq 0 ]] && (( rc+=1 ))
		done
		if [[ $rc -eq 0 ]]; then
			echo there should be NO racer processes:
			ps aux | grep -E "${RACER_PROGS// /|}"
			return 0
		fi
		echo -n "Waited $(( TOT_WAIT + SHORT_WAIT)), rc=$rc "
		(( SHORT_WAIT+=SHORT_WAIT ))
		(( TOT_WAIT+=SHORT_WAIT ))
	done
	ps aux | grep -E "${RACER_PROGS// /|}"
	return 1
}

RC=0

echo "Running $0 for $DURATION seconds. CTRL-C to exit"
trap "
	echo \"Cleaning up\" 
	racer_cleanup
	exit 0
" INT TERM

cd `dirname $0`
for N in `seq 1 $NUM_THREADS`; do
	for P in $RACER_PROGS; do
		./$P.sh $DIR $MAX_FILES &
	done
done

sleep $DURATION
racer_cleanup || RC=$?

# Check our to see whether our test DIR is still available.
df $DIR
(( RC+=$? ))
if [ $RC -eq 0 ]; then
    echo "We survived $0 for $DURATION seconds."
fi
exit $RC
