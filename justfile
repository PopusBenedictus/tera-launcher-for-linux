# https://just.systems/man/en

# show recipes
help:
    just --list

# format files
format:
    treefmt

# build everything
build:
    rm -rf build
    cmake -B build
    make -C build
