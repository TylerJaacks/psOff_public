include(ExternalProject)

ExternalProject_Add(zlib_project
  SOURCE_DIR ${PRJ_SRC_DIR}/third_party/zlib
  BINARY_DIR ${CMAKE_BINARY_DIR}/third_party/zlib
  CMAKE_ARGS
  -DCMAKE_BUILD_TYPE:STRING=Release
  -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/third_party/install
  -DCMAKE_INSTALL_MESSAGE=${CMAKE_INSTALL_MESSAGE}
  -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
  -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
  -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
)

ExternalProject_Add(third_party
  SOURCE_DIR ${PRJ_SRC_DIR}/third_party
  BINARY_DIR ${CMAKE_BINARY_DIR}/third_party
  CMAKE_ARGS
  -DCMAKE_BUILD_TYPE:STRING=Release
  -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/third_party/install
  -DCMAKE_INSTALL_MESSAGE=${CMAKE_INSTALL_MESSAGE}
  -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
  -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
  -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
)
ExternalProject_Add_StepDependencies(third_party install zlib_project)

set(BOOST_INCLUDE_LIBRARIES "program_options;date_time;interprocess;stacktrace;uuid;beast;signals2;thread;url;asio")
ExternalProject_Add(boost
  SOURCE_DIR ${PRJ_SRC_DIR}/third_party/boost
  BINARY_DIR ${CMAKE_BINARY_DIR}/third_party/boost
  CMAKE_ARGS
  -DCMAKE_BUILD_TYPE:STRING=Release
  -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/third_party/install
  -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
  -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
  -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
  -DCMAKE_INSTALL_MESSAGE=NEVER
  -DBoost_USE_STATIC_LIBS=ON
  -DBoost_USE_MULTITHREADED=ON
  -DBUILD_TESTING=OFF
  -DBOOST_INSTALL_LAYOUT=system
  CMAKE_CACHE_ARGS -DBOOST_INCLUDE_LIBRARIES:STRING=${BOOST_INCLUDE_LIBRARIES}
)
