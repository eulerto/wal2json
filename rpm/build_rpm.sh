#!/bin/bash

cd /home/db/wal2json/rpm
rm -f *.rpm
rpmbuild -bb wal2json.spec
cp /home/postgres/rpmbuild/RPMS/x86_64/* ./