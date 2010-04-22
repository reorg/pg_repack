%define sname	pg_reorg

Summary:	Reorganize tables in PostgreSQL databases without any locks. 
Name:		%{sname}
Version:	1.1.1
Release:	1%{?dist}
License:	BSD
Group:		Applications/Databases
Source0:	http://pgfoundry.org/frs/download.php/1301/%{sname}-%{version}.tar.gz
URL:		http://pgfoundry.org/projects/%{sname}/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)

BuildRequires:	postgresql-devel, postgresql
Requires:	postgresql

%description 	

pg_reorg can re-organize tables on a postgres database without any locks so that you can retrieve or update rows in tables being reorganized. The module is developed to be a better alternative of CLUSTER and VACUUM FULL.

%prep
rm -rf %{_libdir}/pgsql/pgxs/src/backend/
rm -rf %{_builddir}/src
rm -rf %{_builddir}/%{sname}

%setup -n %{sname}

%build
USE_PGXS=1 make %{?_smp_mflags}

%install
rm -rf %{buildroot}
USE_PGXS=1 make DESTDIR=%{buildroot} install

%define pg_sharedir 


%files
%defattr(755,root,root,755)
%{_bindir}/pg_reorg
%{_libdir}/pgsql/pg_reorg.so
%defattr(644,root,root,755)
%{_datadir}/pgsql/contrib/pg_reorg.sql 
%{_datadir}/pgsql/contrib/uninstall_pg_reorg.sql 

%clean
rm -rf %{buildroot}
rm -rf %{_libdir}/pgsql/pgxs/src/backend/

%changelog
* Mon Jan 15 2010 - Toru SHIMOGAKI <shimogaki.toru@oss.ntt.co.jp> 1.0.8-1
* Tue Sep 08 2009 - Toru SHIMOGAKI <shimogaki.toru@oss.ntt.co.jp> 1.0.6-1
* Fri May 15 2009 - Toru SHIMOGAKI <shimogaki.toru@oss.ntt.co.jp> 1.0.4-1
- Initial packaging
