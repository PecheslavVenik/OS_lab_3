#!/bin/bash

echo "========================================"
echo "СКРИПТ_ДЛЯ_ЛИНУКСОВ.sh"
echo "========================================"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

MODE="${1:-build}"
COUNT="${2:-1}"

if ! command -v git &> /dev/null; then
    echo -e "${RED}Git ис нот инстолд${NC}"
    exit 1
fi

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}CMake ис нот инстолд${NC}"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo -e "${RED}g++ ис нот инстолд${NC}"
    exit 1
fi

echo
echo "git pull origin main..."
git pull origin main
if [ $? -ne 0 ]; then
    echo -e "${YELLOW}Нет интернета...${NC}"
    echo "Продолжаем без пула..."
fi

build_project() {
    if [ ! -d "build" ]; then
        mkdir build
    fi

    cd build

    echo
    echo "Конфьигуринг проджект виз CMake..."
    cmake ..
    if [ $? -ne 0 ]; then
        echo -e "${RED}Эррор: CMake конфигурэйшн фэилд${NC}"
        cd ..
        exit 1
    fi

    echo
    echo "Билдинг зе проджект..."
    if command -v nproc &> /dev/null; then
        CORES=$(nproc)
    elif command -v sysctl &> /dev/null; then
        CORES=$(sysctl -n hw.ncpu)
    else
        CORES=1
    fi
    make -j"$CORES"
    if [ $? -ne 0 ]; then
        echo -e "${RED}Эррор: Билд фэилд${NC}"
        cd ..
        exit 1
    fi

    cd ..

    echo
    echo -e "${GREEN}========================================"
    echo "Билд комплитед саксесфули!"
    echo "Экзекьютабл: build/lab3"
    echo -e "========================================${NC}"
}

cleanup_runtime() {
    rm -f program.log leader.lock
    if [ -d /dev/shm ]; then
        rm -f /dev/shm/cross_platform_counter_shm 2>/dev/null || true
    fi
}

run_instances() {
    local n="$1"
    if [ "$n" -lt 1 ]; then n=1; fi

    cleanup_runtime

    local pids=()
    if [ "$n" -gt 1 ]; then
        for i in $(seq 2 "$n"); do
            ./build/lab3 follower > /dev/null 2>&1 &
            pids+=($!)
            sleep 0.3
        done
    fi

    ./build/lab3

    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}

case "$MODE" in
    build)
        build_project
        ;;
    run)
        build_project
        run_instances "$COUNT"
        ;;
    clean)
        cleanup_runtime
        echo "Очищены логи и shared memory (если были)"
        ;;
    *)
        echo "Использование: ./build.sh [build|run|clean] [N]"
        ;;
esac
