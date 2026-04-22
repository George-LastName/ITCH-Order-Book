#!/usr/bin/env bash

set -e

clean() {
    rm -rf build
}

build() {
    cmake -S . -B build
    cmake --build build -j32
}

run() {
    ./build/ITCH_Viewer 01302019.NASDAQ_ITCH50
}

case $1 in
    "c")
        clean
        ;;
    "b")
        build
        ;;
    "r")
        run
        ;;
    "all")
        clean
        build
        run
        ;;
    "reset")
        clean
        clickhouse-client --query "DROP DATABASE Market_Data"
        ;;
    "start")
        konsole -e /bin/bash --rcfile <(echo "cd clickhouse_server; clickhouse-server; exit") &
        systemctl start grafana
        echo "Started."
        ;;
    "stop")
        clickhouse-client --query "SYSTEM SHUTDOWN"
        systemctl stop grafana
        echo "Stopped."
        ;;
    *)
        echo "Usage: ./r.sh <command>"
        echo ""
        echo "  start Start ClickHouse & Grafana."
        echo "  stop  Stop ClickHouse & Grafana."
        echo "  c     Clean the Build Folder"
        echo "  b     Build"
        echo "  r     Run"
        echo "  reset Clean Build & Delete Clickhouse Database."
        echo "  all   Clean, Build, Run"
        ;;

esac
