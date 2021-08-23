#!/binbash
docker stop postgres9.4
docker cp postgresql.conf postgres9.4:/var/lib/postgresql/data/postgresql.conf 
docker cp pg_hba.conf postgres9.4:/var/lib/postgresql/data/pg_hba.conf
docker cp ali_decoding_9.4/ali_decoding.so postgres9.4:/usr/lib/postgresql/9.4/lib/
docker cp ali_decoding_9.4/ali_decoding.control postgres9.4:/usr/share/postgresql/9.4/extension/
docker start postgres9.4