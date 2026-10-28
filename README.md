# jfc-thread_group

Task-based concurrency lib. Internally uses moody concurrent queue lockless queue. See the bench directory to compare its performance to a more conventional stl based queue. requires c++20

`pending()` says how many tasks have been added and not yet finished, so a caller can tell when everything handed over has run.

API documentation: https://jfcameron.github.io/jfc-thread_group/

## Getting Started
Install cmake 3.21 or greater, and a c++20 compatible compiler (GCC 11 or later, for `std::atomic::wait`)
```
# initialize the tree
git submodule update --init --recursive

# generate the script to build the library on linux using gcc
cmake --preset linux-gcc

# build the library
cmake --build --preset linux-gcc

# run the test suite
ctest --preset linux-gcc
```
For a browser, configure through Emscripten's wrapper, which supplies its toolchain: `emcmake cmake --preset emscripten`.

See `CMakePresets.json` for the full list of supported platforms and compilers. See `.github/workflows/ci.yml` to see exactly how CI runners build it on different platforms.

## In a browser

`std::thread` is Emscripten's pthreads, so the library builds and runs on a cross-origin isolated page with `-pthread`. What to know there:

- A browser creates the worker for a new thread asynchronously, and the thread runs only once the page's main thread has returned to its event loop. A group made in `main` runs nothing until the first frame ends, unless the page links with `-sPTHREAD_POOL_SIZE=N` so that N workers are started with it; size the pool to every group and every other thread the page makes.
- The main thread must never block waiting on a worker that is itself waiting for the main thread to be free. The group's own waits are in its workers; the destructor joins, which a page whose `main` never returns (`emscripten_set_main_loop`) never reaches.
- The default size, `hardware_concurrency() - 1`, is more than a page should take beside its other threads: pass a count.
- `pending()` is how a page shows "loading" until its first fetches and decodes are through.

