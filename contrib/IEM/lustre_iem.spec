#xyr build defines
%define _xyr_package_name     lustre-iem
%define _xyr_package_source   lustre-iem.tgz
%define _xyr_package_version  1.0
%define _xyr_build_number     1
%define _xyr_pkg_url          http://es-gerrit:8080/lustre-wc-rel
%define _xyr_svn_version      0
#xyr end defines

Name:		%_xyr_package_name
Version:	%_xyr_package_version
Release:	%_xyr_build_number
Summary: Lustre syslog messages to Seagate RAS format
License: GPL
Group: Applications/System
BuildArch: noarch
Source: %_xyr_package_source
Vendor: Seagate Technology LLC
#BuildRequires: syslog-ng >= 3.2
#BuildRequires: libxml2
Requires: syslog-ng >= 3.2

%description
Pattern DB for syslog-ng to convert 
Lustre syslog messages to Seagate RAS format

%prep
%setup -c

%build
sed -n -e "/<csv>/,/<\/csv>/p" lustre_iem.pdb \
|  sed -e "/<csv>/d" -e "/<\/csv>/d" \
> lustre-iem.csv

%install
mkdir -p $RPM_BUILD_ROOT/etc/syslog-ng/conf.d/
cp 20-lustre_iem.conf $RPM_BUILD_ROOT/etc/syslog-ng/conf.d/

mkdir -p  $RPM_BUILD_ROOT/etc/syslog-ng/patterndb.d/
cp lustre_iem.pdb $RPM_BUILD_ROOT/etc/syslog-ng/patterndb.d/


%files
%attr(0644,root,root) /etc/syslog-ng/conf.d/20-lustre_iem.conf
%attr(0644,root,root) /etc/syslog-ng/patterndb.d/lustre_iem.pdb
%doc lustre-iem.csv

# commenting out test as current version of syslog-ng doesn't have
# patterndb-4.xsd schema and test would fail.
# https://github.com/balabit/syslog-ng/commit/a5e11c771787ce1671d95713fa9342a99a1e1289
# so this check could be done manually by adding missed file
#% check
#
#if pdbtool test --validate $RPM_BUILD_ROOT/etc/syslog-ng/patterndb.d/lustre_iem.pdb ; then
#	echo "Test PASSED"
#else
#	echo "Test FAILED"
#	exit 1
#fi

%clean
if [ $RPM_BUILD_ROOT != '/' ]; then rm -fr $RPM_BUILD_ROOT; fi

%changelog
* Thu Jun 18 2015 David Adair <david.adair@seagate.com> - 1.0-1
- Make build more mock / buildsys friendly

* Wed May 22 2015 Denis Kondratenko <denis.kondratenko@seagate.com> 1.0
- first release
