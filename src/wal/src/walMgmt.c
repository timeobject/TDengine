/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "os.h"
#include "taoserror.h"
#include "talloc.h"
#include "tref.h"
#include "tutil.h"
#include "twal.h"
#include "walInt.h"

typedef struct {
  int32_t   refId;
  int32_t   num;
  int32_t   seq;
  int8_t    stop;
  pthread_t thread;
  pthread_mutex_t mutex;
} SWalMgmt;

static SWalMgmt tsWal;
static int32_t  walCreateThread();
static void     walStopThread();
static int32_t  walInitObj(SWal *pWal);
static void     walFreeObj(void *pWal);

int32_t walInit() {
  tmemzero(&tsWal, sizeof(SWalMgmt));
  tsWal.refId = taosOpenRef(TSDB_MIN_VNODES, walFreeObj);

  int32_t code = walCreateThread();
  if (code != TSDB_CODE_SUCCESS) {
    wError("failed to init wal module, reason:%s", tstrerror(code));
    return code;
  }

  wInfo("wal module is initialized, refId:%d", tsWal.refId);
  return code;
}

void walCleanUp() {
  walStopThread();
  taosCloseRef(tsWal.refId);
  wInfo("wal module is cleaned up");
}

void *walOpen(char *path, SWalCfg *pCfg) {
  SWal *pWal = tcalloc(sizeof(SWal));
  if (pWal == NULL) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    return NULL;
  }

  pWal->vgId = pCfg->vgId;
  pWal->fd = -1;
  pWal->fileId = -1;
  pWal->level = pCfg->walLevel;
  pWal->keep = pCfg->keep;
  pWal->fsyncPeriod = pCfg->fsyncPeriod;
  tstrncpy(pWal->path, path, sizeof(pWal->path));
  pthread_mutex_init(&pWal->mutex, NULL);

  pWal->fsyncSeq = pCfg->fsyncPeriod % 1000;
  if (pWal->fsyncSeq <= 0) pWal->fsyncSeq = 1;

  if (walInitObj(pWal) != TSDB_CODE_SUCCESS) {
    walFreeObj(pWal);
    return NULL;
  }

  if (taosAddRef(tsWal.refId, pWal) != TSDB_CODE_SUCCESS) {
    walFreeObj(pWal);
    return NULL;
  }

  atomic_add_fetch_32(&tsWal.num, 1);
  wDebug("vgId:%d, wal:%p is opened, level:%d period:%d path:%s", pWal->vgId, pWal, pWal->level, pWal->fsyncPeriod,
         pWal->path);

  return pWal;
}

int32_t walAlter(void *handle, SWalCfg *pCfg) {
  if (handle == NULL) return TSDB_CODE_WAL_APP_ERROR;
  SWal *pWal = handle;

  if (pWal->level == pCfg->walLevel && pWal->fsyncPeriod == pCfg->fsyncPeriod) {
    wDebug("vgId:%d, old walLevel:%d fsync:%d, new walLevel:%d fsync:%d not change", pWal->vgId, pWal->level,
           pWal->fsyncPeriod, pCfg->walLevel, pCfg->fsyncPeriod);
    return TSDB_CODE_SUCCESS;
  }

  wInfo("vgId:%d, change old walLevel:%d fsync:%d, new walLevel:%d fsync:%d", pWal->vgId, pWal->level,
        pWal->fsyncPeriod, pCfg->walLevel, pCfg->fsyncPeriod);

  pWal->level = pCfg->walLevel;
  pWal->fsyncPeriod = pCfg->fsyncPeriod;
  pWal->fsyncSeq = pCfg->fsyncPeriod % 1000;
  if (pWal->fsyncSeq <= 0) pWal->fsyncSeq = 1;

  return TSDB_CODE_SUCCESS;
}

void walClose(void *handle) {
  if (handle == NULL) return;

  SWal *pWal = handle;
  taosClose(pWal->fd);

  if (pWal->keep == 0) {
    snprintf(pWal->name, sizeof(pWal->name), "%s/%s%" PRIu64, pWal->path, WAL_PREFIX, pWal->fileId);
    if (remove(pWal->name) < 0) {
      wError("vgId:%d, wal:%p file:%s, failed to remove", pWal->vgId, pWal, pWal->name);
    } else {
      wDebug("vgId:%d, wal:%p file:%s, it is removed", pWal->vgId, pWal, pWal->name);
    }
  } else {
    wDebug("vgId:%d, wal:%p file:%s, it is closed and kept", pWal->vgId, pWal, pWal->name);
  }

  taosRemoveRef(tsWal.refId, pWal);
}

static int32_t walInitObj(SWal *pWal) {
  if (taosMkDir(pWal->path, 0755) != 0) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    wError("vgId:%d, wal:%s, failed to create directory, reason:%s", pWal->vgId, pWal->path, strerror(errno));
    return terrno;
  }

  if (pWal->keep == 1) {
    return TSDB_CODE_SUCCESS;
  }

  walRenew(pWal);

  if (pWal && pWal->fd < 0) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    wError("vgId:%d, wal:%s, failed to open file, reason:%s", pWal->vgId, pWal->path, strerror(errno));
    return terrno;
  }

  wDebug("vgId:%d, wal:%s, is initialized", pWal->vgId, pWal->name);
  return TSDB_CODE_SUCCESS;
}

static void walFreeObj(void *wal) {
  SWal *pWal = wal;
  wDebug("vgId:%d, wal:%p is freed", pWal->vgId, pWal);

  taosClose(pWal->fd);
  pthread_mutex_destroy(&pWal->mutex);
  tfree(pWal);
}

static bool walNeedFsync(SWal *pWal) {
  if (pWal->fsyncPeriod <= 0 || pWal->level != TAOS_WAL_FSYNC) {
    return false;
  }

  if (tsWal.seq % pWal->fsyncSeq == 0) {
    return true;
  }

  return false;
}

static void walUpdateSeq() {
  taosMsleep(WAL_REFRESH_MS);
  if (++tsWal.seq <= 0) {
    tsWal.seq = 1;
  }
}

static void walFsyncAll() {
  SWal *pWal = taosIterateRef(tsWal.refId, NULL);
  while (pWal) {
    if (walNeedFsync(pWal)) {
      wTrace("vgId:%d, do fsync, level:%d seq:%d rseq:%d", pWal->vgId, pWal->level, pWal->fsyncSeq, tsWal.seq);
      int32_t code = fsync(pWal->fd);
      if (code != 0) {
        wError("vgId:%d, wal:%s, fsync failed, reason:%s", pWal->vgId, pWal->name, strerror(code));
      }
    }
    pWal = taosIterateRef(tsWal.refId, pWal);
  }
}

static void *walThreadFunc(void *param) {
  while (1) {
    walUpdateSeq();
    walFsyncAll();
    if (tsWal.stop) break;
  }

  return NULL;
}

static int32_t walCreateThread() {
  pthread_attr_t thAttr;
  pthread_attr_init(&thAttr);
  pthread_attr_setdetachstate(&thAttr, PTHREAD_CREATE_JOINABLE);

  if (pthread_create(&tsWal.thread, &thAttr, walThreadFunc, NULL) != 0) {
    wError("failed to create wal thread, reason:%s", strerror(errno));
    return TAOS_SYSTEM_ERROR(errno);
  }

  pthread_attr_destroy(&thAttr);
  wDebug("wal thread is launched");

  return TSDB_CODE_SUCCESS;
}

static void walStopThread() {
  tsWal.stop = 1;
  if (tsWal.thread) {
    pthread_join(tsWal.thread, NULL);
  }

  wDebug("wal thread is stopped");
}
