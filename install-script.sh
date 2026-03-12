#! /bin/bash

echo "Installing socksprox..."

if [ ! -f "build/socksprox" ]; then
    echo "socksprox not found in build/socksprox"
    echo "Try running \"make\" to build"
    exit 0
fi

if [ ! -x "build/socksprox" ]; then
    echo "Making socksprox executable"
    chmod +x build/socksprox
fi

if [ -f "/etc/socksprox.conf" ]; then
    echo "Config file found in /etc/socksprox.conf"
    echo "Leaving configs as is"
else
    echo "Placing configs in /etc/socksprox.conf"
    if [ ! -f "socksprox.conf" ]; then
        echo "Configs not found, using defaults"
        cat <<EOF >> /etc/socksprox.conf
# Configuration for the socksprox SOCKS5 server
# socksprox "follows" rfc 1928
# Run "socksprox -t" to test this config file

# Logging location
access-log = /var/log/socksprox.access
error-log = /var/log/socksprox.error

# Network port to listen on
listen-port = 1080

# IP addresses to listen on
# Add listen = <ip> as needed
# IPv4 or IPv6 allowed
listen = 0.0.0.0
listen = ::

# Let the client request domain names
# No will return address type not supported to the client
allow-domains = yes

# Commands allowed to be requested by clients
# Allow clients to initiate outbound connections
# No will return command not supported to the client
allow-connect = yes
allow-bind = no
allow-udp-associate = yes

# Maximum number of connections allowed at one time
max-connections = 1000

# Authentication methods
# There can be multiple allowed methods
method = no-auth
# method = username-password (not yet)
# method = gssapi (not yet)
EOF

fi

echo "Installation Complete."
