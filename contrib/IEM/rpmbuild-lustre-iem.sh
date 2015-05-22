#!/bin/bash
# build results are in dist dir under current dir
# rpm and csv for documentation

currentDir=`pwd`
rpmTopDir=$(rpm --eval %{_topdir})

mkdir -p ${rpmTopDir}/{SOURCES,SRPMS,BUILD,SPECS,RPMS}

#prepare tgz for spec
# cd lustre.git/contrib/IEM
# git archive --format=tar HEAD . | gzip > ${rpmTopDir}/SOURCES/lustre-iem.tgz
tar czf ${rpmTopDir}/SOURCES/lustre-iem.tgz .

#build rpm
rpmbuild -ba lustre_iem.spec

if [ $? -ne 0 ] ; then
   echo "$0: rpm build failed."
   exit 2
fi

rm -rf ${currentDir}/dist
mkdir -p ${currentDir}/dist

#create csv for documentation
sed -n -e "/<csv>/,/<\/csv>/p" ${currentDir}/lustre_iem.pdb \
|  sed -e "/<csv>/d" -e "/<\/csv>/d" \
> ${currentDir}/dist/lustre-iem.csv

/usr/bin/find ${rpmTopDir}/RPMS -name "lustre-iem*.rpm" \
   -exec mv -f {} ${currentDir}/dist \;

/usr/bin/find ${rpmTopDir}/SRPMS -name "lustre-iem*.rpm" \
   -exec mv -f {} ${currentDir}/dist \;
 
cd ${currentDir}/dist
rpms=`ls lustre-iem*.rpm`
echo "DONE: $rpms"
 
exit 0