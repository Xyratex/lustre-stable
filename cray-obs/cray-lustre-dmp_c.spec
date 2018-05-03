%define vendor_name lustre
%define _version %(if test -s "%_sourcedir/_version"; then cat "%_sourcedir/_version"; else echo "UNKNOWN"; fi)
%define flavor default

%define intranamespace_name %{vendor_name}-%{flavor}

%define source_name %{vendor_namespace}-%{vendor_name}-%{_version}
%define branch trunk

%define kversion %(make -s -C /usr/src/linux-obj/%{_target_cpu}/%{flavor} kernelrelease)
%define pc_files cray-lustre-api-devel.pc cray-lustre-cfsutil-devel.pc cray-lustre-ptlctl-devel.pc
%define _prefix /usr

BuildRequires: kernel-source
BuildRequires: kernel-syms
BuildRequires: pkgconfig
BuildRequires: -post-build-checks
BuildRequires: module-init-tools
BuildRequires: libtool
BuildRequires: libyaml-devel
BuildRequires: zlib-devel
%if "%{?sle_version}" == "120000"
# Only SLES 12 SP0 builds require this. Was needed for EDR IB support in eLogin
# for 6.0UP02. Future versions will use in-kernel drivers.
BuildRequires: ofed-devel
%endif
Group: System/Filesystems
License: GPL
Name: %{namespace}-%{intranamespace_name}
Release: %{release}
Summary: Lustre File System for CLFS SLES-based Nodes
Version: %{_version}
Source: %{source_name}.tar.bz2
URL: %url
BuildRoot: %{_tmppath}/%{name}-%{version}-root

%description
Kernel modules and userspace tools needed for a Lustre client on CLFS SLES-based
service nodes.
Compiled for kernel: %{kversion}

%prep
%incremental_setup -q -n %{source_name}

%build
echo "LUSTRE_VERSION = %{_version}" > LUSTRE-VERSION-FILE

if [ "%reconfigure" == "1" -o ! -x %_builddir/%{source_name}/configure ];then
	chmod +x autogen.sh
	./autogen.sh
fi

if [ -d /usr/src/kernel-modules-ofed/%{_target_cpu}/%{flavor} ]; then
	_with_o2ib="--with-o2ib=/usr/src/kernel-modules-ofed/%{_target_cpu}/%{flavor}"
	syms="/usr/src/kernel-modules-ofed/%{_target_cpu}/%{flavor}/Modules.symvers"
fi

export KBUILD_EXTRA_SYMBOLS=${syms}

CFLAGS="%{optflags} -Werror"

if [ "%reconfigure" == "1" -o ! -f %_builddir/%{source_name}/Makefile ];then
	%configure --disable-checksum \
		--disable-server \
		--with-linux-obj=/usr/src/linux-obj/%{_target_cpu}/%{flavor} \
		${_with_o2ib} \
		--with-systemdsystemunitdir=%{_unitdir} \
		--with-obd-buffer-size=16384
fi
%{__make} %_smp_mflags

%install
make DESTDIR=${RPM_BUILD_ROOT} install

%define cfgdir %{_includedir}/lustre/%{flavor}
for f in cray-lustre-api-devel.pc cray-lustre-cfsutil-devel.pc \
	 cray-lustre-ptlctl-devel.pc cray-lnet.pc
do
	eval "sed -i 's,@includedir@,%{_includedir},' cray-obs/${f}"
	eval "sed -i 's,@libdir@,%{_libdir},' cray-obs/${f}"
	eval "sed -i 's,@symversdir@,%{_datadir},' cray-obs/${f}"
	eval "sed -i 's,@PACKAGE_VERSION@,%{_version},' cray-obs/${f}"
	eval "sed -i 's,@cfgdir@,%{cfgdir},' cray-obs/${f}"
	install -D -m 0644 cray-obs/${f} $RPM_BUILD_ROOT%{_pkgconfigdir}/${f}
done

# Install module directories and files
%{__sed} -e 's/@VERSION@/%{version}-%{release}/g' cray-obs/version.in > .version
%{__sed} -e 's,@pkgconfigdir@,%{_pkgconfigdir},g' cray-obs/module.in > module
%{__install} -D -m 0644 .version %{buildroot}/%{_name_modulefiles_prefix}/.version
%{__install} -D -m 0644 module %{buildroot}/%{_release_modulefile}

rm -f $RPM_BUILD_ROOT%{_libdir}/liblustreapi.la
rm -f $RPM_BUILD_ROOT%{_libdir}/liblnetconfig.la

%files
%defattr(-,root,root)
/sbin/mount.lustre
/etc/udev
/lib/modules/%{kversion}
%{_sbindir}/*
%{_bindir}/*
%{_mandir}/*
%{_unitdir}/lnet.service
%{_includedir}/lustre
%{_includedir}/linux/lnet
%{_includedir}/linux/lustre
%{_libdir}/liblnetconfig.a
%{_libdir}/liblnetconfig.so*
%{_libdir}/liblustreapi.a
%{_libdir}/liblustreapi.so*
%{_pkgconfigdir}/cray-lustre-api-devel.pc
%{_pkgconfigdir}/cray-lustre-cfsutil-devel.pc
%{_pkgconfigdir}/cray-lustre-ptlctl-devel.pc
%dir %{_libdir}/lustre
%{_libdir}/lustre/tests
%{_modulefiles_prefix}
%exclude /etc/modprobe.d/ko2iblnd.conf
%exclude %{_mandir}/man5
%exclude %{_mandir}/man8/lhbadm.8.gz
%exclude %{_libdir}/lustre/tests
%exclude /etc/lnet.conf
%exclude /etc/lnet_routes.conf
%exclude /etc/lustre/perm.conf
%exclude %{_pkgconfigdir}/cray-lnet.pc

%post
DEPMOD_OPTS=""
if [ -f /boot/System.map-%{kversion} ]; then
	DEPMOD_OPTS="-F /boot/System.map-%{kversion}"
fi

depmod -a ${DEPMOD_OPTS} %{kversion}

%postun
DEPMOD_OPTS=""
if [ -f /boot/System.map-%{kversion} ]; then
	DEPMOD_OPTS="-F /boot/System.map-%{kversion}"
fi

depmod -a ${DEPMOD_OPTS} %{kversion}

%clean
%clean_build_root
