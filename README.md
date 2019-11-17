[![Build Status](https://travis-ci.org/jfcameron/jfc-thread_group.svg?branch=master)](https://travis-ci.org/jfcameron/jfc-thread_group) [![Coverage Status](https://coveralls.io/repos/github/jfcameron/jfc-thread_group/badge.svg?branch=master)](https://coveralls.io/github/jfcameron/jfc-thread_group?branch=master) [![Documentation](https://img.shields.io/badge/documentation-doxygen-blue.svg)](https://jfcameron.github.io/jfc-thread_group/)

## jfc-thread_group

Task-based concurrency lib. Threadgroup that wraps the "moody concurrent queue" lockless queue

### unique_handle
Unique Handle models the single owner case with move support to another unique handle or a shared handle. 

### shared_handle
Shared handle models the many owners case, the deletor is invoked when the final owner falls out of scope. A shared handle can also be copied to a Weak Handle. 

### weak_handle
Weak handle is similar to Shared, except it does not contribute to the use_count and the handle cannot be accessed directly. Instead, the lock method must be called, which returns an optional to a shared handle. If the handle that the weak handle is observing has not fallen out of scope at the time of the lock then the optional will contain a new shared handle to the resource. If not, then the optional will be null.

## CI & Documentation

Documentation can be generated with doxygen or viewed online here: https://jfcameron.github.io/jfc-thread_group/

Unit tests written with catch2 are available under test/

Coverage calculated with gcov. Viewable online here: https://coveralls.io/github/jfcameron/jfc-thread_group

CI done using Travis CI. Build scripts cover Windows, Linux, Mac; Clang, GCC, MSVC, MinGW: https://travis-ci.org/jfcameron/jfc-thread_group

