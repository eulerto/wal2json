# wal2json
Name: wal2json
Version: 1.5.2
Release: 1%{?dist}
Summary: PostgreSQL output plugin Wal2Json
Vendor: Subito srl
License: None
URL: None
Group: None
Packager: pietro.partescano@scmitaly.it

%description
PostgreSQL WAL decoding output plug.

%files
%defattr(-,root,root)
/usr/pgsql-9.5/lib/wal2json.so

%prep
PATH=$PATH:/usr/local/go/bin:/usr/pgsql-9.5/bin/
cd /home/db/wal2json/ && \
rm -f wal2json.o wal2json.so && \
USE_PGXS=1 make && \

%install
mkdir -p $RPM_BUILD_ROOT/usr/pgsql-9.5/lib/ && \
cp /home/db/wal2json/wal2json.so $RPM_BUILD_ROOT/usr/pgsql-9.5/lib/
cd /home/db/wal2json/
rm -f wal2json.o wal2json.so 

# %clean
# echo "Not cleaning up in this case."