# © Joseph Cameron - MIT License

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

jfc_generate_documentation_doxygen(
    README_PATH  ../README.md
    
    INCLUDE_DIRECTORIES
        ../include/
)

