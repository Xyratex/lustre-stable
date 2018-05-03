%define vendor_name lustre
%define _version %(if test -s "%_sourcedir/_version"; then cat "%_sourcedir/_version"; else echo "UNKNOWN"; fi)
%if %{with athena}
%define flavor cray_ari_athena_s_cos
%else
%define flavor cray_ari_s_cos
%endif
%define intranamespace_name %{vendor_name}-%{flavor}
%define source_name %{vendor_namespace}-%{vendor_name}-%{_version}
%define branch trunk

%define kversion %(make -s -C /usr/src/linux-obj/%{_target_cpu}/%{flavor} kernelrelease)

%define _prefix    /usr

BuildRequires: zlib-devel
BuildRequires: cray-gni-devel
BuildRequires: cray-gni-headers
BuildRequires: cray-gni-headers-private
BuildRequires: cray-krca-devel
%if %{without athena}
BuildRequires: ofed-devel
%endif
BuildRequires: kernel-source
BuildRequires: kernel-syms
BuildRequires: %{namespace}-krca-devel
BuildRequires: lsb-cray-hss-devel
BuildRequires: pkgconfig
BuildRequires: -post-build-checks
BuildRequires: module-init-tools
BuildRequires: libyaml-devel
Group: System/Filesystems
License: GPL
Name: %{namespace}-%{intranamespace_name}
Release: %{release}
Summary: Lustre File System for Aries CentOS Nodes
Version: %{_version}
Source: %{source_name}.tar.bz2
URL: %url
BuildRoot: %{_tmppath}/%{name}-%{version}-root

%description
Userspace tools and files for the Lustre file system on Baker CentOS nodes.
Compiled for kernel: %{kversion}

%prep
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

export GNICPPFLAGS=`pkg-config --cflags cray-gni cray-gni-headers cray-krca lsb-cray-hss`
if [ -d /usr/src/kernel-modules-ofed/%{_target_cpu}/%{flavor} ]; then
	O2IBPATH=/usr/src/kernel-modules-ofed/%{_target_cpu}/%{flavor}
elif [ -d /usr/src/ofed/%{_target_cpu}/%{flavor} ]; then
	O2IBPATH=/usr/src/ofed/%{_target_cpu}/%{flavor}
else
	O2IBPATH=no
fi

HSS_FLAGS=`pkg-config --cflags lsb-cray-hss`
CFLAGS="%{optflags} -Werror -fno-stack-protector $HSS_FLAGS"

if [ "%reconfigure" == "1" -o ! -f %_builddir/%{source_name}/Makefile ];then
	%configure --disable-checksum \
		--enable-gni \
		--disable-server \
		--with-linux-obj=/usr/src/linux-obj/%{_target_cpu}/%{flavor} \
		--with-systemdsystemunitdir=%{_unitdir} \
		--with-obd-buffer-size=16384
fi
%{__make} %_smp_mflags

%install
# Sets internal kgnilnd build version
export SVN_CODE_REV=%{lustre_version}

make DESTDIR=${RPM_BUILD_ROOT} install

rm -f $RPM_BUILD_ROOT%{_libdir}/liblustreapi.la
rm -f $RPM_BUILD_ROOT%{_libdir}/liblnetconfig.la

%files
%defattr(-,root,root)
/sbin/mount.lustre
/lib/modules/%{kversion}
/etc/*
%{_sbindir}/*
%{_bindir}/*
%{_mandir}/*
%{_unitdir}/lnet.service
%{_includedir}/lustre
%{_includedir}/linux/lnet
%{_includedir}/linux/lustre
%{_libdir}/liblustreapi.a
%{_libdir}/liblustreapi.so*
%{_libdir}/liblnetconfig.a
%{_libdir}/liblnetconfig.so*
%dir %{_libdir}/lustre
%{_libdir}/lustre/tests
%exclude /etc/lustre/perm.conf
%exclude /etc/lustre
%exclude /etc/init.d
%exclude %{_mandir}/man5

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
