dnl config.m4 for extension yac

PHP_ARG_ENABLE([yac], [whether to enable yac support],
  [AS_HELP_STRING([--enable-yac], [Enable yac support])])

PHP_ARG_WITH([system-fastlz], [whether to use system FastLZ library],
  [AS_HELP_STRING([--with-system-fastlz], [Use system FastLZ library])], [no], [no])

PHP_ARG_ENABLE([json], [whether to use json as serializer],
  [AS_HELP_STRING([--enable-json], [Use json as serializer])], [no], [no])

PHP_ARG_ENABLE([msgpack], [whether to use msgpack as serializer],
  [AS_HELP_STRING([--enable-msgpack], [Use msgpack as serializer])], [no], [no])

PHP_ARG_ENABLE([igbinary], [whether to use igbinary as serializer],
  [AS_HELP_STRING([--enable-igbinary], [Use igbinary as serializer])], [no], [no])

dnl ---------------------------------------------------------------------
dnl Helper macros
dnl ---------------------------------------------------------------------

dnl
dnl YAC_SHM_VERIFY: fork-based tail shared by the shared memory probes.
dnl The child writes into the region, the parent verifies visibility.
dnl Expects `char *shm`, `pid_t pid` and `int status` in scope.
dnl
AC_DEFUN([YAC_SHM_VERIFY], [[
  strcpy(shm, "hello");

  pid = fork();
  if (pid < 0) {
    return 5;
  } else if (pid == 0) {
    strcpy(shm, "bye");
    return 6;
  }
  if (wait(&status) != pid) {
    return 7;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 6) {
    return 8;
  }
  if (strcmp(shm, "bye") != 0) {
    return 9;
  }
  return 0;
]])

dnl
dnl YAC_CHECK_CRC32_ISA(desc, cache-var, flag, source, define)
dnl Compile-time probe for hardware CRC32C intrinsics. On success the flag
dnl goes into the extension's own CFLAGS, not the global ones.
dnl
AC_DEFUN([YAC_CHECK_CRC32_ISA],
[AC_CACHE_CHECK([for $1 instruction support], [$2],
  [$2=no
  AX_CHECK_COMPILE_FLAG([$3],
    [yac_saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $3"
    AC_LINK_IFELSE([AC_LANG_SOURCE([$4])], [$2=yes])
    CFLAGS="$yac_saved_CFLAGS"])])
AS_VAR_IF([$2], [yes],
  [AC_DEFINE([$5], [1], [Define to 1 if you have $1 instruction support.])
  YAC_EXTRA_CFLAGS="$YAC_EXTRA_CFLAGS $3"])])

dnl
dnl YAC_CHECK_SERIALIZER(name): --enable-<name> turns on an optional
dnl serializer backend and registers an optional extension dependency.
dnl
AC_DEFUN([YAC_CHECK_SERIALIZER],
[if test "$PHP_[]translit($1, a-z, A-Z)" != "no"; then
  AC_DEFINE([YAC_ENABLE_]translit($1, a-z, A-Z), [1],
    [Define to 1 to use $1 as serializer.])
  PHP_ADD_EXTENSION_DEP(yac, $1, true)
fi])

if test "$PHP_YAC" != "no"; then

dnl ---------------------------------------------------------------------
dnl Shared memory backend (same probes as ext/opcache)
dnl ---------------------------------------------------------------------

AC_CACHE_CHECK([for sysvipc shared memory support], [yac_cv_shm_ipc],
  [AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>

int main(void) {
  pid_t pid;
  int status;
  int ipc_id;
  char *shm;
  struct shmid_ds shmbuf;

  ipc_id = shmget(IPC_PRIVATE, 4096, (IPC_CREAT | SHM_R | SHM_W));
  if (ipc_id == -1) {
    return 1;
  }

  shm = shmat(ipc_id, NULL, 0);
  if (shm == (void *)-1) {
    shmctl(ipc_id, IPC_RMID, NULL);
    return 2;
  }

  if (shmctl(ipc_id, IPC_STAT, &shmbuf) != 0) {
    shmdt(shm);
    shmctl(ipc_id, IPC_RMID, NULL);
    return 3;
  }

  shmbuf.shm_perm.uid = getuid();
  shmbuf.shm_perm.gid = getgid();
  shmbuf.shm_perm.mode = 0600;

  if (shmctl(ipc_id, IPC_SET, &shmbuf) != 0) {
    shmdt(shm);
    shmctl(ipc_id, IPC_RMID, NULL);
    return 4;
  }

  shmctl(ipc_id, IPC_RMID, NULL);
]]YAC_SHM_VERIFY[[
}
]])],
  [yac_cv_shm_ipc=yes], [yac_cv_shm_ipc=no], [yac_cv_shm_ipc=no])])

AC_CACHE_CHECK([for mmap() using MAP_ANON shared memory support], [yac_cv_shm_mmap_anon],
  [AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#ifndef MAP_ANON
# ifdef MAP_ANONYMOUS
#  define MAP_ANON MAP_ANONYMOUS
# endif
#endif
#ifndef MAP_FAILED
# define MAP_FAILED ((void*)-1)
#endif

int main(void) {
  pid_t pid;
  int status;
  char *shm;

  shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
  if (shm == MAP_FAILED) {
    return 1;
  }
]]YAC_SHM_VERIFY[[
}
]])],
  [yac_cv_shm_mmap_anon=yes],
  [yac_cv_shm_mmap_anon=no],
  [AS_CASE([$host_alias], [*linux*|*midipix], [yac_cv_shm_mmap_anon=yes], [yac_cv_shm_mmap_anon=no])])])

AS_VAR_IF([yac_cv_shm_ipc], [yes],
  [AC_DEFINE([HAVE_SHM_IPC], [1], [Define to 1 if you have SysV IPC SHM support.])])
AS_VAR_IF([yac_cv_shm_mmap_anon], [yes],
  [AC_DEFINE([HAVE_SHM_MMAP_ANON], [1], [Define to 1 if you have mmap(MAP_ANON) SHM support.])])

if test "$yac_cv_shm_ipc" != "yes" && test "$yac_cv_shm_mmap_anon" != "yes"; then
  AC_MSG_ERROR([no usable shared memory backend found (need mmap(MAP_ANON) or SysV IPC shm)])
fi

dnl ---------------------------------------------------------------------
dnl Atomics (yac_atomic.h falls back to inline asm / Interlocked*)
dnl ---------------------------------------------------------------------

AC_CACHE_CHECK([for __sync_bool_compare_and_swap support], [yac_cv_builtin_atomic],
  [AC_LINK_IFELSE([AC_LANG_PROGRAM([], [[
    int variable = 1;
    return (__sync_bool_compare_and_swap(&variable, 1, 2)
           && __sync_add_and_fetch(&variable, 1)) ? 1 : 0;
  ]])], [yac_cv_builtin_atomic=yes], [yac_cv_builtin_atomic=no])])
AS_VAR_IF([yac_cv_builtin_atomic], [yes],
  [AC_DEFINE([HAVE_BUILTIN_ATOMIC], [1],
    [Define to 1 if the compiler supports __sync_bool_compare_and_swap().])])

dnl ---------------------------------------------------------------------
dnl Hardware-accelerated CRC32C (yac_storage.c)
dnl ---------------------------------------------------------------------

dnl SSE4.2 path dispatches at runtime via zend_cpu_supports_sse42() (PHP >= 7.3)
dnl Note: in phpize builds PHP_CONFIG is the bare command name, not a path
yac_php_vernum=`$PHP_CONFIG --vernum 2>/dev/null`
if test -n "$yac_php_vernum" && test "$yac_php_vernum" -ge 70300; then
  YAC_CHECK_CRC32_ISA([SSE4.2 CRC32C], [yac_cv_crc32_sse], [-msse4.2], [[
#include <nmmintrin.h>
#include <stdint.h>
int main(void) {
  return (int)_mm_crc32_u32(0xffffffff, (uint32_t)0x01234567);
}]], [HAVE_SSE_CRC32])
fi

YAC_CHECK_CRC32_ISA([ARMv8 CRC32C], [yac_cv_crc32_arm], [-march=armv8-a+crc], [[
#include <arm_acle.h>
#include <stdint.h>
int main(void) {
  uint64_t v = 0x0123456789abcdefULL;
  uint32_t crc = 0xffffffff;
  crc = __crc32cd(crc, v);
  crc = __crc32cw(crc, (uint32_t)v);
  crc = __crc32ch(crc, (uint16_t)v);
  crc = __crc32cb(crc, (uint8_t)v);
  return (int)crc;
}]], [HAVE_ARM_CRC32])

dnl ---------------------------------------------------------------------
dnl Compressor: system FastLZ or the bundled copy
dnl ---------------------------------------------------------------------

YAC_FILES="yac.c storage/yac_storage.c storage/allocator/yac_allocator.c storage/allocator/allocators/shm.c storage/allocator/allocators/mmap.c serializer/php.c serializer/msgpack.c serializer/igbinary.c serializer/json.c"
if test "$PHP_SYSTEM_FASTLZ" != "no"; then
  AC_CHECK_HEADERS([fastlz.h], [],
    [AC_MSG_ERROR([system FastLZ requested, but fastlz.h was not found])])
  PHP_CHECK_LIBRARY(fastlz, fastlz_compress,
    [PHP_ADD_LIBRARY(fastlz, 1, YAC_SHARED_LIBADD)],
    [AC_MSG_ERROR([system FastLZ requested, but libfastlz was not found])])
else
  YAC_FILES="$YAC_FILES compressor/fastlz/fastlz.c"
fi

dnl ---------------------------------------------------------------------
dnl Extension
dnl ---------------------------------------------------------------------

PHP_SUBST(YAC_SHARED_LIBADD)
PHP_NEW_EXTENSION(yac, $YAC_FILES, $ext_shared,, [$YAC_EXTRA_CFLAGS])
PHP_ADD_BUILD_DIR([
  $ext_builddir/storage
  $ext_builddir/storage/allocator
  $ext_builddir/storage/allocator/allocators
  $ext_builddir/serializer
  $ext_builddir/compressor
  $ext_builddir/compressor/fastlz
])

dnl PHP_ADD_EXTENSION_DEP must be called after PHP_NEW_EXTENSION
YAC_CHECK_SERIALIZER([json])
YAC_CHECK_SERIALIZER([msgpack])
YAC_CHECK_SERIALIZER([igbinary])

fi
