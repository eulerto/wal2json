%define _missing_doc_files_terminate_build 0
%define _unpackaged_files_terminate_build 0


%global pginstdir /usr/pgsql-10/bin
%global pgmajorversion 10


Name:		wal2json
Version:	1.0
Release:	1%{?dist}
Summary:	JSON output plugin for changeset extraction

License:	PostgreSQL
URL:		https://github.com/eulerto/wal2json
Source0:	wal2json-1.0.tar.gz

BuildRequires:	postgresql%{pgmajorversion}-devel
Requires:	postgresql%{pgmajorversion}-server

%description
wal2json is an output plugin for logical decoding. It means that the plugin have access to tuples produced by INSERT and UPDATE. Also, UPDATE/DELETE old row versions can be accessed depending on the configured replica identity. Changes can be consumed using the streaming protocol (logical replication slots) or by a special SQL API.

The wal2json output plugin produces a JSON object per transaction. All of the new/old tuples are available in the JSON object. Also, there are options to include properties such as transaction timestamp, schema-qualified, data types, and transaction ids.

wal2json is released under PostgreSQL license.


%prep
%setup -q


%build
PATH=%{pginstdir}/bin:$PATH
USE_PGXS=1 
%make_build


%install
#%make_install
USE_PGXS=1 DESTDIR=%{buildroot} make install

%files
/usr/pgsql-10/lib/wal2json.so

%doc

%changelog
* Wed Feb  14 2018 - Iouri Goussev <igoussev@xmatters.com> 1.0
- Initial packaging.

