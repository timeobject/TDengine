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

#ifndef _TD_INDEX_INT_H_
#define _TD_INDEX_INT_H_

#include "index.h"
#include "index_fst.h"
#include "tlog.h"
#include "thash.h"
#include "taos.h"

#ifdef USE_LUCENE
#include <lucene++/Lucene_c.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef struct SIndexStat {
  int32_t totalAdded;   //  
  int32_t totalDeled;   //
  int32_t totalUpdated; // 
  int32_t totalTerms;   //  
  int32_t distinctCol; // distinct column 
} SIndexStat; 

struct SIndex {
#ifdef USE_LUCENE 
 index_t *index; 
#endif  
 void     *cache;
 void     *tindex; 
 SHashObj *colObj;// < field name, field id> 
 
 int64_t  suid;      // current super table id, -1 is normal table    
 int      colId;  // field id allocated to cache
 int32_t  cVersion; // current version allocated to cache 

 SIndexStat stat;
 pthread_mutex_t mtx;
};   

struct SIndexOpts {
#ifdef USE_LUCENE 
  void *opts; 
#endif   

#ifdef USE_INVERTED_INDEX
  int32_t cacheSize;  // MB  
  // add cache module later
#endif

};

struct SIndexMultiTermQuery {
  EIndexOperatorType opera;   
  SArray *query;
};

// field and key;
typedef struct SIndexTerm {
  int64_t           suid; 
  SIndexOperOnColumn operType; // oper type, add/del/update
  uint8_t            colType;  // term data type, str/interger/json
  char    *colName;
  int32_t nColName;
  char    *colVal;
  int32_t nColVal;
} SIndexTerm;

typedef struct SIndexTermQuery {
  SIndexTerm*     term;
  EIndexQueryType qType;
} SIndexTermQuery;



#define indexFatal(...) do { if (sDebugFlag & DEBUG_FATAL) { taosPrintLog("index FATAL ", 255, __VA_ARGS__); }}     while(0)
#define indexError(...) do { if (sDebugFlag & DEBUG_ERROR) { taosPrintLog("index ERROR ", 255, __VA_ARGS__); }}     while(0)
#define indexWarn(...)  do { if (sDebugFlag & DEBUG_WARN)  { taosPrintLog("index WARN ", 255, __VA_ARGS__); }}      while(0)
#define indexInfo(...)  do { if (sDebugFlag & DEBUG_INFO)  { taosPrintLog("index ", 255, __VA_ARGS__); }}           while(0)
#define indexDebug(...) do { if (sDebugFlag & DEBUG_DEBUG) { taosPrintLog("index ", sDebugFlag, __VA_ARGS__); }} while(0)
#define indexTrace(...) do { if (sDebugFlag & DEBUG_TRACE) { taosPrintLog("index ", sDebugFlag, __VA_ARGS__); }} while(0)


#ifdef __cplusplus
}
#endif

#endif /*_TD_INDEX_INT_H_*/
