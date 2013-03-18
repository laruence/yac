/*
  +----------------------------------------------------------------------+
  | Yet Another Cache                                                    |
  +----------------------------------------------------------------------+
  | Copyright (c) 2013-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Xinchen Hui <laruence@php.net>                               |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include <error.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <unistd.h>

#include "php.h"

#include "storage/yac_storage.h"
#include "yac_lock.h"

int semid;

union semun {
	int              val;    /* Value for SETVAL */
	struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
	unsigned short  *array;  /* Array for GETALL, SETALL */
	struct seminfo  *__buf;  /* Buffer for IPC_INFO (Linux specific) */
};

int sem_create(void) /* {{{ */ {
    int semget_flags;
    int perms = 0777;
    union semun arg;
    key_t key = IPC_PRIVATE;

    semget_flags = IPC_CREAT|IPC_EXCL|perms;

    semid = semget(IPC_PRIVATE, 1, semget_flags);
    if (semid == -1) {
        return 0;
    }

    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) < 0) {
        sem_destroy();
        return 0;
    }

    return semid;
}
/* }}} */

void sem_destroy() /* {{{ */ {
    union semun arg;
 //   semctl(semid, 0, IPC_RMID, arg);
}
/* }}} */

void sem_lock() /* {{{ */ {
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op  = -1;
    op.sem_flg = SEM_UNDO;

    while (semop(semid, &op, 1) < 0);
}
/* }}} */

void sem_unlock() /* {{{ */ {
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op  = 1;
    op.sem_flg = SEM_UNDO;

    while (semop(semid, &op, 1) < 0);
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
