%define vendor_name lustre
%define _version %(if test -s "%_sourcedir/_version"; then cat "%_sourcedir/_version"; else echo "UNKNOWN"; fi)
%define flavor cray_ari_c
%define intranamespace_name %{vendor_name}-%{flavor}_rhine
%define source_name %{vendor_namespace}-%{vendor_name}-%{_version}
%define branch trunk
%define _lnet_version %(echo "%{_version}" | awk -F . '{printf("%s.%s", $1, $2)}')

# Override _prefix to avoid installing into Cray locations under /opt/cray/
%define _prefix /usr
%define kversion %(make -s -C /usr/src/linux-obj/%{_target_cpu}/%{flavor} kernelrelease)

BuildRequires: cray-gni-devel
BuildRequires: cray-gni-headers
BuildRequires: cray-gni-headers-private
BuildRequires: cray-krca-devel
BuildRequires: lsb-cray-hss-devel
BuildRequires: kernel-source
BuildRequires: kernel-syms
BuildRequires: zlib-devel
BuildRequires: module-init-tools
BuildRequires: pkgconfig
BuildRequires: libtool
BuildRequires: libyaml-devel
BuildConflicts: post-build-checks
Group: System/Filesystems
License: GPL
Name: %{namespace}-%{intranamespace_name}
Release: %{release}
Requires: module-init-tools
Summary: Lustre File System for CNL running CLE Rhine
Version: %{_version}
Source: %{source_name}.tar.bz2
URL: %url
BuildRoot: %{_tmppath}/%{name}-%{version}-root

%package -n cray-lustre-cray_ari_c-%{_lnet_version}-devel
Summary: The lnet development package
License: GPL
Group: Development/Libraries/C and C++


%description
Userspace tools and files for the Lustre file system on XT compute nodes.
Compiled for kernel: %{kversion}

%description -n cray-lustre-cray_ari_c-%{_lnet_version}-devel
Development files for building against Lustre library.
Includes headers, dynamic, and static libraries.
Compiled for kernel: %{kversion}

%prep
# using source_name here results in too deep of a macro stack, so use
# definition of source_name directly
%incremental_setup -q -n %{source_name}

%build
echo "LUSTRE_VERSION = %{_version}" > LUSTRE-VERSION-FILE
%define version_path %(basename %url)
%define date %(date +%%F-%%R)
%define lustre_version %{_version}-%{branch}-%{release}-%{build_user}-%{version_path}-%{date}

# Sets internal kgnilnd build version
export SVN_CODE_REV=%{lustre_version}

if [ "%reconfigure" == "1" -o ! -x %_builddir/%{source_name}/configure ];then
	chmod +x autogen.sh
	./autogen.sh
fi

syms="$(pkg-config --variable=symversdir cray-gni)/%{flavor}/Module.symvers"
syms="$syms $(pkg-config --variable=symversdir cray-krca)/%{flavor}/Module.symvers"

export KBUILD_EXTRA_SYMBOLS=${syms}

export GNICPPFLAGS=$(pkg-config --cflags cray-gni cray-gni-headers cray-krca lsb-cray-hss)

HSS_FLAGS=$(pkg-config --cflags lsb-cray-hss)
export CFLAGS="%{optflags} -Werror -fno-stack-protector $HSS_FLAGS"

if [ "%reconfigure" == "1" -o ! -f %_builddir/%{source_name}/Makefile ];then
	%configure --disable-checksum \
		--disable-doc \
		--disable-server \
		--with-o2ib=no \
		--enable-gni \
		--with-linux-obj=/usr/src/linux-obj/%{_target_cpu}/%{flavor} \
		--with-systemdsystemunitdir=%{_unitdir} \
		--with-obd-buffer-size=16384
fi
%{__make} %_smp_mflags

%install
# Sets internal kgnilnd build version
export SVN_CODE_REV=%{lustre_version}

make DESTDIR=${RPM_BUILD_ROOT} install

%{__install} -D -m 0644 ${PWD}/Module.symvers %{buildroot}/%{_datadir}/symvers/%{_arch}/%{flavor}/Module.symvers
%{__install} -D -m 0644 config.h %{buildroot}/%{_includedir}/lustre/%{flavor}/config.h

for f in cray-lustre-api-devel.pc cray-lustre-cfsutil-devel.pc \
	 cray-lustre-ptlctl-devel.pc
do
	eval "sed -i 's,@includedir@,%{_includedir},' cray-obs/${f}"
	eval "sed -i 's,@libdir@,%{_libdir},' cray-obs/${f}"
	eval "sed -i 's,@symversdir@,%{_datadir}/symvers,' cray-obs/${f}"
	eval "sed -i 's,@PACKAGE_VERSION@,%{_version},' cray-obs/${f}"
	eval "sed -i 's,@cfgdir@,%{cfgdir},' cray-obs/${f}"
	install -D -m 0644 cray-obs/${f} $RPM_BUILD_ROOT%{_pkgconfigdir}/${f}
done

# Many things are excluded from compute node packages to save space in
# the compute node image. Here we remove everything that should be left
# out of the package. We need to remove these files rather than simply
# excluding them in the files section otherwise builds against the debug
# kernel will fail.
## '-H' needed in find commands for incremental builds

# Remove everything in _sbindir except lctl, mount.lustre, lustre_rmmod
# and lnetctl
find -H $RPM_BUILD_ROOT%{_sbindir} -type f -print | \
	egrep -v '/lctl$|/mount.lustre$|/lustre_rmmod$|/lnetctl$' | \
	xargs -n 1 rm -f

# Remove everything in _bindir except lfs and lfs_migrate
find -H $RPM_BUILD_ROOT%{_bindir} -type f -print | \
	egrep -v '/lfs$|/lfs_migrate$' | \
	xargs -n 1 rm -f

# Remove all man pages and tests
rm -rf $RPM_BUILD_ROOT%{_mandir}/*
rm -rf $RPM_BUILD_ROOT%{_libdir}/lustre/tests

# Remove sysconf files that are not needed
rm -f $RPM_BUILD_ROOT/etc/lnet.conf
rm -f $RPM_BUILD_ROOT/etc/lnet_routes.conf
rm -f $RPM_BUILD_ROOT/etc/modprobe.d/ko2iblnd.conf
rm -f $RPM_BUILD_ROOT/etc/lustre/perm.conf

rm -f $RPM_BUILD_ROOT%{_libdir}/liblustreapi.la
rm -f $RPM_BUILD_ROOT%{_libdir}/liblnetconfig.la

%files
%defattr(-,root,root)
/sbin/mount.lustre
/etc/udev
/lib/modules/%{kversion}
%{_sbindir}/*
%{_bindir}/*
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

%files -n cray-lustre-cray_ari_c-%{_lnet_version}-devel
%defattr(-,root,root)
%dir %{_datadir}/symvers
%dir %{_datadir}/symvers/%{_arch}
%dir %{_datadir}/symvers/%{_arch}/%{flavor}
%attr (644,root,root) %{_datadir}/symvers/%{_arch}/%{flavor}/Module.symvers
%dir %{_includedir}/lustre/%{flavor}
%{_includedir}/lustre/%{flavor}/config.h

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

%preun

%clean
%clean_build_root
