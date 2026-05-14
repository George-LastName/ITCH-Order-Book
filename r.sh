#!/usr/bin/env bash

set -e

clean() {
    rm -rf build
}

build() {
    cmake -S . -B build -DPROFILING=OFF
    cmake --build build -j32
}

profile() {
    cmake -S . -B build -DPROFILING=ON
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
    "p")
        profile
        ;;
    "r")
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
        echo "  p    Build with Profiling"
        echo "  reset Clean Build & Delete Clickhouse Database."
        echo "  all   Clean, Build, Run"
        ;;

esac
