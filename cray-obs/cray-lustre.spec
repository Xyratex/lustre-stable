%define _version %(if test -s "%_sourcedir/_version"; then cat "%_sourcedir/_version"; else echo "UNKNOWN"; fi)
%define _lnet_version %(echo "%{_version}" | awk -F . '{printf("%s.%s", $1, $2)}')

%define intranamespace_name %{name}
%{expand:%%global OBS_prefix %{_prefix}}
%define prefix /usr
%define _sysconfdir /etc

%if %{_vendor}=="redhat"
%global kversion %(make -s -C /usr/src/kernels/* kernelversion)
%global _with_linux --with-linux=/usr/src/kernels/%{kversion}
%else
%global kversion %(make -s -C /usr/src/linux-obj/%{_target_cpu}/%{flavor} kernelrelease)
%global _with_linux --with-linux=/usr/src/linux
%global _with_linux_obj --with-linux-obj=/usr/src/linux-obj/%{_target_cpu}/%{flavor}
%endif

%global lustre_name cray-lustre
Name: %{lustre_name}
Summary: Cray Lustre Filesystem
Version: %{_version}
Release: %{release}
License: GPL
Group: System/Filesystems
Source: cray-lustre-%{_version}.tar.bz2
Source1: kmp-lustre.preamble
Source2: kmp-lustre.files
URL: %url
BuildRoot: %{_tmppath}/%{name}-%{version}-root

BuildRequires: %kernel_module_package_buildreqs
BuildRequires: libtool libyaml-devel zlib-devel
BuildRequires: systemd
%if %{_vendor}=="redhat"
BuildRequires: kernel-debuginfo-common-%{_target_cpu}
BuildRequires: mlnx-ofa_kernel-devel
BuildRequires: redhat-rpm-config
%else
BuildRequires: kernel-source
%endif

# Disable post-build-checks; See LUS-1345
# Note: build checks can be run manually by first doing an incremental build
# and then doing a second incremental build with post-build-checks enabled.
BuildConflicts: post-build-checks

%description
Userspace tools and files for the Lustre filesystem.
Compiled for kernel: %{kversion}

%package devel
Group: Development/Libraries
License: GPL
Summary: Cray Lustre Header files

%description devel
Development files for building against Lustre library.
Includes headers, dynamic, and static libraries.
Compiled for kernel: %{kversion}

%package lnet-headers
Group: Development/Libraries
License: GPL
Summary: Cray Lustre Network Header files

%description lnet-headers
Cray Lustre Network Header files
Compiled for kernel: %{kversion}

%package %{flavor}-lnet-devel
Group: Development/Libraries
License: GPL
Summary: Cray Lustre Network kernel flavor specific devel files

%description %{flavor}-lnet-devel
Kernel flavor specific development files for building against Lustre
Network (LNet)
Compiled for kernel: %{kversion}

%if %{undefined kmoddir}
	%if %{defined kernel_module_package_moddir}
		%global kmoddir %{kernel_module_package_moddir}
	%else
		%if %{defined suse_kernel_module_package}
			%global kmoddir updates
		%else
			%global kmoddir extra
		%endif
	%endif
%endif

%global modules_fs_path /lib/modules/%{kversion}/%{kmoddir}

%kernel_module_package -n %{name} -p %SOURCE1 -f %SOURCE2 %{flavor}

%prep
%if %{undefined flavor}
%{error:"flavor is undefined"}
exit 1
%endif

%incremental_setup -q -n cray-lustre-%{_version}

# Need '-f' here for incremental builds
ln -f lustre/ChangeLog ChangeLog-lustre
ln -f lnet/ChangeLog ChangeLog-lnet

%build
echo "LUSTRE_VERSION = %{_version}" > LUSTRE-VERSION-FILE

if [ "%reconfigure" == "1" -o ! -x %_builddir/%{name}-%{version}/configure ];then
	chmod +x autogen.sh
	./autogen.sh
fi

if [ -d /usr/src/ofa_kernel/default ]; then
	O2IBPATH=/usr/src/ofa_kernel/default
	export KBUILD_EXTRA_SYMBOLS=/usr/src/ofa_kernel/default/Module.symvers
else
	O2IBPATH=yes
fi

if [ "%reconfigure" == "1" -o ! -f %_builddir/%{name}-%{version}/Makefile ];then
	%configure \
		--enable-server \
		--with-kmp-moddir=%{kmoddir}/%{name} \
		--with-o2ib=${O2IBPATH} \
		%{_with_linux} %{?_with_linux_obj}
fi
%{__make} %_smp_mflags

%install
make DESTDIR=${RPM_BUILD_ROOT} install

for header in api.h lib-lnet.h lib-types.h
do
	%{__install} -D -m 0644 lnet/include/lnet/${header} %{buildroot}/%{_includedir}/lnet/${header}
done

for header in libcfs_debug.h lnetctl.h lnetst.h libcfs_ioctl.h lnet-dlc.h \
	      lnet-types.h nidstr.h
do
	%{__install} -D -m 0644 lnet/include/uapi/linux/lnet/${header} %{buildroot}/%{_includedir}/uapi/linux/lnet/${header}
done

for header in libcfs.h util/list.h curproc.h bitmap.h libcfs_private.h libcfs_cpu.h \
	      libcfs_prim.h libcfs_time.h libcfs_string.h libcfs_workitem.h \
	      libcfs_hash.h libcfs_heap.h libcfs_fail.h libcfs_debug.h range_lock.h
do
	%{__install} -D -m 0644 libcfs/include/libcfs/${header} %{buildroot}/%{_includedir}/libcfs/${header}
done

for header in linux-fs.h linux-mem.h linux-time.h linux-cpu.h linux-crypto.h \
	      linux-misc.h
do
	%{__install} -D -m 0644 libcfs/include/libcfs/linux/${header} %{buildroot}/%{_includedir}/libcfs/linux/${header}
done

%{__install} -D -m 0644 lustre/include/interval_tree.h %{buildroot}/%{_includedir}/interval_tree.h

%define cfgdir %{_includedir}/lustre/%{flavor}
for f in cray-lustre-api-devel.pc cray-lustre-cfsutil-devel.pc \
	 cray-lustre-ptlctl-devel.pc cray-lnet.pc
do
	eval "sed -i 's,@includedir@,%{_includedir},' cray-obs/${f}"
	eval "sed -i 's,@libdir@,%{_libdir},' cray-obs/${f}"
	eval "sed -i 's,@symversdir@,%{_datadir}/symvers,' cray-obs/${f}"
	eval "sed -i 's,@PACKAGE_VERSION@,%{_version},' cray-obs/${f}"
	eval "sed -i 's,@cfgdir@,%{cfgdir},' cray-obs/${f}"
	install -D -m 0644 cray-obs/${f} $RPM_BUILD_ROOT%{_pkgconfigdir}/${f}
done

# Install Module.symvers and config.h for the lnet devel package
%{__install} -D -m 0644 ${PWD}/Module.symvers %{buildroot}/%{_datadir}/symvers/%{_arch}/%{flavor}/Module.symvers
%{__install} -D -m 0644 config.h %{buildroot}/%{cfgdir}/config.h

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
%{_sysconfdir}/lustre
%{_sysconfdir}/lnet.conf
%{_sysconfdir}/lnet_routes.conf
%{_sysconfdir}/modprobe.d
%{_sysconfdir}/udev
%{_sysconfdir}/ha.d
%if %{_vendor}=="redhat"
%{_sysconfdir}/init.d/lustre
%endif
%{_sbindir}/*
%{_bindir}/*
%{_mandir}
%{_libexecdir}
%{_unitdir}/lnet.service
%dir %{_libdir}/lustre
%{_libdir}/lustre/tests
%{_libdir}/lustre/mount_osd_ldiskfs.so
%{_libdir}/libiam.a
# The versioned shared library files for liblnetconfig are needed for
# lnetctl, so they are included in the base package
%{_libdir}/liblnetconfig.so.*

%files devel
%dir %{_datadir}/symvers
%dir %{_datadir}/symvers/%{_arch}
%dir %{_datadir}/symvers/%{_arch}/%{flavor}
%attr (644,root,root) %{_datadir}/symvers/%{_arch}/%{flavor}/Module.symvers
%{_includedir}/lustre
%{_libdir}/liblustreapi.a
%{_libdir}/liblustreapi.so*
%{_libdir}/liblnetconfig.a
%{_libdir}/liblnetconfig.so
%{_pkgconfigdir}/cray-lustre-api-devel.pc
%{_pkgconfigdir}/cray-lustre-cfsutil-devel.pc
%{_pkgconfigdir}/cray-lustre-ptlctl-devel.pc
%{_modulefiles_prefix}
%exclude %{cfgdir}

%files %{flavor}-lnet-devel
%dir %{_datadir}/symvers
%dir %{_datadir}/symvers/%{_arch}
%dir %{_datadir}/symvers/%{_arch}/%{flavor}
%attr (644,root,root) %{_datadir}/symvers/%{_arch}/%{flavor}/Module.symvers
%{cfgdir}

%files lnet-headers
%{_includedir}/lnet
%{_includedir}/linux
%{_includedir}/uapi
%{_includedir}/libcfs
%{_includedir}/interval_tree.h
%{_pkgconfigdir}/cray-lnet.pc

%post
/sbin/ldconfig
%systemd_post lnet.service

%preun
%systemd_preun lnet.service

%postun
/sbin/ldconfig
%systemd_postun_with_restart lnet.service

%post devel
/sbin/ldconfig

%postun devel
/sbin/ldconfig

%clean
%clean_build_root
