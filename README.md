# wal2json

JSON output plugin for changeset extraction

### Forked

This is a fork of the great utility at https://github.com/eulerto/wal2json

I take no credit for the C code. Just a little packaging. 

Unfortunately, the source repository didn't make it into the postgres mainline source. Which makes it a bit tricky to compile with this makefile.  I updated the makefile, so it works.


### Install Steps

(On Debian)

```
# You need postgres 9.4 installed on your machine, you probably have it already
sudo apt-get install postgres-9.4
sudo apt-get install postgres-contrib-9.4

# to compile this code, you need the following
sudo apt-get install build-essential
sudo apt-get build-dep postgresql-9.4
sudo apt-get install postgres-server-dev-9.4

# now go into the directory and...
make
sudo make install
```


### Logical Replication

You need to know quite a few things about logical replication to make use of this.  If this is new to you, please check out [the official postgres documentation](http://www.postgresql.org/docs/9.4/static/logicaldecoding-example.html).  You can replace 'test_decoding' with 'wal2json' to see the magic of this plugin.
