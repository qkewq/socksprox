#! /bin/bash

echo "Installing socksprox..."

OPTSTRING=":hc:"
CONFIG_FILE="/etc/socksprox.conf"

while getopts ${OPTSRING} opt; do
    case ${opt} in
        h)
            echo "Usage $0 [-h] [-c </path/to/config>]"
            ;;
        c)
            CONFIG_FILE="$OPTARG"
            echo "Changing default configs location to ${OPTARG}"
            SED_STR="s|#define CONFIG_FILE .*|#define CONFIG_FILE \"${CONFIG_FILE}\"|g"
            sed -i "${SED_STR}" include/config.h
            ;;
        :)
            echo "Option -${OPTARG} requires argument"
            exit 0
            ;;
    esac
done

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

    else
        mv socksprox.conf /etc/socksprox.conf
    fi
fi

echo "Installation Complete."
