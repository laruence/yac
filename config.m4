dnl $Id$
dnl config.m4 for extension yac

PHP_ARG_ENABLE(yac, whether to enable yac support,
    [  --enable-yac           Enable yac support])

PHP_ARG_WITH(system-fastlz, wheter to use system FastLZ bibrary,
    [  --with-system-fastlz   Use system FastLZ bibrary], no, no)

dnl PHP_ARG_ENABLE(yac, whether to use msgpack as serializer,
dnl    [  --enable-msgpack       Use Messagepack as serializer])

dnl copied from Zend Optimizer Plus
AC_MSG_CHECKING(for sysvipc shared memory support)
AC_TRY_RUN([
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>

int main() {
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
}
],dnl
AC_DEFINE(HAVE_SHM_IPC, 1, [Define if you have SysV IPC SHM support])
    msg=yes,msg=no,msg=no)
AC_MSG_RESULT([$msg])

AC_MSG_CHECKING(for mmap() using MAP_ANON shared memory support)
AC_TRY_RUN([
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

int main() {
  pid_t pid;
  int status;
  char *shm;

  shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
  if (shm == MAP_FAILED) {
    return 1;
  }

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
}
],dnl
AC_DEFINE(HAVE_SHM_MMAP_ANON, 1, [Define if you have mmap(MAP_ANON) SHM support])
    msg=yes,msg=no,msg=no)
AC_MSG_RESULT([$msg])

AC_MSG_CHECKING(for mmap() using /dev/zero shared memory support)
AC_TRY_RUN([
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#ifndef MAP_FAILED
# define MAP_FAILED ((void*)-1)
#endif

int main() {
  pid_t pid;
  int status;
  int fd;
  char *shm;

  fd = open("/dev/zero", O_RDWR, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    return 1;
  }

  shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (shm == MAP_FAILED) {
    return 2;
  }

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
}
],dnl
AC_DEFINE(HAVE_SHM_MMAP_ZERO, 1, [Define if you have mmap("/dev/zero") SHM support])
    msg=yes,msg=no,msg=no)
AC_MSG_RESULT([$msg])

dnl  if test "$PHP_MSGPACK" != "no"; then
dnl    AC_DEFINE(ENABLE_MSGPACK,1,[enable msgpack packager])
dnl    ifdef([PHP_ADD_EXTENSION_DEP],
dnl    [
dnl    PHP_ADD_EXTENSION_DEP(yac, msgpack, true)
dnl    ])
dnl  fi

YAC_FILES="yac.c storage/yac_storage.c storage/allocator/yac_allocator.c storage/allocator/allocators/shm.c storage/allocator/allocators/mmap.c serializer/php.c serializer/msgpack.c"
if test "$PHP_SYSTEM_FASTLZ" != "no"; then
  AC_CHECK_HEADERS([fastlz.h])
  PHP_CHECK_LIBRARY(fastlz, fastlz_compress,
    [PHP_ADD_LIBRARY(fastlz, 1, YAC_SHARED_LIBADD)],
    [AC_MSG_ERROR(FastLZ library not found)])
else
  YAC_FILES="${YAC_FILES} compressor/fastlz/fastlz.c"
fi

if test "$PHP_YAC" != "no"; then
  PHP_SUBST(YAC_SHARED_LIBADD)
  PHP_NEW_EXTENSION(yac, ${YAC_FILES}, $ext_shared)
  PHP_ADD_BUILD_DIR([$ext_builddir/storage])
  PHP_ADD_BUILD_DIR([$ext_builddir/storage/allocator])
  PHP_ADD_BUILD_DIR([$ext_builddir/storage/allocator/allocators])
  PHP_ADD_BUILD_DIR([$ext_builddir/serializer])
  PHP_ADD_BUILD_DIR([$ext_builddir/compressor])
  PHP_ADD_BUILD_DIR([$ext_builddir/compressor/fastlz])
fi
