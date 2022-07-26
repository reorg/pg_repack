# SPEC file for pg_repack
# Copyright(C) 2009-2010 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
%global pgmajorversion 14
%define sname	pg_repack

%global _pgdir   /usr/pgsql-%{pgmajorversion}
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share
%define debug_package %{nil}

Summary:	Reorganize tables in PostgreSQL databases without any locks. 
Name:		%{sname}_%{pgmajorversion}
Version:	1.4.7
Release:	1%{?dist}
License:	BSD
Group:		Applications/Databases
Source0:	%{sname}-%{version}.tar.bz2
URL:		https://reorg.github.io/%{sname}/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)

BuildRequires:	postgresql%{pgmajorversion}-devel, postgresql%{pgmajorversion}
Requires:	postgresql%{pgmajorversion}, postgresql%{pgmajorversion}-libs

%description
pg_repack can re-organize tables on a postgres database without any locks so that 
you can retrieve or update rows in tables being reorganized. 
The module is developed to be a better alternative of CLUSTER and VACUUM FULL.

%prep
%setup -q -n %{sname}-%{version}

%build
USE_PGXS=1 make PG_CONFIG=%{_pgdir}/bin/pg_config %{?_smp_mflags}

%install
%{__rm} -rf  %{buildroot}
USE_PGXS=1 make DESTDIR=%{buildroot} PG_CONFIG=%{_pgdir}/bin/pg_config %{?_smp_mflags}

install -d %{buildroot}%{_libdir}
install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_datadir}/extension

install -m 755 bin/pg_repack			%{buildroot}%{_bindir}/pg_repack
install -m 755 lib/pg_repack.so			%{buildroot}%{_libdir}/pg_repack.so
install -m 644 lib/pg_repack--%{version}.sql	%{buildroot}%{_datadir}/extension/pg_repack--%{version}.sql
install -m 644 lib/pg_repack.control		%{buildroot}%{_datadir}/extension/pg_repack.control

%define pg_sharedir %{nil}

%files
%defattr(755,root,root,755)
%{_bindir}/pg_repack
%{_libdir}/pg_repack.so
%defattr(644,root,root,755)
%{_datadir}/extension/pg_repack--%{version}.sql
%{_datadir}/extension/pg_repack.control

%clean
rm -rf %{buildroot}

%changelog
* Thu Oct 21 2010 - NTT OSS Center <sakamoto.masahiko@oss.ntt.co.jp> 1.1.5-1
* Wed Sep 22 2010 - NTT OSS Center <sakamoto.masahiko@oss.ntt.co.jp> 1.1.4-1
* Thu Apr 22 2010 - NTT OSS Center <itagaki.takahiro@oss.ntt.co.jp> 1.1.2-1
* Mon Jan 15 2010 - Toru SHIMOGAKI <shimogaki.toru@oss.ntt.co.jp> 1.0.8-1
* Tue Sep 08 2009 - Toru SHIMOGAKI <shimogaki.toru@oss.ntt.co.jp> 1.0.6-1
* Fri May 15 2009 - Toru SHIMOGAKI <shimogaki.toru@oss.ntt.co.jp> 1.0.4-1
- Initial packaging
