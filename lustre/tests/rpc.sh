#!/bin/bash
LUSTRE=${LUSTRE:-$(cd $(dirname $BASH_SOURCE)/..; echo $PWD)}
. $LUSTRE/tests/test-framework.sh
init_test_env
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

cmd=$1
shift
$cmd $@

exit $?
