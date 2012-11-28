#!/bin/bash

echo "Checking for a complete tree..."
if [ -d kernel_patches ] ; then
    # This is ldiskfs
    REQUIRED_DIRS="build"
    CONFIGURE_DIRS=""
else
    REQUIRED_DIRS="build libcfs lnet lustre"
    OPTIONAL_DIRS="snmp portals"
    CONFIGURE_DIRS="libsysio lustre-iokit ldiskfs"
fi

for dir in $REQUIRED_DIRS ; do
    if [ ! -d "$dir" ] ; then
	cat >&2 <<EOF
Your tree seems to be missing $dir.
Please read README.lustrecvs for details.
EOF
	exit 1
    fi
    ACLOCAL_FLAGS="$ACLOCAL_FLAGS -I $PWD/$dir/autoconf"
done
# optional directories for Lustre
for dir in $OPTIONAL_DIRS; do
    if [ -d "$dir" ] ; then
	ACLOCAL_FLAGS="$ACLOCAL_FLAGS -I $PWD/$dir/autoconf"
    fi
done

run_cmd()
{
    cmd="$@"
    echo -n "Running $cmd"
    eval $cmd
    res=$?
    if [ $res -ne 0 ]; then
        echo " failed: $res"
        echo "Aborting"
        exit 1
    fi
    echo
}

run_cmd "aclocal $ACLOCAL_FLAGS"
run_cmd "autoheader"
run_cmd "automake -a -c"
run_cmd autoconf

# Run autogen.sh in these directories
for dir in $CONFIGURE_DIRS; do
    if [ -d $dir ] ; then
        pushd $dir >/dev/null
        echo "Running autogen for $dir..."
        run_cmd "sh autogen.sh"
        popd >/dev/null
    fi
done
