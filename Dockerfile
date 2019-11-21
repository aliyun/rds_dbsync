FROM centos:7

ENV PATH=$PATH:/dbsync/bin

COPY . /tmp/aliyun/

RUN set -ex \
    && { \
        echo '[mysql57-community]'; \
        echo 'name=mysql57-community'; \
        echo 'baseurl=http://repo.mysql.com/yum/mysql-5.7-community/el/$releasever/$basearch/'; \
        echo 'enabled=1'; \
        echo 'gpgcheck=0'; \
        echo '[pgdg10]'; \
        echo 'name=pgdg10'; \
        echo 'baseurl=https://download.postgresql.org/pub/repos/yum/10/redhat/rhel-$releasever-$basearch'; \
        echo 'enabled=1'; \
        echo 'gpgcheck=0'; \
    } > /etc/yum.repos.d/dbsync_deps.repo \
    && cp -ra /var/log/yum.log /tmp/yum.log.old \
    && yum install mysql-community-client mysql-community-devel postgresql10-devel gcc gcc-c++ make unzip -y \
    && update-alternatives --install /usr/bin/pg_config pgsql-pg_config /usr/pgsql-10/bin/pg_config 300 \
    && ( \
        cd /tmp/aliyun/dbsync \
        && make \
        && install -D -d /dbsync/bin /dbsync/lib \
        && install -p -D -m 0755 *2pgsql /dbsync/bin \
        && install -p -D -m 0755 ali_recvlogical.so /dbsync/lib \
        && install -p -D -m 0644 my.cfg ../LICENSE ../README.md /dbsync \
        && ln -sf /usr/share/mysql /dbsync/share \
    ) \
    && update-alternatives --remove pgsql-pg_config /usr/pgsql-10/bin/pg_config \
    && mkdir -p /tmp/extbin \
    && curl -L https://github.com/aliyun/rds_dbsync/files/1555186/mysql2pgsql.bin.el7.20171213.zip -o /tmp/extbin/bin.zip \
    && (cd /tmp/extbin && unzip -o bin.zip && install -p -D -m 0755 mysql2pgsql.bin*/bin/binlog_* /dbsync/bin) \
    && yum remove -y mysql-community-devel postgresql10-devel unzip gcc gcc-c++ make cpp glibc-devel glibc-headers libicu-devel libstdc++-devel kernel-headers \
    && yum clean all && mv /tmp/yum.log.old /var/log/yum.log \
    && rm -rf /tmp/aliyun /tmp/extbin /var/cache/yum/* /etc/yum.repos.d/dbsync_deps.repo \
    && ls -alhR /dbsync && ldd /dbsync/bin/* && mysql --version && psql --version && mysql2pgsql -h

WORKDIR /dbsync

CMD ["mysql2pgsql", "-h"]
