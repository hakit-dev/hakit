#!/bin/bash

function gen_ca {
    local ca_dir="$1"

    mkdir -p "$ca_dir"
    [ -f "$ca_dir/serial" ] || echo 01 >"$ca_dir/serial"
    [ -f "$ca_dir/index.txt" ] || touch "$ca_dir/index.txt"
    sed -e 's/\@DIR\@/'$ca_dir'/' ca.cnf.in >"$ca_dir/ca.cnf"
    HOME="$ca_dir" openssl req -x509 -nodes -days 36500 -config "$ca_dir/ca.cnf" -newkey rsa:2048 -out "$ca_dir/ca.crt" -outform PEM -keyout "$ca_dir/ca.key"
    chmod 600 "$ca_dir/ca.key"
}

function gen_cert {
    local ca_dir="$1"
    local cert_dir="$2"

    mkdir -p "$cert_dir"
    HOME="$ca_dir" openssl genrsa -out "$cert_dir/server.key" 2048
    HOME="$ca_dir" openssl req -config server.cnf -new -out "$cert_dir/server.csr" -key "$cert_dir/server.key"
    HOME="$ca_dir" openssl x509 -req -in "$cert_dir/server.csr" -CA "$ca_dir/ca.crt" -CAkey "$ca_dir/ca.key" -CAcreateserial -out "$cert_dir/server.crt" -days 36500
    rm -f "$cert_dir/server.csr"
    chmod 600 "$cert_dir/server.key"
    cp -a "$ca_dir/ca.crt" "$cert_dir/"
}


# Generate two CA
gen_ca ca1
gen_ca ca2

# Generate 2 certs with CA 1
gen_cert ca1 cert1
gen_cert ca1 cert2

# Generate 1 cert with CA 2
gen_cert ca2 cert3
