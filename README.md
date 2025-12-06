# How To Run

cmake -S . -B build

SEEDING
make build-cs
./build/src/server
./build/src/client < scripts/seed_500.sql
./build/src/client

DBMS 
cmake --build build --target mdbms_client
./build/src/mdbms_client

DBMS Multi-Client
cmake --build build --target server client
./build/src/server
./build/src/client
