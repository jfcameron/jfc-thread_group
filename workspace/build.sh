#/bin/sh
SOURCE_ROOT_DIR="../src/"

g++ \
    "${SOURCE_ROOT_DIR}"/main_backup.cpp \
    -I"${PUBLIC_HEADER_DIR}" \
    -I"${PRIVATE_HEADER_DIR}" \
    -std=c++17 \
    -pthread \
    -fpermissive

