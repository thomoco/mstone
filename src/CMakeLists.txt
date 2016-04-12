# Build mailclient for mstone

set(STONESRCS bench.c client.c errexit.c main.c 
  parse.c sysdep.c timefunc.c 
  http-util.c http.c imap4.c pop3.c smtp.c wmap.c 
  idle.c generate.c socket.c multipop.c webmail.c 
  xalloc.c sb.c words.c files.c rcs.c rcsfake.c
  )
set(OS_CFLAGS)
set(OS_LIBS)
set(RPATH)

include(CheckFunctionExists)
include(CheckIncludeFile)
#include(FindDoxygen)

option(DISABLE_THREADS "Disable threading (for debugging)" OFF)
if (NOT DISABLE_THREADS)
  include(${CMAKE_ROOT}/Modules/FindThreads.cmake)
endif (NOT DISABLE_THREADS)

if (CMAKE_COMPILER_IS_GNUCC)
  set(REL_OS_CFLAGS -O -Wall)
  set(DBG_OS_CFLAGS -g -Wall -O1)
  if(CMAKE_USE_PTHREADS_INIT)
    #FindThreads doesn't set this, but it does have an effect (on Linux)
    set(OS_CFLAGS ${OS_CFLAGS} -pthread)
  endif(CMAKE_USE_PTHREADS_INIT)
else(CMAKE_COMPILER_IS_GNUCC)
  set(REL_OS_CFLAGS)
  set(DBG_OS_CFLAGS)
endif(CMAKE_COMPILER_IS_GNUCC)

if(UNIX)
  exec_program("${CMAKE_SOURCE_DIR}/bin/nsarch" OUTPUT_VARIABLE OS_ARCH)
  message(STATUS "os_arch is ${OS_ARCH}") #DEBUG
  set(OS_LIBS ${OS_LIBS} m)
  if(CMAKE_USE_PTHREADS_INIT)
    set(OS_CFLAGS ${OS_CFLAGS} -DUSE_PTHREADS)
  endif(CMAKE_USE_PTHREADS_INIT)


  #doesnt work right#CHECK_INCLUDE_FILE(sys/select.h HAVE_SELECT_H)
  FIND_FILE(HAVE_SELECT_H sys/select.h /usr/include)
  if (HAVE_SELECT_H)
    set(OS_CFLAGS ${OS_CFLAGS} -DHAVE_SELECT_H)
  endif (HAVE_SELECT_H)

  CHECK_INCLUDE_FILE(wait.h HAVE_WAIT_H)
  if (HAVE_WAIT_H)
    set(OS_CFLAGS ${OS_CFLAGS} -DHAVE_WAIT_H)
  endif (HAVE_WAIT_H)

  CHECK_FUNCTION_EXISTS(lrand48 HAVE_LRAND48)
  if (HAVE_LRAND48)
    set(OS_CFLAGS ${OS_CFLAGS} -DUSE_LRAND48)
  else(HAVE_LRAND48)
    CHECK_FUNCTION_EXISTS(lrand48_r HAVE_LRAND48_R)
    if(HAVE_LRAND48_R)
      set(OS_CFLAGS ${OS_CFLAGS} -DUSE_LRAND48_R)
    endif(HAVE_LRAND48_R)
  endif(HAVE_LRAND48)

  # find Apache Portable Runtime (required by subversion libraries)
  set(APR_DIR_DEFAULT "/usr")
  find_file(APR_CONFIG apr-1-config
    ${APR_DIR_DEFAULT}/bin ${APR_DIR_DEFAULT}/lib ${APR_DIR_DEFAULT}/etc)
  if (APR_CONFIG)
    get_filename_component(APR_DIR ${APR_CONFIG} PATH)
    get_filename_component(APR_DIR ${APR_DIR} PATH)
    #string(REGEX REPLACE "/.../apr-1-config$" "" APR_DIR ${APR_CONFIG})
    #message(STATUS "apr_config=${APR_CONFIG} apr_dir=${APR_DIR}") #DEBUG
    # find subversion client libraries (probably same location as APR)
    find_library(SVN_CLIENT_LIB svn_client-1
      ${APR_DIR}/lib ${APR_DIR_DEFAULT}/lib)
    if (SVN_CLIENT_LIB)
      get_filename_component(SVN_DIR ${SVN_CLIENT_LIB} PATH)
      get_filename_component(SVN_DIR ${SVN_DIR} PATH)
      #message(STATUS "svn_lib=${SVN_CLIENT_LIB} svn_dir=${SVN_DIR}") #DEBUG
      option(SVN_MODULE "Enable Subversion module" ON)
    endif (SVN_CLIENT_LIB)
  endif (APR_CONFIG)
  
  ### Special configuration tweaks for various OSes
  # These work with the existing #ifdefs in the code.
  if(${OS_ARCH} MATCHES Linux)
    set(OS_CFLAGS ${OS_CFLAGS} -D__LINUX__)
  endif(${OS_ARCH} MATCHES Linux)

  ### The following OSes haven't been tested under cmake ###
  if(${OS_ARCH} MATCHES SunOS)
    set(OS_CFLAGS ${OS_CFLAGS} -D__SOLARIS__
      -DXP_UNIX -D_REENTRANT -DUSE_GETHOSTBYNAME_R -DUSE_GETPROTOBYNAME_R)
    set(OS_LIBS ${OS_LIBS} nsl socket posix4)
    if(NOT CMAKE_COMPILER_IS_GNUCC)
      set(REL_OS_CFLAGS -mt -xstrconst -v -O)
      set(DBG_OS_CFLAGS -mt -xstrconst -v -g -xs)
    endif(NOT CMAKE_COMPILER_IS_GNUCC)
  endif(${OS_ARCH} MATCHES SunOS)

  if(${OS_ARCH} MATCHES FreeBSD)
    set(OS_CFLAGS ${OS_CFLAGS} -D__FREEBSD__)
  endif(${OS_ARCH} MATCHES FreeBSD)

  if(${OS_ARCH} MATCHES IRIX)
    set(OS_CFLAGS ${OS_CFLAGS} -D__IRIX__)
    if(NOT CMAKE_COMPILER_IS_GNUCC)
      set(REL_OS_CFLAGS -fullwarn)
      set(DBG_OS_CFLAGS -fullwarn)
    endif(NOT CMAKE_COMPILER_IS_GNUCC)
  endif(${OS_ARCH} MATCHES IRIX)

  if(${OS_ARCH} MATCHES HP-UX)
    set(OS_CFLAGS ${OS_CFLAGS} -D__HPUX__)
    if(NOT CMAKE_COMPILER_IS_GNUCC)
      set(REL_OS_CFLAGS +DAportable +DS2.0 -Ae +ESlit)
      set(DBG_OS_CFLAGS +Z +DAportable +DS2.0 -g -Ae +ESlit)
    endif(NOT CMAKE_COMPILER_IS_GNUCC)
  endif(${OS_ARCH} MATCHES HP-UX)

  if(${OS_ARCH} MATCHES OSF1)
    set(OS_CFLAGS ${OS_CFLAGS} -D__OSF1__)
    if(NOT CMAKE_COMPILER_IS_GNUCC)
      set(REL_OS_CFLAGS  -warnprotos -verbose -newc -std1
        -pthread -w0 -readonly_strings)
      set(DBG_OS_CFLAGS  -warnprotos -verbose -newc -std1
        -pthread -w0 -readonly_strings)
    endif(NOT CMAKE_COMPILER_IS_GNUCC)
  endif(${OS_ARCH} MATCHES OSF1)

  if(${OS_ARCH} MATCHES AIX)
    set(OS_CFLAGS ${OS_CFLAGS} -D__AIX__  -D_THREAD_SAFE)
    if(NOT CMAKE_COMPILER_IS_GNUCC)
      set(REL_OS_CFLAGS -qro -qroconst -qfullpath -qsrcmsg -qflag=I:W)
      set(DBG_OS_CFLAGS -qro -qroconst -qfullpath -qsrcmsg)
    endif(NOT CMAKE_COMPILER_IS_GNUCC)
  endif(${OS_ARCH} MATCHES AIX)
endif(UNIX)

set(FEATURE_SRCS)
set(FEATURE_DEFINES)
set(FEATURE_INCS)
set(FEATURE_LIBS)

option(DEBUG "Enable debug messsages" OFF)
if(DEBUG)
  set(OS_CFLAGS ${OS_CFLAGS} ${DBG_OS_CFLAGS} -D_DEBUG -DDIRECT_OUT)
else(DEBUG)
  set(OS_CFLAGS ${OS_CFLAGS} ${REL_OS_CFLAGS})
endif(DEBUG)

option(AUTOGEN "automatic body generation" ON)
if(AUTOGEN)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DAUTOGEN)
endif(AUTOGEN)

option(IMAP_RAMPDOWN "ramp-down time for IMAP" ON)
if(IMAP_RAMPDOWN)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DIMAP_RAMPDOWN)
endif(IMAP_RAMPDOWN)

option(SOCK_LINESPEED "(primitive) linespeed limitation" ON)
if(SOCK_LINESPEED)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DSOCK_LINESPEED)
endif(SOCK_LINESPEED)

if(CMAKE_USE_PTHREADS_INIT)
option(USE_EVENTS "use event-queue model -- fewer threads" OFF)
endif(CMAKE_USE_PTHREADS_INIT)

if(USE_EVENTS)
  option(DYNAMIC_THROTTLE "throttle preload dynamically" ON)
  set(FEATURE_SRCS ${FEATURE_SRCS} event.c)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DUSE_EVENTS)
endif(USE_EVENTS)

if(DYNAMIC_THROTTLE)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DDYNAMIC_THROTTLE)
endif(DYNAMIC_THROTTLE)

option(GEN_CHECKSUM "try to verify message integrity" ON)
if(GEN_CHECKSUM)
  set(FEATURE_SRCS ${FEATURE_SRCS} checksum.c md5.c)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DGEN_CHECKSUM)
endif(GEN_CHECKSUM)

find_library(SSL_LIB ssl /lib /usr/lib)
find_library(CRYPTO_LIB crypto /lib /usr/lib)
if(SSL_LIB AND CRYPTO_LIB)
  option(SOCK_SSL "SSL/TLS and STARTTLS" OFF)
  if(SOCK_SSL)
    set(FEATURE_LIBS ${FEATURE_LIBS} ${SSL_LIB} ${CRYPTO_LIB})
    set(FEATURE_DEFINES ${FEATURE_DEFINES} -DSOCK_SSL)
  endif(SOCK_SSL)
endif(SSL_LIB AND CRYPTO_LIB)

if (SVN_MODULE)
  find_library(GSS_LIB gssapi_krb5 /lib /usr/lib)
  exec_program("${APR_CONFIG} --cppflags --includes"
    OUTPUT_VARIABLE APR_CFLAGS)
  exec_program("${APR_CONFIG} --ldflags --link-ld --libs"
    OUTPUT_VARIABLE APR_LDFLAGS)
  #message(STATUS "apr_cflags=${APR_CFLAGS} apr_ldflags=${APR_LDFLAGS}") #DEBUG
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DSVN_MODULE)
  set(FEATURE_SRCS ${FEATURE_SRCS} svn.c )
  # ASSUMES that apr and svn libraries are in the same directory
  SET_SOURCE_FILES_PROPERTIES(
    svn.c PROPERTIES
    COMPILE_FLAGS "-I${SVN_DIR}/include/subversion-1 ${APR_CFLAGS}")
  set(FEATURE_LIBS ${FEATURE_LIBS}
    -L${SVN_DIR}/lib -lsvn_client-1 -lsvn_ra-1 -lsvn_subr-1
    ${GSS_LIB} ${SSL_LIB}
    ${APR_LDFLAGS})
  if (NOT ${SVN_DIR} EQUAL "/usr")
    set(RPATH "${RPATH};${SVN_DIR}/lib")
  endif (NOT ${SVN_DIR} EQUAL "/usr")
  if (NOT ${APR_DIR} EQUAL "/usr")
    set(RPATH "${RPATH};${APR_DIR}/lib")
  endif (NOT ${APR_DIR} EQUAL "/usr")
endif (SVN_MODULE)

option(P4_MODULE "Enable perforce (p4) module" OFF)
if (P4_MODULE) 
  set(P4_DIR_DEFAULT "/usr/local")
  set(FEATURE_SRCS ${FEATURE_SRCS} p4.cc)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DP4_MODULE)
  find_library(P4_CLIENT_LIB client ${P4_DEFAULT_DIR}/lib)
  get_filename_component(P4_DIR ${P4_CLIENT_LIB} PATH)
  get_filename_component(P4_DIR ${P4_DIR} PATH)
  #message(STATUS "p4_client_lib=${P4_CLIENT_LIB} p4_dir=${P4_DIR}") #DEBUG
  find_library(P4_RPC_LIB rpc ${P4_DIR}/lib ${P4_DEFAULT_DIR}/lib)
  find_library(P4_SUPP_LIB supp ${P4_DIR}/lib ${P4_DEFAULT_DIR}/lib)
  set(FEATURE_LIBS ${FEATURE_LIBS} ${P4_CLIENT_LIB} ${P4_RPC_LIB} ${P4_SUPP_LIB})
  SET_SOURCE_FILES_PROPERTIES(
    p4.cc PROPERTIES COMPILE_FLAGS "-I${P4_DIR}/include")
  if (NOT ${P4_DIR} EQUAL "/usr")
    set(RPATH "${RPATH};${P4_DIR}/lib")
  endif (NOT ${P4_DIR} EQUAL "/usr")
endif (P4_MODULE) 

if(SOCK_SSL OR SVN_MODULE)
  set(FEATURE_DEFINES ${FEATURE_DEFINES} -DHAVE_SSL)
  set(FEATURE_SRCS ${FEATURE_SRCS} ssl.c)
  # BUG??? Are we pulling in the right headers?
endif(SOCK_SSL OR SVN_MODULE)


add_definitions(${OS_CFLAGS} ${FEATURE_DEFINES})
include_directories(${FEATURE_INCS})
#link_directories()

add_executable(getdist getdist.c idle.c xalloc.c)
target_link_libraries(getdist m)

add_executable(mailclient ${STONESRCS} ${FEATURE_SRCS})
target_link_libraries(mailclient 
  ${OS_LIBS} ${CMAKE_THREAD_LIBS_INIT} ${FEATURE_LIBS})

if (RPATH)
  string(REGEX REPLACE "^;" "" RPATH "${RPATH}")
  message(STATUS "mailclient install rpath is ${RPATH}") #DEBUG
  SET_TARGET_PROPERTIES(mailclient PROPERTIES INSTALL_RPATH "${RPATH}")
endif (RPATH)

set(MSTONE_HOME     ${CMAKE_INSTALL_PREFIX}/mstone)
set(ARCH_HOME       ${MSTONE_HOME}/build/${OS_ARCH})
set(BIN_HOME        ${MSTONE_HOME}/bin)
set(CONF_HOME       ${MSTONE_HOME}/conf)
set(DATA_HOME       ${MSTONE_HOME}/data)
set(DOC_HOME        ${MSTONE_HOME}/doc)
set(GNUPLOT_HOME    ${MSTONE_HOME}/gnuplot)
set(PERL_HOME       ${MSTONE_HOME}/perl)
install(TARGETS mailclient getdist RUNTIME DESTINATION ${ARCH_HOME})

if (UNIX)
  # A cmake script file which creates the install symlinks
  # This could be a static file...
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/make_mstone_symlink.cmake
    "exec_program(${CMAKE_COMMAND} ARGS -E chdir ${MSTONE_HOME} ${CMAKE_COMMAND} -E create_symlink mstone setup)\n"
    "exec_program(${CMAKE_COMMAND} ARGS -E chdir ${MSTONE_HOME} ${CMAKE_COMMAND} -E create_symlink mstone cleanup)\n"
    "exec_program(${CMAKE_COMMAND} ARGS -E chdir ${MSTONE_HOME} ${CMAKE_COMMAND} -E create_symlink mstone checktime)\n"
    )
  # Now add it as post-install script for "make install"
  install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/make_mstone_symlink.cmake)
endif (UNIX)
